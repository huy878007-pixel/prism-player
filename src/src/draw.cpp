#include "ui.h"

#include "ass_render.h"

#include <d2d1_1helper.h>

namespace theme {
Color Bg = Hex(0x08090D);
Color Bg2 = Hex(0x0B0D13);
Color Surface = Hex(0x10131A);
Color Text = Hex(0xF3F5FA);
Color Text2 = Hex(0xF3F5FA, 0.62f);
Color Text3 = Hex(0xF3F5FA, 0.38f);
Color Line = Hex(0xFFFFFF, 0.075f);
Color LineHi = Hex(0xFFFFFF, 0.14f);
Color Accent = Hex(0x74E3F3);
Color AccentWarm = Hex(0xFFC978);
Color Danger = Hex(0xFF6B6B);
}  // namespace theme

static int g_accentPreset = 0;

void ApplyAccentPreset(int preset) {
    static const Color accents[] = {Hex(0x74E3F3), Hex(0xB18CFF), Hex(0xFF8FB1), Hex(0xFFC978),
                                    Hex(0x8BE28B)};
    g_accentPreset = std::clamp(preset, 0, 4);
    theme::Accent = accents[g_accentPreset];
    theme::AccentWarm = g_accentPreset == 3 ? Hex(0x8BE28B) : Hex(0xFFC978);
}

void ApplyThemeColors() {
    theme::Bg = Hex(0x08090D);
    theme::Bg2 = Hex(0x0B0D13);
    theme::Surface = Hex(0x10131A);
    theme::Text = Hex(0xF3F5FA);
    theme::Text2 = Hex(0xF3F5FA, 0.62f);
    theme::Text3 = Hex(0xF3F5FA, 0.38f);
    theme::Line = Hex(0xFFFFFF, 0.075f);
    theme::LineHi = Hex(0xFFFFFF, 0.14f);
    theme::Accent = Hex(0x74E3F3);
    theme::AccentWarm = Hex(0xFFC978);
    theme::Danger = Hex(0xFF6B6B);
    ApplyAccentPreset(g_accentPreset);
}

namespace {

const wchar_t* kBrand = L"PRISM";
const wchar_t* kTagline = L"本地播放器";

void StrokeArc(Painter& p, float cx, float cy, float radius, float startDeg, float sweepDeg, float width,
               Color c) {
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(p.factory()->CreatePathGeometry(&geo))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink))) return;
    float a0 = startDeg * 3.14159265f / 180.f;
    float a1 = (startDeg + sweepDeg) * 3.14159265f / 180.f;
    D2D1_POINT_2F start = D2D1::Point2F(cx + cosf(a0) * radius, cy + sinf(a0) * radius);
    D2D1_POINT_2F end = D2D1::Point2F(cx + cosf(a1) * radius, cy + sinf(a1) * radius);
    sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
    D2D1_ARC_SEGMENT arc{};
    arc.point = end;
    arc.size = D2D1::SizeF(radius, radius);
    arc.rotationAngle = 0;
    arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arc.arcSize = std::fabs(sweepDeg) > 180.f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
    sink->AddArc(arc);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    p.brush()->SetColor(c);
    p.dc()->DrawGeometry(geo.Get(), p.brush(), width);
}

void PrismMark(Painter& p, float cx, float cy, float size, float stroke, Color color, bool beams) {
    float h = size * 0.866f;
    ComPtr<ID2D1PathGeometry> geo;
    if (SUCCEEDED(p.factory()->CreatePathGeometry(&geo))) {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(geo->Open(&sink))) {
            D2D1_POINT_2F a = D2D1::Point2F(cx, cy - h * 0.62f);
            D2D1_POINT_2F b = D2D1::Point2F(cx - size * 0.5f, cy + h * 0.38f);
            D2D1_POINT_2F c = D2D1::Point2F(cx + size * 0.5f, cy + h * 0.38f);
            sink->BeginFigure(a, D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(b);
            sink->AddLine(c);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            p.brush()->SetColor(color);
            p.dc()->DrawGeometry(geo.Get(), p.brush(), stroke);
        }
    }
    if (beams) {
        float hitX = cx + size * 0.14f;
        float hitY = cy + size * 0.02f;
        p.Line(hitX, hitY, cx + size * 0.86f, hitY - size * 0.26f, std::max(1.f, stroke * 0.7f),
               Fade(theme::Accent, 0.9f));
        p.Line(hitX, hitY, cx + size * 0.86f, hitY, std::max(1.f, stroke * 0.7f),
               Fade(theme::AccentWarm, 0.85f));
        p.Line(hitX, hitY, cx + size * 0.86f, hitY + size * 0.26f, std::max(1.f, stroke * 0.7f),
               Fade(theme::Text2, 0.7f));
    }
}

void IconPlay(Painter& p, float cx, float cy, float s, Color c) {
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(p.factory()->CreatePathGeometry(&geo))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink))) return;
    sink->BeginFigure(D2D1::Point2F(cx - s * 0.42f, cy - s * 0.55f), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(cx + s * 0.55f, cy));
    sink->AddLine(D2D1::Point2F(cx - s * 0.42f, cy + s * 0.55f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    p.brush()->SetColor(c);
    p.dc()->FillGeometry(geo.Get(), p.brush());
}

void IconPause(Painter& p, float cx, float cy, float s, Color c) {
    float bw = s * 0.20f, bh = s * 1.02f, gap = s * 0.26f;
    p.Round({cx - gap / 2 - bw, cy - bh / 2, bw, bh}, 1.f, c);
    p.Round({cx + gap / 2, cy - bh / 2, bw, bh}, 1.f, c);
}

void IconSkip(Painter& p, float cx, float cy, float s, Color c, bool forward) {
    float dir = forward ? 1.f : -1.f;
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(p.factory()->CreatePathGeometry(&geo))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink))) return;
    sink->BeginFigure(D2D1::Point2F(cx - dir * s * 0.45f, cy - s * 0.45f), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(cx + dir * s * 0.10f, cy));
    sink->AddLine(D2D1::Point2F(cx - dir * s * 0.45f, cy + s * 0.45f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    p.brush()->SetColor(c);
    p.dc()->FillGeometry(geo.Get(), p.brush());
    p.Round({cx + dir * s * 0.16f - (forward ? 0 : s * 0.08f), cy - s * 0.45f, s * 0.08f, s * 0.9f}, 1.f, c);
}

void IconVolume(Painter& p, float cx, float cy, float s, Color c, int level) {
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(p.factory()->CreatePathGeometry(&geo))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink))) return;
    float x = cx - s * 0.5f;
    sink->BeginFigure(D2D1::Point2F(x, cy - s * 0.18f), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(x + s * 0.20f, cy - s * 0.18f));
    sink->AddLine(D2D1::Point2F(x + s * 0.45f, cy - s * 0.45f));
    sink->AddLine(D2D1::Point2F(x + s * 0.45f, cy + s * 0.45f));
    sink->AddLine(D2D1::Point2F(x + s * 0.20f, cy + s * 0.18f));
    sink->AddLine(D2D1::Point2F(x, cy + s * 0.18f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    p.brush()->SetColor(c);
    p.dc()->FillGeometry(geo.Get(), p.brush());
    if (level <= 0) {
        p.Line(cx + s * 0.12f, cy - s * 0.18f, cx + s * 0.42f, cy + s * 0.18f, 1.5f, c);
        p.Line(cx + s * 0.12f, cy + s * 0.18f, cx + s * 0.42f, cy - s * 0.18f, 1.5f, c);
    } else {
        StrokeArc(p, cx + s * 0.10f, cy, s * 0.28f, -50.f, 100.f, 1.5f, c);
        if (level > 1) StrokeArc(p, cx + s * 0.10f, cy, s * 0.46f, -50.f, 100.f, 1.5f, Fade(c, 0.75f));
    }
}

void IconGear(Painter& p, float cx, float cy, float s, Color c) {
    const int N = 16;
    const float rOut = s * 0.50f;
    const float rIn = s * 0.385f;
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(p.factory()->CreatePathGeometry(&geo))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(&sink))) return;
    for (int i = 0; i < N; ++i) {
        float a = (-3.14159265f / 2.f) + i * (6.2831853f / N);
        float r = (i & 1) ? rIn : rOut;
        D2D1_POINT_2F pt = D2D1::Point2F(cx + cosf(a) * r, cy + sinf(a) * r);
        if (i == 0)
            sink->BeginFigure(pt, D2D1_FIGURE_BEGIN_HOLLOW);
        else
            sink->AddLine(pt);
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    p.brush()->SetColor(c);
    p.dc()->DrawGeometry(geo.Get(), p.brush(), 1.35f);
    p.Ring(cx, cy, s * 0.14f, 1.35f, c);
}

void IconSliders(Painter& p, float cx, float cy, float s, Color c) {
    const float w = s * 0.42f;
    const float knobX[3] = {0.f, -0.22f, 0.16f};
    for (int i = 0; i < 3; ++i) {
        float y = cy + (i - 1) * s * 0.19f;
        p.Round({cx - w, y - 0.65f, w * 2, 1.3f}, 0.65f, Fade(c, 0.85f));
        p.Circle(cx + w * knobX[i], y, 2.3f, c);
    }
}

void IconList(Painter& p, float cx, float cy, float s, Color c) {
    for (int i = 0; i < 3; ++i) {
        float y = cy + (i - 1) * s * 0.19f;
        p.Circle(cx - s * 0.36f, y, 1.2f, c);
        p.Round({cx - s * 0.20f, y - 0.75f, s * 0.56f, 1.5f}, 0.75f, c);
    }
}

void IconFullscreen(Painter& p, float cx, float cy, float s, Color c, bool compact) {
    float h = s * 0.42f, l = s * 0.24f;
    float dir = compact ? 1.f : -1.f;
    for (int i = 0; i < 4; ++i) {
        float sx = (i & 1) ? 1.f : -1.f;
        float sy = (i & 2) ? 1.f : -1.f;
        float x = cx + sx * h, y = cy + sy * h;
        p.Line(x, y, x - sx * l, y, 1.5f, c);
        p.Line(x, y, x, y - sy * l, 1.5f, c);
    }
}

void IconChevron(Painter& p, float cx, float cy, float s, Color c, bool left) {
    float sgn = left ? -1.f : 1.f;
    p.Line(cx - sgn * s * 0.18f, cy - s * 0.36f, cx + sgn * s * 0.18f, cy, 1.8f, c);
    p.Line(cx + sgn * s * 0.18f, cy, cx - sgn * s * 0.18f, cy + s * 0.36f, 1.8f, c);
}

void IconClose(Painter& p, RectF r, Color c, float s = 10.f) {
    p.Line(r.Cx() - s / 2, r.Cy() - s / 2, r.Cx() + s / 2, r.Cy() + s / 2, 1.4f, c);
    p.Line(r.Cx() - s / 2, r.Cy() + s / 2, r.Cx() + s / 2, r.Cy() - s / 2, 1.4f, c);
}

void Switch(Painter& p, const RectF& hit, bool on, bool hover) {
    RectF tr{hit.Right() - 46, hit.Cy() - 12, 42, 24};
    p.Round(tr, 12, on ? Fade(theme::Accent, 0.9f) : Hex(0xFFFFFF, hover ? 0.18f : 0.12f));
    p.Circle(on ? tr.Right() - 13 : tr.x + 13, tr.Cy(), 8.5f, on ? Hex(0x0A0C10) : theme::Text2);
}

void ButtonBase(Painter& p, const RectF& r, bool primary, bool hover, bool active) {
    float a = active ? 0.88f : 1.f;
    if (primary) {
        p.Round(r, r.h * 0.5f, Fade(hover ? Hex(0xFFFFFF) : Hex(0xE9ECF3), a));
    } else {
        p.Round(r, r.h * 0.5f, Hex(0xFFFFFF, hover ? 0.09f : 0.035f));
        p.RoundStroke(r, r.h * 0.5f, 1.f, hover ? theme::LineHi : theme::Line);
    }
}

void IconHover(Painter& p, const RectF& r, bool hover, float radius = 0.f) {
    if (!hover) return;
    if (radius > 0)
        p.Circle(r.Cx(), r.Cy(), radius, Hex(0xFFFFFF, 0.09f));
    else
        p.Round(r, 8, Hex(0xFFFFFF, 0.09f));
}

std::wstring SpeedText(double speed) {
    if (std::fabs(speed - 1.0) < 0.001) return L"1.0×";
    wchar_t buf[32];
    swprintf_s(buf, L"%.2f", speed);
    std::wstring s = buf;
    while (s.size() > 3 && s.back() == L'0') s.pop_back();
    return s + L"×";
}

std::vector<std::wstring> SplitPathParts(const std::wstring& path) {
    std::vector<std::wstring> parts;
    std::wstring cur;
    for (wchar_t c : path) {
        if (c == L'\\' || c == L'/') {
            if (!cur.empty()) {
                parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

std::wstring PrettyContainer(const std::string& name) {
    std::string n = name;
    size_t comma = n.find(',');
    if (comma != std::string::npos) n = n.substr(0, comma);
    for (auto& ch : n) ch = (char)toupper((unsigned char)ch);
    if (n.find("MATROSKA") != std::string::npos) return L"MKV";
    if (n.find("MP4") != std::string::npos || n.find("MOV") != std::string::npos) return L"MP4 / MOV";
    if (n.find("MPEGTS") != std::string::npos || n == "MPEGTS") return L"MPEG-TS";
    if (n.find("AVI") != std::string::npos) return L"AVI";
    if (n.find("FLAC") != std::string::npos) return L"FLAC";
    if (n.find("MP3") != std::string::npos) return L"MP3";
    if (n.find("OGG") != std::string::npos) return L"OGG";
    if (n.find("WAV") != std::string::npos) return L"WAV";
    if (n.find("AAC") != std::string::npos) return L"AAC";
    return Widen(n);
}

}  // namespace

void App::drawLogo(float x, float y, float size, float alpha) {
    PrismMark(p_, x + size * 0.46f, y + size * 0.46f, size * 0.62f, 1.5f, Fade(theme::Text, alpha), true);
    p_.TextTracked(kBrand, RectF{x + size + 10, y + 2, 140, 20}, 13.f, Fade(theme::Text, alpha), 0.28f, 600,
                   FontFace::Display);
    p_.Text(kTagline, RectF{x + size + 10, y + 20, 140, 16}, 9.5f, Fade(theme::Text3, alpha), 400);
}

void App::drawBackdrop(const RectF& area) {
    p_.Fill(area, theme::Bg);
    p_.Linear(area, Hex(0x0A0C12), Hex(0x06070B), 90.f);
    RectF g1{W_ * 0.58f, -H_ * 0.30f, W_ * 0.70f, H_ * 0.85f};
    p_.Glow(g1, Fade(theme::Accent, 0.10f));
    RectF g2{-W_ * 0.25f, H_ * 0.55f, W_ * 0.85f, H_ * 0.75f};
    p_.Glow(g2, Fade(theme::AccentWarm, 0.05f));
    RectF g3{-W_ * 0.1f, -H_ * 0.2f, W_ * 0.6f, H_ * 0.7f};
    p_.Glow(g3, Fade(theme::Accent, 0.035f));
    p_.Noise(area, 0.035f);
}


void IconStats(Painter& p, float cx, float cy, float s, Color c) {
    const float w = s * 0.16f;
    const float gap = s * 0.22f;
    const float base = cy + s * 0.34f;
    const float hs[4] = {0.34f, 0.64f, 0.46f, 0.82f};
    for (int i = 0; i < 4; ++i) {
        float h = s * hs[i];
        p.Round({cx - s * 0.36f + i * (w + gap), base - h, w, h}, w * 0.5f, c);
    }
}

void App::drawWindowButtons() {
    if (fullscreen_) return;
    struct B {
        RectF r;
        int id;
    } buttons[] = {{rMin_, kHitMin}, {rMax_, kHitMax}, {rClose_, kHitClose}};
    for (auto& b : buttons) {
        bool hover = hovered(b.id);
        if (hover)
            p_.Round(b.r.Inset(6, 6), 7, Fade(b.id == kHitClose ? Hex(0xE5484D) : Hex(0xFFFFFF), 0.12f));
        Color c = b.id == kHitClose && hover ? Hex(0xFFFFFF) : theme::Text2;
        if (b.id == kHitMin) {
            p_.Line(b.r.Cx() - 6, b.r.Cy(), b.r.Cx() + 6, b.r.Cy(), 1.3f, c);
        } else if (b.id == kHitClose) {
            IconClose(p_, b.r, c, 10.f);
        } else {
            bool zoomed = hwnd_ && IsZoomed(hwnd_);
            if (zoomed) {
                p_.RoundStroke({b.r.Cx() - 6, b.r.Cy() - 6, 9, 9}, 1.5f, 1.2f, c);
                p_.RoundStroke({b.r.Cx() - 3, b.r.Cy() - 3, 9, 9}, 1.5f, 1.2f, c);
            } else {
                p_.RoundStroke({b.r.Cx() - 5, b.r.Cy() - 5, 10, 10}, 1.f, 1.2f, c);
            }
        }
    }
}

void App::drawTitleBar() {
    bool home = screen_ == Screen::Home;
    if (home) {
        drawLogo(28, 14, 28, 1.f);
        IconHover(p_, rSettingsTop_, hovered(kHitSettingsTop), 10.f);
        IconGear(p_, rSettingsTop_.Cx(), rSettingsTop_.Cy(), 19.f, theme::Text2);
        IconHover(p_, rStatsTop_, hovered(kHitStatsTop), 10.f);
        IconStats(p_, rStatsTop_.Cx(), rStatsTop_.Cy(), 17.f, showStats_ ? theme::Accent : theme::Text3);
        drawWindowButtons();
    }
}

void App::drawHome() {
    drawBackdrop({0, 0, W_, H_});
    drawTitleBar();

    // -- hero copy
    float heroSize = std::clamp(W_ * 0.028f, 34.f, 44.f);
    p_.TextCore(L"本地播放，简单一点。", rHero_, heroSize, theme::Text, 550, FontFace::Display,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false, 0.f, 1.12f, false);
    p_.TextWrap(L"Prism 使用 FFmpeg 解码，支持常见的视频与音频格式。"
                L"所有处理都在本机完成，不转码、不上传。",
                rSub_, 13.5f, theme::Text2, 400, FontFace::Text, 1.68f);

    bool hovOpen = hovered(kHitOpenFile);
    bool hovFolder = hovered(kHitOpenFolder);
    ButtonBase(p_, rOpen_, true, hovOpen, pressedId_ == kHitOpenFile && mouseDown_);
    p_.TextC(L"打开文件", rOpen_, 13.5f, Hex(0x0A0C10), 600);
    ButtonBase(p_, rFolder_, false, hovFolder, pressedId_ == kHitOpenFolder && mouseDown_);
    p_.TextC(L"打开文件夹", rFolder_, 13.5f, theme::Text2, 500);

    p_.Text(L"或把文件拖进窗口任意位置  ·  Ctrl O", rDrop_, 11.5f, theme::Text3, 400);

    // -- format breadth, only when the column has room for it
    float fmtY = std::max(H_ - 236.f, rDrop_.Bottom() + 48.f);
    if (fmtY + 74.f < H_ - 64.f) {
        RectF formats{64, fmtY, 600, 84};
        p_.TextTracked(L"常见格式", RectF{64, formats.y - 28, 400, 18}, 9.5f,
                       Fade(theme::Text3, 0.9f), 0.10f, 500);
        p_.TextWrap(L"MKV · MP4 · MOV · TS · WEBM · HEVC · H.264 · AV1 · VP9 · "
                    L"FLAC · AAC · Opus · AC-3 · DTS · HDR",
                    formats, 10.5f, theme::Text3, 400, FontFace::Text, 1.9f);
    }

    // -- recent panel
    RectF panel = rRecentPanel_;
    p_.Round(panel, 22.f, Hex(0x0F1219, 0.96f));
    p_.RoundStroke(panel, 22.f, 1.f, Hex(0xFFFFFF, 0.10f));
    p_.LinearRound({panel.x + 1, panel.y + 1, panel.w - 2, 150}, 21.f, Hex(0xFFFFFF, 0.045f), Hex(0xFFFFFF, 0.f),
                   90.f);

    p_.Text(L"最近播放", RectF{panel.x + 24, panel.y + 22, 160, 22}, 12.5f, theme::Text2, 500);
    if (!recents_.empty()) {
        wchar_t buf[32];
        swprintf_s(buf, L"%d 部", (int)std::min<size_t>(recents_.size(), 4));
        p_.TextR(buf, RectF{panel.Right() - 120, panel.y + 22, 96, 22}, 11.f, theme::Text3, 400);
    }

    if (recents_.empty()) {
        float cx = panel.Cx();
        float cy = panel.y + 168;
        p_.Glow({cx - 170, cy - 120, 340, 250}, Fade(theme::Accent, 0.10f));
        PrismMark(p_, cx, cy, 58.f, 1.3f, Fade(theme::Text, 0.92f), true);
        p_.TextC(L"这里会记住你看到哪儿", RectF{panel.x + 30, cy + 66, panel.w - 60, 26}, 15.f, theme::Text2, 500);
        p_.TextC(L"打开影片后，随时从上次的位置继续", RectF{panel.x + 30, cy + 94, panel.w - 60, 20}, 11.5f,
                 theme::Text3, 400);
    } else {
        for (size_t i = 0; i < rRecent_.size(); ++i) {
            RectF row = rRecent_[i];
            bool hov = hovered(kHitRecentBase + (int)i);
            if (hov) p_.Round(row, 12.f, Hex(0xFFFFFF, 0.045f));
            RectF thumb{row.x + 8, row.y + 6, 104, 58};
            p_.PushClip(thumb, 8.f);
            auto recentThumb = recentThumbs_.find(recents_[i].path);
            if (recentThumb != recentThumbs_.end() && recentThumb->second.bitmap) {
                p_.Bitmap(recentThumb->second.bitmap.Get(), thumb, 1.f, 0.f,
                          D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
                p_.Linear(thumb, Hex(0x000000, 0.02f), Hex(0x000000, 0.30f), 90.f);
            } else {
                p_.Linear(thumb, Hex(0x17202C), Hex(0x2A3550), 135.f);
                p_.Linear(thumb, Fade(theme::Accent, 0.12f), Fade(theme::AccentWarm, 0.06f), 45.f);
            }
            p_.PopClip();
            p_.PushClip(thumb, 8.f);
            p_.Circle(thumb.Cx(), thumb.Cy(), 14.f, Hex(0x000000, 0.35f));
            IconPlay(p_, thumb.Cx(), thumb.Cy(), 11.f, Hex(0xFFFFFF, 0.92f));
            p_.PopClip();
            p_.RoundStroke(thumb, 8.f, 1.f, Hex(0xFFFFFF, 0.08f));

            double pct = recents_[i].duration > 0 ? recents_[i].position / recents_[i].duration : 0;
            if (pct > 0.02) {
                RectF bar{thumb.x + 6, thumb.Bottom() - 7, (thumb.w - 12) * (float)std::clamp(pct, 0.0, 1.0), 2};
                p_.Round(bar, 1.f, Fade(theme::Accent, 0.9f));
            }
            p_.Text(recents_[i].title, {row.x + 126, row.y + 10, row.w - 140, 22}, 13.f, theme::Text, 500);
            std::wstring meta;
            if (recents_[i].position > 3 && recents_[i].duration > 0)
                meta = L"上次看到 " + FormatTime(recents_[i].position) + L"  ·  共 " +
                       FormatTime(recents_[i].duration);
            else if (recents_[i].duration > 0)
                meta = L"共 " + FormatTime(recents_[i].duration);
            else
                meta = L"未播放";
            p_.Text(meta, {row.x + 126, row.y + 34, row.w - 140, 18}, 10.5f, theme::Text3, 400);
        }
    }

    // -- kernel stat card
    if (panel.h < 420.f) {
        p_.Text(L"FFmpeg 8.1  ·  x86-64 AVX2 汇编内核  ·  WASAPI 共享模式", RectF{64, H_ - 42, 640, 18}, 10.5f,
                theme::Text3, 400);
        p_.TextR(L"Prism 1.0", RectF{W_ - 160, H_ - 42, 96, 18}, 10.5f, theme::Text3, 400);
        return;
    }
    RectF box{panel.x + 16, panel.Bottom() - 112, panel.w - 32, 96};
    p_.Round(box, 14.f, Hex(0xFFFFFF, 0.032f));
    p_.RoundStroke(box, 14.f, 1.f, Hex(0xFFFFFF, 0.07f));
    p_.TextTracked(L"本机内核  ·  x86-64 AVX2 汇编", {box.x + 16, box.y + 12, box.w - 32, 16}, 9.5f,
                   theme::Text3, 0.08f, 600);
    wchar_t num[32];
    swprintf_s(num, L"%.2f", benchAsmMs_);
    p_.Text(num, {box.x + 16, box.y + 36, 100, 34}, 24.f, theme::Accent, 500, FontFace::Mono);
    float numW = p_.Measure(num, 24.f, 500, FontFace::Mono);
    p_.Text(L"ms / 帧 · 1080p 色彩转换", {box.x + 24 + numW, box.y + 44, box.w - 150, 20}, 10.5f,
            theme::Text3, 400);
    if (benchRefMs_ > 0 && benchAsmMs_ > 0) {
        wchar_t ratio[32];
        swprintf_s(ratio, L"%.1f×", benchRefMs_ / benchAsmMs_);
        p_.TextR(ratio, {box.Right() - 96, box.y + 30, 80, 28}, 17.f, theme::Text, 500, FontFace::Mono);
        p_.TextR(L"快于标量 C", {box.Right() - 96, box.y + 58, 80, 16}, 9.f, theme::Text3, 400);
    }

    p_.Text(L"FFmpeg 8.1  ·  x86-64 AVX2 汇编内核  ·  WASAPI 共享模式", RectF{64, H_ - 42, 640, 18}, 10.5f,
            theme::Text3, 400);
    p_.TextR(L"Prism 1.0", RectF{W_ - 160, H_ - 42, 96, 18}, 10.5f, theme::Text3, 400);
}

void App::drawVideoArea() {
    if (!hasMedia_) return;
    if (!engine_.hasVideo()) return;
    if (frame_) {
        gfx_->drawVideo(rVideo_, frame_->rotation);
    }
}

void App::drawScrims() {
    float a = controlsAlpha_;
    if (a <= 0.01f) return;
    p_.PushOpacity(a);
    p_.Linear({0, 0, W_, 170}, Hex(0x000000, 0.78f), Hex(0x000000, 0.f), 90.f);
    p_.Linear({0, H_ - 330, W_, 330}, Hex(0x000000, 0.f), Hex(0x000000, 0.94f), 90.f);
    p_.PopOpacity();
}

void App::drawAudioOnly(const RectF& area) {
    p_.Fill(area, Hex(0x06070B));
    p_.Glow({W_ * 0.2f, -H_ * 0.2f, W_ * 0.8f, H_ * 0.7f}, Fade(theme::Accent, 0.07f));
    p_.Glow({-W_ * 0.1f, H_ * 0.5f, W_ * 0.7f, H_ * 0.7f}, Fade(theme::AccentWarm, 0.05f));
    p_.Noise(area, 0.03f);

    float cx = W_ * 0.5f;
    float cy = H_ * 0.37f;
    float cover = std::min(230.f, H_ * 0.28f);
    float pulse = engine_.hasAudio() ? std::clamp(engine_.peak(), 0.f, 1.f) : 0.f;
    p_.Glow({cx - cover * 1.2f, cy - cover * 1.2f, cover * 2.4f, cover * 2.4f},
            Fade(theme::Accent, 0.10f + pulse * 0.10f));
    RectF art{cx - cover / 2, cy - cover / 2, cover, cover};
    p_.Circle(cx, cy, cover / 2, Hex(0x0D1118));
    p_.PushClip({art.x, art.y, art.w, art.h}, cover / 2);
    p_.Linear(art, Hex(0x1A2838), Hex(0x0C0F16), 120.f);
    p_.Linear(art, Fade(theme::Accent, 0.16f), Fade(theme::AccentWarm, 0.05f), 30.f);
    p_.PopClip();
    p_.Ring(cx, cy, cover / 2 - 0.5f, 1.f, Hex(0xFFFFFF, 0.10f));
    PrismMark(p_, cx, cy - 4, cover * 0.30f, 1.6f, Hex(0xF5F7FC), true);

    double pct = duration_ > 0 ? std::clamp(position_ / duration_, 0.0, 1.0) : 0.0;
    p_.Ring(cx, cy, cover / 2 + 10, 2.f, Hex(0xFFFFFF, 0.08f));
    if (pct > 0.001) {
        StrokeArc(p_, cx, cy, cover / 2 + 10, -90.f, (float)(pct * 360.0), 2.f, theme::Accent);
    }

    std::wstring title = engine_.info().title;
    if (title.empty()) title = engine_.info().filename;
    std::wstring artist = engine_.info().artist;
    p_.TextC(title, {W_ * 0.15f, cy + cover / 2 + 44, W_ * 0.7f, 32}, 22.f, theme::Text, 600, FontFace::Display);
    if (!artist.empty())
        p_.TextC(artist, {W_ * 0.15f, cy + cover / 2 + 78, W_ * 0.7f, 22}, 12.5f, theme::Text2, 400);
    std::wstring fmt = engine_.statusLine();
    p_.TextC(fmt, {W_ * 0.15f, cy + cover / 2 + (artist.empty() ? 80 : 104), W_ * 0.7f, 20}, 10.5f,
             theme::Text3, 400);
}

void App::drawPlayer() {
    p_.Fill(rVideo_, Hex(0x000000));
    if (hasMedia_ && engine_.hasVideo()) {
        drawVideoArea();
    } else if (hasMedia_) {
        drawAudioOnly(rVideo_);
    }

    drawScrims();

    if (controlsAlpha_ > 0.01f) {
        p_.PushOpacity(controlsAlpha_);
        if (!fullscreen_) {
            drawWindowButtons();
        }
        IconHover(p_, rBack_, hovered(kHitBack), 18.f);
        IconChevron(p_, rBack_.Cx(), rBack_.Cy(), 15.f, theme::Text, true);

        std::wstring title = hasMedia_ ? FileNameOf(engine_.path()) : L"";
        p_.Text(title, rTitleText_, 14.f, theme::Text, 500);
        if (hasMedia_) p_.Text(engine_.statusLine(), rStatus_, 10.5f, theme::Text3, 400);

        IconHover(p_, rSettingsBtn_, hovered(kHitSettingsBtn), 18.f);
        IconGear(p_, rSettingsBtn_.Cx(), rSettingsBtn_.Cy(), 19.f, theme::Text2);
        IconHover(p_, rStatsBtn_, hovered(kHitStatsBtn), 18.f);
        IconStats(p_, rStatsBtn_.Cx(), rStatsBtn_.Cy(), 17.f, showStats_ ? theme::Accent : theme::Text3);
        p_.PopOpacity();
    }

    drawSubtitle();
    if (hasMedia_) drawSecondarySubtitle();

    if (controlsAlpha_ > 0.01f && hasMedia_) {
        p_.PushOpacity(controlsAlpha_);
        drawControls();
        p_.PopOpacity();
    }

    drawCenterOverlay();

    if (popup_ == Popup::Playlist) drawPlaylist();
    if (popup_ == Popup::Settings) drawSettings();
    if (popup_ == Popup::Speed) drawSpeedMenu();
    if (popup_ == Popup::Subtitles) drawSubtitleMenu();
    if (popup_ == Popup::SubtitleStyle) drawSubtitleStyle();
    if (popup_ == Popup::FontPicker) drawFontPicker();
}

void App::drawControls() {
    double shown = draggingSeek_ ? dragSeekTime_ : position_;
    double pct = duration_ > 0 ? std::clamp(shown / duration_, 0.0, 1.0) : 0.0;

    float cy = rSeek_.Cy();
    RectF track{rSeek_.x, cy - 1.5f, rSeek_.w, 3.f};
    p_.Round(track, 1.5f, Hex(0xFFFFFF, 0.16f));
    float playedW = track.w * (float)pct;
    if (playedW > 0.5f) {
        p_.PushClip({track.x, track.y, playedW, track.h}, 1.5f);
        p_.Linear(track, theme::Accent, theme::AccentWarm, 0.f);
        p_.PopClip();
    }
    float knobX = track.x + playedW;
    bool seekHover = hovered(kHitSeek) || draggingSeek_;
    p_.Circle(knobX, cy, seekHover ? 7.f : 5.5f, Hex(0xFFFFFF));
    p_.Ring(knobX, cy, seekHover ? 7.f : 5.5f, 0.8f, Hex(0x000000, 0.25f));

    if ((seekHover || draggingSeek_) && duration_ > 0) {
        float mx = draggingSeek_ ? rSeek_.x + (float)(dragSeekTime_ / duration_) * rSeek_.w : mouseX_;
        mx = std::clamp(mx, rSeek_.x, rSeek_.Right());
        double tt = ((mx - rSeek_.x) / rSeek_.w) * duration_;
        std::wstring tmp = FormatTime(tt);
        float tw = p_.Measure(tmp, 11.f, 500, FontFace::Mono) + 22.f;
        RectF chip{mx - tw / 2, cy - 42, tw, 26};
        chip.x = std::clamp(chip.x, 12.f, W_ - 12.f - chip.w);
        p_.Round(chip, 7.f, Hex(0x151922, 0.96f));
        p_.RoundStroke(chip, 7.f, 1.f, theme::LineHi);
        p_.TextC(tmp, chip, 11.f, theme::Text, 500, FontFace::Mono);
        p_.Line(mx, cy + 8, mx, cy + 16, 1.f, Hex(0xFFFFFF, 0.35f));
    }

    // transport
    bool playHover = hovered(kHitPlay);
    p_.Circle(rPlay_.Cx(), rPlay_.Cy(), 22.f, Fade(playHover ? Hex(0xFFFFFF) : Hex(0xEDF1F7), 0.98f));
    if (playing_)
        IconPause(p_, rPlay_.Cx(), rPlay_.Cy(), 13.f, Hex(0x090B10));
    else
        IconPlay(p_, rPlay_.Cx() + 1.5f, rPlay_.Cy(), 15.f, Hex(0x090B10));

    if (playlist_.size() > 1) {
        IconHover(p_, rPrev_, hovered(kHitPrev), 16.f);
        IconHover(p_, rNext_, hovered(kHitNext), 16.f);
        IconSkip(p_, rPrev_.Cx(), rPrev_.Cy(), 12.f, theme::Text2, false);
        IconSkip(p_, rNext_.Cx(), rNext_.Cy(), 12.f, theme::Text2, true);
    }

    std::wstring cur = FormatTime(shown);
    std::wstring total = FormatTime(duration_);
    float cw = p_.Measure(cur, 12.f, 500, FontFace::Mono);
    float tw = p_.Measure(L" / " + total, 12.f, 400, FontFace::Mono);
    RectF timeRect{rTime_.x, rTime_.y, cw + tw + 8, rTime_.h};
    p_.Text(cur, timeRect, 12.f, theme::Text, 500, FontFace::Mono);
    p_.Text(L" / " + total, RectF{timeRect.x + cw, timeRect.y, tw + 4, timeRect.h}, 12.f, theme::Text3, 400,
            FontFace::Mono);

    int volLevel = muted_ || volume_ < 0.01f ? 0 : (volume_ < 0.55f ? 1 : 2);
    IconHover(p_, rMute_, hovered(kHitMute), 16.f);
    IconVolume(p_, rMute_.Cx(), rMute_.Cy(), 15.f, theme::Text2, volLevel);

    RectF vs = rVolSlider_;
    float vx = vs.x + vs.w * (muted_ ? 0.f : std::clamp(volume_, 0.f, 1.f));
    p_.Round({vs.x, vs.Cy() - 1.5f, vs.w, 3.f}, 1.5f, Hex(0xFFFFFF, 0.16f));
    if (vx > vs.x) p_.Round({vs.x, vs.Cy() - 1.5f, vx - vs.x, 3.f}, 1.5f, Fade(theme::Text, 0.75f));
    bool volHover = hovered(kHitVolume) || draggingVolume_;
    p_.Circle(vx, vs.Cy(), volHover ? 6.f : 4.5f, Hex(0xFFFFFF, volHover ? 1.f : 0.9f));

    bool subs = engine_.hasSubtitles() || engine_.subtitlePending();
    Color ccColor = !subs ? Fade(theme::Text3, 0.55f) : (subtitleEnabled_ ? theme::Accent : theme::Text2);
    IconHover(p_, rSubtitle_, hovered(kHitSubtitle), 16.f);
    RectF ccBox{rSubtitle_.Cx() - 13, rSubtitle_.Cy() - 9, 26, 18};
    p_.RoundStroke(ccBox, 4.f, 1.2f, ccColor);
    p_.TextC(L"CC", ccBox, 9.f, ccColor, 700);

    bool speedHover = hovered(kHitSpeed) || popup_ == Popup::Speed;
    if (speedHover) p_.Round(rSpeed_, 8.f, Hex(0xFFFFFF, 0.08f));
    p_.TextC(SpeedText(engine_.speed()), rSpeed_, 11.5f, speedHover ? theme::Text : theme::Text2, 500,
             FontFace::Mono);

    IconHover(p_, rPlaylistBtn_, hovered(kHitPlaylistBtn) || popup_ == Popup::Playlist, 16.f);
    IconList(p_, rPlaylistBtn_.Cx(), rPlaylistBtn_.Cy(), 17.f,
             popup_ == Popup::Playlist ? theme::Text : theme::Text2);

    IconHover(p_, rFullscreen_, hovered(kHitFullscreen), 16.f);
    IconFullscreen(p_, rFullscreen_.Cx(), rFullscreen_.Cy(), 17.f, theme::Text2, fullscreen_);
}

static Color SubtitleFill(int idx) {
    switch (idx) {
        case 1: return Hex(0xFFD9A0);
        case 2: return Hex(0x9FE8F5);
        case 3: return Hex(0xB8F5C8);
        default: return Hex(0xF7F9FD);
    }
}

void App::drawSubtitle() {
    if (!hasMedia_ || !subtitleEnabled_ || !engine_.hasVideo()) return;
    if (popup_ != Popup::None) return;
    if (assNativeActive()) {
        drawAssSubtitle();
        return;
    }
    // Text mode (or native unavailable): release any ASS layer so stale pixels can't linger.
    if (assData_) {
        assData_.reset();
        if (assRenderer_) assRenderer_->setTrack(nullptr);
        gfx_->clearSubtitle();
    }
    if (subtitleText_.empty()) return;
    const float size = 21.f * subtitleScale_;
    const float bottom = H_ - (112.f + subtitlePos_ * 72.f) + subY_ * H_;
    const float cx = W_ * 0.5f + subX_ * W_;

    float maxW = 0.f;
    int lines = 0;
    size_t pos = 0;
    while (pos <= subtitleText_.size()) {
        size_t nl = subtitleText_.find(L'\n', pos);
        std::wstring line = subtitleText_.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        if (!line.empty()) maxW = std::max(maxW, p_.Measure(line, size, 500, FontFace::Custom));
        ++lines;
        if (nl == std::wstring::npos) break;
        pos = nl + 1;
    }
    if (lines <= 0) lines = 1;
    const float totalH = size * 1.32f * (float)lines;
    // The box grows with the text: at 300–500% a multi-line cue no longer fits
    // the old fixed 150 px and would be clipped.
    const float areaH = std::max(150.f, totalH + 16.f);
    RectF area{cx - W_ * 0.42f, bottom - areaH, W_ * 0.84f, areaH};

    if (subtitleBg_ > 0) {
        Color bg = subtitleBg_ == 1 ? Hex(0x000000, 0.45f) : Hex(0x000000, 0.72f);
        RectF box{cx - maxW * 0.5f - 16.f, bottom - totalH - 10.f, maxW + 32.f, totalH + 20.f};
        p_.Round(box, 8.f, bg);
    }
    const float ow = subtitleOutline_ == 0 ? 0.f : (subtitleOutline_ == 1 ? 1.2f : (subtitleOutline_ == 2 ? 2.0f : 3.0f));
    if (ow > 0) {
        Color outline = Hex(0x000000, 0.82f);
        for (int i = 0; i < 8; ++i) {
            const float a = (float)i * 0.7853982f;
            p_.TextCore(subtitleText_, area.Offset(std::cos(a) * ow, std::sin(a) * ow), size, outline, 500,
                        FontFace::Custom, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_FAR, true, 0.f,
                        1.32f, false);
        }
    }
    p_.TextCore(subtitleText_, area, size, SubtitleFill(subtitleColor_), 500, FontFace::Custom,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_FAR, true, 0.f, 1.32f, false);
    if (draggingSub_ && subDragRect_.w > 0) {
        p_.RoundStroke(subDragRect_.Expand(4), 8.f, 1.f, Fade(theme::Accent, 0.85f));
    }
}

void App::drawAssSubtitle() {
    if (!assRenderer_ || !assRenderer_->available()) return;
    const RectF video = VideoDisplayRect(rVideo_, frame_ ? frame_->rotation : 0, engine_.info().video.width,
                                         engine_.info().video.height);
    if (video.w <= 0 || video.h <= 0) return;

    auto data = engine_.assTrack();
    if (data != assData_) {
        assData_ = data;
        if (!data || !assRenderer_->setTrack(data)) gfx_->clearSubtitle();
    }
    if (!assData_) return;

    // Render at the on-screen pixel size: sharp on screen, cheap for 4K sources.
    const float s = gfx_->scale();
    assRenderer_->setFrameSize(std::max(1, (int)std::lround(video.w * s)),
                               std::max(1, (int)std::lround(video.h * s)));
    const double t = std::max(0.0, (draggingSeek_ ? dragSeekTime_ : position_) - subtitleDelay_);
    if (assRenderer_->render(t)) {
        gfx_->updateSubtitle(assRenderer_->pixels(), assRenderer_->width(), assRenderer_->height(),
                             assRenderer_->stride());
    }

    // Overall layer scale/position from the subtitle style panel still applies.
    RectF dest = video;
    if (std::fabs(subtitleScale_ - 1.f) > 0.001f) {
        const float w = dest.w * subtitleScale_, h = dest.h * subtitleScale_;
        dest = RectF{dest.Cx() - w * 0.5f, dest.Cy() - h * 0.5f, w, h};
    }
    dest.x += subX_ * W_;
    dest.y += subY_ * H_;
    p_.Bitmap(gfx_->subtitleBitmap(), dest, 1.f, 0.f, D2D1_INTERPOLATION_MODE_LINEAR);
}


void App::drawSecondarySubtitle() {
    if (!secondaryEnabled_ || secondaryText_.empty() || popup_ != Popup::None) return;
    const float scale = std::clamp(subtitleScale_, 0.5f, 2.5f);
    const float size = 16.f * scale;
    const float primaryBottom = H_ - (112.f + subtitlePos_ * 72.f) + subY_ * H_;
    const float bottom = primaryBottom - 38.f * scale;
    RectF area{W_ * 0.10f, bottom - 120.f, W_ * 0.80f, 120.f};
    const float ow = std::max(1.5f, size * 0.055f);
    for (int i = 0; i < 8; ++i) {
        const float a = (float)i * 0.7853982f;
        p_.TextCore(secondaryText_, area.Offset(std::cos(a) * ow, std::sin(a) * ow), size,
                    Hex(0x000000, 0.86f), 500, FontFace::Custom, DWRITE_TEXT_ALIGNMENT_CENTER,
                    DWRITE_PARAGRAPH_ALIGNMENT_FAR, true, 0.f, 1.30f, false);
    }
    p_.TextCore(secondaryText_, area, size, Hex(0xF3F5FA, 0.82f), 500, FontFace::Custom,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_FAR, true, 0.f, 1.30f, false);
}

void App::drawSubtitleMenu() {
    p_.Fill({0, 0, W_, H_}, Hex(0x000000, 0.28f));
    const RectF panel = rSubtitlePanel_;
    p_.Round(panel, 18.f, Hex(0x0C0F15, 0.985f));
    p_.RoundStroke(panel, 18.f, 1.f, theme::Line);
    p_.Text(L"字幕", {panel.x + 24, panel.y + 18, 220, 28}, 16.f, theme::Text, 600, FontFace::Display);
    wchar_t count[192];
    const bool assReady = assRenderer_ && assRenderer_->available();
    swprintf_s(count, L"%d 条字幕 · %s · %s", (int)subtitleAllOrds_.size(),
               subtitleEnabled_ ? L"已开启" : L"已关闭", assReady ? L"原生 ASS 就绪" : L"原生 ASS 不可用");
    p_.Text(count, {panel.x + 24, panel.y + 46, panel.w - 48, 18}, 10.5f,
            assReady ? theme::Text3 : Fade(theme::AccentWarm, 0.85f), 400);

    p_.Text(L"字幕模式", {panel.x + 24, panel.y + 70, 82, 20}, 10.5f, theme::Text3, 500);
    static const wchar_t* modeNames[3] = {L"自动", L"原始", L"自定义"};
    for (int i = 0; i < 3; ++i) {
        const bool active = subtitleMode_ == i;
        const bool hov = hovered(kHitSubModeAuto + i);
        p_.Round(rSubMode_[i], 7.f,
                 active ? Fade(theme::Accent, 0.16f) : (hov ? Hex(0xFFFFFF, 0.09f) : Hex(0xFFFFFF, 0.045f)));
        if (active) p_.RoundStroke(rSubMode_[i], 7.f, 1.f, Fade(theme::Accent, 0.55f));
        p_.TextC(modeNames[i], rSubMode_[i], 10.f,
                 active ? theme::Accent : (hov ? theme::Text : theme::Text2), active ? 600 : 500);
    }

    RectF closeR{panel.Right() - 50, panel.y + 12, 36, 36};
    IconHover(p_, closeR, hovered(kHitSubtitleClose), 15.f);
    IconClose(p_, closeR, theme::Text3, 9.f);

    for (size_t k = 0; k < subtitleRowRects_.size(); ++k) {
        const RectF r = subtitleRowRects_[k];
        const int ord = subtitleRowOrds_[k];
        const int hit = kHitSubtitleRowBase + (int)subtitleScroll_ + (int)k;
        const bool cur = subtitleEnabled_ && ord == engine_.subtitleTrack();
        const bool hov = hovered(hit);
        if (cur) p_.Round(r, 9.f, Hex(0xFFFFFF, 0.075f));
        else if (hov) p_.Round(r, 9.f, Hex(0xFFFFFF, 0.045f));
        if (cur) {
            p_.Round({r.x, r.y + 8, 3.f, r.h - 16}, 1.5f, theme::Accent);
            p_.Circle(r.Right() - 20, r.Cy(), 3.2f, theme::Accent);
        }
        const auto& t = engine_.info().subtitles[ord];
        p_.Text(t.title, {r.x + 14, r.y + 8, r.w - 110, 20}, 11.5f, cur ? theme::Text : theme::Text2,
                cur ? 600 : 400);
        std::wstring tag = t.external ? L"外挂" : Widen(t.lang);
        if (tag.empty()) tag = L"内嵌";
        p_.Text(tag, {r.Right() - 96, r.y + 9, 80, 18}, 9.5f, t.external ? theme::AccentWarm : theme::Text3, 400);
    }

    const int total = (int)subtitleAllOrds_.size();
    const int vis = (int)subtitleRowRects_.size();
    if (total > vis && vis > 0) {
        RectF track{panel.Right() - 9, panel.y + 112, 3.f, panel.h - 182};
        p_.Round(track, 1.5f, Hex(0xFFFFFF, 0.08f));
        const float th = std::max(28.f, track.h * (float)vis / (float)total);
        const float yy = track.y + (track.h - th) * (subtitleScroll_ / (float)std::max(1, total - vis));
        p_.Round({track.x, yy, track.w, th}, 1.5f, Hex(0xFFFFFF, 0.28f));
    }

    auto btn = [&](RectF r, const wchar_t* label, int id, bool accent) {
        bool hov = hovered(id);
        p_.Round(r, 8.f, hov ? Hex(0xFFFFFF, 0.11f) : Hex(0xFFFFFF, 0.05f));
        p_.TextC(label, r, 10.f, accent ? theme::Accent : (hov ? theme::Text : theme::Text2), 500);
    };
    btn(rSubOff_, L"关闭字幕", kHitSubtitleOff, !subtitleEnabled_);
    btn(rSubStyleBtn_, L"样式", kHitSubtitleStyle, false);
    btn(rSubImport_, L"导入字幕…", kHitSubtitleImport, true);
}

void App::drawSubtitleStyle() {
    p_.Fill({0, 0, W_, H_}, Hex(0x000000, 0.35f));
    const RectF panel = rSubStylePanel_;
    p_.Round(panel, 20.f, Hex(0x0D1017, 0.99f));
    p_.RoundStroke(panel, 20.f, 1.f, theme::LineHi);
    p_.Text(L"字幕样式", {panel.x + 24, panel.y + 18, 260, 28}, 16.f, theme::Text, 600, FontFace::Display);
    RectF closeR{panel.Right() - 50, panel.y + 12, 36, 36};
    IconHover(p_, closeR, hovered(kHitSubStyleClose), 15.f);
    IconClose(p_, closeR, theme::Text3, 9.f);

    RectF preview{panel.x + 24, panel.y + 62, panel.w - 48, 56};
    p_.Round(preview, 10.f, Hex(0x000000, 0.40f));
    // Cap the swatch so 300–500% still fits inside the 56 px preview.
    p_.TextC(L"示例字幕：今晚的月亮真美。", preview, std::min(18.f * subtitleScale_, 40.f),
             SubtitleFill(subtitleColor_), 500, FontFace::Custom);

    const wchar_t* labels[5] = {L"字号", L"颜色", L"描边", L"背景", L"位置"};
    const wchar_t* colors[4] = {L"白色", L"暖黄", L"冰蓝", L"薄荷"};
    const wchar_t* outlines[4] = {L"无", L"细", L"中", L"粗"};
    const wchar_t* bgs[3] = {L"无", L"半透明", L"黑底"};
    const wchar_t* positions[3] = {L"低", L"中", L"高"};
    wchar_t sizeText[16];
    swprintf_s(sizeText, L"%d%%", (int)std::lround(subtitleScale_ * 100.f));
    const wchar_t* values[5] = {sizeText, colors[std::clamp(subtitleColor_, 0, 3)],
                                outlines[std::clamp(subtitleOutline_, 0, 3)], bgs[std::clamp(subtitleBg_, 0, 2)],
                                positions[std::clamp(subtitlePos_, 0, 2)]};
    for (int i = 0; i < 5 && i < (int)rSubStyleRows_.size(); ++i) {
        RectF r = rSubStyleRows_[i];
        bool hov = hovered(kHitSubStyleRowBase + i);
        if (hov) p_.Round(r, 9.f, Hex(0xFFFFFF, 0.055f));
        p_.Text(labels[i], {r.x + 12, r.y + 11, 140, 20}, 11.5f, theme::Text2, 400);
        p_.Text(values[i], {r.Right() - 160, r.y + 11, 148, 20}, 11.5f, theme::Text, 500);
    }
    if (rSubStyleRows_.size() > 5) {
        RectF r = rSubStyleRows_[5];
        bool hov = hovered(kHitSubStyleFont);
        const std::wstring& family = subFontFamily_;
        if (hov) p_.Round(r, 9.f, Hex(0xFFFFFF, 0.055f));
        p_.Text(L"字体", {r.x + 12, r.y + 11, 60, 20}, 11.5f, theme::Text2, 400);
        p_.Text(family.empty() ? L"系统字体" : family, {r.x + 76, r.y + 11, r.w - 76 - 130, 20}, 11.5f,
                family.empty() ? theme::Text3 : theme::Text, 500);
        RectF chip{r.Right() - 116, r.y + 6, 104, 28};
        p_.Round(chip, 7.f, hov ? Hex(0xFFFFFF, 0.12f) : Hex(0xFFFFFF, 0.055f));
        p_.TextC(L"选择…", chip, 9.5f, hov ? theme::Text : theme::Accent, 500);
    }
    p_.TextC(L"点击行循环切换 · 字体行选择已安装字体", {panel.x, panel.Bottom() - 34, panel.w, 20}, 9.5f,
             theme::Text3, 400);
}

void App::drawFontPicker() {
    p_.Fill({0, 0, W_, H_}, Hex(0x000000, 0.35f));
    const RectF panel = rFontPanel_;
    p_.Round(panel, 20.f, Hex(0x0D1017, 0.99f));
    p_.RoundStroke(panel, 20.f, 1.f, theme::LineHi);
    p_.LinearRound({panel.x + 1, panel.y + 1, panel.w - 2, 120}, 19.f, Hex(0xFFFFFF, 0.03f), Hex(0xFFFFFF, 0.f),
                   90.f);

    const auto& familyNames = p_.SystemFontFamilies();
    const std::wstring active = subFontFamily_.empty() ? p_.SubtitleFamily() : subFontFamily_;
    p_.Text(L"选择字体", {panel.x + 24, panel.y + 18, 260, 28}, 16.f, theme::Text, 600, FontFace::Display);
    wchar_t count[256];
    swprintf_s(count, L"%d 个已安装字体 · 当前 %ls", (int)familyNames.size(), active.c_str());
    p_.Text(count, {panel.x + 24, panel.y + 46, panel.w - 100, 18}, 10.5f, theme::Text3, 400);

    RectF closeR{panel.Right() - 50, panel.y + 12, 36, 36};
    IconHover(p_, closeR, hovered(kHitFontPickerClose), 15.f);
    IconClose(p_, closeR, theme::Text3, 9.f);

    p_.PushClip(rFontList_, 8.f);
    for (size_t k = 0; k < rFontRows_.size(); ++k) {
        const RectF r = rFontRows_[k];
        const size_t idx = (size_t)fontScroll_ + k;
        if (idx >= familyNames.size()) break;
        const std::wstring& family = familyNames[idx];
        const bool cur = _wcsicmp(active.c_str(), family.c_str()) == 0;
        const bool hov = hovered(kHitFontRowBase + (int)idx);
        if (cur) p_.Round(r, 9.f, Hex(0xFFFFFF, 0.075f));
        else if (hov) p_.Round(r, 9.f, Hex(0xFFFFFF, 0.045f));
        if (cur) {
            p_.Round({r.x, r.y + 7, 3.f, r.h - 14}, 1.5f, theme::Accent);
            p_.Line(r.Right() - 25, r.Cy() - 0.5f, r.Right() - 21, r.Cy() + 3.5f, 1.7f, theme::Accent);
            p_.Line(r.Right() - 21, r.Cy() + 3.5f, r.Right() - 13, r.Cy() - 4.5f, 1.7f, theme::Accent);
        }
        p_.Text(family, {r.x + 14, r.y + 6, r.w - 56, 20}, 11.5f, cur ? theme::Text : theme::Text2,
                cur ? 600 : 400);
    }
    p_.PopClip();

    if (fontMaxScroll_ > 0 && !rFontRows_.empty()) {
        RectF track{panel.Right() - 9, rFontList_.y + 2, 3.f, rFontList_.h - 4};
        p_.Round(track, 1.5f, Hex(0xFFFFFF, 0.08f));
        const float th = std::max(28.f, track.h * (float)rFontRows_.size() / (float)familyNames.size());
        const float yy = track.y + (track.h - th) * ((float)fontScroll_ / (float)fontMaxScroll_);
        p_.Round({track.x, yy, track.w, th}, 1.5f, Hex(0xFFFFFF, 0.28f));
    }

    p_.Fill({panel.x + 16, rFontDefault_.y - 10, panel.w - 32, 1}, theme::Line);
    const bool defCur = subFontFamily_.empty();
    const bool defHov = hovered(kHitFontDefault);
    p_.Round(rFontDefault_, 9.f, defHov ? Hex(0xFFFFFF, 0.10f) : Hex(0xFFFFFF, 0.045f));
    p_.TextC(L"使用系统默认", rFontDefault_, 11.5f, defCur ? theme::Accent : theme::Text2, defCur ? 600 : 500);
}

void App::drawFileBrowser() {
    p_.Fill({0, 0, W_, H_}, Hex(0x000000, 0.48f));
    const RectF panel = rFbPanel_;
    p_.Round(panel, 20.f, Hex(0x0D1017, 0.99f));
    p_.RoundStroke(panel, 20.f, 1.f, theme::LineHi);
    p_.LinearRound({panel.x + 1, panel.y + 1, panel.w - 2, 140}, 19.f, Hex(0xFFFFFF, 0.035f),
                   Hex(0xFFFFFF, 0.f), 90.f);

    const wchar_t* title = fbMode_ == FbMode::Subtitle ? L"导入字幕"
                           : fbMode_ == FbMode::MediaFolder ? L"打开文件夹"
                                                             : L"打开文件";
    p_.Text(title, {panel.x + 24, panel.y + 16, 320, 28}, 16.f, theme::Text, 600, FontFace::Display);
    int dirCount = 0, fileCount = 0;
    for (const auto& e : fbEntries_) (e.dir ? dirCount : fileCount)++;
    wchar_t count[128];
    if (fbMode_ == FbMode::MediaFolder)
        swprintf_s(count, L"%d 个子文件夹 · 打开后扫描其中的影片", dirCount);
    else if (fbMode_ == FbMode::Subtitle)
        swprintf_s(count, L"%d 个文件夹 · %d 个字幕文件", dirCount, fileCount);
    else
        swprintf_s(count, L"%d 个文件夹 · %d 个媒体文件", dirCount, fileCount);
    p_.Text(count, {panel.x + 24, panel.y + 44, panel.w - 120, 18}, 10.5f, theme::Text3, 400);

    IconHover(p_, rFbClose_, hovered(kHitFbClose), 15.f);
    IconClose(p_, rFbClose_, theme::Text3, 9.f);

    // -- up + breadcrumb line
    const bool upHover = hovered(kHitFbUp);
    p_.Round(rFbUp_, 9.f, upHover && fbCanUp_ ? Hex(0xFFFFFF, 0.10f) : Hex(0xFFFFFF, 0.045f));
    p_.RoundStroke(rFbUp_, 9.f, 1.f, upHover && fbCanUp_ ? theme::LineHi : theme::Line);
    p_.TextC(L"↑ 上级", rFbUp_, 11.f,
             fbCanUp_ ? (upHover ? theme::Text : theme::Text2) : Fade(theme::Text3, 0.7f), 500);

    p_.Round(rFbPath_, 9.f, Hex(0xFFFFFF, 0.03f));
    p_.RoundStroke(rFbPath_, 9.f, 1.f, theme::Line);
    const std::vector<std::wstring> parts = SplitPathParts(fbDir_);
    auto joinFrom = [&](size_t from) {
        std::wstring s;
        for (size_t i = from; i < parts.size(); ++i) {
            if (i > from) s += L"  ›  ";
            s += parts[i];
        }
        return s;
    };
    std::wstring crumbs = joinFrom(0);
    const float crumbW = std::max(40.f, rFbPath_.w - 24.f);
    for (size_t from = 1; from + 1 < parts.size() && p_.Measure(crumbs, 11.f, 400) > crumbW; ++from)
        crumbs = L"…  ›  " + joinFrom(from);
    if (crumbs.empty()) crumbs = L"—";
    p_.Text(crumbs, {rFbPath_.x + 12, rFbPath_.y, rFbPath_.w - 24, rFbPath_.h}, 11.f, theme::Text2, 400);

    // -- drive row
    p_.Text(L"驱动器", {rFbDrivesTag_.x, rFbDrivesTag_.y + 7, rFbDrivesTag_.w, 16}, 9.5f, theme::Text3, 500);
    auto upper = [](wchar_t c) { return (c >= L'a' && c <= L'z') ? (wchar_t)(c - 32) : c; };
    for (size_t i = 0; i < rFbDrive_.size() && i < fbDrives_.size(); ++i) {
        const RectF r = rFbDrive_[i];
        const bool active = fbDir_.size() >= 2 && upper(fbDir_[0]) == fbDrives_[i][0];
        const bool hov = hovered(kHitFbDriveBase + (int)i);
        p_.Round(r, 7.f, active ? Fade(theme::Accent, 0.16f)
                                : (hov ? Hex(0xFFFFFF, 0.09f) : Hex(0xFFFFFF, 0.035f)));
        if (active) p_.RoundStroke(r, 7.f, 1.f, Fade(theme::Accent, 0.55f));
        p_.TextC(fbDrives_[i].substr(0, 2), r, 10.f,
                 active ? theme::Accent : (hov ? theme::Text : theme::Text2), 500);
    }

    p_.Fill({panel.x + 24, rFbList_.y - 8, panel.w - 48, 1}, theme::Line);

    // -- listing
    p_.PushClip(rFbList_, 10.f);
    const bool mediaMode = fbMode_ == FbMode::MediaFile;
    for (size_t k = 0; k < rFbRows_.size(); ++k) {
        const RectF r = rFbRows_[k];
        const int idx = fbScroll_ + (int)k;
        if (idx >= (int)fbEntries_.size()) break;
        const FbEntry& e = fbEntries_[idx];
        const bool sel = idx == fbSelected_;
        const bool hov = hovered(kHitFbRowBase + idx);
        if (sel) p_.Round(r, 9.f, Fade(theme::Accent, 0.13f));
        else if (hov) p_.Round(r, 9.f, Hex(0xFFFFFF, 0.045f));
        if (sel) {
            p_.Round({r.x, r.y + 10.f, 3.f, r.h - 20.f}, 1.5f, theme::Accent);
            p_.RoundStroke(r, 9.f, 1.f, Fade(theme::Accent, 0.30f));
        }
        const float nameX = r.x + (mediaMode ? kFbThumbW + 22.f : 42.f);
        if (mediaMode) {
            const RectF tile{r.x + 10.f, r.Cy() - kFbThumbH * 0.5f, kFbThumbW, kFbThumbH};
            p_.Round(tile, 5.f, Hex(0xFFFFFF, sel ? 0.075f : 0.045f));
            if (e.dir) {
                const Color warm = Fade(theme::AccentWarm, sel ? 0.95f : 0.8f);
                const float fx = tile.Cx() - 10.5f, fy = tile.Cy() - 6.5f;
                p_.Round({fx, fy, 10.f, 6.f}, 1.5f, warm);
                p_.Round({fx, fy + 3.f, 21.f, 13.f}, 2.5f, warm);
            } else {
                auto thumb = fbThumbs_.find(e.path);
                if (thumb != fbThumbs_.end() && thumb->second.bitmap) {
                    const FbThumb& t = thumb->second;
                    const float s = std::min(tile.w / (float)t.width, tile.h / (float)t.height);
                    const float dw = t.width * s, dh = t.height * s;
                    p_.Bitmap(t.bitmap.Get(), {tile.Cx() - dw * 0.5f, tile.Cy() - dh * 0.5f, dw, dh}, 1.f, 4.f);
                } else {
                    const RectF box{tile.Cx() - 7.5f, tile.Cy() - 7.5f, 15.f, 15.f};
                    p_.RoundStroke(box, 3.5f, 1.2f, Fade(theme::Text3, 0.95f));
                    IconPlay(p_, box.Cx() + 0.5f, box.Cy(), 6.f, Fade(theme::Text2, 0.9f));
                }
            }
            p_.RoundStroke(tile, 5.f, 1.f, Fade(theme::LineHi, sel ? 0.9f : 0.6f));
        } else {
            const float ix = r.x + 14.f;
            if (e.dir) {
                const Color warm = Fade(theme::AccentWarm, sel ? 0.95f : 0.8f);
                p_.Round({ix, r.Cy() - 6.f, 8.f, 5.f}, 1.5f, warm);
                p_.Round({ix, r.Cy() - 3.5f, 17.f, 11.f}, 2.5f, warm);
            } else {
                RectF box{ix, r.Cy() - 8.f, 16.f, 16.f};
                p_.RoundStroke(box, 3.5f, 1.2f, Fade(theme::Text3, 0.95f));
                IconPlay(p_, box.Cx() + 0.5f, box.Cy(), 6.5f, Fade(theme::Text2, 0.9f));
            }
        }
        p_.Text(e.name, {nameX, r.y + 2, r.w - (nameX - r.x) - 66, r.h - 4}, mediaMode ? 12.5f : 12.f,
                sel ? theme::Text : theme::Text2, sel ? 600 : 400);
        if (e.dir) {
            IconChevron(p_, r.Right() - 20, r.Cy(), 10.f, Fade(theme::Text3, 0.9f), false);
        } else {
            std::wstring ext = FileExtOf(e.name);
            for (auto& c : ext)
                if (c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
            p_.TextR(ext, {r.Right() - 66, r.y + 2, 48, r.h - 4}, 9.f, sel ? theme::Accent : theme::Text3, 500,
                     FontFace::Mono);
        }
    }
    p_.PopClip();

    if (fbEntries_.empty()) {
        p_.TextC(fbMode_ == FbMode::MediaFolder ? L"没有子文件夹 · 可直接打开当前文件夹"
                  : fbMode_ == FbMode::Subtitle  ? L"这里没有可导入的字幕文件"
                                                 : L"这里没有可播放的媒体文件",
                 {rFbList_.x, rFbList_.Cy() - 12, rFbList_.w, 24}, 12.f, theme::Text3, 400);
    }

    if (fbMaxScroll_ > 0 && !rFbRows_.empty()) {
        RectF track{panel.Right() - 9, rFbList_.y + 2, 3.f, rFbList_.h - 4};
        p_.Round(track, 1.5f, Hex(0xFFFFFF, 0.08f));
        const float th = std::max(28.f, track.h * (float)fbVisibleRows_ / (float)fbEntries_.size());
        const float yy = track.y + (track.h - th) * ((float)fbScroll_ / (float)fbMaxScroll_);
        p_.Round({track.x, yy, track.w, th}, 1.5f, Hex(0xFFFFFF, 0.30f));
    }

    // -- footer
    p_.Fill({panel.x + 24, panel.Bottom() - 88, panel.w - 48, 1}, theme::Line);
    p_.Text(fbMode_ == FbMode::MediaFolder ? L"单击选择文件夹 · 双击进入 · 打开即播放该文件夹"
            : fbMode_ == FbMode::Subtitle  ? L"单击选择 · 双击文件导入字幕 · Esc 取消"
                                           : L"单击选择 · 双击文件直接播放 · Esc 取消",
            {panel.x + 24, rFbOpen_.Cy() - 9, std::max(60.f, rFbCancel_.x - panel.x - 48), 18}, 9.5f,
            theme::Text3, 400);
    ButtonBase(p_, rFbCancel_, false, hovered(kHitFbCancel), pressedId_ == kHitFbCancel && mouseDown_);
    p_.TextC(L"取消", rFbCancel_, 12.f, theme::Text2, 500);
    const bool openHover = hovered(kHitFbOpen);
    p_.Round(rFbOpen_, rFbOpen_.h * 0.5f,
             Fade(theme::Accent, openHover ? 1.f : 0.86f));
    p_.RoundStroke(rFbOpen_, rFbOpen_.h * 0.5f, 1.f, Hex(0xFFFFFF, 0.18f));
    p_.TextC(L"打开", rFbOpen_, 12.5f, Hex(0x081016), 600);
}

void App::drawCenterOverlay() {
    if (!hasMedia_) return;
    if (engine_.ended()) {
        RectF card = rEndCard_;
        p_.Round(card, 20.f, Hex(0x0C0F16, 0.94f));
        p_.RoundStroke(card, 20.f, 1.f, theme::Line);
        p_.TextC(L"播放结束", {card.x, card.y + 28, card.w, 30}, 18.f, theme::Text, 600, FontFace::Display);
        p_.TextC(FileNameOf(engine_.path()), {card.x + 24, card.y + 64, card.w - 48, 20}, 11.f, theme::Text3, 400);
        bool hovReplay = hovered(kHitReplay);
        ButtonBase(p_, rReplay_, true, hovReplay, pressedId_ == kHitReplay && mouseDown_);
        p_.TextC(L"重播", rReplay_, 13.f, Hex(0x0A0C10), 600);
        if (playlist_.size() > 1) {
            bool hovNext = hovered(kHitNextUp);
            ButtonBase(p_, rNextUp_, false, hovNext, pressedId_ == kHitNextUp && mouseDown_);
            p_.TextC(L"下一集", rNextUp_, 13.f, theme::Text2, 500);
        }
        return;
    }

    if (centerPlay_ > 0.01f) {
        float a = centerPlay_ * controlsAlpha_;
        p_.PushOpacity(a);
        float cx = W_ / 2, cy = H_ / 2;
        bool hov = hovered(kHitPlay);
        p_.Circle(cx, cy, hov ? 42.f : 39.f, Hex(0x000000, 0.38f));
        p_.Ring(cx, cy, hov ? 42.f : 39.f, 1.f, Hex(0xFFFFFF, 0.20f));
        IconPlay(p_, cx + 2, cy, 24.f, Hex(0xFFFFFF, 0.94f));
        p_.PopOpacity();
    }

    if (engine_.buffering()) {
        float spin = (float)fmod(now_ * 220.0, 360.0);
        float cx = W_ / 2, cy = H_ / 2 + (centerPlay_ > 0.5f ? 0 : 0);
        p_.PushOpacity(std::max(0.35f, controlsAlpha_));
        StrokeArc(p_, cx, cy, 17.f, spin, 270.f, 2.2f, Fade(theme::Text, 0.85f));
        p_.TextC(L"正在缓冲", {cx - 60, cy + 30, 120, 20}, 11.f, theme::Text3, 400);
        p_.PopOpacity();
    }
}

void App::drawToast() {
    if (toastAlpha_ <= 0.01f || toastText_.empty()) return;
    float tw = p_.Measure(toastText_, 12.f, 500) + 58.f;
    float ty = screen_ == Screen::Player ? H_ - 170.f : H_ - 84.f;
    if (screen_ == Screen::Player && subtitleEnabled_ && !subtitleText_.empty()) ty -= 84.f;
    RectF r{W_ / 2 - tw / 2, ty, tw, 40};
    p_.PushOpacity(toastAlpha_);
    p_.Round(r, 20.f, Hex(0x14171F, 0.96f));
    p_.RoundStroke(r, 20.f, 1.f, theme::LineHi);
    p_.Circle(r.x + 24, r.Cy(), 9.f, Fade(theme::Accent, 0.18f));
    p_.Line(r.x + 20, r.Cy(), r.x + 23.5f, r.Cy() + 3.5f, 1.6f, theme::Accent);
    p_.Line(r.x + 23.5f, r.Cy() + 3.5f, r.x + 29, r.Cy() - 3.5f, 1.6f, theme::Accent);
    p_.Text(toastText_, {r.x + 40, r.y, r.w - 52, r.h}, 12.f, theme::Text, 500);
    p_.PopOpacity();
}

void App::drawPlaylist() {
    p_.Fill({0, 0, W_, H_}, Hex(0x000000, 0.35f));
    RectF panel = rPlaylistPanel_;
    p_.Fill({panel.x - 1, 0, 1, H_}, theme::Line);
    p_.Fill(panel, Hex(0x0A0C11, 0.98f));
    p_.Linear({panel.x, 0, panel.w, 160}, Hex(0xFFFFFF, 0.025f), Hex(0xFFFFFF, 0.f), 90.f);

    p_.Text(L"播放列表", {panel.x + 28, 24, 200, 26}, 15.f, theme::Text, 600);
    wchar_t buf[64];
    swprintf_s(buf, L"%d 个影片", (int)playlist_.size());
    p_.Text(buf, {panel.x + 28, 50, 200, 18}, 10.5f, theme::Text3, 400);
    RectF closeR{panel.Right() - 54, 20, 34, 34};
    if (hovered(kHitPlaylistClose)) p_.Round(closeR.Inset(4, 4), 7, Hex(0xFFFFFF, 0.09f));
    IconClose(p_, closeR, theme::Text3, 9.f);

    if (playlist_.empty()) {
        p_.TextC(L"列表是空的", {panel.x, H_ / 2 - 10, panel.w, 24}, 12.5f, theme::Text3, 400);
        return;
    }
    int maxRows = std::max(0, (int)((H_ - 104.f) / 64.f));
    int rowCount = std::min<int>((int)playlist_.size(), maxRows);
    for (int i = 0; i < rowCount; ++i) {
        RectF row{rPlaylistPanel_.x + 12, 84.f + i * 64.f, rPlaylistPanel_.w - 24, 56};
        bool current = i == current_;
        bool hov = hovered(kHitPlaylistRowBase + i);
        if (current) p_.Round(row, 10.f, Hex(0xFFFFFF, 0.055f));
        else if (hov) p_.Round(row, 10.f, Hex(0xFFFFFF, 0.03f));
        if (current) p_.Round({row.x, row.y + 16, 2.f, row.h - 32}, 1.f, theme::Accent);
        if (current && playing_) {
            p_.TextR(FormatTime(position_), {row.Right() - 78, row.y, 66, row.h}, 10.5f, theme::Accent, 500,
                     FontFace::Mono);
        } else {
            wchar_t idx[16];
            swprintf_s(idx, L"%02d", i + 1);
            p_.Text(idx, {row.x + 18, row.y, 30, row.h}, 10.f, current ? theme::Accent : theme::Text3, 500,
                     FontFace::Mono);
            p_.Text(playlist_[i].title, {row.x + 54, row.y + 6, row.w - 130, 24}, 12.5f,
                    current ? theme::Text : theme::Text2, 500);
            if (playlist_[i].duration > 0) {
                p_.Text(FormatTime(playlist_[i].duration), {row.x + 54, row.y + 30, row.w - 130, 16}, 10.f,
                        theme::Text3, 400);
                p_.TextR(FormatTime(playlist_[i].duration), {row.Right() - 76, row.y, 64, row.h}, 10.f,
                         theme::Text3, 400, FontFace::Mono);
            } else {
                p_.Text(L"\u2014", {row.x + 54, row.y + 30, 40, 16}, 10.f, theme::Text3, 400);
            }
        }
    }
}

void App::drawSettings() {
    p_.Fill({0, 0, W_, H_}, Hex(0x000000, 0.58f));
    RectF panel = rSettingsPanel_;
    p_.Round(panel, 20.f, Hex(0x0D1017, 0.99f));
    p_.RoundStroke(panel, 20.f, 1.f, theme::LineHi);
    p_.LinearRound({panel.x + 1, panel.y + 1, panel.w - 2, 150}, 19.f, Hex(0xFFFFFF, 0.035f),
                   Hex(0xFFFFFF, 0.f), 90.f);

    p_.Text(L"设置", {panel.x + 32, panel.y + 20, 240, 30}, 19.f, theme::Text, 650, FontFace::Display);
    p_.Text(L"所有设置即时生效", {panel.x + 34, panel.y + 50, 300, 18}, 10.f, theme::Text3, 400);
    RectF closeR{panel.Right() - 52, panel.y + 16, 36, 36};
    p_.Round(closeR, 9.f, hovered(kHitSettingsClose) ? Hex(0xFFFFFF, 0.09f) : Hex(0xFFFFFF, 0.02f));
    IconClose(p_, closeR, theme::Text3, 9.f);

    const float sx = panel.x, sy = panel.y;
    auto section = [&](float y, const wchar_t* label) {
        p_.TextTracked(label, {sx + 32, y, 220, 16}, 9.5f, theme::Text3, 0.14f, 650);
        p_.Fill({sx + 32, y + 22, panel.w - 448, 1}, theme::Line);
    };
    auto toggleRow = [&](float y, const wchar_t* label, bool on, int id) {
        p_.Text(label, {sx + 32, y, 200, 24}, 12.5f, theme::Text2, 500);
        Switch(p_, {sx + 230, y - 8, 130, 40}, on, hovered(id));
    };
    auto valueRow = [&](float y, const wchar_t* label, const std::wstring& value, int id) {
        p_.Text(label, {sx + 32, y, 160, 24}, 12.5f, theme::Text2, 500);
        p_.TextR(value, {sx + 210, y, 150, 24}, 11.f, theme::Text3, 400, FontFace::Mono);
        if (hovered(id)) p_.Round({sx + 24, y - 3, 336, 30}, 7.f, Hex(0xFFFFFF, 0.035f));
    };

    section(sy + 82, L"播放");
    toggleRow(sy + 112, L"自动续播", autoResume_, kHitAutoResume);
    toggleRow(sy + 154, L"自动下一集", autoNext_, kHitAutoNext);
    toggleRow(sy + 196, L"界面动效", animations_, kHitAnimations);
    toggleRow(sy + 238, L"硬件解码", !softwareDecode_, kHitHwDecode);

    section(sy + 274, L"画面");
    toggleRow(sy + 304, L"HDR 输出", !hdrDisabled_, kHitHdrOutput);
    p_.Text(L"主题强调色", {sx + 32, sy + 346, 180, 24}, 12.5f, theme::Text2, 500);
    static const Color accents[] = {Hex(0x74E3F3), Hex(0xB18CFF), Hex(0xFF8FB1), Hex(0xFFC978),
                                    Hex(0x8BE28B)};
    for (int i = 0; i < 5; ++i) {
        const float ax = sx + 236 + i * 26.f;
        p_.Circle(ax + 9, sy + 358, i == accentPreset_ ? 10.f : 8.f, accents[i]);
        if (i == accentPreset_) p_.Circle(ax + 9, sy + 358, 14.f, Fade(accents[i], 0.25f));
    }
    toggleRow(sy + 390, L"文件缩略图", thumbnails_, kHitThumbs);

    section(sy + 426, L"音频 / 字幕");
    toggleRow(sy + 456, L"独占音频（高保真）", exclusiveAudio_, kHitExclusive);
    valueRow(sy + 500, L"音轨", engine_.audioTrackCount() > 1 ? engine_.audioTrackName() : L"默认", kHitAudioTrack);
    valueRow(sy + 542, L"字幕样式", L"字体 / 字号 / 位置", kHitSettingsSubStyle);

    // Right-hand media/system dossier.
    RectF data{sx + 400, sy + 80, panel.w - 432, panel.h - 158};
    p_.Round(data, 14.f, Hex(0x080A10, 0.72f));
    p_.RoundStroke(data, 14.f, 1.f, theme::Line);
    p_.TextTracked(L"媒体信息", {data.x + 18, data.y + 16, 220, 16}, 9.5f, theme::Text3, 0.14f, 650);
    auto dl = [&](float y, const wchar_t* k, const std::wstring& v) {
        p_.Text(k, {data.x + 18, y, 78, 18}, 9.5f, theme::Text3, 400);
        p_.Text(v, {data.x + 100, y, data.w - 116, 18}, 9.5f, theme::Text2, 450, FontFace::Mono);
    };
    if (hasMedia_) {
        const MediaInfo& mi = engine_.info();
        const EngineStats st = engine_.stats();
        std::wstring file = FileNameOf(mi.path);
        if (file.size() > 34) file = file.substr(0, 32) + L"…";
        dl(data.y + 46, L"文件", file);
        dl(data.y + 70, L"容器", PrettyContainer(mi.container));
        wchar_t b[256];
        swprintf_s(b, L"%s  ·  %s", FormatTime(mi.duration).c_str(), FormatSize(mi.fileSize).c_str());
        dl(data.y + 94, L"时长", b);
        if (mi.hasVideo) {
            std::wstring codec = Widen(mi.video.codec);
            if (!mi.video.profile.empty()) codec += L" " + Widen(mi.video.profile);
            if (codec.size() > 26) codec = codec.substr(0, 24) + L"…";
            dl(data.y + 122, L"视频", codec);
            swprintf_s(b, L"%dx%d  ·  %.2f fps", mi.video.width, mi.video.height, mi.video.fps);
            dl(data.y + 146, L"画面", b);
            swprintf_s(b, L"%s  ·  %d-bit  ·  %s", Widen(mi.video.pixFmt).c_str(), mi.video.bitDepth,
                       mi.video.hdr ? L"HDR" : L"SDR");
            dl(data.y + 170, L"色彩", b);
            dl(data.y + 194, L"解码", st.hw ? L"D3D11VA / GPU" : L"FFmpeg / CPU");
            swprintf_s(b, L"%llu 帧  ·  %.2f ms  ·  丢 %llu", (unsigned long long)st.frames, st.avgDecodeMs,
                       (unsigned long long)st.dropped);
            dl(data.y + 218, L"统计", b);
        }
        if (mi.hasAudio) {
            swprintf_s(b, L"%s  ·  %d Hz  ·  %d ch", Widen(mi.audio.codec).c_str(), mi.audio.sampleRate,
                       mi.audio.channels);
            dl(data.y + 248, L"音频", b);
        }
        swprintf_s(b, L"%d 轨  ·  %s  ·  %d cues", (int)mi.subtitles.size(),
                   st.subtitleBackend == SubtitleBackend::Ass ? L"ASS" :
                   st.subtitleBackend == SubtitleBackend::Text ? L"TEXT" : L"—",
                   st.subtitleCues);
        dl(data.y + 272, L"字幕", b);
        swprintf_s(b, L"缓存 %d  ·  丢帧 %llu  ·  欠载 %llu", st.audioBufferFrames,
                   (unsigned long long)st.dropped, (unsigned long long)st.underruns);
        dl(data.y + 296, L"健康", b);
        p_.Fill({data.x + 18, data.y + 326, data.w - 36, 1}, theme::Line);
        swprintf_s(b, L"%dx%d  ·  %.0f%%  ·  %s", gfx_->width(), gfx_->height(), gfx_->scale() * 100.f,
                   gfx_->hdrOutput() ? L"scRGB HDR" : L"SDR");
        dl(data.y + 344, L"显示", b);
        swprintf_s(b, L"%.0f MB  ·  CPU %.1f%%", workingSet_ / 1048576.0, processCpu_);
        dl(data.y + 368, L"占用", b);
        swprintf_s(b, L"%.1f fps  ·  队列 %d", renderFps_, (int)st.queue);
        dl(data.y + 392, L"渲染", b);
    } else {
        p_.Text(L"未打开文件", {data.x + 18, data.y + 52, data.w - 36, 24}, 13.f, theme::Text3, 500);
        p_.Text(L"打开一部影片后，这里会显示编码、HDR、音轨、字幕和性能数据。",
                {data.x + 18, data.y + 86, data.w - 36, 60}, 10.5f, theme::Text3, 400);
    }
    p_.Text(L"Prism 1.0  ·  FFmpeg 8.1  ·  x86-64 AVX2", {data.x + 16, data.Bottom() - 24, data.w - 32, 16},
            8.5f, theme::Text3, 400);

    // Bottom shortcuts.
    auto button = [&](RectF r, const wchar_t* label, int id) {
        p_.Round(r, 8.f, hovered(id) ? Hex(0xFFFFFF, 0.08f) : Hex(0xFFFFFF, 0.035f));
        p_.RoundStroke(r, 8.f, 1.f, hovered(id) ? theme::LineHi : theme::Line);
        p_.TextC(label, r, 10.5f, theme::Text2, 500);
    };
    button({sx + 32, sy + 588, 170, 34}, L"默认应用 / 图标", kHitDefaultApps);
    button({sx + 210, sy + 588, 130, 34}, L"快捷键…", kHitKeyEditor);
    button({sx + 348, sy + 588, 120, 34}, L"打开日志", kHitOpenLog);
    button({sx + 476, sy + 588, 120, 34}, showStats_ ? L"关闭统计" : L"播放统计", kHitStatsRow);
}


void App::drawKeyEditor() {
    p_.Fill({0, 0, W_, H_}, Hex(0x000000, 0.62f));
    RectF panel = rKeyPanel_;
    p_.Round(panel, 20.f, Hex(0x0D1017, 0.99f));
    p_.RoundStroke(panel, 20.f, 1.f, theme::LineHi);
    p_.Text(L"快捷键", {panel.x + 32, panel.y + 18, 240, 30}, 19.f, theme::Text, 650, FontFace::Display);
    p_.Text(keyCapturing_ ? L"按下新的按键组合…  Esc 取消" : L"点击一行，然后按下新的按键",
            {panel.x + 34, panel.y + 50, panel.w - 80, 18}, 10.5f,
            keyCapturing_ ? theme::Accent : theme::Text3, 400);
    RectF closeR{panel.Right() - 52, panel.y + 16, 36, 36};
    p_.Round(closeR, 9.f, hovered(kHitKeyClose) ? Hex(0xFFFFFF, 0.09f) : Hex(0xFFFFFF, 0.02f));
    IconClose(p_, closeR, theme::Text3, 9.f);
    p_.Fill({panel.x + 18, panel.y + 78, panel.w - 36, 1}, theme::Line);

    const float top = panel.y + 96.f;
    const float bottom = panel.Bottom() - 78.f;
    p_.PushClip({panel.x + 12, top, panel.w - 24, bottom - top});
    for (size_t i = 0; i < rKeyRows_.size(); ++i) {
        const int idx = keyScroll_ + (int)i;
        if (idx >= (int)keyBindings_.size()) break;
        const RectF r = rKeyRows_[i];
        const bool sel = idx == keySelected_;
        const bool cap = sel && keyCapturing_;
        if (cap) p_.Round(r, 8.f, Fade(theme::Accent, 0.16f));
        else if (sel) p_.Round(r, 8.f, Hex(0xFFFFFF, 0.07f));
        else if (hovered(kHitKeyRowBase + idx)) p_.Round(r, 8.f, Hex(0xFFFFFF, 0.04f));
        if (sel) p_.Round({r.x, r.y + 7, 2.5f, r.h - 14}, 1.2f, theme::Accent);
        const KeyBinding& b = keyBindings_[idx];
        p_.Text(ActionLabel(b.action), {r.x + 16, r.y + 4, r.w - 210, 22}, 11.5f,
                sel ? theme::Text : theme::Text2, 500);
        std::wstring combo = cap ? L"按下按键…" : ComboText(b);
        const float cw = p_.Measure(combo, 10.f, 500, FontFace::Mono);
        RectF capR{r.Right() - cw - 30, r.y + 5, cw + 20, 20};
        p_.Round(capR, 5.f, cap ? Fade(theme::Accent, 0.2f) : Hex(0xFFFFFF, 0.06f));
        p_.RoundStroke(capR, 5.f, 1.f, cap ? Fade(theme::Accent, 0.6f) : theme::Line);
        p_.TextC(combo, capR, 10.f, cap ? theme::Accent : theme::Text3, 500, FontFace::Mono);
    }
    p_.PopClip();

    if (keyMaxScroll_ > 0) {
        RectF track{panel.Right() - 18, top, 4, bottom - top};
        p_.Round(track, 2.f, Hex(0xFFFFFF, 0.06f));
        const float h = std::max(28.f, track.h * (float)keyVisibleRows_ / std::max(1, (int)keyBindings_.size()));
        const float y = track.y + (track.h - h) * (float)keyScroll_ / std::max(1, keyMaxScroll_);
        p_.Round({track.x, y, track.w, h}, 2.f, Fade(theme::Accent, 0.65f));
    }

    auto button = [&](RectF r, const wchar_t* label, int id) {
        p_.Round(r, 8.f, hovered(id) ? Hex(0xFFFFFF, 0.08f) : Hex(0xFFFFFF, 0.035f));
        p_.RoundStroke(r, 8.f, 1.f, hovered(id) ? theme::LineHi : theme::Line);
        p_.TextC(label, r, 10.5f, theme::Text2, 500);
    };
    button({panel.x + 24, panel.Bottom() - 54, 124, 32}, L"恢复默认", kHitKeyReset);
    button({panel.x + 158, panel.Bottom() - 54, 150, 32}, L"打开 keys.ini", kHitKeyFile);
    p_.Text(L"即时写入 keys.ini", {panel.x + 326, panel.Bottom() - 47, 150, 18}, 9.f, theme::Text3, 400);
}

void App::drawStats() {
    const EngineStats st = engine_.stats();
    const MediaInfo& mi = engine_.info();
    const float w = 408.f;
    const float h = std::min(590.f, H_ - 100.f);
    RectF card{W_ - w - 24, 76, w, h};
    p_.Round(card, 16.f, Hex(0x07090E, 0.94f));
    p_.RoundStroke(card, 16.f, 1.f, theme::LineHi);
    p_.Text(L"播放统计", {card.x + 20, card.y + 16, 200, 24}, 15.f, theme::Text, 650, FontFace::Display);
    p_.Text(L"I 关闭", {card.Right() - 82, card.y + 20, 62, 16}, 9.5f, theme::Text3, 400);
    p_.Fill({card.x + 20, card.y + 48, card.w - 40, 1}, theme::Line);

    float y = card.y + 62;
    auto section = [&](const wchar_t* name) {
        p_.TextTracked(name, {card.x + 20, y, 220, 14}, 8.5f, theme::Accent, 0.14f, 650);
        y += 20;
    };
    auto line = [&](const wchar_t* k, const std::wstring& v) {
        p_.Text(k, {card.x + 20, y, 104, 16}, 9.5f, theme::Text3, 400);
        p_.TextR(v, {card.x + 124, y, card.w - 144, 16}, 9.5f, theme::Text2, 450, FontFace::Mono);
        y += 19;
    };
    auto bar = [&](float pct, Color c) {
        RectF r{card.x + 20, y, card.w - 40, 3};
        p_.Round(r, 2.f, Hex(0xFFFFFF, 0.08f));
        p_.Round({r.x, r.y, r.w * std::clamp(pct, 0.f, 1.f), r.h}, 2.f, c);
        y += 10;
    };
    wchar_t b[256];
    if (hasMedia_) {
        std::wstring file = FileNameOf(mi.path);
        if (file.size() > 38) file = file.substr(0, 36) + L"…";
        line(L"文件", file);
        line(L"容器", PrettyContainer(mi.container));
        swprintf_s(b, L"%s  ·  %s", FormatTime(mi.duration).c_str(), FormatSize(mi.fileSize).c_str());
        line(L"时长", b);
        section(L"视频");
        std::wstring codec = Widen(mi.video.codec);
        if (!mi.video.profile.empty()) codec += L" " + Widen(mi.video.profile);
        line(L"编码", codec);
        swprintf_s(b, L"%dx%d  ·  %.3f fps", mi.video.width, mi.video.height, mi.video.fps);
        line(L"分辨率", b);
        swprintf_s(b, L"%s  ·  %d-bit  ·  %s", Widen(mi.video.pixFmt).c_str(), mi.video.bitDepth,
                   mi.video.hdr ? L"HDR10 / PQ" : L"SDR");
        line(L"色彩", b);
        line(L"解码器", st.hw ? L"D3D11VA" : L"FFmpeg software");
        swprintf_s(b, L"%llu  ·  丢 %llu  ·  %.2f ms", (unsigned long long)st.frames,
                   (unsigned long long)st.dropped, st.avgDecodeMs);
        line(L"帧 / 延迟", b);
        swprintf_s(b, L"%d", (int)st.queue);
        line(L"帧队列", b);
        bar(st.queue / 8.f, theme::Accent);
        section(L"音频");
        swprintf_s(b, L"%s  ·  %d Hz  ·  %d ch", Widen(mi.audio.codec).c_str(), mi.audio.sampleRate,
                   mi.audio.channels);
        line(L"格式", b);
        line(L"音轨", engine_.audioTrackCount() > 1 ? engine_.audioTrackName() : L"默认");
        swprintf_s(b, L"%d 帧  ·  欠载 %llu  ·  饥饿 %llu", st.audioBufferFrames,
                   (unsigned long long)st.underruns, (unsigned long long)st.starvations);
        line(L"缓冲", b);
        section(L"字幕");
        swprintf_s(b, L"%d 轨  ·  %d cues  ·  %s", (int)mi.subtitles.size(), st.subtitleCues,
                   st.subtitleBackend == SubtitleBackend::Ass ? L"ASS" :
                   st.subtitleBackend == SubtitleBackend::Text ? L"TEXT" : L"—");
        line(L"状态", b);
        swprintf_s(b, L"%+.2f s  ·  %.0f%%", subtitleDelay_, subtitleScale_ * 100.f);
        line(L"延迟 / 缩放", b);
        section(L"系统");
        line(L"速度", SpeedText(engine_.speed()));
        swprintf_s(b, L"%.0f MB  ·  CPU %.1f%%", workingSet_ / 1048576.0, processCpu_);
        line(L"占用", b);
        swprintf_s(b, L"%dx%d  ·  %.0f%%", gfx_->width(), gfx_->height(), gfx_->scale() * 100.f);
        line(L"显示", b);
        line(L"输出", gfx_->hdrOutput() ? L"scRGB 16F HDR" : L"SDR BGRA8");
        swprintf_s(b, L"%.1f fps", renderFps_);
        line(L"渲染", b);
    } else {
        p_.Text(L"未打开文件", {card.x + 20, y, card.w - 40, 24}, 13.f, theme::Text3, 500);
    }
}

void App::drawSpeedMenu() {
    p_.Fill({0, 0, W_, H_}, Hex(0x000000, 0.25f));
    RectF m = rSpeedMenu_;
    p_.Round(m, 12.f, Hex(0x14181F, 0.98f));
    p_.RoundStroke(m, 12.f, 1.f, theme::LineHi);
    static const double speeds[7] = {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
    for (int i = 0; i < 7; ++i) {
        RectF item = rSpeedItems_[i];
        bool hov = hovered(kHitSpeedBase + i);
        bool current = std::fabs(engine_.speed() - speeds[i]) < 0.01;
        if (hov) p_.Round(item, 8.f, Hex(0xFFFFFF, 0.07f));
        p_.Text(SpeedText(speeds[i]), {item.x + 12, item.y, 70, item.h}, 11.5f,
                current ? theme::Accent : theme::Text2, current ? 600 : 400, FontFace::Mono);
        if (current) p_.Circle(item.Right() - 14, item.Cy(), 2.5f, theme::Accent);
    }
}
