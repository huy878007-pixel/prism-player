#include "ui.h"

#include "ass_render.h"

#include <cwchar>

#include <sstream>
#include <cwctype>
#include <psapi.h>
#include <chrono>
#include <filesystem>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>

namespace {

double QpcNow() {
    static double freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return (double)f.QuadPart;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / freq;
}

float Clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

// Style-panel "字号" cycle. Ctrl+wheel is free-form between kSubtitleScaleMin/Max.
constexpr float kSubtitleScalePresets[] = {0.8f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f};
constexpr float kSubtitleScaleMin = 0.5f;
constexpr float kSubtitleScaleMax = 5.0f;

double ProbeDuration(const std::wstring& path) {
    AVFormatContext* ctx = nullptr;
    std::string utf8 = Narrow(path);
    if (avformat_open_input(&ctx, utf8.c_str(), nullptr, nullptr) < 0) return 0;
    double d = ctx->duration != AV_NOPTS_VALUE ? (double)ctx->duration / AV_TIME_BASE : 0;
    if (d <= 0 && avformat_find_stream_info(ctx, nullptr) >= 0 && ctx->duration != AV_NOPTS_VALUE)
        d = (double)ctx->duration / AV_TIME_BASE;
    avformat_close_input(&ctx);
    return d;
}

// Keeps "C:\" intact but drops trailing separators from deeper paths.
std::wstring NormalizeDir(std::wstring dir) {
    while (dir.size() > 3 && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    return dir;
}

std::wstring ParentDirOf(const std::wstring& dir) {
    std::filesystem::path p(dir);
    std::filesystem::path parent = p.parent_path();
    if (parent.empty() || parent == p) return {};
    return NormalizeDir(parent.wstring());
}

}  // namespace

App::App() = default;

App::~App() { shutdown(); }

bool App::init(Gfx* gfx, HWND hwnd, bool headless) {
    gfx_ = gfx;
    ApplyThemeColors();
    loadKeyBindings();
    hwnd_ = hwnd;
    headless_ = headless;
    if (!p_.Init(gfx)) return false;
    W_ = (float)gfx->width() / gfx->scale();
    H_ = (float)gfx->height() / gfx->scale();
    savedPlacement_.length = sizeof(savedPlacement_);
    loadSettings();
    runKernelBench();
    now_ = QpcNow();
    lastTick_ = now_;
    lastMouseMove_ = now_;
    if (!subFontFamily_.empty()) applySubtitleFamily(subFontFamily_);
    if (hwnd_) DragAcceptFiles(hwnd_, TRUE);
    inited_ = true;
    return true;
}

void App::shutdown() {
    fbThumbsWorker_.stop();
    if (!inited_) return;
    saveCurrentPosition();
    saveSettings();
    if (frame_) {
        engine_.releaseFrame(frame_);
        frame_ = nullptr;
    }
    engine_.close();
    inited_ = false;
}

void App::resize(int width, int height) {
    if (!gfx_) return;
    dirty_ = true;
    W_ = (float)width / gfx_->scale();
    H_ = (float)height / gfx_->scale();
}

void App::addHit(int id, const RectF& r) { hits_.push_back({id, r}); }

int App::hitTest(float x, float y) const {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (it->rect.Contains(x, y)) return it->id;
    }
    return kHitNone;
}


static bool IsTextSubCodec(const std::string& codec) {
    return codec == "subrip" || codec == "ass" || codec == "ssa" || codec == "mov_text" || codec == "webvtt" ||
           codec == "text";
}

static int SubLangRank(const std::string& lang) {
    if (lang == "chi" || lang == "zho" || lang == "zh") return 0;
    if (lang == "eng") return 1;
    if (lang == "jpn") return 2;
    return 3;
}

void App::layout() {
    hits_.clear();
    const bool fontPickerOpened = popup_ == Popup::FontPicker && laidOutPopup_ != Popup::FontPicker;
    laidOutPopup_ = popup_;
    // The video surface is registered first so every control sits above it.
    if (screen_ == Screen::Player) addHit(kHitVideoArea, {0, 0, W_, H_});
    subDragRect_ = {};
    if (screen_ == Screen::Player && subtitleEnabled_ && !subtitleText_.empty() && popup_ == Popup::None) {
        const float size = 21.f * subtitleScale_;
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
        if (lines < 1) lines = 1;
        const float totalH = size * 1.32f * (float)lines;
        const float bottom = H_ - (112.f + subtitlePos_ * 72.f) + subY_ * H_;
        const float cx = W_ * 0.5f + subX_ * W_;
        subDragRect_ = {cx - maxW * 0.5f - 16.f, bottom - totalH - 12.f, maxW + 32.f, totalH + 24.f};
        addHit(kHitSubtitleDrag, subDragRect_);
    }
    const float titleH = 56.f;
    const float pad = 64.f;
    rTitleBar_ = {0, 0, W_, titleH};

    float bw = 46.f, bh = 36.f;
    rClose_ = {W_ - bw, 10, bw, bh};
    rMax_ = {W_ - bw * 2, 10, bw, bh};
    rMin_ = {W_ - bw * 3, 10, bw, bh};
    rSettingsTop_ = {W_ - bw * 3 - 48, 8, 40, 40};
    rStatsTop_ = {rSettingsTop_.x - 44, 8, 40, 40};
    rOpenTop_ = {W_ - bw * 3 - 176, 8, 116, 40};

    addHit(kHitClose, rClose_);
    addHit(kHitMax, rMax_);
    addHit(kHitMin, rMin_);
    addHit(kHitSettingsTop, rSettingsTop_);
    addHit(kHitStatsTop, rStatsTop_);
    rSettingsPanel_ = {W_ / 2 - 380, H_ / 2 - 320, 760, 640};
    if (popup_ == Popup::Settings) {
        const float sx = rSettingsPanel_.x, sy = rSettingsPanel_.y;
        addHit(kHitSettingsClose, {rSettingsPanel_.Right() - 52, sy + 16, 36, 36});
        addHit(kHitAutoResume, {sx + 230, sy + 104, 130, 40});
        addHit(kHitAutoNext, {sx + 230, sy + 146, 130, 40});
        addHit(kHitAnimations, {sx + 230, sy + 188, 130, 40});
        addHit(kHitHwDecode, {sx + 230, sy + 230, 130, 40});
        addHit(kHitHdrOutput, {sx + 230, sy + 300, 130, 40});
        addHit(kHitAccent, {sx + 230, sy + 342, 130, 40});
        addHit(kHitThumbs, {sx + 230, sy + 384, 130, 40});
        addHit(kHitExclusive, {sx + 230, sy + 454, 130, 40});
        addHit(kHitAudioTrack, {sx + 32, sy + 496, 360, 36});
        addHit(kHitSettingsSubStyle, {sx + 32, sy + 538, 360, 36});
        addHit(kHitDefaultApps, {sx + 32, sy + 588, 220, 34});
        addHit(kHitOpenLog, {sx + 266, sy + 588, 140, 34});
        addHit(kHitStatsRow, {sx + 420, sy + 588, 140, 34});
    }
    if (screen_ == Screen::Home) {
        float panelW = std::clamp(W_ * 0.345f, 430.f, 620.f);
        rRecentPanel_ = {W_ - pad - panelW, std::max(120.f, H_ * 0.155f), panelW, std::min(520.f, H_ - 240)};

        rHero_ = {pad, H_ * 0.30f, std::min(660.f, W_ * 0.50f), 74};
        rSub_ = {pad, rHero_.Bottom() + 24, std::min(610.f, W_ * 0.46f), 82};
        rOpen_ = {pad, rSub_.Bottom() + 38, 142, 46};
        rFolder_ = {rOpen_.Right() + 12, rOpen_.y, 152, 46};
        rDrop_ = {pad, rOpen_.Bottom() + 30, std::min(520.f, W_ * 0.42f), 20};

        rRecent_.clear();
        float rowH = 76.f;
        int maxRows = std::max(1, (int)((rRecentPanel_.h - 84.f - 116.f) / rowH));
        maxRows = std::min(maxRows, 4);
        float y = rRecentPanel_.y + 84;
        for (int i = 0; i < maxRows; ++i) {
            if (i >= (int)recents_.size()) break;
            RectF r{rRecentPanel_.x + 16, y, rRecentPanel_.w - 32, rowH - 8};
            rRecent_.push_back(r);
            addHit(kHitRecentBase + i, r);
            y += rowH;
        }

        addHit(kHitOpenFile, rOpen_);
        addHit(kHitOpenFolder, rFolder_);
        if (popup_ == Popup::FileBrowser) layoutFileBrowser();
        return;
    }

    // Player
    rVideo_ = {0, 0, W_, H_};
    rBack_ = {20, 16, 40, 40};
    rTitleText_ = {72, 12, W_ - 520, 26};
    rStatus_ = {72, 38, W_ - 520, 18};
    rSettingsBtn_ = {W_ - 186, 8, 40, 40};
    rStatsBtn_ = {rSettingsBtn_.x - 44, 8, 40, 40};

    float rowY = H_ - 88;
    float cx = 50.f;
    rPlay_ = {cx - 22, rowY + 2, 44, 44};
    rPrev_ = {cx + 34, rowY + 8, 32, 32};
    rNext_ = {cx + 70, rowY + 8, 32, 32};
    bool hasPrevNext = playlist_.size() > 1;
    float timeX = hasPrevNext ? 160.f : 96.f;
    rTime_ = {timeX, rowY + 8, 230, 32};

    float right = W_ - 28.f;
    rFullscreen_ = {right - 40, rowY + 6, 40, 40};
    rPlaylistBtn_ = {right - 88, rowY + 6, 40, 40};
    rSubtitle_ = {right - 130, rowY + 6, 34, 40};
    rSpeed_ = {right - 192, rowY + 10, 56, 32};
    rVolSlider_ = {right - 288, rowY + 18, 86, 16};
    rMute_ = {right - 326, rowY + 10, 32, 32};
    rSeek_ = {28, H_ - 122, W_ - 56, 28};

    addHit(kHitBack, rBack_);
    addHit(kHitPlay, rPlay_);
    if (hasPrevNext) {
        addHit(kHitPrev, rPrev_);
        addHit(kHitNext, rNext_);
    }
    addHit(kHitMute, rMute_);
    addHit(kHitVolume, rVolSlider_);
    addHit(kHitSpeed, rSpeed_);
    addHit(kHitPlaylistBtn, rPlaylistBtn_);
    addHit(kHitSubtitle, rSubtitle_);
    addHit(kHitFullscreen, rFullscreen_);
    addHit(kHitSeek, rSeek_);
    addHit(kHitSettingsBtn, rSettingsBtn_);
    addHit(kHitStatsBtn, rStatsBtn_);

    rCenter_ = {W_ / 2 - 40, H_ / 2 - 40, 80, 80};
    addHit(kHitPlay, rCenter_);

    rEndCard_ = {W_ / 2 - 190, H_ / 2 - 92, 380, 184};
    rReplay_ = {rEndCard_.Cx() - 118, rEndCard_.Bottom() - 68, 112, 42};
    rNextUp_ = {rEndCard_.Cx() + 6, rEndCard_.Bottom() - 68, 112, 42};
    if (engine_.ended()) {
        addHit(kHitReplay, rReplay_);
        if (playlist_.size() > 1) addHit(kHitNextUp, rNextUp_);
    }

    rPlaylistPanel_.w = std::min(400.f, W_ * 0.34f);
    rPlaylistPanel_.x = W_ - rPlaylistPanel_.w;
    rPlaylistPanel_.y = 0;
    rPlaylistPanel_.h = H_;
    rPlaylistRows_.clear();
    int maxRows = std::max(0, (int)((H_ - 104.f) / 64.f));
    int rowCount = std::min<int>((int)playlist_.size(), maxRows);
    for (int i = 0; i < rowCount; ++i) {
        RectF r{rPlaylistPanel_.x + 12, 84.f + i * 64.f, rPlaylistPanel_.w - 24, 56};
        rPlaylistRows_.push_back(r);
    }

    rSpeedMenu_ = {rSpeed_.x - 84, rSpeed_.y - 8 - 7 * 34, 140, 7 * 34 + 8};
    rSpeedItems_.clear();
    for (int i = 0; i < 7; ++i) rSpeedItems_.push_back({rSpeedMenu_.x + 6, rSpeedMenu_.y + 4 + i * 34,
                                                        rSpeedMenu_.w - 12, 32});

    if (popup_ != Popup::None) {
        addHit(kHitBackdrop, {0, 0, W_, H_});
        if (popup_ == Popup::Playlist) {
            addHit(kHitBackdrop, {0, 0, W_ - rPlaylistPanel_.w, H_});
            addHit(kHitPlaylistClose, {rPlaylistPanel_.Right() - 54, 20, 34, 34});
            for (size_t i = 0; i < rPlaylistRows_.size(); ++i) addHit(kHitPlaylistRowBase + (int)i, rPlaylistRows_[i]);
        } else if (popup_ == Popup::Speed) {
            for (int i = 0; i < 7; ++i) addHit(kHitSpeedBase + i, rSpeedItems_[i]);
        } else if (popup_ == Popup::Subtitles) {
            rSubtitlePanel_ = {W_ - 448.f, std::max(56.f, (H_ - 568.f) * 0.5f), 408.f,
                               std::min(568.f, H_ - 100.f)};
            addHit(kHitSubtitleClose, {rSubtitlePanel_.Right() - 50, rSubtitlePanel_.y + 12, 36, 36});
            rSubOff_ = {rSubtitlePanel_.x + 18, rSubtitlePanel_.Bottom() - 54, 100, 36};
            rSubStyleBtn_ = {rSubtitlePanel_.x + 126, rSubtitlePanel_.Bottom() - 54, 92, 36};
            rSubImport_ = {rSubtitlePanel_.Right() - 144, rSubtitlePanel_.Bottom() - 54, 126, 36};
            addHit(kHitSubtitleOff, rSubOff_);
            addHit(kHitSubtitleStyle, rSubStyleBtn_);
            addHit(kHitSubtitleImport, rSubImport_);
            const float modeW = (rSubtitlePanel_.w - 110.f - 18.f - 8.f) / 3.f;
            for (int i = 0; i < 3; ++i) {
                rSubMode_[i] = {rSubtitlePanel_.x + 110.f + i * (modeW + 4.f), rSubtitlePanel_.y + 64.f, modeW,
                                32.f};
                addHit(kHitSubModeAuto + i, rSubMode_[i]);
            }
            subtitleAllOrds_.clear();
            const auto& subs = engine_.info().subtitles;
            for (int i = 0; i < (int)subs.size(); ++i)
                if (IsTextSubCodec(subs[i].codec)) subtitleAllOrds_.push_back(i);
            std::stable_sort(subtitleAllOrds_.begin(), subtitleAllOrds_.end(), [&](int a, int b) {
                int ra = SubLangRank(subs[a].lang), rb = SubLangRank(subs[b].lang);
                if (ra != rb) return ra < rb;
                return _wcsicmp(subs[a].title.c_str(), subs[b].title.c_str()) < 0;
            });
            const float top = rSubtitlePanel_.y + 112.f;
            const float bottom = rSubtitlePanel_.Bottom() - 70.f;
            const int visible = std::max(1, (int)((bottom - top) / 40.f));
            const int maxScroll = std::max(0, (int)subtitleAllOrds_.size() - visible);
            subtitleScroll_ = std::clamp(subtitleScroll_, 0.f, (float)maxScroll);
            subtitleRowOrds_.clear();
            subtitleRowRects_.clear();
            for (int k = 0; k < visible; ++k) {
                int idx = (int)subtitleScroll_ + k;
                if (idx >= (int)subtitleAllOrds_.size()) break;
                RectF r{rSubtitlePanel_.x + 14, top + k * 40.f, rSubtitlePanel_.w - 28, 36};
                subtitleRowOrds_.push_back(subtitleAllOrds_[idx]);
                subtitleRowRects_.push_back(r);
                addHit(kHitSubtitleRowBase + idx, r);
            }
        } else if (popup_ == Popup::SubtitleStyle) {
            rSubStylePanel_ = {W_ * 0.5f - 230.f, H_ * 0.5f - 220.f, 460.f, 440.f};
            addHit(kHitSubStyleClose, {rSubStylePanel_.Right() - 50, rSubStylePanel_.y + 12, 36, 36});
            rSubStyleRows_.clear();
            for (int i = 0; i < 6; ++i) {
                RectF r{rSubStylePanel_.x + 20, rSubStylePanel_.y + 132 + i * 44.f, rSubStylePanel_.w - 40, 40};
                rSubStyleRows_.push_back(r);
                addHit(i < 5 ? kHitSubStyleRowBase + i : kHitSubStyleFont, r);
            }
        } else if (popup_ == Popup::FontPicker) {
            rFontPanel_ = {W_ * 0.5f - 210.f, H_ * 0.5f - 260.f, 420.f, 520.f};
            addHit(kHitFontPickerClose, {rFontPanel_.Right() - 50, rFontPanel_.y + 12, 36, 36});
            rFontDefault_ = {rFontPanel_.x + 16, rFontPanel_.Bottom() - 52, rFontPanel_.w - 32, 36};
            addHit(kHitFontDefault, rFontDefault_);
            rFontList_ = {rFontPanel_.x + 2, rFontPanel_.y + 80, rFontPanel_.w - 4,
                          rFontDefault_.y - 12 - (rFontPanel_.y + 80)};
            const auto& familyNames = p_.SystemFontFamilies();
            if (fontPickerOpened) {
                const std::wstring active = subFontFamily_.empty() ? p_.SubtitleFamily() : subFontFamily_;
                auto it = std::find_if(familyNames.begin(), familyNames.end(), [&](const std::wstring& name) {
                    return _wcsicmp(name.c_str(), active.c_str()) == 0;
                });
                fontScroll_ = it == familyNames.end() ? 0 : (int)(it - familyNames.begin());
            }
            const int visible = std::max(1, (int)(rFontList_.h / 36.f));
            fontMaxScroll_ = std::max(0, (int)familyNames.size() - visible);
            fontScroll_ = std::clamp(fontScroll_, 0, fontMaxScroll_);
            rFontRows_.clear();
            for (int k = 0; k < visible; ++k) {
                int idx = fontScroll_ + k;
                if (idx >= (int)familyNames.size()) break;
                RectF r{rFontPanel_.x + 14, rFontList_.y + k * 36.f, rFontPanel_.w - 28, 32};
                rFontRows_.push_back(r);
                addHit(kHitFontRowBase + idx, r);
            }
        } else if (popup_ == Popup::FileBrowser) {
            layoutFileBrowser();
        }
    }
}

void App::loadSettings() {
    std::string text = ReadTextFile(DataDir() + L"\\settings.txt");
    if (text.empty()) return;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("volume=", 0) == 0) volume_ = std::clamp((float)atof(line.c_str() + 7), 0.f, 1.f);
        else if (line.rfind("muted=", 0) == 0) muted_ = atoi(line.c_str() + 6) != 0;
        else if (line.rfind("autoresume=", 0) == 0) autoResume_ = atoi(line.c_str() + 11) != 0;
        else if (line.rfind("animations=", 0) == 0) animations_ = atoi(line.c_str() + 11) != 0;
        else if (line.rfind("subtitle=", 0) == 0)
            subtitleScale_ = std::clamp((float)atof(line.c_str() + 9), kSubtitleScaleMin, kSubtitleScaleMax);
        else if (line.rfind("sub_color=", 0) == 0) subtitleColor_ = std::clamp(atoi(line.c_str() + 10), 0, 3);
        else if (line.rfind("sub_outline=", 0) == 0) subtitleOutline_ = std::clamp(atoi(line.c_str() + 12), 0, 3);
        else if (line.rfind("sub_bg=", 0) == 0) subtitleBg_ = std::clamp(atoi(line.c_str() + 7), 0, 2);
        else if (line.rfind("sub_pos=", 0) == 0) subtitlePos_ = std::clamp(atoi(line.c_str() + 8), 0, 2);
        else if (line.rfind("sub_mode=", 0) == 0)
            subtitleMode_ = subtitleModeSaved_ = std::clamp(atoi(line.c_str() + 9), 0, 2);
        else if (line.rfind("exclusive=", 0) == 0) exclusiveAudio_ = atoi(line.c_str() + 10) != 0;
        else if (line.rfind("auto_next=", 0) == 0) autoNext_ = atoi(line.c_str() + 10) != 0;
        else if (line.rfind("soft_decode=", 0) == 0) softwareDecode_ = atoi(line.c_str() + 12) != 0;
        else if (line.rfind("hdr_off=", 0) == 0) hdrDisabled_ = atoi(line.c_str() + 8) != 0;
        else if (line.rfind("thumbs=", 0) == 0) thumbnails_ = atoi(line.c_str() + 7) != 0;
        else if (line.rfind("accent=", 0) == 0) accentPreset_ = std::clamp(atoi(line.c_str() + 7), 0, 4);
        else if (line.rfind("sub_x=", 0) == 0) subX_ = std::clamp((float)atof(line.c_str() + 6), -0.45f, 0.45f);
        else if (line.rfind("sub_y=", 0) == 0) subY_ = std::clamp((float)atof(line.c_str() + 6), -0.35f, 0.35f);
        else if (line.rfind("sub_delay=", 0) == 0) subtitleDelay_ = std::clamp((float)atof(line.c_str() + 10), -10.f, 10.f);
        else if (line.rfind("sub_font_family=", 0) == 0) subFontFamily_ = Widen(line.c_str() + 16);
        else if (line.rfind("last_dir=", 0) == 0) lastDir_ = Widen(line.c_str() + 9);
        else if (line.rfind("sub_font=", 0) == 0) {
            // Legacy font-file key: file import is gone, so drop it on the next save.
            Log("settings: ignoring legacy sub_font=%s", line.c_str() + 9);
        } else if (line.rfind("recent ", 0) == 0) {
            // recent <position> <duration> <path>
            const char* s = line.c_str() + 7;
            char* e = nullptr;
            double p = strtod(s, &e);
            if (!e || e == s) continue;
            double d = strtod(e, &e);
            if (!e) continue;
            while (*e == ' ') ++e;
            if (!*e) continue;
            RecentEntry re;
            re.position = p;
            re.duration = d;
            re.path = Widen(e);
            re.title = FileNameOf(re.path);
            std::filesystem::path fp(re.path);
            if (std::filesystem::exists(fp)) recents_.push_back(re);
        }
    }
    if (recents_.size() > 10) recents_.resize(10);
    buildRecentMeta();
    ApplyAccentPreset(accentPreset_);
}

void App::saveSettings() {
    std::string out;
    char buf[256];
    snprintf(buf, sizeof(buf),
             "volume=%.3f\nmuted=%d\nautoresume=%d\nanimations=%d\nsubtitle=%.2f\nsub_color=%d\nsub_outline=%d\n"
             "sub_bg=%d\nsub_pos=%d\nexclusive=%d\nsub_x=%.3f\nsub_y=%.3f\nsub_delay=%.2f\nsub_mode=%d\n"
             "auto_next=%d\nsoft_decode=%d\nhdr_off=%d\nthumbs=%d\naccent=%d\n",
             volume_, muted_ ? 1 : 0, autoResume_ ? 1 : 0, animations_ ? 1 : 0, subtitleScale_, subtitleColor_,
             subtitleOutline_, subtitleBg_, subtitlePos_, exclusiveAudio_ ? 1 : 0, subX_, subY_, subtitleDelay_,
             subtitleModeSaved_, autoNext_ ? 1 : 0, softwareDecode_ ? 1 : 0, hdrDisabled_ ? 1 : 0,
             thumbnails_ ? 1 : 0, accentPreset_);
    out += buf;
    if (!subFontFamily_.empty()) {
        out += "sub_font_family=";
        out += Narrow(subFontFamily_);
        out += "\n";
    }
    if (!lastDir_.empty()) {
        out += "last_dir=";
        out += Narrow(lastDir_);
        out += "\n";
    }
    for (const auto& r : recents_) {
        snprintf(buf, sizeof(buf), "recent %.3f %.3f ", r.position, r.duration);
        out += buf;
        out += Narrow(r.path);
        out += "\n";
    }
    WriteTextFile(DataDir() + L"\\settings.txt", out);
}

void App::buildRecentMeta() {
    for (auto& r : recents_) {
        if (r.meta.empty()) {
            r.meta = FormatTime(r.duration);
        }
    }
}

void App::pushRecent(const std::wstring& path, double pos, double duration) {
    for (auto it = recents_.begin(); it != recents_.end(); ++it) {
        if (it->path == path) {
            it->position = pos;
            it->duration = duration;
            RecentEntry copy = *it;
            recents_.erase(it);
            recents_.insert(recents_.begin(), copy);
            saveSettings();
            return;
        }
    }
    RecentEntry e;
    e.path = path;
    e.position = pos;
    e.duration = duration;
    e.title = FileNameOf(path);
    e.meta = FormatTime(duration);
    recents_.insert(recents_.begin(), e);
    if (recents_.size() > 8) recents_.resize(8);
    saveSettings();
}

void App::saveCurrentPosition() {
    if (!hasMedia_ || !engine_.hasAudio() && !engine_.hasVideo()) return;
    double pos = engine_.position();
    if (pos < 3) pos = 0;
    pushRecent(engine_.path(), pos, duration_);
}

void App::showToast(const std::wstring& text) {
    dirty_ = true;
    toastText_ = text;
    toastUntil_ = now_ + 2.6;
    toastAlpha_ = 1.f;
}

void App::setControlsVisible(bool visible) {
    controlsVisible_ = visible;
    controlsTarget_ = visible ? 1.f : 0.f;
    if (visible) lastMouseMove_ = now_;
}

void App::togglePlay() {
    if (!hasMedia_) return;
    if (engine_.paused()) {
        if (engine_.eof()) {
            engine_.seek(0);
        }
        engine_.play();
        playing_ = true;
    } else {
        engine_.pause();
        playing_ = false;
        saveCurrentPosition();
    }
    setControlsVisible(true);
    Log("ui: play=%d pos=%.2f speed=%.2f", playing_ ? 1 : 0, engine_.position(), engine_.speed());
}

void App::toggleMute() {
    muted_ = !muted_;
    if (!muted_ && volume_ <= 0.001f) volume_ = 0.6f;
    engine_.setVolume(muted_ ? 0.f : volume_);
    showToast(muted_ ? L"已静音" : (L"音量 " + std::to_wstring((int)std::lround(volume_ * 100)) + L"%"));
}

void App::setVolume(float v) {
    volume_ = std::clamp(v, 0.f, 1.f);
    if (volume_ > 0.001f) muted_ = false;
    engine_.setVolume(muted_ ? 0.f : volume_);
}

void App::seekBy(double delta) {
    if (!hasMedia_) return;
    double target = std::clamp(engine_.position() + delta, 0.0, std::max(0.0, duration_ - 0.05));
    engine_.seek(target);
    if (engine_.paused()) position_ = target;
    showToast((delta > 0 ? L"快进 " : L"后退 ") + FormatTime(std::fabs(delta)));
    Log("ui: seekBy %.1f -> %.2f", delta, target);
}

void App::openFiles(const std::vector<std::wstring>& files) {
    std::vector<std::wstring> media;
    for (const auto& f : files) {
        std::error_code ec;
        if (std::filesystem::is_directory(f, ec)) {
            std::vector<std::wstring> dir;
            for (auto& e : std::filesystem::directory_iterator(f, std::filesystem::directory_options::skip_permission_denied)) {
                if (!e.is_regular_file()) continue;
                if (IsPlayableMediaName(e.path().filename().wstring())) dir.push_back(e.path().wstring());
            }
            std::sort(dir.begin(), dir.end());
            media.insert(media.end(), dir.begin(), dir.end());
        } else {
            media.push_back(f);
        }
    }
    if (media.empty()) {
        showToast(L"没有找到可播放的文件");
        return;
    }
    std::vector<std::wstring> queued;
    for (auto& m : media) {
        if (!IsPlayableMediaName(FileNameOf(m))) continue;
        queued.push_back(m);
    }
    if (queued.empty()) {
        showToast(L"不支持的格式：") ;
        return;
    }
    Log("openFiles: %zu queued", queued.size());
    playlist_.clear();
    for (auto& q : queued) {
        PlaylistItem item;
        item.path = q;
        item.title = FileNameOf(q);
        item.duration = 0;  // do not block the dialog on avformat_find_stream_info
        playlist_.push_back(item);
    }
    current_ = 0;
    openPath(playlist_[0].path, true);
}

void App::openDropped(HDROP drop) {
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> files;
    for (UINT i = 0; i < count; ++i) {
        wchar_t buf[MAX_PATH * 4];
        UINT len = DragQueryFileW(drop, i, buf, (UINT)std::size(buf));
        if (len > 0) files.emplace_back(buf, len);
    }
    DragFinish(drop);
    if (!files.empty()) openFiles(files);
}


std::vector<std::wstring> ScanFolderVideos(const std::wstring& file) {
    std::vector<std::wstring> out;
    std::error_code ec;
    std::filesystem::path p(file);
    if (!std::filesystem::is_regular_file(p, ec)) return out;
    const std::filesystem::path dir = p.parent_path();
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        if (!IsPlayableMediaName(e.path().filename().wstring())) continue;
        out.push_back(std::filesystem::absolute(e.path()).lexically_normal().wstring());
    }
    std::sort(out.begin(), out.end(),
              [](const std::wstring& a, const std::wstring& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; });
    if (out.size() > 300) out.resize(300);
    return out;
}


std::string LowerA(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}

std::string TrimA(std::string s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

int ParseKeyName(const std::string& raw) {
    std::string s = LowerA(TrimA(raw));
    if (s.size() == 1) {
        if (s[0] >= 'a' && s[0] <= 'z') return (int)(s[0] - 'a' + 'A');
        if (s[0] >= '0' && s[0] <= '9') return (int)s[0];
        switch (s[0]) {
            case '[': return VK_OEM_4;
            case ']': return VK_OEM_6;
            case '+': return VK_OEM_PLUS;
            case '-': return VK_OEM_MINUS;
            case ',': return VK_OEM_COMMA;
            case '.': return VK_OEM_PERIOD;
            case '/': return VK_OEM_2;
            case '\\': return VK_OEM_5;
            case ';': return VK_OEM_1;
            case '\'': return VK_OEM_7;
            case '`': return VK_OEM_3;
            case '=': return VK_OEM_PLUS;
        }
    }
    if (s.size() >= 2 && s.size() <= 3 && s[0] == 'f') {
        int n = atoi(s.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    struct Pair { const char* n; int vk; };
    static const Pair map[] = {
        {"space", VK_SPACE},     {"enter", VK_RETURN},   {"esc", VK_ESCAPE},     {"escape", VK_ESCAPE},
        {"tab", VK_TAB},         {"backspace", VK_BACK}, {"insert", VK_INSERT},  {"delete", VK_DELETE},
        {"home", VK_HOME},       {"end", VK_END},        {"pgup", VK_PRIOR},     {"pgdn", VK_NEXT},
        {"up", VK_UP},           {"down", VK_DOWN},      {"left", VK_LEFT},      {"right", VK_RIGHT},
        {"plus", VK_OEM_PLUS},   {"equal", VK_OEM_PLUS}, {"minus", VK_OEM_MINUS},{"comma", VK_OEM_COMMA},
        {"period", VK_OEM_PERIOD},{"slash", VK_OEM_2},   {"backslash", VK_OEM_5},{"semicolon", VK_OEM_1},
        {"quote", VK_OEM_7},     {"lbracket", VK_OEM_4}, {"rbracket", VK_OEM_6},{"backtick", VK_OEM_3}};
    for (const auto& p : map)
        if (s == p.n) return p.vk;
    return 0;
}

const char* DefaultKeysText() {
    return "# Prism key bindings. Edit this file, then press F2 in Prism.\n"
           "# format: [ctrl+][shift+][alt+]key = action [arg]\n"
           "# actions: toggle play pause seek seek_start seek_end volume mute fullscreen\n"
           "#          speed boost next prev subtitle_toggle subtitle_next subtitle_prev\n"
           "#          subtitle_delay open keys reload_keys escape quit\n"
           "space = toggle\n"
           "left = seek -5\n"
           "right = boost 3.0\n"
           "shift+left = seek -1\n"
           "shift+right = seek 1\n"
           "up = volume 0.05\n"
           "down = volume -0.05\n"
           "m = mute\n"
           "f = fullscreen\n"
           "j = seek -10\n"
           "l = seek 10\n"
           "n = next\n"
           "p = prev\n"
           "home = seek_start\n"
           "end = seek_end\n"
           "plus = speed 0.25\n"
           "minus = speed -0.25\n"
           "a = audio_next\n"
           "shift+a = audio_prev\n"
           "v = subtitle_next\n"
           "shift+v = subtitle_prev\n"
           "c = subtitle_toggle\n"
           "[ = subtitle_delay -0.1\n"
           "] = subtitle_delay 0.1\n"
           "o = open\n"
           "esc = escape\n"
           "i = stats\n"
           "f1 = keys\n"
           "f2 = reload_keys\n";
}

void App::loadKeyBindings() {
    keyBindings_.clear();
    std::wstring path = DataDir() + L"\\keys.ini";
    std::string text = ReadTextFile(path);
    if (text.empty()) {
        text = DefaultKeysText();
        WriteTextFile(path, text);
    }
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = TrimA(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string combo = TrimA(line.substr(0, eq));
        std::string rhs = TrimA(line.substr(eq + 1));
        if (combo.empty() || rhs.empty()) continue;
        KeyBinding b;
        size_t start = 0;
        std::vector<std::string> parts;
        while (true) {
            size_t plus = combo.find('+', start);
            parts.push_back(TrimA(combo.substr(start, plus == std::string::npos ? std::string::npos : plus - start)));
            if (plus == std::string::npos) break;
            start = plus + 1;
        }
        for (size_t i = 0; i + 1 < parts.size(); ++i) {
            std::string m = LowerA(parts[i]);
            if (m == "ctrl" || m == "control") b.ctrl = true;
            else if (m == "shift") b.shift = true;
            else if (m == "alt") b.alt = true;
        }
        b.vk = ParseKeyName(parts.back());
        if (!b.vk) {
            Log("keys: unknown key '%s'", parts.back().c_str());
            continue;
        }
        size_t sp = rhs.find_first_of(" \t");
        b.action = LowerA(sp == std::string::npos ? rhs : rhs.substr(0, sp));
        if (sp != std::string::npos) b.arg = (float)atof(rhs.c_str() + sp + 1);
        keyBindings_.push_back(b);
    }
    Log("keys: loaded %zu bindings from %s", keyBindings_.size(), Narrow(path).c_str());
}

bool App::runKeyAction(const KeyBinding& b) {
    const std::string& a = b.action;
    if (!hasMedia_ && a != "open" && a != "keys" && a != "reload_keys" && a != "fullscreen" && a != "escape")
        return false;
    if (a == "toggle") togglePlay();
    else if (a == "play") { if (engine_.paused()) togglePlay(); }
    else if (a == "pause") { if (!engine_.paused()) togglePlay(); }
    else if (a == "seek") seekBy(b.arg);
    else if (a == "seek_start") { engine_.seek(0); position_ = 0; }
    else if (a == "seek_end") engine_.seek(std::max(0.0, duration_ - 0.2));
    else if (a == "volume") {
        setVolume(std::clamp(volume_ + b.arg, 0.f, 1.f));
        showToast(L"\\u97f3\\u91cf " + std::to_wstring((int)std::lround(volume_ * 100)) + L"%");
    } else if (a == "mute") toggleMute();
    else if (a == "fullscreen") setFullscreen(!fullscreen_);
    else if (a == "speed") {
        engine_.setSpeed(std::clamp(engine_.speed() + b.arg, 0.25, 4.0));
        showToast(L"\\u901f\\u5ea6 " + std::to_wstring((int)std::lround(engine_.speed() * 100)) + L"%");
    } else if (a == "boost") {
        if (!speedBoostActive_) {
            speedBoostPrev_ = engine_.speed();
            speedBoostActive_ = true;
            boostVk_ = b.vk;
            engine_.setSpeed(std::clamp(b.arg, 0.25f, 4.f));
            showToast(std::to_wstring(engine_.speed()).substr(0, 4) + L" \\u00d7 \\u5feb\\u8fdb");
            Log("keys: boost %.2f", engine_.speed());
        }
    } else if (a == "next") nextTrack();
    else if (a == "prev") prevTrack();
    else if (a == "subtitle_toggle") {
        if (!engine_.hasSubtitles() && !engine_.subtitlePending()) { showToast(L"\\u6ca1\\u6709\\u53ef\\u7528\\u5b57\\u5e55"); return true; }
        subtitleEnabled_ = !subtitleEnabled_;
        subtitleText_ = subtitleEnabled_ ? engine_.subtitleAt(position_ - subtitleDelay_) : L"";
        showToast(subtitleEnabled_ ? L"\\u5b57\\u5e55\\u5df2\\u5f00\\u542f" : L"\\u5b57\\u5e55\\u5df2\\u5173\\u95ed");
    } else if (a == "subtitle_next" || a == "subtitle_prev") {
        if (!engine_.cycleSubtitle(a == "subtitle_prev" ? -1 : 1)) {
            showToast(L"\\u6ca1\\u6709\\u53ef\\u5207\\u6362\\u7684\\u5b57\\u5e55");
            return true;
        }
        subtitleEnabled_ = true;
        subtitleText_.clear();
        subtitleTrack_ = engine_.subtitleTrack();
        showToast(L"\\u5b57\\u5e55\\uff1a" + engine_.subtitleName());
    } else if (a == "subtitle_delay") {
        subtitleDelay_ = std::clamp(subtitleDelay_ + b.arg, -10.f, 10.f);
        subtitleText_ = engine_.subtitleAt(position_ - subtitleDelay_);
        wchar_t buf[64];
        swprintf_s(buf, L"\\u5b57\\u5e55\\u5ef6\\u8fdf %+.1f s", subtitleDelay_);
        showToast(buf);
    } else if (a == "audio_next" || a == "audio_prev") {
        if (engine_.audioTrackCount() < 2) {
            showToast(L"只有一个音轨");
            return true;
        }
        if (engine_.cycleAudio(a == "audio_prev" ? -1 : 1))
            showToast(L"音轨：" + engine_.audioTrackName());
    } else if (a == "stats") {
        showStats_ = !showStats_;
        dirty_ = true;
    } else if (a == "open") openDialog(false);
    else if (a == "keys") openKeysFile();
    else if (a == "reload_keys") {
        loadKeyBindings();
        showToast(L"\\u5feb\\u6377\\u952e\\u5df2\\u91cd\\u8f7d");
    } else if (a == "escape") {
        if (showStats_) { showStats_ = false; return true; }
        if (fullscreen_) setFullscreen(false);
        else if (popup_ == Popup::FontPicker) popup_ = Popup::SubtitleStyle;
        else if (popup_ != Popup::None) popup_ = Popup::None;
        else closeMedia();
    } else if (a == "quit") {
        if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    } else {
        Log("keys: unknown action '%s'", a.c_str());
        return false;
    }
    return true;
}

void App::openKeysFile() {
    loadKeyBindings();
    std::wstring path = DataDir() + L"\\keys.ini";
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::onKeyUp(int vk, bool ctrl, bool shift, bool alt) {
    (void)ctrl; (void)shift; (void)alt;
    if (speedBoostActive_ && vk == boostVk_) {
        engine_.setSpeed(std::clamp(speedBoostPrev_, 0.25, 4.0));
        speedBoostActive_ = false;
        Log("keys: boost end %.2f", engine_.speed());
    }
}

bool App::openPath(const std::wstring& path, bool autoplay, double resumeAt) {
    dirty_ = true;
    saveCurrentPosition();
    if (frame_) {
        engine_.releaseFrame(frame_);
        frame_ = nullptr;
    }
    gfx_->clearVideo();
    gfx_->clearSubtitle();
    assData_.reset();
    frameSerial_ = 0;
    std::string err;
    engine_.setExclusiveAudio(exclusiveAudio_);
    if (!engine_.open(path, err, !headless_)) {
        mediaError_ = Widen(err);
        showToast(Widen(err));
        Log("openPath failed: %s", err.c_str());
        hasMedia_ = false;
        screen_ = Screen::Home;
        gfx_->clearVideo();
        gfx_->clearSubtitle();
        assData_.reset();
        return false;
    }
    hasMedia_ = true;
    if (engine_.externalAudio())
        showToast(L"已加载外部音轨：" + FileNameOf(engine_.externalAudioPath()));
    if (engine_.subtitleBackend() == SubtitleBackend::Ass) ensureAssRenderer();
    if (gfx_) {
        bool hdrOn = gfx_->setHdrOutput(engine_.info().video.hdr && gfx_->hdrDisplay() && !hdrDisabled_);
        ApplyThemeColors();
        if (hdrOn) showToast(L"HDR10 → scRGB 直通");
    }
    screen_ = Screen::Player;
    popup_ = Popup::None;
    duration_ = engine_.duration();
    position_ = 0;
    engine_.setVolume(muted_ ? 0.f : volume_);
    setControlsVisible(true);
    lastMouseMove_ = now_;

    int idx = -1;
    if (!headless_) {
        std::vector<std::wstring> folder = ScanFolderVideos(path);
        if (folder.size() > 1) {
            playlist_.clear();
            for (const auto& f : folder) {
                PlaylistItem item;
                item.path = f;
                item.title = FileNameOf(f);
                playlist_.push_back(item);
            }
            std::filesystem::path self = std::filesystem::absolute(path).lexically_normal();
            for (size_t i = 0; i < playlist_.size(); ++i) {
                if (std::filesystem::absolute(playlist_[i].path).lexically_normal() == self) {
                    idx = (int)i;
                    break;
                }
            }
            if (idx < 0) playlist_.clear();
            Log("playlist: %zu files from folder", playlist_.size());
        }
    }
    if (idx < 0) {
        playlist_.clear();
        PlaylistItem item;
        item.path = path;
        item.title = FileNameOf(path);
        item.duration = duration_;
        playlist_.push_back(item);
        current_ = 0;
    } else {
        current_ = idx;
        playlist_[idx].duration = duration_;
    }
    autoNextAt_ = 0;

    double resume = resumeAt;
    if (resume < 0 && autoResume_) {
        for (const auto& r : recents_)
            if (r.path == path && r.position > 20 && r.duration > 60 && r.position < r.duration - 20) {
                resume = r.position;
                break;
            }
    }
    if (resume > 0) {
        engine_.seek(resume);
        showToast(L"从 " + FormatTime(resume) + L" 继续播放");
    }
    subtitleEnabled_ = !engine_.info().subtitles.empty();
    subtitleTrack_ = engine_.subtitleTrack();
    if (!headless_) {
        std::error_code wec;
        std::filesystem::path wp(path);
        watchDir_ = wp.parent_path().wstring();
        watchNames_.clear();
        if (!watchDir_.empty()) {
            for (const auto& e : std::filesystem::directory_iterator(watchDir_, wec)) {
                if (wec) break;
                if (e.is_regular_file(wec) && IsPlayableMediaName(e.path().filename().wstring()))
                    watchNames_.push_back(e.path().filename().wstring());
            }
            std::sort(watchNames_.begin(), watchNames_.end(),
                      [](const std::wstring& a, const std::wstring& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; });
        }
        lastWatchScan_ = now_;
    }
    subtitleText_ = engine_.subtitleAt(0);
    if (!headless_ && engine_.subtitlePending()) showToast(L"字幕加载中…");
    if (autoplay) {
        engine_.play();
        playing_ = true;
    } else {
        engine_.pause();
        playing_ = false;
    }
    pushRecent(path, resume > 0 ? resume : 0, duration_);
    Log("playing %s (%.2fs)", Narrow(path).c_str(), duration_);
    return true;
}

void App::playIndex(int index) {
    if (index < 0 || index >= (int)playlist_.size()) return;
    current_ = index;
    openPath(playlist_[index].path, true);
}

void App::nextTrack() {
    if (playlist_.empty()) return;
    playIndex((current_ + 1) % (int)playlist_.size());
}

void App::prevTrack() {
    if (playlist_.empty()) return;
    playIndex((current_ - 1 + (int)playlist_.size()) % (int)playlist_.size());
}

void App::closeMedia() {
    dirty_ = true;
    if (gfx_ && gfx_->hdrOutput()) {
        gfx_->setHdrOutput(false);
        ApplyThemeColors();
    }
    saveCurrentPosition();
    if (frame_) {
        engine_.releaseFrame(frame_);
        frame_ = nullptr;
    }
    engine_.close();
    gfx_->clearVideo();
    gfx_->clearSubtitle();
    assData_.reset();
    hasMedia_ = false;
    playing_ = false;
    subtitleEnabled_ = false;
    subtitleText_.clear();
    screen_ = Screen::Home;
    popup_ = Popup::None;
    buildRecentMeta();
    setControlsVisible(true);
}

void App::openSubtitleDialog() {
    if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
    draggingSub_ = false;
    draggingSeek_ = false;
    draggingVolume_ = false;
    mouseDown_ = false;
    fbMode_ = FbMode::Subtitle;
    fbDir_ = fbStartDir();
    fbReload();
    popup_ = Popup::FileBrowser;
    dirty_ = true;
}

bool App::applySubtitleFamily(const std::wstring& family) {
    bool ok = p_.SetSubtitleFamily(family);
    subFontFamily_ = ok ? family : std::wstring();
    dirty_ = true;
    return ok;
}

bool App::assNativeActive() {
    // Auto: ASS tracks render natively, everything else stays on the text renderer.
    // Original: same native path (text tracks have no original style to restore).
    // Custom: always the plain-text parser with tags stripped.
    if (subtitleMode_ == 2 || !subtitleEnabled_ || !engine_.hasVideo()) return false;
    if (engine_.subtitleBackend() != SubtitleBackend::Ass) return false;
    if (!assRenderer_ || !assRenderer_->available()) ensureAssRenderer();
    return assRenderer_ && assRenderer_->available();
}

void App::ensureAssRenderer() {
    if (assRenderer_) return;
    assRenderer_ = std::make_unique<AssRenderer>();
    if (!assRenderer_->init()) Log("subtitle: native ASS disabled — %s", assRenderer_->error().c_str());
}

void App::setSubtitleMode(int mode) {
    subtitleMode_ = subtitleModeSaved_ = std::clamp(mode, 0, 2);
    static const wchar_t* names[3] = {L"自动", L"原始样式", L"自定义样式"};
    std::wstring msg = std::wstring(L"字幕模式：") + names[subtitleMode_];
    if (subtitleMode_ != 2 && engine_.subtitleBackend() == SubtitleBackend::Ass &&
        (!assRenderer_ || !assRenderer_->available()))
        msg += L"（libass 未加载，ASS 回退纯文本）";
    showToast(msg);
    saveSettings();
    dirty_ = true;
}

void App::openDialog(bool folder) {
    if (headless_) return;
    if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
    draggingSub_ = false;
    draggingSeek_ = false;
    draggingVolume_ = false;
    mouseDown_ = false;
    fbMode_ = folder ? FbMode::MediaFolder : FbMode::MediaFile;
    std::error_code ec;
    if (fbDir_.empty() || !std::filesystem::is_directory(fbDir_, ec)) fbDir_ = fbStartDir();
    fbReload();
    popup_ = Popup::FileBrowser;
    dirty_ = true;
}

std::wstring App::fbStartDir() const {
    auto usable = [](const std::wstring& dir) {
        std::error_code ec;
        return !dir.empty() && std::filesystem::is_directory(dir, ec);
    };
    auto mediaDir = [&] {
        std::error_code ec;
        return NormalizeDir(std::filesystem::absolute(engine_.path(), ec).parent_path().wstring());
    };
    // Headless screenshots need a deterministic start: the --media folder wins.
    if (headless_ && hasMedia_ && usable(mediaDir())) return mediaDir();
    if (usable(lastDir_)) return NormalizeDir(lastDir_);
    if (hasMedia_ && usable(mediaDir())) return mediaDir();
    wchar_t profile[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0) {
        std::wstring downloads = std::wstring(profile) + L"\\Downloads";
        if (usable(downloads)) return downloads;
    }
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
        if (mask & (1u << i)) return std::wstring(1, (wchar_t)(L'A' + i)) + L":\\";
    return L"C:\\";
}

void App::fbEnsureInit() {
    if (!fbDir_.empty()) return;
    fbDir_ = fbStartDir();
    fbReload();
}

void App::fbReload() {
    fbThumbsWorker_.cancel();
    fbThumbActive_ = false;
    fbThumbDir_.clear();
    fbEntries_.clear();
    fbSelected_ = -1;
    fbScroll_ = 0;
    fbLastClickIdx_ = -1;
    std::vector<FbEntry> dirs, files;
    std::error_code ec;
    const std::wstring dir = fbDir_;
    for (std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        std::error_code fec;
        const bool isDir = it->is_directory(fec);
        if (fec) continue;
        const std::wstring name = it->path().filename().wstring();
        if (isDir) {
            dirs.push_back({name, it->path().wstring(), true});
        } else if (fbMode_ != FbMode::MediaFolder) {
            const bool wanted = fbMode_ == FbMode::Subtitle
                                    ? IsSubtitleExt(FileExtOf(StripPartialName(name)))
                                    : IsPlayableMediaName(name);
            if (!wanted) continue;
            files.push_back({name, it->path().wstring(), false});
        }
    }
    auto byName = [](const FbEntry& a, const FbEntry& b) {
        return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
    };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    fbEntries_ = dirs;
    fbEntries_.insert(fbEntries_.end(), files.begin(), files.end());
    // File and subtitle modes preselect the first row; folder mode starts
    // unselected so 打开 means "play the folder I am looking at".
    if (fbMode_ != FbMode::MediaFolder && !fbEntries_.empty()) fbSelected_ = 0;
    // Keep the scroll bounds usable before the next layout() (e.g. fbGoUp
    // revealing the child folder we just came from).
    fbMaxScroll_ = std::max(0, (int)fbEntries_.size() - std::max(1, fbVisibleRows_));
    Log("file browser: %s (%zu entries, mode=%d)", Narrow(fbDir_).c_str(), fbEntries_.size(), (int)fbMode_);
}

void App::fbNavigate(const std::wstring& dir) {
    std::error_code ec;
    std::filesystem::path target(dir);
    if (!std::filesystem::is_directory(target, ec)) {
        showToast(L"无法打开该文件夹");
        return;
    }
    fbDir_ = NormalizeDir(target.wstring());
    fbReload();
    fbEnsureVisible();
    dirty_ = true;
}

void App::fbGoUp() {
    const std::wstring parent = ParentDirOf(fbDir_);
    if (parent.empty()) return;
    const std::wstring from = FileNameOf(fbDir_);
    fbNavigate(parent);
    if (fbDir_ != parent) return;
    for (size_t i = 0; i < fbEntries_.size(); ++i) {
        if (fbEntries_[i].dir && _wcsicmp(fbEntries_[i].name.c_str(), from.c_str()) == 0) {
            fbSelected_ = (int)i;
            break;
        }
    }
    fbEnsureVisible();
}

void App::fbEnsureVisible() {
    if (fbSelected_ < 0) return;
    const int visible = std::max(1, fbVisibleRows_);
    if (fbSelected_ < fbScroll_) fbScroll_ = fbSelected_;
    if (fbSelected_ >= fbScroll_ + visible) fbScroll_ = fbSelected_ - visible + 1;
    fbScroll_ = std::clamp(fbScroll_, 0, std::max(0, fbMaxScroll_));
}

void App::fbApplyThumb(ThumbResult& result) {
    auto existing = fbThumbs_.find(result.path);
    if (existing != fbThumbs_.end() && existing->second.key == result.key) {
        fbThumbMissing_.erase(result.path);
        return;
    }
    fbThumbMissing_.erase(result.path);
    if (!result.bgra || result.width <= 0 || result.height <= 0) {
        fbThumbs_.erase(result.path);
        fbThumbMissing_[result.path] = result.key;
        dirty_ = true;
        return;
    }
    auto bitmap = p_.CreateBitmapFromBgra(result.bgra->data(), result.width, result.height, result.width * 4);
    if (!bitmap) {
        fbThumbs_.erase(result.path);
        fbThumbMissing_[result.path] = result.key;
        Log("thumbnail: bitmap create failed for %s", Narrow(result.path).c_str());
        dirty_ = true;
        return;
    }
    // ponytail: flat cap instead of LRU. Dropping the bitmaps also cancels the
    // worker generation so the visible rows are re-requested from its pixel cache.
    if (fbThumbs_.size() >= 96) {
        fbThumbs_.clear();
        fbThumbMissing_.clear();
        fbThumbsWorker_.cancel();
    }
    fbThumbs_.emplace(result.path, FbThumb{result.key, result.width, result.height, std::move(bitmap)});
    dirty_ = true;
}

bool App::fbThumbsSettled() const {
    const int first = std::max(0, fbScroll_);
    const int last = std::min((int)fbEntries_.size(), first + std::max(1, fbVisibleRows_));
    for (int i = first; i < last; ++i) {
        const FbEntry& e = fbEntries_[i];
        if (e.dir) continue;
        if (!fbThumbs_.count(e.path) && !fbThumbMissing_.count(e.path)) return false;
    }
    return true;
}

void App::fbSyncThumbs(bool request) {
    if (!thumbnails_ || popup_ != Popup::FileBrowser || fbMode_ != FbMode::MediaFile) {
        if (fbThumbActive_) {
            fbThumbsWorker_.cancel();
            fbThumbActive_ = false;
        }
        fbThumbDir_.clear();
        return;
    }
    if (!fbThumbActive_ || fbThumbDir_ != fbDir_) {
        fbThumbsWorker_.cancel();
        fbThumbDir_ = fbDir_;
        fbThumbActive_ = true;
    }
    for (ThumbResult& result : fbThumbsWorker_.poll()) fbApplyThumb(result);
    if (!request) return;

    std::vector<std::wstring> paths;
    const int first = std::max(0, fbScroll_);
    const int last = std::min((int)fbEntries_.size(), first + std::max(1, fbVisibleRows_));
    for (int i = first; i < last; ++i) {
        if (!fbEntries_[i].dir) paths.push_back(fbEntries_[i].path);
    }
    fbThumbsWorker_.request(paths);
}

void App::fbMoveSelection(int delta) {
    if (fbEntries_.empty()) return;
    if (fbSelected_ < 0) fbSelected_ = 0;
    else fbSelected_ = std::clamp(fbSelected_ + delta, 0, (int)fbEntries_.size() - 1);
    fbEnsureVisible();
    dirty_ = true;
}

void App::fbOpenTarget(const std::wstring& path) {
    lastDir_ = fbDir_;
    saveSettings();
    popup_ = Popup::None;
    Log("file browser: open %s", Narrow(path).c_str());
    openFiles({path});
}

void App::fbAccept() {
    if (fbSelected_ >= 0 && fbSelected_ < (int)fbEntries_.size()) {
        const FbEntry& e = fbEntries_[fbSelected_];
        if (e.dir) {
            if (fbMode_ == FbMode::MediaFolder) fbOpenTarget(e.path);
            else fbNavigate(e.path);
        } else if (fbMode_ == FbMode::Subtitle) {
            fbImportSubtitle(e.path);
        } else {
            fbOpenTarget(e.path);
        }
        return;
    }
    if (fbMode_ == FbMode::MediaFolder) fbOpenTarget(fbDir_);
    else showToast(fbMode_ == FbMode::Subtitle ? L"请选择字幕文件" : L"请选择一个文件");
}

void App::fbImportSubtitle(const std::wstring& path) {
    lastDir_ = fbDir_;
    saveSettings();
    std::string err;
    if (!engine_.loadExternalSubtitle(path, err)) {
        showToast(L"字幕导入失败：" + Widen(err));
        return;
    }
    subtitleEnabled_ = true;
    subtitleTrack_ = engine_.subtitleTrack();
    subtitleText_ = engine_.subtitleAt(position_);
    popup_ = Popup::None;
    showToast(L"已导入字幕：" + FileNameOf(path));
}

void App::fbOnKey(int vk) {
    switch (vk) {
        case VK_ESCAPE: popup_ = Popup::None; break;
        case VK_RETURN: fbAccept(); break;
        case VK_UP: fbMoveSelection(-1); break;
        case VK_DOWN: fbMoveSelection(1); break;
        case VK_PRIOR: fbMoveSelection(-std::max(1, fbVisibleRows_)); break;
        case VK_NEXT: fbMoveSelection(std::max(1, fbVisibleRows_)); break;
        case VK_HOME:
            if (!fbEntries_.empty()) fbSelected_ = 0;
            fbEnsureVisible();
            break;
        case VK_END:
            if (!fbEntries_.empty()) fbSelected_ = (int)fbEntries_.size() - 1;
            fbEnsureVisible();
            break;
        case VK_BACK:
        case VK_LEFT: fbGoUp(); break;
        case VK_RIGHT:
            if (fbSelected_ >= 0 && fbSelected_ < (int)fbEntries_.size() && fbEntries_[fbSelected_].dir)
                fbNavigate(fbEntries_[fbSelected_].path);
            break;
        default: break;
    }
}

void App::layoutFileBrowser() {
    fbEnsureInit();
    const float pad = 24.f;
    const float pw = std::min(760.f, W_ - 32.f);
    const float ph = std::min(640.f, H_ - 32.f);
    rFbPanel_ = {W_ * 0.5f - pw * 0.5f, H_ * 0.5f - ph * 0.5f, pw, ph};
    addHit(kHitBackdrop, {0, 0, W_, H_});
    const RectF panel = rFbPanel_;

    rFbClose_ = {panel.Right() - pad - 34, panel.y + 16, 34, 34};
    addHit(kHitFbClose, rFbClose_);

    rFbUp_ = {panel.x + pad, panel.y + 64, 88, 34};
    fbCanUp_ = !ParentDirOf(fbDir_).empty();
    addHit(kHitFbUp, rFbUp_);
    rFbPath_ = {rFbUp_.Right() + 10, rFbUp_.y, panel.Right() - pad - (rFbUp_.Right() + 10), rFbUp_.h};

    // Drive row: caption plus one chip per logical drive that has a root.
    fbDrives_.clear();
    rFbDrive_.clear();
    rFbDrivesTag_ = {panel.x + pad, rFbUp_.Bottom() + 10, 44, 28};
    const DWORD mask = GetLogicalDrives();
    float dx = rFbDrivesTag_.Right() + 8;
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        std::wstring root(1, (wchar_t)(L'A' + i));
        root += L":\\";
        if (GetDriveTypeW(root.c_str()) == DRIVE_NO_ROOT_DIR) continue;
        RectF chip{dx, rFbDrivesTag_.y, 44, 28};
        if (chip.Right() > panel.Right() - pad) break;
        rFbDrive_.push_back(chip);
        fbDrives_.push_back(root);
        addHit(kHitFbDriveBase + (int)fbDrives_.size() - 1, chip);
        dx += 50.f;
    }

    const float footerTop = panel.Bottom() - 88.f;
    const float listY = rFbDrivesTag_.Bottom() + 14.f;
    rFbList_ = {panel.x + 10, listY, panel.w - 20, std::max(40.f, footerTop - 14.f - listY)};
    fbVisibleRows_ = std::max(1, (int)(rFbList_.h / kFbRowH));
    fbMaxScroll_ = std::max(0, (int)fbEntries_.size() - fbVisibleRows_);
    if (fbSelected_ >= (int)fbEntries_.size()) fbSelected_ = (int)fbEntries_.size() - 1;
    fbScroll_ = std::clamp(fbScroll_, 0, fbMaxScroll_);

    rFbRows_.clear();
    for (int k = 0; k < fbVisibleRows_; ++k) {
        const int idx = fbScroll_ + k;
        if (idx >= (int)fbEntries_.size()) break;
        RectF r{rFbList_.x + 6, rFbList_.y + k * kFbRowH, rFbList_.w - 12, kFbRowH - 4.f};
        rFbRows_.push_back(r);
        addHit(kHitFbRowBase + idx, r);
    }

    const float footY = panel.Bottom() - 66.f;
    rFbOpen_ = {panel.Right() - pad - 112, footY, 112, 42};
    addHit(kHitFbOpen, rFbOpen_);
    rFbCancel_ = {rFbOpen_.x - 10 - 92, footY, 92, 42};
    addHit(kHitFbCancel, rFbCancel_);
}

void App::setFullscreen(bool on) {
    if (!hwnd_ || on == fullscreen_) return;
    if (on) {
        savedPlacement_.length = sizeof(savedPlacement_);
        GetWindowPlacement(hwnd_, &savedPlacement_);
        savedStyle_ = GetWindowLongW(hwnd_, GWL_STYLE);
        maximizedBeforeFullscreen_ = IsZoomed(hwnd_) != 0;
        HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        SetWindowLongW(hwnd_, GWL_STYLE, (savedStyle_ & ~(WS_CAPTION | WS_THICKFRAME)) | WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        DWORD border = 0xFFFFFFFEu;  // DWMWA_COLOR_NONE
        DwmSetWindowAttribute(hwnd_, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
        int corner = 1;  // DWMWCP_DONOTROUND
        DwmSetWindowAttribute(hwnd_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
        fullscreen_ = true;
        dirty_ = true;
        RECT cr{};
        GetClientRect(hwnd_, &cr);
        Log("ui: fullscreen=1 client=%ldx%ld gfx=%dx%d scale=%.2f W=%.1f H=%.1f seekY=%.1f", cr.right, cr.bottom,
            gfx_->width(), gfx_->height(), gfx_->scale(), W_, H_, H_ - 122.f);
    } else {
        DWORD border = 0xFFFFFFFFu;  // DWMWA_COLOR_DEFAULT
        DwmSetWindowAttribute(hwnd_, 34, &border, sizeof(border));
        int corner = 0;  // DWMWCP_DEFAULT
        DwmSetWindowAttribute(hwnd_, 33, &corner, sizeof(corner));
        SetWindowLongW(hwnd_, GWL_STYLE, savedStyle_);
        SetWindowPlacement(hwnd_, &savedPlacement_);
        SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        fullscreen_ = false;
        dirty_ = true;
        Log("ui: fullscreen=0");
    }
}


void App::pollFolder() {
    if (watchDir_.empty()) return;
    std::vector<std::wstring> names;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(watchDir_, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec) && IsPlayableMediaName(e.path().filename().wstring()))
            names.push_back(e.path().filename().wstring());
    }
    std::sort(names.begin(), names.end(),
              [](const std::wstring& a, const std::wstring& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; });
    if (names == watchNames_) return;
    const size_t before = watchNames_.size();
    watchNames_ = names;
    refreshFolderPlaylist();
    if (names.size() > before) {
        showToast(L"发现新文件，播放列表已更新");
        Log("watch: %zu -> %zu files in %ls", before, names.size(), watchDir_.c_str());
    }
}

void App::refreshFolderPlaylist() {
    if (!hasMedia_ || current_ < 0 || current_ >= (int)playlist_.size()) return;
    const std::wstring cur = playlist_[current_].path;
    const std::wstring curName = StripPartialName(FileNameOf(cur));
    const double keepDuration = duration_;
    std::vector<std::wstring> folder = ScanFolderVideos(cur);
    if (folder.size() < 2) return;
    int found = -1;
    std::vector<PlaylistItem> next;
    next.reserve(folder.size());
    for (const auto& f : folder) {
        PlaylistItem item;
        item.path = f;
        item.title = FileNameOf(f);
        if (found < 0) {
            if (_wcsicmp(f.c_str(), cur.c_str()) == 0 ||
                _wcsicmp(StripPartialName(FileNameOf(f)).c_str(), curName.c_str()) == 0)
                found = (int)next.size();
        }
        next.push_back(std::move(item));
    }
    if (found < 0) return;
    next[found].duration = keepDuration;
    playlist_ = std::move(next);
    current_ = found;
    if (playlist_[current_].path != cur) Log("watch: current renamed -> %ls", playlist_[current_].path.c_str());
}


void App::updateSystemStats() {
    if (now_ - lastCpuSample_ < 1.0 && systemStatsInit_) return;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        workingSet_ = pmc.WorkingSetSize;
        privateBytes_ = pmc.PrivateUsage;
    }
    FILETIME c{}, e{}, k{}, u{};
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
        auto q = [](const FILETIME& f) -> uint64_t {
            return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime;
        };
        const uint64_t kk = q(k), uu = q(u);
        const int cores = std::max(1, (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        if (systemStatsInit_ && now_ > lastCpuSample_) {
            const double wall = now_ - lastCpuSample_;
            const double used = (double)((kk + uu) - (lastCpuKernel_ + lastCpuUser_)) / 1e7;
            processCpu_ = std::clamp(used / (wall * cores) * 100.0, 0.0, 100.0);
        }
        lastCpuKernel_ = kk;
        lastCpuUser_ = uu;
    }
    lastCpuSample_ = now_;
    systemStatsInit_ = true;
}

void App::tick(double now, double dt) {
    now_ = now;
    dt = std::clamp(dt, 0.0, 0.1);
    lastTick_ = now;
    updateSystemStats();

    if (hasMedia_) {
        if (!headless_ && !watchDir_.empty() && now_ - lastWatchScan_ > 0.8) {
            lastWatchScan_ = now_;
            pollFolder();
        }
        if (!engine_.paused()) {
            position_ = engine_.position();
            if (engine_.ended()) {
                engine_.pause();
                playing_ = false;
                setControlsVisible(true);
            }
        } else {
            position_ = draggingSeek_ ? dragSeekTime_ : engine_.position();
        }
        playing_ = !engine_.paused();
        if (engine_.partial()) {
            const bool waiting = engine_.buffering();
            if (waiting && !engine_.paused()) {
                engine_.pause();
                bufferingPaused_ = true;
                playing_ = false;
            }
            if (waiting && !bufferToast_) {
                showToast(L"缓冲中：等待下载…");
                bufferToast_ = true;
            } else if (!waiting) {
                bufferToast_ = false;
            }
            if (!waiting && bufferingPaused_) {
                engine_.play();
                bufferingPaused_ = false;
                playing_ = true;
            }
        } else {
            bufferToast_ = false;
            bufferingPaused_ = false;
        }

        bool needFrame = !frame_ || engine_.serial() != frameSerial_;
        if (needFrame || playing_) {
            if (VideoFrame* f = engine_.acquireFrame()) {
                if (f->hdr)
                    gfx_->updateVideoHdr(f->planes.data(), f->width, f->height, f->yStride, f->uvStride);
                else
                    gfx_->updateVideo(f->pixels.data(), f->width, f->height, f->stride);
                if (frame_) engine_.releaseFrame(frame_);
                frame_ = f;
                frameSerial_ = engine_.serial();
                videoDirty_ = true;
            }
        }

        if (subtitleEnabled_) {
            double subTime = (draggingSeek_ ? dragSeekTime_ : position_) - subtitleDelay_;
            std::wstring next = engine_.subtitleAt(subTime);
            if (next != subtitleText_) { subtitleText_ = next; dirty_ = true; }
        } else if (!subtitleText_.empty()) {
            subtitleText_.clear();
        }

        if (playing_ && !draggingSeek_ && popup_ == Popup::None && now_ - lastMouseMove_ > 2.6)
            controlsTarget_ = 0.f;

        if (now_ - lastPositionSave_ > 5.0 && playing_) {
            lastPositionSave_ = now_;
            saveCurrentPosition();
        }
    } else {
        controlsTarget_ = 1.f;
    }

    if (hasMedia_ && engine_.ended() && autoNext_ && playlist_.size() > 1 &&
        current_ + 1 < (int)playlist_.size()) {
        if (autoNextAt_ <= 0) {
            autoNextAt_ = now + 2.0;
            showToast(L"\u5373\u5c06\u64ad\u653e\u4e0b\u4e00\u96c6");
        } else if (now >= autoNextAt_) {
            autoNextAt_ = 0;
            nextTrack();
        }
    } else if (!engine_.ended()) {
        autoNextAt_ = 0;
    }

    if (!animations_) {
        controlsAlpha_ = controlsTarget_;
    } else {
        float k = 1.f - std::exp(-16.f * (float)dt);
        controlsAlpha_ += (controlsTarget_ - controlsAlpha_) * k;
        if (std::fabs(controlsTarget_ - controlsAlpha_) < 0.002f) controlsAlpha_ = controlsTarget_;
    }

    float seekTarget = hoverId_ == kHitSeek || draggingSeek_ ? 1.f : 0.f;
    seekHover_ += (seekTarget - seekHover_) * (1.f - std::exp(-20.f * (float)dt));

    float centerTarget = (hasMedia_ && engine_.paused() && !engine_.eof()) ? 1.f : 0.f;
    centerPlay_ += (centerTarget - centerPlay_) * (1.f - std::exp(-12.f * (float)dt));

    float toastTarget = now_ < toastUntil_ ? 1.f : 0.f;
    toastAlpha_ += (toastTarget - toastAlpha_) * (1.f - std::exp(-10.f * (float)dt));
    if (toastAlpha_ < 0.01f) toastAlpha_ = 0.f;

    cursorHidden_ = hasMedia_ && playing_ && controlsAlpha_ < 0.06f && hoverId_ == 0;
    fbSyncThumbs(false);
}

bool App::wantsRender(double now) const {
    if (dirty_ || lastRender_ <= 0.0) return true;
    if (hoverId_ != renderedHover_) return true;
    if (videoDirty_) return true;
    if (std::fabs(controlsAlpha_ - controlsTarget_) > 0.004f) return true;
    if (std::fabs(seekHover_ - ((hoverId_ == kHitSeek || draggingSeek_) ? 1.f : 0.f)) > 0.01f) return true;
    if (std::fabs(centerPlay_ - ((hasMedia_ && engine_.paused()) ? 1.f : 0.f)) > 0.01f) return true;
    if (toastAlpha_ > 0.01f || now < toastUntil_) return true;
    if (playing_ && now - lastRender_ > 0.2) return true;
    if (!playing_ && now - lastRender_ > 0.5 && hasMedia_) return true;
    return false;
}

void App::render(double now) {
    if (!gfx_) return;
    now_ = now;
    layout();
    fbSyncThumbs(true);
    gfx_->beginFrame();
    if (screen_ == Screen::Home) {
        drawHome();
        if (popup_ == Popup::Settings) drawSettings();
    } else {
        drawPlayer();
    }
    // The file browser works over both screens, so it is not part of drawPlayer.
    if (popup_ == Popup::FileBrowser) drawFileBrowser();
    if (showStats_) drawStats();
    drawToast();
    gfx_->endFrame();
    if (gfx_->takeDeviceLost()) {
        fbThumbs_.clear();
        fbThumbMissing_.clear();
        fbThumbsWorker_.cancel();
        fbThumbActive_ = false;
        fbThumbDir_.clear();
        Log("ui: device lost; thumbnail bitmaps dropped");
    }
    lastRender_ = now;
    renderFrames_++;
    if (lastFpsSample_ <= 0.0) lastFpsSample_ = now;
    else if (now - lastFpsSample_ >= 1.0) {
        renderFps_ = renderFrames_ / (now - lastFpsSample_);
        renderFrames_ = 0;
        lastFpsSample_ = now;
    }
    videoDirty_ = false;
    dirty_ = false;
    renderedHover_ = hoverId_;
}

void App::updateHover() {
    int id = hitTest(mouseX_, mouseY_);
    if (id != hoverId_) hoverId_ = id;
}

void App::onMouseMove(float x, float y) {
    dirty_ = true;
    mouseX_ = x;
    mouseY_ = y;
    lastMouseMove_ = now_;
    if (controlsTarget_ < 1.f && screen_ == Screen::Player) setControlsVisible(true);
    if (draggingSub_) {
        subX_ = std::clamp(subDragBaseX_ + (x - subDragMouseX_) / W_, -0.45f, 0.45f);
        subY_ = std::clamp(subDragBaseY_ + (y - subDragMouseY_) / H_, -0.35f, 0.35f);
        return;
    }
    if (draggingSeek_ && rSeek_.w > 0) {
        float t = Clamp01((x - rSeek_.x) / rSeek_.w);
        dragSeekTime_ = t * duration_;
    }
    if (draggingVolume_ && rVolSlider_.w > 0) {
        float t = Clamp01((x - rVolSlider_.x) / rVolSlider_.w);
        setVolume(t);
    }
    updateHover();
}

void App::onMouseDown(float x, float y) {
    dirty_ = true;
    mouseDown_ = true;
    int id = hitTest(x, y);
    pressedId_ = id;
    if (id == kHitSubtitleDrag) {
        draggingSub_ = true;
        subDragMouseX_ = x;
        subDragMouseY_ = y;
        subDragBaseX_ = subX_;
        subDragBaseY_ = subY_;
        SetCapture(hwnd_);
        return;
    }
    if (id == kHitSeek && duration_ > 0) {
        draggingSeek_ = true;
        float t = Clamp01((x - rSeek_.x) / rSeek_.w);
        dragSeekTime_ = t * duration_;
        SetCapture(hwnd_);
    } else if (id == kHitVolume) {
        draggingVolume_ = true;
        setVolume(Clamp01((x - rVolSlider_.x) / rVolSlider_.w));
        SetCapture(hwnd_);
    }
}

void App::onMouseUp(float x, float y) {
    dirty_ = true;
    if (draggingSub_) {
        draggingSub_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        saveSettings();
        return;
    }
    int id = hitTest(x, y);
    if (draggingSeek_) {
        draggingSeek_ = false;
        engine_.seek(dragSeekTime_);
        position_ = dragSeekTime_;
        if (engine_.paused()) {
            engine_.pause();
        }
        if (GetCapture() == hwnd_) ReleaseCapture();
    } else if (draggingVolume_) {
        draggingVolume_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
    } else if (mouseDown_ && pressedId_ != 0 && pressedId_ == id) {
        activate(pressedId_);
    }
    mouseDown_ = false;
    pressedId_ = 0;
    lastMouseMove_ = now_;
}

void App::onMouseWheel(float delta) {
    dirty_ = true;
    if (popup_ == Popup::FileBrowser) {
        fbScroll_ = std::clamp(fbScroll_ + (delta > 0 ? -3 : 3), 0, std::max(0, fbMaxScroll_));
        return;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) && subtitleEnabled_ && !subtitleText_.empty()) {
        subtitleScale_ = std::clamp(subtitleScale_ + delta * 0.1f, kSubtitleScaleMin, kSubtitleScaleMax);
        saveSettings();
        return;
    }
    if (popup_ == Popup::Subtitles) {
        subtitleScroll_ = std::max(0.f, subtitleScroll_ - delta * 2.f);
        return;
    }
    if (popup_ == Popup::FontPicker) {
        fontScroll_ = std::clamp(fontScroll_ + (delta > 0 ? -3 : 3), 0, fontMaxScroll_);
        return;
    }
    if (!hasMedia_) return;
    setVolume(volume_ + (delta > 0 ? 0.04f : -0.04f));
    showToast(L"音量 " + std::to_wstring((int)std::lround(volume_ * 100)) + L"%");
}

void App::onKeyDown(int vk, bool ctrl, bool shift) {
    dirty_ = true;
    // The file browser is modal: it consumes every key while it is open so
    // arrows/space can never leak into playback shortcuts underneath.
    if (popup_ == Popup::FileBrowser) {
        fbOnKey(vk);
        return;
    }
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    for (const auto& b : keyBindings_) {
        if (b.vk == vk && b.ctrl == ctrl && b.shift == shift && b.alt == alt) {
            runKeyAction(b);
            return;
        }
    }
    if (!ctrl && (vk == 'I' || vk == 'i')) {
        showStats_ = !showStats_;
        dirty_ = true;
        return;
    }
    if (!ctrl && (vk == 'A' || vk == 'a')) {
        if (engine_.audioTrackCount() >= 2 && engine_.cycleAudio(shift ? -1 : 1))
            showToast(L"音轨：" + engine_.audioTrackName());
        else
            showToast(engine_.audioTrackCount() < 2 ? L"只有一个音轨" : L"无法切换音轨");
        return;
    }
    if (ctrl && (vk == 'O' || vk == 'o')) {
        openDialog(false);
        return;
    }
    if (screen_ == Screen::Home) {
        if (vk == VK_RETURN) openDialog(false);
        if (vk == VK_ESCAPE && fullscreen_) setFullscreen(false);
        return;
    }
    switch (vk) {
        case VK_SPACE:
        case 'K': togglePlay(); break;
        case VK_LEFT: seekBy(shift ? -1.0 : -5.0); break;
        case VK_RIGHT: seekBy(shift ? 1.0 : 5.0); break;
        case 'J': seekBy(-10.0); break;
        case 'L': seekBy(10.0); break;
        case VK_UP: setVolume(volume_ + 0.05f); showToast(L"音量 " + std::to_wstring((int)std::lround(volume_ * 100)) + L"%"); break;
        case VK_DOWN: setVolume(volume_ - 0.05f); showToast(L"音量 " + std::to_wstring((int)std::lround(volume_ * 100)) + L"%"); break;
        case 'V': {
            if (!engine_.cycleSubtitle(shift ? -1 : 1)) {
                showToast(L"没有可切换的字幕轨");
                break;
            }
            subtitleEnabled_ = true;
            subtitleText_.clear();
            subtitleTrack_ = engine_.subtitleTrack();
            showToast(L"字幕：" + engine_.subtitleName());
            break;
        }
        case 'M': toggleMute(); break;
        case 'F': setFullscreen(!fullscreen_); break;
        case VK_ESCAPE:
            if (showStats_) { showStats_ = false; break; }
            if (fullscreen_) setFullscreen(false);
            else if (popup_ == Popup::FontPicker) popup_ = Popup::SubtitleStyle;
            else if (popup_ != Popup::None) popup_ = Popup::None;
            else closeMedia();
            break;
        case 'P':
        case 'N': nextTrack(); break;
        case 'B': prevTrack(); break;
        case VK_HOME: engine_.seek(0); break;
        case VK_END: engine_.seek(duration_ - 0.2); break;
        case VK_ADD:
        case VK_OEM_PLUS: engine_.setSpeed(std::min(4.0, engine_.speed() + 0.25)); showToast(L"速度 " + std::to_wstring((int)std::lround(engine_.speed() * 100)) + L"%"); break;
        case VK_SUBTRACT:
        case VK_OEM_MINUS: engine_.setSpeed(std::max(0.25, engine_.speed() - 0.25)); showToast(L"速度 " + std::to_wstring((int)std::lround(engine_.speed() * 100)) + L"%"); break;
        default: break;
    }
}

void App::activate(int id) {
    dirty_ = true;
    // File-browser ids only exist while its popup is open, so they cannot
    // collide with the font-row / control id ranges handled below.
    if (popup_ == Popup::FileBrowser && id >= kHitFbRowBase) {
        const int idx = id - kHitFbRowBase;
        if (idx < 0 || idx >= (int)fbEntries_.size()) return;
        const bool doubleClick = idx == fbLastClickIdx_ && now_ - fbLastClickAt_ < 0.45;
        fbLastClickAt_ = now_;
        fbLastClickIdx_ = idx;
        fbSelected_ = idx;
        if (doubleClick) {
            if (fbEntries_[idx].dir) fbNavigate(fbEntries_[idx].path);
            else if (fbMode_ == FbMode::Subtitle) fbImportSubtitle(fbEntries_[idx].path);
            else fbOpenTarget(fbEntries_[idx].path);
        }
        return;
    }
    if (popup_ == Popup::FileBrowser && id >= kHitFbDriveBase && id < kHitFbDriveBase + 32) {
        const int idx = id - kHitFbDriveBase;
        if (idx >= 0 && idx < (int)fbDrives_.size()) fbNavigate(fbDrives_[idx]);
        return;
    }
    if (id >= kHitFontRowBase) {
        const auto& familyNames = p_.SystemFontFamilies();
        const size_t idx = (size_t)(id - kHitFontRowBase);
        if (idx >= familyNames.size()) return;
        const std::wstring family = familyNames[idx];
        if (applySubtitleFamily(family)) {
            showToast(L"字幕字体：" + family);
        } else {
            showToast(L"该字体不可用，已回退到系统字体");
        }
        saveSettings();
        popup_ = Popup::SubtitleStyle;
        return;
    }
    if (id >= kHitSubtitleRowBase && id < kHitSubtitleRowBase + (int)subtitleAllOrds_.size()) {
        int idx = id - kHitSubtitleRowBase;
        int ord = subtitleAllOrds_[idx];
        if (engine_.selectSubtitle(ord)) {
            subtitleEnabled_ = true;
            subtitleTrack_ = engine_.subtitleTrack();
            subtitleText_ = engine_.subtitleAt(position_);
            popup_ = Popup::None;
            showToast(L"字幕：" + engine_.subtitleName());
        }
        return;
    }
    if (id >= kHitSubStyleRowBase && id < kHitSubStyleRowBase + 5) {
        switch (id - kHitSubStyleRowBase) {
            case 0: {
                auto next = std::upper_bound(std::begin(kSubtitleScalePresets), std::end(kSubtitleScalePresets),
                                             subtitleScale_ + 0.0001f);
                subtitleScale_ = next == std::end(kSubtitleScalePresets) ? kSubtitleScalePresets[0] : *next;
                break;
            }
            case 1: subtitleColor_ = (subtitleColor_ + 1) % 4; break;
            case 2: subtitleOutline_ = (subtitleOutline_ + 1) % 4; break;
            case 3: subtitleBg_ = (subtitleBg_ + 1) % 3; break;
            case 4:
                subtitlePos_ = (subtitlePos_ + 1) % 3;
                // Changing the preset also recenters the free-drag offset, so a
                // subtitle dragged off-screen can always be recovered here.
                subX_ = 0.f;
                subY_ = 0.f;
                break;
        }
        saveSettings();
        return;
    }
    if (id >= kHitRecentBase && id < kHitRecentBase + 100) {
        int idx = id - kHitRecentBase;
        if (idx >= 0 && idx < (int)recents_.size()) {
            playlist_.clear();
            PlaylistItem it;
            it.path = recents_[idx].path;
            it.title = recents_[idx].title;
            it.duration = recents_[idx].duration;
            playlist_.push_back(it);
            current_ = 0;
            openPath(it.path, true, recents_[idx].position);
        }
        return;
    }
    if (id >= kHitPlaylistRowBase && id < kHitPlaylistRowBase + 100) {
        playIndex(id - kHitPlaylistRowBase);
        popup_ = Popup::None;
        return;
    }
    if (id >= kHitSpeedBase && id < kHitSpeedBase + 7) {
        static const double speeds[7] = {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
        engine_.setSpeed(speeds[id - kHitSpeedBase]);
        popup_ = Popup::None;
        showToast(L"速度 " + FormatTime(engine_.speed()) + L"×");
        return;
    }
    switch (id) {
        case kHitOpenFile: openDialog(false); break;
        case kHitOpenFolder: openDialog(true); break;
        case kHitFbUp: fbGoUp(); break;
        case kHitFbOpen: fbAccept(); break;
        case kHitFbCancel:
        case kHitFbClose: popup_ = Popup::None; break;
        case kHitSettingsTop:
        case kHitSettingsBtn: popup_ = popup_ == Popup::Settings ? Popup::None : Popup::Settings; break;
        case kHitSubtitle:
            if (engine_.info().subtitles.empty()) {
                openSubtitleDialog();
            } else {
                popup_ = popup_ == Popup::Subtitles ? Popup::None : Popup::Subtitles;
                subtitleScroll_ = 0;
                setControlsVisible(true);
            }
            break;
        case kHitSubtitleClose:
            popup_ = Popup::None;
            break;
        case kHitSubModeAuto:
        case kHitSubModeOriginal:
        case kHitSubModeCustom:
            setSubtitleMode(id - kHitSubModeAuto);
            break;
        case kHitSubtitleOff:
            subtitleEnabled_ = false;
            subtitleText_.clear();
            popup_ = Popup::None;
            showToast(L"字幕已关闭");
            break;
        case kHitSubtitleImport:
            openSubtitleDialog();
            break;
        case kHitSubtitleStyle:
            popup_ = Popup::SubtitleStyle;
            break;
        case kHitSubStyleClose:
            popup_ = Popup::Subtitles;
            break;
        case kHitSubStyleFont:
            popup_ = Popup::FontPicker;
            break;
        case kHitFontPickerClose:
            popup_ = Popup::SubtitleStyle;
            break;
        case kHitFontDefault:
            applySubtitleFamily(L"");
            saveSettings();
            showToast(L"字幕字体：系统默认");
            popup_ = Popup::SubtitleStyle;
            break;
        case kHitSpeed: popup_ = popup_ == Popup::Speed ? Popup::None : Popup::Speed; break;
        case kHitPlaylistBtn: popup_ = popup_ == Popup::Playlist ? Popup::None : Popup::Playlist; break;
        case kHitMin: ShowWindow(hwnd_, SW_MINIMIZE); break;
        case kHitMax: ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE); break;
        case kHitClose: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
        case kHitBack: closeMedia(); break;
        case kHitPlay: togglePlay(); break;
        case kHitPrev: prevTrack(); break;
        case kHitNext: nextTrack(); break;
        case kHitMute: toggleMute(); break;
        case kHitFullscreen: setFullscreen(!fullscreen_); break;
        case kHitVideoArea:
            if (popup_ == Popup::None) togglePlay();
            break;
        case kHitBackdrop:
            // The file browser is modal: only Esc, 取消 and the close button leave it.
            if (popup_ == Popup::FileBrowser) break;
            popup_ = popup_ == Popup::FontPicker ? Popup::SubtitleStyle : Popup::None;
            break;
        case kHitSettingsClose: popup_ = Popup::None; break;
        case kHitPlaylistClose: popup_ = Popup::None; break;
        case kHitAutoResume: autoResume_ = !autoResume_; saveSettings(); break;
        case kHitAnimations: animations_ = !animations_; saveSettings(); break;
        case kHitAutoNext: autoNext_ = !autoNext_; saveSettings(); break;
        case kHitExclusive: exclusiveAudio_ = !exclusiveAudio_; saveSettings(); break;
        case kHitHwDecode:
            softwareDecode_ = !softwareDecode_;
            saveSettings();
            showToast(softwareDecode_ ? L"已切换为软件解码（下次打开生效）" : L"已启用硬件解码（下次打开生效）");
            break;
        case kHitHdrOutput:
            hdrDisabled_ = !hdrDisabled_;
            if (hasMedia_) {
                bool on = gfx_->setHdrOutput(engine_.info().video.hdr && gfx_->hdrDisplay() && !hdrDisabled_);
                ApplyThemeColors();
                showToast(on ? L"HDR scRGB 已启用" : L"已切换 SDR 输出");
            }
            saveSettings();
            break;
        case kHitAccent:
            accentPreset_ = (accentPreset_ + 1) % 5;
            ApplyAccentPreset(accentPreset_);
            saveSettings();
            break;
        case kHitThumbs:
            thumbnails_ = !thumbnails_;
            if (!thumbnails_) {
                fbThumbsWorker_.cancel();
                fbThumbs_.clear();
                fbThumbMissing_.clear();
            }
            saveSettings();
            break;
        case kHitAudioTrack:
            if (engine_.audioTrackCount() < 2) showToast(L"只有一个音轨");
            else if (engine_.cycleAudio(1)) showToast(L"音轨：" + engine_.audioTrackName());
            break;
        case kHitSettingsSubStyle: popup_ = Popup::SubtitleStyle; break;
        case kHitStatsTop:
        case kHitStatsBtn:
            showStats_ = !showStats_;
            break;
        case kHitStatsRow:
            showStats_ = !showStats_;
            if (showStats_) popup_ = Popup::None;
            break;
        case kHitDefaultApps: {
            std::wstring script = AppDir() + L"\\RegisterFileTypes.ps1";
            std::error_code ec;
            if (std::filesystem::exists(script, ec)) {
                std::wstring args = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script + L"\" -Associate";
                ShellExecuteW(nullptr, L"open", L"powershell.exe", args.c_str(), AppDir().c_str(), SW_SHOWNORMAL);
                showToast(L"正在打开默认应用注册");
            } else {
                ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
            }
            break;
        }
        case kHitOpenLog:
            ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + DataDir() + L"\\prism.log\"").c_str(),
                          nullptr, SW_SHOWNORMAL);
            break;
        case kHitReplay:
            engine_.seek(0);
            engine_.play();
            playing_ = true;
            break;
        case kHitNextUp: nextTrack(); break;
        default: break;
    }
}

bool App::handleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& out) {
    hwnd_ = hwnd;
    out = 0;
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wp) {
                if (IsZoomed(hwnd) || fullscreen_) return false;
                return false;
            }
            return false;
        case WM_NCHITTEST: {
            if (fullscreen_) return false;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            float sx = gfx_ ? gfx_->scale() : 1.f;
            float x = pt.x / sx, y = pt.y / sx;
            layout();
            int id = hitTest(x, y);
            // Any registered control or popup hit must reach WM_LBUTTONDOWN. Only
            // empty title-bar space (the full-screen video surface, or nothing on
            // the home screen) becomes a drag caption.
            if (id != 0 && id != kHitVideoArea) return false;
            if (y < 56.f) {
                out = HTCAPTION;
                return true;
            }
            if (!IsZoomed(hwnd)) {
                const int border = (int)(6 * sx);
                RECT rc;
                GetClientRect(hwnd, &rc);
                if (pt.x < border) out = HTLEFT;
                else if (pt.x >= rc.right - border) out = HTRIGHT;
                else if (pt.y < border) out = HTTOP;
                else if (pt.y >= rc.bottom - border) out = HTBOTTOM;
                else if (pt.x < border && pt.y < border) out = HTTOPLEFT;
                else if (pt.x >= rc.right - border && pt.y < border) out = HTTOPRIGHT;
                else if (pt.x < border && pt.y >= rc.bottom - border) out = HTBOTTOMLEFT;
                else if (pt.x >= rc.right - border && pt.y >= rc.bottom - border) out = HTBOTTOMRIGHT;
                else return false;
                return true;
            }
            return false;
        }
        case WM_MOUSEMOVE: {
            float sx = gfx_ ? gfx_->scale() : 1.f;
            float x = GET_X_LPARAM(lp) / sx, y = GET_Y_LPARAM(lp) / sx;
            onMouseMove(x, y);
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return false;
        }
        case WM_MOUSELEAVE:
            hoverId_ = 0;
            return false;
        case WM_LBUTTONDOWN: {
            float sx = gfx_ ? gfx_->scale() : 1.f;
            SetFocus(hwnd);
            onMouseDown(GET_X_LPARAM(lp) / sx, GET_Y_LPARAM(lp) / sx);
            return false;
        }
        case WM_LBUTTONUP: {
            float sx = gfx_ ? gfx_->scale() : 1.f;
            onMouseUp(GET_X_LPARAM(lp) / sx, GET_Y_LPARAM(lp) / sx);
            return false;
        }
        case WM_MOUSEWHEEL:
            onMouseWheel((float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
            return true;
        case WM_KEYDOWN: {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            onKeyDown((int)wp, ctrl, shift);
            (void)alt;
            return true;
        }
        case WM_KEYUP: {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            onKeyUp((int)wp, ctrl, shift, alt);
            return true;
        }
        case WM_KILLFOCUS:
            if (speedBoostActive_) {
                engine_.setSpeed(std::clamp(speedBoostPrev_, 0.25, 4.0));
                speedBoostActive_ = false;
            }
            return false;
        case WM_SETCURSOR: {
            if (LOWORD(lp) != HTCLIENT) return false;
            if (cursorHidden_) {
                SetCursor(nullptr);
                return true;
            }
            if (hoverId_ != 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return true;
            }
            return false;
        }
        case WM_DROPFILES:
            openDropped((HDROP)wp);
            return true;
        case WM_DPICHANGED: {
            RECT* r = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return true;
        }
        case WM_NCACTIVATE:
            return true;
        case WM_ERASEBKGND:
            return true;
        case WM_SIZE:
            if (gfx_) gfx_->resize((int)LOWORD(lp), (int)HIWORD(lp));
            resize(LOWORD(lp), HIWORD(lp));
            return false;
        default:
            return false;
    }
}

bool App::headlessSetup(const std::wstring& media, double time, Screen screen) {
    screen_ = screen;
    if (!media.empty()) {
        std::string err;
        if (!engine_.open(media, err, false)) {
            Log("headless open failed: %s", err.c_str());
            return false;
        }
        hasMedia_ = true;
        duration_ = engine_.duration();
        playlist_.clear();
        PlaylistItem it;
        it.path = media;
        it.title = FileNameOf(media);
        it.duration = duration_;
        playlist_.push_back(it);
        current_ = 0;
        position_ = std::clamp(time, 0.0, std::max(0.0, duration_ - 0.1));
        engine_.seek(position_);
        if (engine_.hasVideo()) {
            for (int i = 0; i < 400; ++i) {
                if (VideoFrame* f = engine_.acquireFrame()) {
                    if (f->hdr)
                        gfx_->updateVideoHdr(f->planes.data(), f->width, f->height, f->yStride, f->uvStride);
                    else
                        gfx_->updateVideo(f->pixels.data(), f->width, f->height, f->stride);
                    if (frame_) engine_.releaseFrame(frame_);
                    frame_ = f;
                    frameSerial_ = engine_.serial();
                    break;
                }
                Sleep(5);
            }
        }
        engine_.pause();
    }
    if (screen_ == Screen::Player) {
        controlsAlpha_ = 1.f;
        controlsTarget_ = 1.f;
        subtitleEnabled_ = engine_.hasSubtitles();
        subtitleText_ = engine_.subtitleAt(position_);
    }
    return true;
}

bool App::headlessSave(const std::wstring& outPath) { return gfx_->saveFramePng(outPath); }

void App::headlessSetPopup(int popup) {
    // 1..7 == the Popup enum order, 7 being the file browser (--popup files);
    // 8 opens the same browser in subtitle-import mode (--popup subfiles).
    if (popup == 8) {
        openSubtitleDialog();
        return;
    }
    if (popup >= 1 && popup <= (int)Popup::FileBrowser) popup_ = (Popup)popup;
}

void App::headlessSetSubtitleMode(int mode) { subtitleMode_ = std::clamp(mode, 0, 2); }

void App::headlessWarmThumbnails() {
    if (!headless_ || popup_ != Popup::FileBrowser || fbMode_ != FbMode::MediaFile) return;
    const double deadline = QpcNow() + 6.0;
    for (;;) {
        layout();
        fbSyncThumbs(true);
        if (fbThumbsSettled()) return;
        if (QpcNow() >= deadline) {
            Log("thumbnail warm: timed out waiting for the shell");
            return;
        }
        Sleep(10);
    }
}

void App::headlessSetPlaylist(const std::vector<std::wstring>& files) {
    playlist_.clear();
    for (size_t i = 0; i < files.size() && i < 64; ++i) {
        PlaylistItem it;
        it.path = files[i];
        it.title = FileNameOf(files[i]);
        it.duration = ProbeDuration(files[i]);
        playlist_.push_back(it);
    }
    current_ = 0;
    for (size_t i = 0; i < playlist_.size(); ++i)
        if (playlist_[i].path == engine_.path()) current_ = (int)i;
}

void App::runKernelBench() {
    const int w = 1920, h = 1080;
    std::vector<uint8_t> y((size_t)w * h), u((size_t)w / 2 * h / 2), v((size_t)w / 2 * h / 2),
        dst((size_t)w * h * 4);
    prism::FillTestPattern(y.data(), w, h, w, 1);
    prism::FillTestPattern(u.data(), w / 2, h / 2, w / 2, 2);
    prism::FillTestPattern(v.data(), w / 2, h / 2, w / 2, 3);
    using clock = std::chrono::high_resolution_clock;
    auto runAsm = [&] {
        prism::Yuv420pToBgra(dst.data(), w * 4, y.data(), u.data(), v.data(), w, w / 2, w, h,
                             prism::Matrix::BT709, false);
    };
    auto runRef = [&] {
        prism::Yuv420pToBgraRef(dst.data(), w * 4, y.data(), u.data(), v.data(), w, w / 2, w, h,
                                prism::Matrix::BT709, false);
    };
    runAsm();
    runRef();
    const int nA = 30;
    auto t0 = clock::now();
    for (int i = 0; i < nA; ++i) runAsm();
    auto t1 = clock::now();
    const int nR = 3;
    for (int i = 0; i < nR; ++i) runRef();
    auto t2 = clock::now();
    benchAsmMs_ = std::chrono::duration<double, std::milli>(t1 - t0).count() / nA;
    benchRefMs_ = std::chrono::duration<double, std::milli>(t2 - t1).count() / nR;
    Log("kernel bench: asm %.3f ms/frame, scalar %.3f ms/frame (%.1fx)", benchAsmMs_, benchRefMs_,
        benchRefMs_ / std::max(0.001, benchAsmMs_));
}
