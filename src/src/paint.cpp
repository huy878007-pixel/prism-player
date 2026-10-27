#include "paint.h"

#include <d2d1_1helper.h>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

bool Painter::Init(Gfx* gfx) {
    gfx_ = gfx;
    dc_ = gfx->dc();
    dwrite_ = gfx->dwrite();
    if (!dc_ || !dwrite_) return false;

    if (FAILED(dc_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_))) return false;

    std::wstring families[3] = {L"Segoe UI Variable Text", L"Segoe UI Variable Display", L"Consolas"};
    if (SUCCEEDED(dwrite_->GetSystemFontCollection(&systemFonts_, FALSE)) && systemFonts_) {
        const wchar_t* fallbacks[3] = {L"Segoe UI", L"Segoe UI", L"Consolas"};
        for (int i = 0; i < 3; ++i) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            systemFonts_->FindFamilyName(families[i].c_str(), &index, &exists);
            if (!exists) families[i] = fallbacks[i];
        }
    }
    familyText_ = families[0];
    familyDisplay_ = families[1];
    familyMono_ = families[2];
    Log("Fonts: text=%ls display=%ls mono=%ls", familyText_.c_str(), familyDisplay_.c_str(),
        familyMono_.c_str());

    // A tiny tiled grain overlay. It hides banding in the large soft gradients and
    // gives the dark surfaces a little material.
    const int N = 128;
    std::vector<uint32_t> px((size_t)N * N);
    uint32_t seed = 0x9E3779B9u;
    for (int i = 0; i < N * N; ++i) {
        seed = seed * 1664525u + 1013904223u;
        uint32_t a = (seed >> 24) & 0xFF;
        a = (uint32_t)(a * 0.55f);
        px[i] = (a << 24) | (a << 16) | (a << 8) | a;
    }
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.f, 96.f);
    if (SUCCEEDED(dc_->CreateBitmap(D2D1::SizeU(N, N), px.data(), N * 4, &bp, &noise_))) {
        D2D1_BITMAP_BRUSH_PROPERTIES1 brushProps = D2D1::BitmapBrushProperties1(
            D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        D2D1_BRUSH_PROPERTIES brushBase = D2D1::BrushProperties(1.f, D2D1::IdentityMatrix());
        dc_->CreateBitmapBrush(noise_.Get(), brushProps, brushBase, &noiseBrush_);
    }
    return true;
}

const wchar_t* Painter::Family(FontFace face) {
    switch (face) {
        case FontFace::Display: return familyDisplay_.c_str();
        case FontFace::Mono: return familyMono_.c_str();
        case FontFace::Custom: return familyCustom_.empty() ? familyText_.c_str() : familyCustom_.c_str();
        default: return familyText_.c_str();
    }
}

const std::vector<std::wstring>& Painter::SystemFontFamilies() {
    static const std::vector<std::wstring> families = [this] {
        std::vector<std::wstring> out;
        ComPtr<IDWriteFontCollection> collection;
        if (!dwrite_ || FAILED(dwrite_->GetSystemFontCollection(&collection, FALSE)) || !collection) return out;
        const UINT32 count = collection->GetFontFamilyCount();
        out.reserve(count);
        for (UINT32 i = 0; i < count; ++i) {
            ComPtr<IDWriteFontFamily> family;
            if (FAILED(collection->GetFontFamily(i, &family)) || !family) continue;
            ComPtr<IDWriteLocalizedStrings> names;
            if (FAILED(family->GetFamilyNames(&names)) || !names || names->GetCount() == 0) continue;
            UINT32 at = 0;
            BOOL found = FALSE;
            wchar_t userLocale[LOCALE_NAME_MAX_LENGTH]{};
            GetUserDefaultLocaleName(userLocale, LOCALE_NAME_MAX_LENGTH);
            const wchar_t* prefs[] = {L"zh-cn", L"zh-Hans", userLocale, L"en-us"};
            for (const wchar_t* loc : prefs) {
                if (!loc || !*loc) continue;
                if (SUCCEEDED(names->FindLocaleName(loc, &at, &found)) && found) break;
            }
            if (!found) at = 0;
            UINT32 len = 0;
            if (FAILED(names->GetStringLength(at, &len)) || len == 0) continue;
            std::wstring name(len + 1, L'\0');
            if (FAILED(names->GetString(at, name.data(), len + 1))) continue;
            name.resize(len);
            // GDI's "@Family" vertical aliases are not useful for horizontal text.
            if (name.empty() || name.front() == L'@') continue;
            out.push_back(std::move(name));
        }
        std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
        });
        return out;
    }();
    return families;
}

bool Painter::SetSubtitleFamily(const std::wstring& family) {
    familyCustom_ = family;
    ++customGen_;
    formats_.clear();
    ellipses_.clear();
    if (familyCustom_.empty()) return true;

    UINT32 index = 0;
    BOOL installed = FALSE;
    if (systemFonts_) systemFonts_->FindFamilyName(familyCustom_.c_str(), &index, &installed);
    ComPtr<IDWriteTextFormat> probe;
    HRESULT hr = E_FAIL;
    if (installed) {
        hr = dwrite_->CreateTextFormat(familyCustom_.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.f, L"zh-cn",
                                       &probe);
    }
    if (installed && SUCCEEDED(hr)) {
        Log("subtitle font: family=%s", Narrow(familyCustom_).c_str());
        return true;
    }
    Log("subtitle font: family=%s unavailable (installed=%d hr=0x%08X), fallback=%s",
        Narrow(familyCustom_).c_str(), installed ? 1 : 0, (unsigned)hr, Narrow(familyText_).c_str());
    familyCustom_ = familyText_;
    ++customGen_;
    formats_.clear();
    ellipses_.clear();
    return false;
}

IDWriteTextFormat* Painter::Format(float size, int weight, FontFace face, DWRITE_TEXT_ALIGNMENT align,
                                   bool wrap, float lineHeight) {
    // Subtitles resolve their family through the system collection; a selected
    // name only changes the family string, never the collection.
    const bool custom = face == FontFace::Custom && !familyCustom_.empty();
    FormatKey key{size, weight, (int)face, (int)align, wrap ? 1 : 0, lineHeight, custom ? customGen_ : 0};
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second.Get();

    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = dwrite_->CreateTextFormat(Family(face), nullptr,
                                           (DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, size, L"zh-cn", &format);
    if (FAILED(hr) && custom) {
        Log("subtitle font: CreateTextFormat failed 0x%08X, using system text font", (unsigned)hr);
        hr = dwrite_->CreateTextFormat(familyText_.c_str(), nullptr, (DWRITE_FONT_WEIGHT)weight,
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-cn",
                                       &format);
    }
    if (FAILED(hr)) return nullptr;
    format->SetTextAlignment(align);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, size * lineHeight, size * 0.82f);
    IDWriteTextFormat* raw = format.Get();
    formats_.emplace(key, std::move(format));
    return raw;
}

IDWriteInlineObject* Painter::Ellipsis(IDWriteTextFormat* format) {
    if (!format) return nullptr;
    auto it = ellipses_.find(format);
    if (it != ellipses_.end()) return it->second.Get();
    ComPtr<IDWriteInlineObject> sign;
    if (FAILED(dwrite_->CreateEllipsisTrimmingSign(format, &sign)) || !sign) return nullptr;
    IDWriteInlineObject* raw = sign.Get();
    ellipses_.emplace(format, std::move(sign));
    return raw;
}

void Painter::TextCore(std::wstring_view s, const RectF& r, float size, Color color, int weight, FontFace face,
                       DWRITE_TEXT_ALIGNMENT align, DWRITE_PARAGRAPH_ALIGNMENT valign, bool wrap, float tracking,
                       float lineHeight, bool ellipsis) {
    if (s.empty() || r.w <= 0 || r.h <= 0 || size <= 0) return;
    IDWriteTextFormat* format = Format(size, weight, face, align, wrap, lineHeight);
    if (!format) return;
    format->SetParagraphAlignment(valign);

    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = dwrite_->CreateTextLayout(s.data(), (UINT32)s.size(), format, std::max(r.w, 1.f),
                                           std::max(r.h, size * 1.25f), &layout);
    if (FAILED(hr) || !layout) return;

    if (tracking != 0.f) {
        ComPtr<IDWriteTextLayout1> l1;
        if (SUCCEEDED(layout.As(&l1))) {
            DWRITE_TEXT_RANGE range{0, (UINT32)s.size()};
            l1->SetCharacterSpacing(tracking * size, tracking * size, 0.f, range);
        }
    }
    if (ellipsis && !wrap) {
        if (IDWriteInlineObject* sign = Ellipsis(format)) {
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            layout->SetTrimming(&trimming, sign);
        }
    }
    brush_->SetColor(color);
    dc_->DrawTextLayout(D2D1::Point2F(r.x, r.y), layout.Get(), brush_, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Painter::Text(std::wstring_view s, const RectF& r, float size, Color c, int weight, FontFace face,
                   bool ellipsis) {
    TextCore(s, r, size, c, weight, face, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false,
             0.f, 1.3f, ellipsis);
}

void Painter::TextC(std::wstring_view s, const RectF& r, float size, Color c, int weight, FontFace face,
                    bool ellipsis) {
    TextCore(s, r, size, c, weight, face, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false,
             0.f, 1.3f, ellipsis);
}

void Painter::TextR(std::wstring_view s, const RectF& r, float size, Color c, int weight, FontFace face,
                    bool ellipsis) {
    TextCore(s, r, size, c, weight, face, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false,
             0.f, 1.3f, ellipsis);
}

void Painter::TextWrap(std::wstring_view s, const RectF& r, float size, Color c, int weight, FontFace face,
                       float lineHeight) {
    TextCore(s, r, size, c, weight, face, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true, 0.f,
             lineHeight, false);
}

void Painter::TextTracked(std::wstring_view s, const RectF& r, float size, Color c, float tracking, int weight,
                          FontFace face) {
    TextCore(s, r, size, c, weight, face, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false,
             tracking, 1.3f, false);
}

float Painter::Measure(std::wstring_view s, float size, int weight, FontFace face) {
    if (s.empty()) return 0.f;
    IDWriteTextFormat* format = Format(size, weight, face, DWRITE_TEXT_ALIGNMENT_LEADING, false, 1.3f);
    if (!format) return 0.f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(s.data(), (UINT32)s.size(), format, 100000.f, size * 2.f, &layout)))
        return 0.f;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void Painter::Fill(const RectF& r, Color c) {
    brush_->SetColor(c);
    dc_->FillRectangle(r.ToD2D(), brush_);
}

void Painter::Round(const RectF& r, float radius, Color c) {
    float rad = std::min({radius, r.w * 0.5f, r.h * 0.5f});
    brush_->SetColor(c);
    dc_->FillRoundedRectangle(D2D1::RoundedRect(r.ToD2D(), rad, rad), brush_);
}

void Painter::RoundStroke(const RectF& r, float radius, float width, Color c) {
    float rad = std::min({radius, r.w * 0.5f, r.h * 0.5f});
    brush_->SetColor(c);
    dc_->DrawRoundedRectangle(D2D1::RoundedRect(r.Inset(width * 0.5f).ToD2D(), rad, rad), brush_, width);
}

void Painter::Linear(const RectF& r, Color a, Color b, float angleDeg) {
    D2D1_GRADIENT_STOP stops[2] = {{0.f, a}, {1.f, b}};
    ComPtr<ID2D1GradientStopCollection> coll;
    if (FAILED(dc_->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll))) return;
    float ang = angleDeg * kPi / 180.f;
    float dx = std::cos(ang), dy = std::sin(ang);
    float len = std::fabs(r.w * dx) + std::fabs(r.h * dy);
    D2D1_POINT_2F center = D2D1::Point2F(r.Cx(), r.Cy());
    D2D1_POINT_2F start = D2D1::Point2F(center.x - dx * len * 0.5f, center.y - dy * len * 0.5f);
    D2D1_POINT_2F end = D2D1::Point2F(center.x + dx * len * 0.5f, center.y + dy * len * 0.5f);
    ComPtr<ID2D1LinearGradientBrush> grad;
    if (FAILED(dc_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(start, end), coll.Get(), &grad)))
        return;
    dc_->FillRectangle(r.ToD2D(), grad.Get());
}

void Painter::LinearRound(const RectF& r, float radius, Color a, Color b, float angleDeg) {
    PushClip(r, radius);
    Linear(r, a, b, angleDeg);
    PopClip();
}

void Painter::Glow(const RectF& r, Color inner) {
    D2D1_GRADIENT_STOP stops[3] = {{0.f, inner}, {0.5f, Fade(inner, inner.a * 0.35f)}, {1.f, Fade(inner, 0.f)}};
    ComPtr<ID2D1GradientStopCollection> coll;
    if (FAILED(dc_->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll))) return;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props{};
    props.center = D2D1::Point2F(r.Cx(), r.Cy());
    props.gradientOriginOffset = D2D1::Point2F(0, 0);
    props.radiusX = r.w * 0.5f;
    props.radiusY = r.h * 0.5f;
    ComPtr<ID2D1RadialGradientBrush> grad;
    if (FAILED(dc_->CreateRadialGradientBrush(props, coll.Get(), &grad))) return;
    dc_->FillEllipse(D2D1::Ellipse(props.center, props.radiusX, props.radiusY), grad.Get());
}

void Painter::Line(float x1, float y1, float x2, float y2, float width, Color c) {
    brush_->SetColor(c);
    dc_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_, width);
}

void Painter::Circle(float cx, float cy, float radius, Color c) {
    brush_->SetColor(c);
    dc_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush_);
}

void Painter::Ring(float cx, float cy, float radius, float stroke, Color c) {
    brush_->SetColor(c);
    dc_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush_, stroke);
}

void Painter::PushClip(const RectF& r, float radius) {
    radius = std::min({radius, r.w * 0.5f, r.h * 0.5f});
    if (radius <= 0.5f) {
        dc_->PushAxisAlignedClip(r.ToD2D(), D2D1_ANTIALIAS_MODE_ALIASED);
        layerStack_.push_back(1);
        return;
    }
    ComPtr<ID2D1RoundedRectangleGeometry> geo;
    if (FAILED(gfx_->factory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r.ToD2D(), radius, radius), &geo))) {
        dc_->PushAxisAlignedClip(r.ToD2D(), D2D1_ANTIALIAS_MODE_ALIASED);
        layerStack_.push_back(1);
        return;
    }
    ComPtr<ID2D1Layer> layer;
    if (FAILED(dc_->CreateLayer(nullptr, &layer))) return;
    dc_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), geo.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                          D2D1::IdentityMatrix(), 1.f, nullptr, D2D1_LAYER_OPTIONS1_NONE),
                   layer.Get());
    layers_.push_back(layer);
    layerStack_.push_back(2);
}

void Painter::PopClip() {
    if (layerStack_.empty()) return;
    int kind = layerStack_.back();
    layerStack_.pop_back();
    if (kind == 1) dc_->PopAxisAlignedClip();
    else if (kind == 2) {
        dc_->PopLayer();
        if (!layers_.empty()) layers_.pop_back();
    }
}

void Painter::PushOpacity(float opacity) {
    ComPtr<ID2D1Layer> layer;
    if (FAILED(dc_->CreateLayer(nullptr, &layer))) return;
    dc_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                          D2D1::IdentityMatrix(), std::clamp(opacity, 0.f, 1.f), nullptr,
                                          D2D1_LAYER_OPTIONS1_NONE),
                   layer.Get());
    layers_.push_back(layer);
    layerStack_.push_back(3);
}

void Painter::PopOpacity() {
    if (layerStack_.empty()) return;
    if (layerStack_.back() != 3) {
        PopClip();
        return;
    }
    layerStack_.pop_back();
    dc_->PopLayer();
    if (!layers_.empty()) layers_.pop_back();
}

void Painter::Noise(const RectF& r, float opacity) {
    if (!noiseBrush_ || opacity <= 0.f) return;
    noiseBrush_->SetOpacity(std::clamp(opacity, 0.f, 1.f));
    dc_->FillRectangle(r.ToD2D(), noiseBrush_.Get());
}

void Painter::Bitmap(ID2D1Bitmap1* bmp, const RectF& dest, float opacity, float radius,
                     D2D1_INTERPOLATION_MODE mode) {
    if (!bmp) return;
    if (radius > 0.5f) PushClip(dest, radius);
    if (opacity < 0.999f) PushOpacity(opacity);
    dc_->DrawBitmap(bmp, dest.ToD2D(), 1.f, mode, nullptr);
    if (opacity < 0.999f) PopOpacity();
    if (radius > 0.5f) PopClip();
}

ComPtr<ID2D1Bitmap1> Painter::CreateBitmapFromBgra(const uint8_t* bgra, int width, int height, int stride) {
    ComPtr<ID2D1Bitmap1> bmp;
    if (!dc_ || !bgra || width <= 0 || height <= 0 || stride < width * 4) return bmp;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
    if (FAILED(dc_->CreateBitmap(D2D1::SizeU((UINT32)width, (UINT32)height), bgra, (UINT32)stride, &props, &bmp)))
        bmp.Reset();
    return bmp;
}
