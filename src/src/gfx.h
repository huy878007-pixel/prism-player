// Direct3D 11 + Direct2D rendering surface, video texture upload and PNG capture.
#pragma once

#include "common.h"

class Gfx {
public:
    bool initDevice();
    bool initWindow(HWND hwnd, int width, int height, float uiScale);
    bool initHeadless(int width, int height, float uiScale);
    void shutdown();

    void resize(int width, int height);
    void beginFrame();
    void endFrame();
    bool saveFramePng(const std::wstring& path);
    // True once after EndDraw/Present detected a lost device; owner drops
    // device-dependent bitmaps on the next frame.
    bool takeDeviceLost() {
        const bool lost = deviceLost_;
        deviceLost_ = false;
        return lost;
    }

    bool setHdrOutput(bool on);
    bool hdrDisplay() const { return hdrDisplay_; }
    bool hdrOutput() const { return hdrOutput_; }

    void updateVideo(const uint8_t* bgra, int width, int height, int stride);
    void updateVideoHdr(const uint8_t* planes, int width, int height, int yStride, int uvStride);
    void clearVideo();
    void drawVideo(const RectF& area, int rotation, bool fillCrop = false);

    // Premultiplied BGRA subtitle layer (native ASS). Scaled by Direct2D into the video rect.
    void updateSubtitle(const uint8_t* bgra, int width, int height, int stride);
    void clearSubtitle();
    ID2D1Bitmap1* subtitleBitmap() const { return subtitle_.Get(); }

    ID2D1DeviceContext* dc() const { return dc_.Get(); }
    IDWriteFactory* dwrite() const { return dwrite_.Get(); }
    IWICImagingFactory* wic() const { return wic_.Get(); }
    ID2D1Factory1* factory() const { return d2dFactory_.Get(); }

    int width() const { return width_; }
    int height() const { return height_; }
    float scale() const { return scale_; }
    bool headless() const { return headless_; }
    bool valid() const { return device_ && dc_; }

private:
    bool createDeviceResources();
    bool createD2DTarget(IDXGISurface* surface);
    void releaseTarget();
    bool initHdrPipeline();
    void releaseHdrSize();
    bool createSwapChainForFormat(DXGI_FORMAT format);
    bool detectHdrDisplay();
    float dipW() const { return width_ / scale_; }
    float dipH() const { return height_ / scale_; }

    HWND hwnd_ = nullptr;
    bool headless_ = false;
    bool deviceLost_ = false;
    int width_ = 0, height_ = 0;
    float scale_ = 1.f;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGISwapChain1> swap_;
    DXGI_FORMAT swapFormat_ = DXGI_FORMAT_B8G8R8A8_UNORM;
    bool hdrDisplay_ = false;
    bool hdrOutput_ = false;
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> dc_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IWICImagingFactory> wic_;

    ComPtr<ID3D11Texture2D> offscreen_;
    ComPtr<IDXGISurface> offscreenSurface_;

    ComPtr<ID2D1Bitmap1> video_;
    int videoW_ = 0, videoH_ = 0;

    ComPtr<ID2D1Bitmap1> subtitle_;
    std::vector<uint16_t> subtitleHdr_;
    int subtitleW_ = 0, subtitleH_ = 0;
    DXGI_FORMAT subtitleFormat_ = DXGI_FORMAT_UNKNOWN;

    ComPtr<ID3D11VertexShader> hdrVs_;
    ComPtr<ID3D11PixelShader> hdrPs_;
    ComPtr<ID3D11SamplerState> hdrSampler_;
    ComPtr<ID3D11RasterizerState> hdrRaster_;
    ComPtr<ID3D11Texture2D> hdrY_, hdrU_, hdrV_, hdrOut_;
    ComPtr<ID3D11ShaderResourceView> hdrYSrv_, hdrUSrv_, hdrVSrv_;
    ComPtr<ID3D11RenderTargetView> hdrRtv_;
    ComPtr<ID2D1Bitmap1> hdrBitmap_;
    ComPtr<ID3D11Buffer> hdrParams_;
    int hdrW_ = 0, hdrH_ = 0;
};

// Letterboxed on-screen rect of the video inside `area` (rotation applied).
RectF VideoDisplayRect(const RectF& area, int rotation, int videoW, int videoH, bool fillCrop = false);
