#include "gfx.h"

#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include <cstring>

namespace {

const D3D_FEATURE_LEVEL kLevels[] = {
    D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
};

bool SaveWicPng(IWICImagingFactory* wic, const std::wstring& path, const uint8_t* bgra, int width, int height,
                int stride) {
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr = wic->CreateStream(&stream);
    if (FAILED(hr)) { Log("png: CreateStream 0x%08X", (unsigned)hr); return false; }
    hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) return false;
    hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) return false;
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return false;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) return false;
    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) return false;
    hr = frame->SetSize((UINT)width, (UINT)height);
    if (FAILED(hr)) return false;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
    hr = frame->SetPixelFormat(&fmt);
    if (FAILED(hr)) return false;
    hr = frame->WritePixels((UINT)height, (UINT)stride, (UINT)(stride * height), (BYTE*)bgra);
    if (FAILED(hr)) return false;
    hr = frame->Commit();
    if (FAILED(hr)) return false;
    hr = encoder->Commit();
    if (FAILED(hr)) return false;
    return true;
}

uint16_t HalfFloat(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exp = (int32_t)((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = bits & 0x7FFFFFu;
    if (exp <= 0) {
        if (exp < -10) return (uint16_t)sign;
        mant |= 0x800000u;
        const int shift = 14 - exp;
        uint32_t out = mant >> shift;
        if (shift > 0 && ((mant >> (shift - 1)) & 1u)) ++out;
        return (uint16_t)(sign | out);
    }
    if (exp >= 31) return (uint16_t)(sign | 0x7C00u);
    mant += 0x1000u;
    if (mant & 0x800000u) {
        mant = 0;
        if (++exp >= 31) return (uint16_t)(sign | 0x7C00u);
    }
    return (uint16_t)(sign | ((uint32_t)exp << 10) | (mant >> 13));
}

}  // namespace

RectF VideoDisplayRect(const RectF& area, int rotation, int videoW, int videoH, bool fillCrop) {
    rotation = ((rotation % 360) + 360) % 360;
    const bool sideways = rotation == 90 || rotation == 270;
    const float srcW = sideways ? (float)videoH : (float)videoW;
    const float srcH = sideways ? (float)videoW : (float)videoH;
    if (srcW <= 0 || srcH <= 0 || area.w <= 0 || area.h <= 0) return {};
    const float s = fillCrop ? std::max(area.w / srcW, area.h / srcH)
                             : std::min(area.w / srcW, area.h / srcH);
    const float dw = srcW * s, dh = srcH * s;
    return RectF{area.Cx() - dw * 0.5f, area.Cy() - dh * 0.5f, dw, dh};
}

bool Gfx::createDeviceResources() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, kLevels,
                                   (UINT)std::size(kLevels), D3D11_SDK_VERSION, &device_, &got, &ctx_);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, kLevels, (UINT)std::size(kLevels),
                               D3D11_SDK_VERSION, &device_, &got, &ctx_);
        if (FAILED(hr)) {
            Log("D3D11CreateDevice failed: 0x%08X", (unsigned)hr);
            return false;
        }
        Log("Using WARP renderer");
    }
    Log("gfx: d3d11 device ok (feature level 0x%X)", (unsigned)got);

    D2D1_FACTORY_OPTIONS fo{};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                           (void**)d2dFactory_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        Log("D2D1CreateFactory failed: 0x%08X", (unsigned)hr);
        return false;
    }

    ComPtr<IDXGIDevice1> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) return false;
    if (FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_))) return false;
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_))) return false;

    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    dc_->SetUnitMode(D2D1_UNIT_MODE_DIPS);
    Log("gfx: d2d device context ok");

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             (IUnknown**)dwrite_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        Log("DWriteCreateFactory failed: 0x%08X", (unsigned)hr);
        return false;
    }

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));
    if (FAILED(hr)) {
        Log("WIC factory failed: 0x%08X", (unsigned)hr);
        return false;
    }
    Log("gfx: dwrite + wic ok");
    return true;
}

bool Gfx::initDevice() { return createDeviceResources(); }

bool Gfx::initWindow(HWND hwnd, int width, int height, float uiScale) {
    hwnd_ = hwnd;
    headless_ = false;
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    scale_ = uiScale <= 0 ? 1.f : uiScale;
    if (!valid() && !createDeviceResources()) return false;
    hdrDisplay_ = detectHdrDisplay();
    hdrOutput_ = false;
    g_hdrOutput = false;
    if (!createSwapChainForFormat(DXGI_FORMAT_B8G8R8A8_UNORM)) return false;
    Log("gfx: window swapchain %dx%d format=%d hdrDisplay=%d", width_, height_, (int)swapFormat_, hdrDisplay_ ? 1 : 0);
    return true;
}

bool Gfx::detectHdrDisplay() {
    ComPtr<IDXGIDevice1> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) return false;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;
    HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIOutput> output;
        if (adapter->EnumOutputs(i, &output) != S_OK) break;
        DXGI_OUTPUT_DESC od{};
        if (FAILED(output->GetDesc(&od))) continue;
        if (od.Monitor != mon) continue;
        ComPtr<IDXGIOutput6> output6;
        if (SUCCEEDED(output.As(&output6))) {
            DXGI_OUTPUT_DESC1 d1{};
            if (SUCCEEDED(output6->GetDesc1(&d1))) {
                const bool hdr = d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                                 d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
                Log("gfx: output colorspace=%d hdr=%d maxLum=%.0f", (int)d1.ColorSpace, hdr ? 1 : 0, d1.MaxLuminance);
                return hdr;
            }
        }
        break;
    }
    return false;
}

bool Gfx::createSwapChainForFormat(DXGI_FORMAT format) {
    releaseTarget();
    swap_.Reset();
    ComPtr<IDXGIDevice1> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) return false;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = (UINT)width_;
    desc.Height = (UINT)height_;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Scaling = DXGI_SCALING_STRETCH;
    HRESULT hr = factory->CreateSwapChainForHwnd(device_.Get(), hwnd_, &desc, nullptr, nullptr, &swap_);
    if (FAILED(hr)) {
        Log("CreateSwapChainForHwnd(%d) failed: 0x%08X", (int)format, (unsigned)hr);
        return false;
    }
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        ComPtr<IDXGISwapChain3> swap3;
        hr = SUCCEEDED(swap_.As(&swap3)) ? swap3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
                                         : E_NOINTERFACE;
        Log("gfx: scRGB swapchain colorspace 0x%08X", (unsigned)hr);
    }
    ComPtr<IDXGISurface> surface;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    if (!createD2DTarget(surface.Get())) return false;
    swapFormat_ = format;
    return true;
}

bool Gfx::setHdrOutput(bool on) {
    if (headless_ || !hdrDisplay_) return false;
    if (on == hdrOutput_) return true;
    releaseHdrSize();
    video_.Reset();
    clearSubtitle();
    if (on) {
        if (!createSwapChainForFormat(DXGI_FORMAT_R16G16B16A16_FLOAT)) {
            Log("gfx: scRGB swapchain failed, reverting to SDR");
            createSwapChainForFormat(DXGI_FORMAT_B8G8R8A8_UNORM);
            hdrOutput_ = false;
            g_hdrOutput = false;
            return false;
        }
        hdrOutput_ = true;
        g_hdrOutput = true;
        Log("gfx: scRGB HDR output enabled");
    } else {
        if (!createSwapChainForFormat(DXGI_FORMAT_B8G8R8A8_UNORM)) return false;
        hdrOutput_ = false;
        g_hdrOutput = false;
        Log("gfx: SDR output");
    }
    return true;
}

bool Gfx::initHeadless(int width, int height, float uiScale) {
    headless_ = true;
    hdrDisplay_ = false;
    hdrOutput_ = false;
    g_hdrOutput = false;
    hwnd_ = nullptr;
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    scale_ = uiScale <= 0 ? 1.f : uiScale;
    if (!valid() && !createDeviceResources()) return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = (UINT)width_;
    desc.Height = (UINT)height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &offscreen_);
    if (FAILED(hr)) {
        Log("offscreen texture failed: 0x%08X", (unsigned)hr);
        return false;
    }
    if (FAILED(offscreen_.As(&offscreenSurface_))) return false;
    Log("gfx: headless target %dx%d @%.2f", width_, height_, scale_);
    return createD2DTarget(offscreenSurface_.Get());
}

bool Gfx::createD2DTarget(IDXGISurface* surface) {
    DXGI_SURFACE_DESC sd{};
    if (FAILED(surface->GetDesc(&sd))) return false;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(sd.Format, D2D1_ALPHA_MODE_IGNORE), 96.f * scale_, 96.f * scale_);
    ComPtr<ID2D1Bitmap1> bitmap;
    HRESULT hr = dc_->CreateBitmapFromDxgiSurface(surface, &props, &bitmap);
    if (FAILED(hr)) {
        Log("CreateBitmapFromDxgiSurface failed: 0x%08X", (unsigned)hr);
        return false;
    }
    target_ = bitmap;
    dc_->SetTarget(target_.Get());
    dc_->SetDpi(96.f * scale_, 96.f * scale_);
    return true;
}

void Gfx::releaseTarget() {
    if (dc_) dc_->SetTarget(nullptr);
    target_.Reset();
}

void Gfx::shutdown() {
    clearVideo();
    clearSubtitle();
    releaseHdrSize();
    hdrVs_.Reset();
    hdrPs_.Reset();
    hdrSampler_.Reset();
    hdrRaster_.Reset();
    releaseTarget();
    swap_.Reset();
    offscreen_.Reset();
    offscreenSurface_.Reset();
    dc_.Reset();
    d2dDevice_.Reset();
    d2dFactory_.Reset();
    dwrite_.Reset();
    wic_.Reset();
    ctx_.Reset();
    device_.Reset();
}

void Gfx::resize(int width, int height) {
    if (headless_ || !swap_) return;
    width = std::max(1, width);
    height = std::max(1, height);
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    releaseTarget();
    HRESULT hr = swap_->ResizeBuffers(2, (UINT)width_, (UINT)height_, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        Log("ResizeBuffers failed: 0x%08X", (unsigned)hr);
        return;
    }
    ComPtr<IDXGISurface> surface;
    if (SUCCEEDED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) createD2DTarget(surface.Get());
}

void Gfx::beginFrame() {
    dc_->BeginDraw();
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    dc_->PushAxisAlignedClip(D2D1::RectF(0, 0, dipW(), dipH()), D2D1_ANTIALIAS_MODE_ALIASED);
}

void Gfx::endFrame() {
    dc_->PopAxisAlignedClip();
    HRESULT hr = dc_->EndDraw();
    if (FAILED(hr)) {
        Log("EndDraw failed: 0x%08X", (unsigned)hr);
        if (hr == D2DERR_RECREATE_TARGET) deviceLost_ = true;
    }
    if (swap_) {
        hr = swap_->Present(1, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            Log("Present device lost: 0x%08X", (unsigned)hr);
            deviceLost_ = true;
        }
    }
}

void Gfx::updateVideo(const uint8_t* bgra, int width, int height, int stride) {
    if (!bgra || width <= 0 || height <= 0) return;
    if (video_ && hdrBitmap_ && video_.Get() == hdrBitmap_.Get()) {
        video_.Reset();
        hdrBitmap_.Reset();
        videoW_ = videoH_ = 0;
    }
    if (!video_ || videoW_ != width || videoH_ != height) {
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(hdrOutput_ ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_IGNORE),
            96.f, 96.f);
        video_.Reset();
        HRESULT hr = dc_->CreateBitmap(D2D1::SizeU((UINT32)width, (UINT32)height), nullptr, 0, &props, &video_);
        if (FAILED(hr)) {
            Log("CreateBitmap(video) failed: 0x%08X", (unsigned)hr);
            return;
        }
        videoW_ = width;
        videoH_ = height;
    }
    HRESULT hr = video_->CopyFromMemory(nullptr, bgra, (UINT32)stride);
    if (FAILED(hr)) Log("CopyFromMemory(video) failed: 0x%08X", (unsigned)hr);
}

void Gfx::clearVideo() {
    video_.Reset();
    hdrBitmap_.Reset();
    videoW_ = videoH_ = 0;
}

void Gfx::updateSubtitle(const uint8_t* bgra, int width, int height, int stride) {
    if (!bgra || width <= 0 || height <= 0 || !dc_) return;
    // The HDR target is scRGB 16F: sRGB subtitle pixels must be linearized and
    // scaled so SDR white stays at 203 nits, like every other UI color.
    const DXGI_FORMAT format =
        hdrOutput_ ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    if (!subtitle_ || subtitleW_ != width || subtitleH_ != height || subtitleFormat_ != format) {
        subtitle_.Reset();
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(format, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
        HRESULT hr = dc_->CreateBitmap(D2D1::SizeU((UINT32)width, (UINT32)height), nullptr, 0, &props, &subtitle_);
        if (FAILED(hr)) {
            Log("CreateBitmap(subtitle) failed: 0x%08X", (unsigned)hr);
            return;
        }
        subtitleW_ = width;
        subtitleH_ = height;
        subtitleFormat_ = format;
    }
    HRESULT hr;
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        hr = subtitle_->CopyFromMemory(nullptr, bgra, (UINT32)stride);
    } else {
        subtitleHdr_.resize((size_t)width * height * 4);
        uint16_t* dst = subtitleHdr_.data();
        const float white = 203.f / 80.f;
        for (int y = 0; y < height; ++y) {
            const uint8_t* row = bgra + (size_t)y * stride;
            for (int x = 0; x < width; ++x) {
                const float a = row[x * 4 + 3] / 255.f;
                if (a <= 0.f) {
                    *dst++ = 0;
                    *dst++ = 0;
                    *dst++ = 0;
                    *dst++ = 0;
                    continue;
                }
                const float inv = 1.f / a;
                auto channel = [&](uint8_t c) {
                    return SrgbToLinear(std::min(1.f, (c / 255.f) * inv)) * white * a;
                };
                *dst++ = HalfFloat(channel(row[x * 4 + 2]));  // R
                *dst++ = HalfFloat(channel(row[x * 4 + 1]));  // G
                *dst++ = HalfFloat(channel(row[x * 4 + 0]));  // B
                *dst++ = HalfFloat(a);
            }
        }
        hr = subtitle_->CopyFromMemory(nullptr, subtitleHdr_.data(), (UINT32)width * 8);
    }
    if (FAILED(hr)) Log("CopyFromMemory(subtitle) failed: 0x%08X", (unsigned)hr);
}

void Gfx::clearSubtitle() {
    subtitle_.Reset();
    subtitleHdr_.clear();
    subtitleW_ = subtitleH_ = 0;
    subtitleFormat_ = DXGI_FORMAT_UNKNOWN;
}

void Gfx::drawVideo(const RectF& area, int rotation, bool fillCrop) {
    if (!video_) return;
    rotation = ((rotation % 360) + 360) % 360;
    const float vw = (float)videoW_, vh = (float)videoH_;
    const bool sideways = rotation == 90 || rotation == 270;
    const float srcW = sideways ? vh : vw;
    const float srcH = sideways ? vw : vh;
    if (srcW <= 0 || srcH <= 0 || area.w <= 0 || area.h <= 0) return;

    float dw, dh;
    if (fillCrop) {
        float s = std::max(area.w / srcW, area.h / srcH);
        dw = srcW * s;
        dh = srcH * s;
    } else {
        float s = std::min(area.w / srcW, area.h / srcH);
        dw = srcW * s;
        dh = srcH * s;
    }
    const float cx = area.Cx(), cy = area.Cy();
    RectF dest{cx - dw * 0.5f, cy - dh * 0.5f, dw, dh};
    if (sideways) dest = RectF{cx - dh * 0.5f, cy - dw * 0.5f, dh, dw};

    // High-quality cubic for both down- and up-scaling; 1080p -> 1440p
    // benefits visibly over bilinear and the GPU cost is negligible.
    D2D1_INTERPOLATION_MODE mode = D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;
    if (rotation != 0) dc_->SetTransform(D2D1::Matrix3x2F::Rotation((float)rotation, D2D1::Point2F(cx, cy)));
    dc_->DrawBitmap(video_.Get(), dest.ToD2D(), 1.f, mode, nullptr);
    if (rotation != 0) dc_->SetTransform(D2D1::Matrix3x2F::Identity());
}


// ---------------------------------------------------------------------------
// HDR10 / Dolby Vision (profile 8, HDR10 base) -> SDR tonemap.
// YUV420P10 planes are uploaded as R16 textures and a fullscreen pixel shader
// does BT.2020 -> linear -> ACES -> BT.709 -> gamma 2.2.
// ---------------------------------------------------------------------------
namespace {
const char* kHdrShader = R"HLSL(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut vs_main(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = float2(p.x, 1.0 - p.y);
    return o;
}

Texture2D<float> texY : register(t0);
Texture2D<float> texU : register(t1);
Texture2D<float> texV : register(t2);
SamplerState samp : register(s0);
cbuffer Params : register(b0) { uint hdrOut; uint pad0; uint pad1; uint pad2; };

float3 PqToLinear(float3 e) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 p = pow(max(e, 1e-5), 1.0 / m2);
    float3 num = max(p - c1, 0.0);
    float3 den = max(c2 - c3 * p, 1e-5);
    return pow(num / den, 1.0 / m1) * 10000.0;
}

float3 Hable(float3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float4 ps_main(VSOut i) : SV_Target {
    const float codeScale = 65535.0;
    float y = texY.Sample(samp, i.uv).r * codeScale;
    float u = texU.Sample(samp, i.uv).r * codeScale;
    float v = texV.Sample(samp, i.uv).r * codeScale;
    y = (y - 64.0) / 876.0;
    u = (u - 512.0) / 896.0;
    v = (v - 512.0) / 896.0;
    float3 rgb = float3(y + 1.4746 * v, y - 0.16455 * u - 0.57135 * v, y + 1.8814 * u);
    rgb = max(rgb, 0.0);
    if (hdrOut == 2) {
        float3 lin = PqToLinear(rgb);
        float3 o;
        o.r = dot(lin, float3(1.6605, -0.5876, -0.0728));
        o.g = dot(lin, float3(-0.1246, 1.1329, -0.0083));
        o.b = dot(lin, float3(-0.0182, -0.1006, 1.1187));
        return float4(max(o, 0.0) / 80.0, 1.0);  // scRGB: 1.0 == 80 nits
    }
    if (hdrOut != 0) return float4(saturate(rgb), 1.0);
    float3 lin = PqToLinear(rgb) / 100.0;   // 100 nits == SDR white
    float3 t = Hable(lin * 1.35) / 0.6837;
    float3 o;
    o.r = dot(t, float3(1.6605, -0.5876, -0.0728));
    o.g = dot(t, float3(-0.1246, 1.1329, -0.0083));
    o.b = dot(t, float3(-0.0182, -0.1006, 1.1187));
    o = pow(saturate(o), 1.0 / 2.2);
    return float4(o, 1.0);
}
)HLSL";
}  // namespace

bool Gfx::initHdrPipeline() {
    if (hdrPs_ && hdrVs_) return true;
    if (!device_) return false;
    ComPtr<ID3DBlob> vsBlob, psBlob, errors;
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS;
    HRESULT hr = D3DCompile(kHdrShader, std::strlen(kHdrShader), "prism_hdr", nullptr, nullptr, "vs_main", "vs_5_0",
                            flags, 0, &vsBlob, &errors);
    if (FAILED(hr)) {
        Log("HDR shader VS: %s", errors ? (const char*)errors->GetBufferPointer() : "compile failed");
        return false;
    }
    hr = D3DCompile(kHdrShader, std::strlen(kHdrShader), "prism_hdr", nullptr, nullptr, "ps_main", "ps_5_0", flags, 0,
                    &psBlob, &errors);
    if (FAILED(hr)) {
        Log("HDR shader PS: %s", errors ? (const char*)errors->GetBufferPointer() : "compile failed");
        return false;
    }
    if (FAILED(device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
                                           hdrVs_.ReleaseAndGetAddressOf())) ||
        FAILED(device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
                                          hdrPs_.ReleaseAndGetAddressOf()))) {
        Log("HDR shader creation failed");
        return false;
    }
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sd, hdrSampler_.ReleaseAndGetAddressOf()))) return false;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device_->CreateRasterizerState(&rd, hdrRaster_.ReleaseAndGetAddressOf()))) return false;
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = 16;
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&cb, nullptr, hdrParams_.ReleaseAndGetAddressOf()))) return false;
    Log("HDR: pipeline ready (BT.2020 PQ; HDR10 passthrough or Hable tonemap)");
    return true;
}

void Gfx::releaseHdrSize() {
    if (hdrBitmap_ && video_.Get() == hdrBitmap_.Get()) video_.Reset();
    hdrBitmap_.Reset();
    hdrRtv_.Reset();
    hdrOut_.Reset();
    hdrYSrv_.Reset();
    hdrUSrv_.Reset();
    hdrVSrv_.Reset();
    hdrY_.Reset();
    hdrU_.Reset();
    hdrV_.Reset();
    hdrW_ = hdrH_ = 0;
}

void Gfx::updateVideoHdr(const uint8_t* planes, int width, int height, int yStride, int uvStride) {
    if (!planes || width <= 0 || height <= 0 || !dc_ || !ctx_ || !device_) return;
    if (!initHdrPipeline()) return;
    const int cw = (width + 1) / 2;
    const int ch = (height + 1) / 2;
    if (hdrW_ != width || hdrH_ != height) {
        releaseHdrSize();
        auto makePlane = [&](int tw, int th, ComPtr<ID3D11Texture2D>& tex,
                             ComPtr<ID3D11ShaderResourceView>& srv) {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = (UINT)tw;
            d.Height = (UINT)th;
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = DXGI_FORMAT_R16_UNORM;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DYNAMIC;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device_->CreateTexture2D(&d, nullptr, tex.ReleaseAndGetAddressOf()))) return false;
            return SUCCEEDED(device_->CreateShaderResourceView(tex.Get(), nullptr, srv.ReleaseAndGetAddressOf()));
        };
        if (!makePlane(width, height, hdrY_, hdrYSrv_) || !makePlane(cw, ch, hdrU_, hdrUSrv_) ||
            !makePlane(cw, ch, hdrV_, hdrVSrv_))
            return;
        D3D11_TEXTURE2D_DESC od{};
        od.Width = (UINT)width;
        od.Height = (UINT)height;
        od.MipLevels = 1;
        od.ArraySize = 1;
        od.Format = hdrOutput_ ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
        od.SampleDesc.Count = 1;
        od.Usage = D3D11_USAGE_DEFAULT;
        od.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&od, nullptr, hdrOut_.ReleaseAndGetAddressOf())) ||
            FAILED(device_->CreateRenderTargetView(hdrOut_.Get(), nullptr, hdrRtv_.ReleaseAndGetAddressOf())))
            return;
        hdrW_ = width;
        hdrH_ = height;
    }
    if (!hdrBitmap_) {
        ComPtr<IDXGISurface> surface;
        if (FAILED(hdrOut_.As(&surface))) return;
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(hdrOutput_ ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_IGNORE),
            96.f, 96.f);
        HRESULT hr = dc_->CreateBitmapFromDxgiSurface(surface.Get(), &props, hdrBitmap_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            Log("CreateBitmapFromDxgiSurface(hdr) failed: 0x%08X", (unsigned)hr);
            return;
        }
    }

    auto upload = [&](ID3D11Texture2D* tex, const uint8_t* src, int srcStride, int rowBytes, int rows) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx_->Map(tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
        for (int r = 0; r < rows; ++r)
            std::memcpy((uint8_t*)m.pData + (size_t)r * m.RowPitch, src + (size_t)r * srcStride, (size_t)rowBytes);
        ctx_->Unmap(tex, 0);
        return true;
    };
    const size_t yBytes = (size_t)yStride * height;
    const size_t cBytes = (size_t)uvStride * ch;
    if (!upload(hdrY_.Get(), planes, yStride, width * 2, height) ||
        !upload(hdrU_.Get(), planes + yBytes, uvStride, cw * 2, ch) ||
        !upload(hdrV_.Get(), planes + yBytes + cBytes, uvStride, cw * 2, ch))
        return;

    D3D11_MAPPED_SUBRESOURCE cbData{};
    if (SUCCEEDED(ctx_->Map(hdrParams_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cbData))) {
        uint32_t mode[4] = {hdrOutput_ ? 2u : 0u, 0u, 0u, 0u};
        std::memcpy(cbData.pData, mode, sizeof(mode));
        ctx_->Unmap(hdrParams_.Get(), 0);
    }
    ID3D11RenderTargetView* rtv = hdrRtv_.Get();
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0.f, 0.f, (float)width, (float)height, 0.f, 1.f};
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(hdrRaster_.Get());
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(hdrVs_.Get(), nullptr, 0);
    ctx_->PSSetShader(hdrPs_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[3] = {hdrYSrv_.Get(), hdrUSrv_.Get(), hdrVSrv_.Get()};
    ctx_->PSSetShaderResources(0, 3, srvs);
    ctx_->PSSetSamplers(0, 1, hdrSampler_.GetAddressOf());
    ctx_->PSSetConstantBuffers(0, 1, hdrParams_.GetAddressOf());
    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* nulls[3] = {};
    ctx_->PSSetShaderResources(0, 3, nulls);
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);

    video_ = hdrBitmap_;
    videoW_ = width;
    videoH_ = height;
}

bool Gfx::saveFramePng(const std::wstring& path) {
    if (!offscreen_ || !wic_) return false;
    D3D11_TEXTURE2D_DESC desc{};
    offscreen_->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging))) return false;
    ctx_->CopyResource(staging.Get(), offscreen_.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    bool ok = SaveWicPng(wic_.Get(), path, (const uint8_t*)map.pData, (int)desc.Width, (int)desc.Height,
                         (int)map.RowPitch);
    ctx_->Unmap(staging.Get(), 0);
    return ok;
}
