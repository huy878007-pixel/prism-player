#include "asm_c.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#include <immintrin.h>
#else
#include <cpuid.h>
#endif

extern "C" void prism_yuv420_row_avx2(uint8_t* dst, const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                      int groups8, const int32_t* coef);

namespace prism {
namespace {

int32_t RoundQ16(double v) { return (int32_t)std::lround(v * 65536.0); }

inline int Clip8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

inline int Cvt(int sample, int32_t coef) {
    return (int)(((int32_t)sample * coef + 32768) >> 16);
}

CpuInfo DetectCpu() {
    CpuInfo info;
    int regs[4] = {0, 0, 0, 0};
#if defined(_MSC_VER)
    char brand[49] = {0};
    __cpuid(regs, 0x80000000);
    unsigned maxExt = (unsigned)regs[0];
    unsigned last = maxExt < 0x80000004u ? maxExt : 0x80000004u;
    for (unsigned leaf = 0x80000002u; leaf <= last; ++leaf) {
        __cpuidex(regs, (int)leaf, 0);
        memcpy(brand + (leaf - 0x80000002u) * 16u, regs, 16);
    }
    brand[48] = 0;
    for (int i = 0; i < 48; ++i)
        if (brand[i] == 0) brand[i] = ' ';
    int end = 47;
    while (end >= 0 && brand[end] == ' ') brand[end--] = 0;
    info.brand = brand;
    __cpuidex(regs, 7, 0);
    info.avx2 = (regs[1] & (1 << 5)) != 0;
    info.avx512 = (regs[1] & (1 << 16)) != 0;
    bool fmaCpu = (regs[1] & (1 << 12)) != 0;
    __cpuidex(regs, 1, 0);
    bool osxsave = (regs[2] & (1 << 27)) != 0;
    bool avx = (regs[2] & (1 << 28)) != 0;
    info.fma = fmaCpu && (regs[2] & (1 << 12));
    if (osxsave && avx) {
        unsigned long long xcr0 = _xgetbv(0);
        bool ymmState = (xcr0 & 0x6) == 0x6;
        info.avx2 = info.avx2 && ymmState;
        info.avx512 = info.avx512 && (xcr0 & 0xE6) == 0xE6;
        info.fma = info.fma && ymmState;
    } else {
        info.avx2 = info.avx512 = info.fma = false;
    }
    if (info.brand.empty()) info.brand = "x86-64";
#endif
    return info;
}

}  // namespace

const CpuInfo& Cpu() {
    static CpuInfo info = DetectCpu();
    return info;
}

const char* CpuSummary() {
    static std::string s = [] {
        const CpuInfo& c = Cpu();
        std::string out = c.brand;
        out += c.avx2 ? " | AVX2" : " | SSE2";
        out += c.avx512 ? " + AVX-512" : "";
        return out;
    }();
    return s.c_str();
}

YuvCoef MakeCoef(Matrix m, bool fullRange, int bitDepth) {
    double kr = 0.2126, kb = 0.0722;
    switch (m) {
        case Matrix::BT601: kr = 0.2990; kb = 0.1140; break;
        case Matrix::BT709: kr = 0.2126; kb = 0.0722; break;
        case Matrix::BT2020: kr = 0.2627; kb = 0.0593; break;
    }
    double kg = 1.0 - kr - kb;
    const double shift = (double)(1 << (bitDepth - 8));
    double ys, cs, offY, offC;
    if (!fullRange) {
        ys = 255.0 / (219.0 * shift);
        cs = 255.0 / (224.0 * shift);
        offY = 16.0 * shift;
    } else {
        ys = 1.0 / shift;
        cs = 1.0 / shift;
        offY = 0.0;
    }
    offC = 128.0 * shift;
    YuvCoef c{};
    c.ys = RoundQ16(ys);
    c.rv = RoundQ16(2.0 * (1.0 - kr) * cs);
    c.gu = RoundQ16(-2.0 * kb * (1.0 - kb) / kg * cs);
    c.gv = RoundQ16(-2.0 * kr * (1.0 - kr) / kg * cs);
    c.bu = RoundQ16(2.0 * (1.0 - kb) * cs);
    c.offY = (int32_t)std::lround(offY);
    c.offC = (int32_t)std::lround(offC);
    return c;
}

void Yuv420pToBgraRef(uint8_t* dst, int dstStride, const uint8_t* y, const uint8_t* u, const uint8_t* v,
                      int yStride, int uvStride, int width, int height, Matrix matrix, bool fullRange) {
    YuvCoef c = MakeCoef(matrix, fullRange, 8);
    for (int row = 0; row < height; ++row) {
        const uint8_t* yr = y + (size_t)row * yStride;
        const uint8_t* ur = u + (size_t)(row >> 1) * uvStride;
        const uint8_t* vr = v + (size_t)(row >> 1) * uvStride;
        uint8_t* out = dst + (size_t)row * dstStride;
        for (int x = 0; x < width; ++x) {
            int yv = Cvt((int)yr[x] - c.offY, c.ys);
            int uu = (int)ur[x >> 1] - c.offC;
            int vv = (int)vr[x >> 1] - c.offC;
            int r = yv + Cvt(vv, c.rv);
            int g = yv + (((int32_t)uu * c.gu + (int32_t)vv * c.gv + 32768) >> 16);
            int b = yv + Cvt(uu, c.bu);
            out[x * 4 + 0] = (uint8_t)Clip8(b);
            out[x * 4 + 1] = (uint8_t)Clip8(g);
            out[x * 4 + 2] = (uint8_t)Clip8(r);
            out[x * 4 + 3] = 255;
        }
    }
}

void Yuv420pToBgra(uint8_t* dst, int dstStride, const uint8_t* y, const uint8_t* u, const uint8_t* v,
                   int yStride, int uvStride, int width, int height, Matrix matrix, bool fullRange) {
    YuvCoef c = MakeCoef(matrix, fullRange, 8);
    const int groups = width / 8;
    for (int row = 0; row < height; ++row) {
        uint8_t* out = dst + (size_t)row * dstStride;
        const uint8_t* yr = y + (size_t)row * yStride;
        const uint8_t* ur = u + (size_t)(row >> 1) * uvStride;
        const uint8_t* vr = v + (size_t)(row >> 1) * uvStride;
        if (Cpu().avx2 && groups > 0) {
            prism_yuv420_row_avx2(out, yr, ur, vr, groups, (const int32_t*)&c);
            int x0 = groups * 8;
            for (int x = x0; x < width; ++x) {
                int yv = Cvt((int)yr[x] - c.offY, c.ys);
                int uu = (int)ur[x >> 1] - c.offC;
                int vv = (int)vr[x >> 1] - c.offC;
                int r = yv + Cvt(vv, c.rv);
                int g = yv + (((int32_t)uu * c.gu + (int32_t)vv * c.gv + 32768) >> 16);
                int b = yv + Cvt(uu, c.bu);
                out[x * 4 + 0] = (uint8_t)Clip8(b);
                out[x * 4 + 1] = (uint8_t)Clip8(g);
                out[x * 4 + 2] = (uint8_t)Clip8(r);
                out[x * 4 + 3] = 255;
            }
        } else {
            for (int x = 0; x < width; ++x) {
                int yv = Cvt((int)yr[x] - c.offY, c.ys);
                int uu = (int)ur[x >> 1] - c.offC;
                int vv = (int)vr[x >> 1] - c.offC;
                int r = yv + Cvt(vv, c.rv);
                int g = yv + (((int32_t)uu * c.gu + (int32_t)vv * c.gv + 32768) >> 16);
                int b = yv + Cvt(uu, c.bu);
                out[x * 4 + 0] = (uint8_t)Clip8(b);
                out[x * 4 + 1] = (uint8_t)Clip8(g);
                out[x * 4 + 2] = (uint8_t)Clip8(r);
                out[x * 4 + 3] = 255;
            }
        }
    }
}

void Nv12ToBgra(uint8_t* dst, int dstStride, const uint8_t* y, const uint8_t* uv, int yStride, int uvStride,
                int width, int height, Matrix matrix, bool fullRange) {
    YuvCoef c = MakeCoef(matrix, fullRange, 8);
    for (int row = 0; row < height; ++row) {
        const uint8_t* yr = y + (size_t)row * yStride;
        const uint8_t* uvr = uv + (size_t)(row >> 1) * uvStride;
        uint8_t* out = dst + (size_t)row * dstStride;
        for (int x = 0; x < width; ++x) {
            int yv = Cvt((int)yr[x] - c.offY, c.ys);
            int uu = (int)uvr[(x >> 1) * 2] - c.offC;
            int vv = (int)uvr[(x >> 1) * 2 + 1] - c.offC;
            int r = yv + Cvt(vv, c.rv);
            int g = yv + (((int32_t)uu * c.gu + (int32_t)vv * c.gv + 32768) >> 16);
            int b = yv + Cvt(uu, c.bu);
            out[x * 4 + 0] = (uint8_t)Clip8(b);
            out[x * 4 + 1] = (uint8_t)Clip8(g);
            out[x * 4 + 2] = (uint8_t)Clip8(r);
            out[x * 4 + 3] = 255;
        }
    }
}

namespace {
inline int Cvt10(int sample, int32_t coef) {
    return (int)(((int64_t)sample * coef + 32768) >> 16);
}
}  // namespace


// 10-bit planes are packed down to 8 bits and fed to the AVX2 8-bit kernel. The
// 10-bit scalar path costs ~18 ms at 4K; this costs ~2 ms plus the asm kernel.
// Shift is 2 for yuv420p10le (10 bits in the low bits) and 8 for P010 (10 bits
// in the high bits of each 16-bit sample).
template <int Shift>
static void Pack16To8(uint8_t* dst, const uint16_t* src, size_t count) {
    size_t i = 0;
#if defined(_M_X64) || defined(__x86_64__)
    for (; i + 8 <= count; i += 8) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        v = _mm_srli_epi16(v, Shift);
        __m128i p = _mm_packus_epi16(v, v);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(dst + i), p);
    }
#endif
    for (; i < count; ++i) dst[i] = (uint8_t)(src[i] >> Shift);
}

void Yuv420p10ToBgra(uint8_t* dst, int dstStride, const uint16_t* y, const uint16_t* u, const uint16_t* v,
                     int yStride, int uvStride, int width, int height, Matrix matrix, bool fullRange) {
    if (width <= 0 || height <= 0) return;
    const int cw = (width + 1) / 2;
    const int ch = (height + 1) / 2;
    thread_local std::vector<uint8_t> y8, u8, v8;
    y8.resize((size_t)width * height);
    u8.resize((size_t)cw * ch);
    v8.resize((size_t)cw * ch);
    for (int row = 0; row < height; ++row)
        Pack16To8<2>(y8.data() + (size_t)row * width, y + (size_t)row * yStride, (size_t)width);
    for (int row = 0; row < ch; ++row) {
        Pack16To8<2>(u8.data() + (size_t)row * cw, u + (size_t)row * uvStride, (size_t)cw);
        Pack16To8<2>(v8.data() + (size_t)row * cw, v + (size_t)row * uvStride, (size_t)cw);
    }
    Yuv420pToBgra(dst, dstStride, y8.data(), u8.data(), v8.data(), width, cw, width, height, matrix, fullRange);
}

void P010ToBgra(uint8_t* dst, int dstStride, const uint16_t* y, const uint16_t* uv, int yStride, int uvStride,
                int width, int height, Matrix matrix, bool fullRange) {
    if (width <= 0 || height <= 0) return;
    const int cw = (width + 1) / 2;
    const int ch = (height + 1) / 2;
    thread_local std::vector<uint8_t> y8, uv8;
    y8.resize((size_t)width * height);
    uv8.resize((size_t)cw * 2 * ch);
    for (int row = 0; row < height; ++row)
        Pack16To8<8>(y8.data() + (size_t)row * width, y + (size_t)row * yStride, (size_t)width);
    for (int row = 0; row < ch; ++row)
        Pack16To8<8>(uv8.data() + (size_t)row * cw * 2, uv + (size_t)row * uvStride, (size_t)cw * 2);
    Nv12ToBgra(dst, dstStride, y8.data(), uv8.data(), width, cw * 2, width, height, matrix, fullRange);
}

void FillTestPattern(uint8_t* data, int w, int h, int stride, int seed) {
    for (int y = 0; y < h; ++y) {
        uint8_t* row = data + (size_t)y * stride;
        for (int x = 0; x < w; ++x) {
            row[x] = (uint8_t)((x * 3 + y * 5 + seed * 29) & 0xFF);
        }
    }
}

void FillTestPattern16(uint16_t* data, int w, int h, int stride, int seed) {
    for (int y = 0; y < h; ++y) {
        uint16_t* row = data + (size_t)y * stride;
        for (int x = 0; x < w; ++x) {
            row[x] = (uint16_t)((x * 7 + y * 11 + seed * 101) & 0x3FF);
        }
    }
}

}  // namespace prism
