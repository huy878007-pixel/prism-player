// Low-level pixel kernels: hand-written x86-64 assembly plus scalar references.
#pragma once

#include <cstdint>
#include <string>

namespace prism {

enum class Matrix { BT601, BT709, BT2020 };

struct CpuInfo {
    std::string brand;
    bool avx2 = false;
    bool avx512 = false;
    bool fma = false;
};
const CpuInfo& Cpu();
const char* CpuSummary();

// Identical fixed-point math in C and assembly, so tests can demand bit equality.
struct YuvCoef {
    int32_t ys, rv, gu, gv, bu, offY, offC, pad;
};
YuvCoef MakeCoef(Matrix m, bool fullRange, int bitDepth);

void Yuv420pToBgra(uint8_t* dst, int dstStride, const uint8_t* y, const uint8_t* u, const uint8_t* v,
                   int yStride, int uvStride, int width, int height, Matrix matrix, bool fullRange);
void Yuv420pToBgraRef(uint8_t* dst, int dstStride, const uint8_t* y, const uint8_t* u, const uint8_t* v,
                      int yStride, int uvStride, int width, int height, Matrix matrix, bool fullRange);
void Nv12ToBgra(uint8_t* dst, int dstStride, const uint8_t* y, const uint8_t* uv, int yStride, int uvStride,
                int width, int height, Matrix matrix, bool fullRange);
void Yuv420p10ToBgra(uint8_t* dst, int dstStride, const uint16_t* y, const uint16_t* u, const uint16_t* v,
                     int yStride, int uvStride, int width, int height, Matrix matrix, bool fullRange);
void P010ToBgra(uint8_t* dst, int dstStride, const uint16_t* y, const uint16_t* uv, int yStride, int uvStride,
                int width, int height, Matrix matrix, bool fullRange);

// Bench helper: fills a plane with a deterministic gradient.
void FillTestPattern(uint8_t* data, int w, int h, int stride, int seed);
void FillTestPattern16(uint16_t* data, int w, int h, int stride, int seed);

}  // namespace prism
