// Prism Player — shared definitions and small utilities.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <d2d1_3.h>
#include <d2d1helper.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------- geometry / color

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    float Right() const { return x + w; }
    float Bottom() const { return y + h; }
    float Cx() const { return x + w * 0.5f; }
    float Cy() const { return y + h * 0.5f; }
    D2D1_RECT_F ToD2D() const { return D2D1::RectF(x, y, x + w, y + h); }
    D2D1_POINT_2F Center() const { return D2D1::Point2F(Cx(), Cy()); }
    bool Contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    bool Contains(const D2D1_POINT_2F& p) const { return Contains(p.x, p.y); }
    RectF Inset(float d) const { return RectF{x + d, y + d, w - 2 * d, h - 2 * d}; }
    RectF Inset(float dx, float dy) const { return RectF{x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    RectF Offset(float dx, float dy) const { return RectF{x + dx, y + dy, w, h}; }
    RectF Expand(float d) const { return Inset(-d); }
};

using Color = D2D1_COLOR_F;

// HDR10 output: UI colors are authored in sRGB, but the swapchain is PQ/BT.2020.
// Map SDR white (1.0) to 203 nits, then ST2084 encode.
extern bool g_hdrOutput;
inline float SrgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float PqEncodeNits(float nits) {
    const float m1 = 0.1593017578125f, m2 = 78.84375f;
    const float c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
    float y = std::clamp(nits / 10000.f, 0.f, 1.f);
    float ym = std::pow(y, m1);
    return std::pow((c1 + c2 * ym) / (1.f + c3 * ym), m2);
}

inline Color Col(float r, float g, float b, float a = 1.f) { return Color{r, g, b, a}; }
inline Color Hex(uint32_t rgb, float a = 1.f) {
    float r = ((rgb >> 16) & 0xFF) / 255.f;
    float g = ((rgb >> 8) & 0xFF) / 255.f;
    float b = (rgb & 0xFF) / 255.f;
    if (g_hdrOutput) {
        // scRGB: 1.0 == 80 nits; SDR white maps to 203 nits.
        r = SrgbToLinear(r) * (203.f / 80.f);
        g = SrgbToLinear(g) * (203.f / 80.f);
        b = SrgbToLinear(b) * (203.f / 80.f);
    }
    return Color{r, g, b, a};
}
inline Color Fade(Color c, float alpha) { c.a *= alpha; return c; }
inline Color Mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// ---------------------------------------------------------------- strings / files

std::wstring Widen(std::string_view s);
std::string Narrow(std::wstring_view s);
std::wstring FileNameOf(const std::wstring& path);
std::wstring FileExtOf(const std::wstring& path);   // lowercase, without dot
std::wstring FormatTime(double seconds, bool withMillis = false);
std::wstring FormatBitrate(int64_t bps);
std::wstring FormatSize(int64_t bytes);

bool IsVideoExt(const std::wstring& ext);
bool IsAudioExt(const std::wstring& ext);
bool IsMediaExt(const std::wstring& ext);
bool IsSubtitleExt(const std::wstring& ext);
// Download-in-progress files: file.mkv.qkdownloading, file.mp4.part, ...
bool IsPartialName(const std::wstring& name);
std::wstring StripPartialName(const std::wstring& name);
bool IsPlayableMediaName(const std::wstring& name);

// %LOCALAPPDATA%\Prism (created on first use)
const std::wstring& DataDir();
const std::wstring& AppDir();

void Log(const char* fmt, ...);

// Reads a UTF-8 text file; empty on failure.
std::string ReadTextFile(const std::wstring& path);
bool WriteTextFile(const std::wstring& path, const std::string& text);

// Simple exponential smoothing for UI animation, frame-rate independent.
struct Spring {
    float value = 0.f;
    float target = 0.f;
    float speed = 14.f;
    void Step(float dt) {
        float k = 1.f - std::exp(-speed * dt);
        value += (target - value) * k;
        if (std::fabs(target - value) < 0.0008f) value = target;
    }
    bool Settled() const { return std::fabs(target - value) < 0.0008f; }
    void Snap(float v) { value = target = v; }
};

inline float EaseOutCubic(float t) { t = std::clamp(t, 0.f, 1.f); float u = 1.f - t; return 1.f - u * u * u; }
inline float EaseInOutCubic(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f;
}
