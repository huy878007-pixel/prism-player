// Small immediate-mode drawing helper over Direct2D: shapes, gradients, text, clips.
#pragma once

#include "gfx.h"

enum class FontFace { Text, Display, Mono, Custom };

class Painter {
public:
    bool Init(Gfx* gfx);

    ID2D1DeviceContext* dc() const { return dc_; }
    ID2D1SolidColorBrush* brush() const { return brush_; }
    ID2D1Factory* factory() const { return gfx_ ? gfx_->factory() : nullptr; }

    void Fill(const RectF& r, Color c);
    void Round(const RectF& r, float radius, Color c);
    void RoundStroke(const RectF& r, float radius, float width, Color c);
    void Linear(const RectF& r, Color a, Color b, float angleDeg = 90.f);
    void LinearRound(const RectF& r, float radius, Color a, Color b, float angleDeg = 90.f);
    void Glow(const RectF& r, Color inner);
    void Line(float x1, float y1, float x2, float y2, float width, Color c);
    void Circle(float cx, float cy, float radius, Color c);
    void Ring(float cx, float cy, float radius, float stroke, Color c);

    void Text(std::wstring_view s, const RectF& r, float size, Color c, int weight = 400,
              FontFace face = FontFace::Text, bool ellipsis = true);
    void TextC(std::wstring_view s, const RectF& r, float size, Color c, int weight = 400,
               FontFace face = FontFace::Text, bool ellipsis = true);
    void TextR(std::wstring_view s, const RectF& r, float size, Color c, int weight = 400,
               FontFace face = FontFace::Text, bool ellipsis = true);
    void TextWrap(std::wstring_view s, const RectF& r, float size, Color c, int weight = 400,
                  FontFace face = FontFace::Text, float lineHeight = 1.45f);
    void TextTracked(std::wstring_view s, const RectF& r, float size, Color c, float tracking,
                     int weight = 500, FontFace face = FontFace::Text);
    void TextCore(std::wstring_view s, const RectF& r, float size, Color color, int weight, FontFace face,
                  DWRITE_TEXT_ALIGNMENT align, DWRITE_PARAGRAPH_ALIGNMENT valign, bool wrap, float tracking,
                  float lineHeight, bool ellipsis);

    float Measure(std::wstring_view s, float size, int weight = 400, FontFace face = FontFace::Text);

    // Selects the installed family used by FontFace::Custom (subtitle text only).
    // The name is resolved against the system font collection, so no font file is
    // loaded. Returns false and falls back to the UI text family when the family
    // is missing or CreateTextFormat rejects it.
    bool SetSubtitleFamily(const std::wstring& family);

    // Family FontFace::Custom currently resolves to (the selected family, or the
    // UI text family when no selection is stored).
    const std::wstring& SubtitleFamily() const { return familyCustom_.empty() ? familyText_ : familyCustom_; }

    // Installed system font families (en-us names), sorted. Enumerated from the
    // DirectWrite system collection once per process; the reference is stable.
    const std::vector<std::wstring>& SystemFontFamilies();

    void PushClip(const RectF& r, float radius = 0.f);
    void PopClip();
    void PushOpacity(float opacity);
    void PopOpacity();

    void Bitmap(ID2D1Bitmap1* bmp, const RectF& dest, float opacity = 1.f, float radius = 0.f,
                D2D1_INTERPOLATION_MODE mode = D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    // Top-down premultiplied BGRA. Direct2D resources stay on the UI thread.
    ComPtr<ID2D1Bitmap1> CreateBitmapFromBgra(const uint8_t* bgra, int width, int height, int stride);
    void Noise(const RectF& r, float opacity);

private:
    IDWriteTextFormat* Format(float size, int weight, FontFace face, DWRITE_TEXT_ALIGNMENT align, bool wrap,
                              float lineHeight);
    IDWriteInlineObject* Ellipsis(IDWriteTextFormat* format);
    const wchar_t* Family(FontFace face);

    Gfx* gfx_ = nullptr;
    ID2D1DeviceContext* dc_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    IDWriteFactory* dwrite_ = nullptr;
    ComPtr<IDWriteFontCollection> systemFonts_;
    IDWriteInlineObject* ellipsis_ = nullptr;

    std::wstring familyText_ = L"Segoe UI Variable Text";
    std::wstring familyDisplay_ = L"Segoe UI Variable Display";
    std::wstring familyMono_ = L"Consolas";
    std::wstring familyCustom_;
    int customGen_ = 0;

    struct FormatKey {
        float size;
        int weight;
        int face;
        int align;
        int wrap;
        float lineHeight;
        int custom;
        bool operator==(const FormatKey& o) const {
            return size == o.size && weight == o.weight && face == o.face && align == o.align && wrap == o.wrap &&
                   lineHeight == o.lineHeight && custom == o.custom;
        }
    };
    struct FormatHash {
        size_t operator()(const FormatKey& k) const {
            size_t h = (size_t)(k.size * 7.13f);
            h = h * 131 + (size_t)k.weight;
            h = h * 131 + (size_t)k.face;
            h = h * 131 + (size_t)k.align;
            h = h * 131 + (size_t)k.wrap;
            h = h * 131 + (size_t)(k.lineHeight * 100.f);
            h = h * 131 + (size_t)k.custom;
            return h;
        }
    };
    std::unordered_map<FormatKey, ComPtr<IDWriteTextFormat>, FormatHash> formats_;
    std::unordered_map<IDWriteTextFormat*, ComPtr<IDWriteInlineObject>> ellipses_;

    std::vector<int> layerStack_;  // 1 = axis clip, 2 = rounded layer, 3 = opacity layer
    std::vector<ComPtr<ID2D1Layer>> layers_;
    ComPtr<ID2D1Bitmap1> noise_;
    ComPtr<ID2D1BitmapBrush1> noiseBrush_;
};
