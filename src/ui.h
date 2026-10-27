// Application UI: home library, player overlay, playlist drawer, settings.
#pragma once

#include "common.h"
#include "engine.h"
#include "gfx.h"
#include "paint.h"
#include "thumbs.h"

#include <deque>

class AssRenderer;

enum class Screen { Home, Player };
enum class Popup { None, Settings, Playlist, Speed, Subtitles, SubtitleStyle, FontPicker, FileBrowser, KeyEditor };
enum class FbMode { MediaFile, MediaFolder, Subtitle };

enum {
    kHitNone = 0,
    kHitOpenFile = 1,
    kHitOpenFolder = 2,
    kHitSettingsTop = 3,
    kHitMin = 4,
    kHitMax = 5,
    kHitClose = 6,
    kHitOpenTop = 7,
    kHitBack = 10,
    kHitPlay = 11,
    kHitPrev = 12,
    kHitNext = 13,
    kHitMute = 14,
    kHitVolume = 15,
    kHitSpeed = 16,
    kHitPlaylistBtn = 17,
    kHitFullscreen = 18,
    kHitSeek = 19,
    kHitReplay = 20,
    kHitNextUp = 21,
    kHitVideoArea = 22,
    kHitSettingsBtn = 23,
    kHitSubtitle = 24,
    kHitPlaylistClose = 25,
    kHitSubtitleDrag = 26,
    kHitStatsTop = 27,
    kHitStatsBtn = 28,
    kHitSpeedBase = 100,
    kHitRecentBase = 200,
    kHitPlaylistRowBase = 300,
    kHitClearList = 400,
    kHitSettingsClose = 401,
    kHitAutoResume = 402,
    kHitAnimations = 403,
    kHitSubtitleClose = 410,
    kHitSubtitleOff = 411,
    kHitSubtitleImport = 412,
    kHitSubtitleStyle = 413,
    kHitSubStyleClose = 414,
    kHitSubStyleFont = 415,
    kHitFontPickerClose = 416,
    kHitFontDefault = 417,
    kHitSubModeAuto = 420,
    kHitSubModeOriginal = 421,
    kHitSubModeCustom = 422,
    kHitSubtitleRowBase = 600,
    kHitSubStyleRowBase = 700,
    kHitExclusive = 404,
    kHitAutoNext = 405,
    kHitHwDecode = 406,
    kHitHdrOutput = 407,
    kHitAccent = 408,
    kHitThumbs = 409,
    kHitAudioTrack = 418,
    kHitSettingsSubStyle = 419,
    kHitDefaultApps = 423,
    kHitOpenLog = 424,
    kHitStatsRow = 425,
    kHitKeyEditor = 426,
    kHitKeyClose = 430,
    kHitKeyReset = 431,
    kHitKeyFile = 432,
    kHitKeyRowBase = 12000,
    kHitBackdrop = 900,
    kHitFbClose = 500,
    kHitFbUp = 501,
    kHitFbOpen = 502,
    kHitFbCancel = 503,
    kHitFbDriveBase = 3000,
    kHitFbRowBase = 10000,
    kHitFontRowBase = 2000,
};

namespace theme {
extern Color Bg;
extern Color Bg2;
extern Color Surface;
extern Color Text;
extern Color Text2;
extern Color Text3;
extern Color Line;
extern Color LineHi;
extern Color Accent;
extern Color AccentWarm;
extern Color Danger;
}  // namespace theme

void ApplyThemeColors();
void ApplyAccentPreset(int preset);

struct RecentEntry {
    std::wstring path;
    double position = 0;
    double duration = 0;
    std::wstring title;
    std::wstring meta;
};

struct KeyBinding {
    int vk = 0;
    bool ctrl = false, shift = false, alt = false;
    std::string action;
    float arg = 0.f;
};

std::wstring ComboText(const KeyBinding& b);
std::wstring ActionLabel(const std::string& action);

struct PlaylistItem {
    std::wstring path;
    std::wstring title;
    double duration = 0;
};

class App {
public:
    App();
    ~App();

    bool init(Gfx* gfx, HWND hwnd, bool headless);
    void shutdown();
    void resize(int width, int height);
    void tick(double now, double dt);
    void updateSystemStats();
    void toggleSecondarySubtitle();
    void headlessSetSecondary(int ordinal);
    void pollFolder();
    void refreshFolderPlaylist();
    void render(double now);
    bool wantsRender(double now) const;
    bool handleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& out);

    bool openPath(const std::wstring& path, bool autoplay = true, double resumeAt = -1);
    void openDialog(bool folder);
    void openDropped(HDROP drop);
    void openFiles(const std::vector<std::wstring>& files);
    void seekBy(double delta);
    bool headlessSetup(const std::wstring& media, double time, Screen screen);
    bool headlessSave(const std::wstring& outPath);
    void headlessSetPopup(int popup);
    void headlessSetPlaylist(const std::vector<std::wstring>& files);
    void headlessSetSubtitleMode(int mode);
    void headlessWarmThumbnails();
    void headlessShowStats() { showStats_ = true; }

    bool fullscreen() const { return fullscreen_; }
    void setFullscreen(bool on);
    bool hasMedia() const { return hasMedia_; }
    HWND hwnd() const { return hwnd_; }

private:
    struct Hit {
        int id;
        RectF rect;
    };

    // layout
    void layout();
    void layoutFileBrowser();
    void layoutKeyEditor();
    void addHit(int id, const RectF& r);
    int hitTest(float x, float y) const;
    bool hovered(int id) const { return hoverId_ == id; }

    // drawing
    void drawBackdrop(const RectF& area);
    void drawTitleBar();
    void drawHome();
    void drawPlayer();
    void drawVideoArea();
    void drawAudioOnly(const RectF& area);
    void drawControls();
    void drawPlaylist();
    void drawSettings();
    void drawStats();
    void drawSpeedMenu();
    void drawToast();
    void drawCenterOverlay();
    void drawScrims();
    void drawLogo(float x, float y, float size, float alpha);
    void drawWindowButtons();
    void drawSubtitle();
    void drawAssSubtitle();
    void drawSecondarySubtitle();
    void drawSubtitleMenu();
    void drawSubtitleStyle();
    void drawFontPicker();
    void drawFileBrowser();
    void drawKeyEditor();

    // interaction
    void onMouseMove(float x, float y);
    void onMouseDown(float x, float y);
    void onMouseUp(float x, float y);
    void onMouseWheel(float delta);
    void onKeyDown(int vk, bool ctrl, bool shift);
    void onKeyUp(int vk, bool ctrl, bool shift, bool alt);
    void loadKeyBindings();
    bool runKeyAction(const KeyBinding& b);
    void openKeysFile();
    void openKeyEditor();
    void saveKeyBindings();
    void resetKeyBindings();
    void activate(int id);
    void showToast(const std::wstring& text);
    void updateHover();
    void setControlsVisible(bool visible);
    void togglePlay();
    void toggleMute();
    void setVolume(float v);
    void playIndex(int index);
    void nextTrack();
    void prevTrack();
    void closeMedia();
    void syncWindowState(double now);
    void openSubtitleDialog();
    bool applySubtitleFamily(const std::wstring& family);
    bool assNativeActive();
    void setSubtitleMode(int mode);
    void ensureAssRenderer();

    // in-app file browser popup: media file, media folder, or subtitle import
    struct FbEntry {
        std::wstring name;
        std::wstring path;
        bool dir = false;
    };
    struct FbThumb {
        std::wstring key;
        int width = 0;
        int height = 0;
        ComPtr<ID2D1Bitmap1> bitmap;
    };
    static constexpr float kFbRowH = 54.f;
    static constexpr float kFbThumbW = 80.f;
    static constexpr float kFbThumbH = 45.f;
    void fbEnsureInit();
    void fbReload();
    void fbNavigate(const std::wstring& dir);
    void fbGoUp();
    void fbAccept();
    void fbImportSubtitle(const std::wstring& path);
    void fbMoveSelection(int delta);
    void fbEnsureVisible();
    void fbOnKey(int vk);
    void fbOpenTarget(const std::wstring& path);
    void fbSyncThumbs(bool request);
    void fbApplyThumb(ThumbResult& result);
    bool fbThumbsSettled() const;
    std::wstring fbStartDir() const;

    // settings
    void loadSettings();
    void saveSettings();
    void pushRecent(const std::wstring& path, double pos, double duration);
    void saveCurrentPosition();
    void buildRecentMeta();
    void runKernelBench();

    Gfx* gfx_ = nullptr;
    HWND hwnd_ = nullptr;
    Painter p_;
    bool headless_ = false;
    bool inited_ = false;

    float W_ = 1440, H_ = 900;
    float mouseX_ = -1, mouseY_ = -1;
    int hoverId_ = 0;
    int pressedId_ = 0;
    bool mouseDown_ = false;
    double lastMouseMove_ = 0;
    bool cursorHidden_ = false;
    bool dirty_ = true;
    int renderedHover_ = -1;

    Screen screen_ = Screen::Home;
    Popup popup_ = Popup::None;
    Popup laidOutPopup_ = Popup::None;
    bool fullscreen_ = false;
    WINDOWPLACEMENT savedPlacement_{};
    LONG savedStyle_ = 0;
    bool maximizedBeforeFullscreen_ = false;

    Engine engine_;
    bool hasMedia_ = false;
    bool playing_ = false;
    VideoFrame* frame_ = nullptr;
    uint64_t frameSerial_ = 0;
    bool videoDirty_ = false;
    double lastClockDraw_ = 0;
    double lastPositionSave_ = 0;
    std::wstring mediaError_;

    std::vector<PlaylistItem> playlist_;
    int current_ = -1;
    std::vector<RecentEntry> recents_;

    double position_ = 0, duration_ = 0;
    double dragSeekTime_ = 0;
    bool draggingSeek_ = false;
    bool draggingVolume_ = false;
    bool muted_ = false;
    float volume_ = 1.f;
    float volumeBeforeMute_ = 1.f;
    int subtitleTrack_ = -1;
    bool subtitleEnabled_ = false;
    bool secondaryEnabled_ = false;
    int secondaryOrdinal_ = -1;
    std::wstring secondaryText_;
    std::wstring subtitleText_;
    int subtitleMode_ = 0;  // 0 auto, 1 original styles (native ASS), 2 custom plain text
    int subtitleModeSaved_ = 0;  // persisted default; CLI test overrides must not stick
    std::unique_ptr<AssRenderer> assRenderer_;
    std::shared_ptr<const AssTrackData> assData_;

    float controlsAlpha_ = 1.f;
    float controlsTarget_ = 1.f;
    bool controlsVisible_ = true;
    float seekHover_ = 0.f;
    float centerPlay_ = 0.f;
    float toastAlpha_ = 0.f;
    std::wstring toastText_;
    double toastUntil_ = 0;
    double now_ = 0;
    double lastTick_ = 0;
    double lastRender_ = 0;

    // settings values
    bool autoResume_ = true;
    bool animations_ = true;
    float subtitleScale_ = 1.f;
    float subtitleDelay_ = 0.f;
    int subtitleColor_ = 0;
    int subtitleOutline_ = 2;
    int subtitleBg_ = 0;
    int subtitlePos_ = 1;
    std::wstring subFontFamily_;
    bool exclusiveAudio_ = true;
    float subtitleScroll_ = 0.f;
    int fontScroll_ = 0;
    int fontMaxScroll_ = 0;
    std::wstring lastDir_;
    FbMode fbMode_ = FbMode::MediaFile;
    std::wstring fbDir_;
    std::vector<FbEntry> fbEntries_;
    int fbSelected_ = -1;
    int fbScroll_ = 0;
    int fbMaxScroll_ = 0;
    int fbVisibleRows_ = 0;
    double fbLastClickAt_ = 0;
    int fbLastClickIdx_ = -1;
    bool fbCanUp_ = false;
    ThumbnailCache fbThumbsWorker_;
    bool fbThumbActive_ = false;
    std::wstring fbThumbDir_;
    std::unordered_map<std::wstring, FbThumb> fbThumbs_;
    std::unordered_map<std::wstring, std::wstring> fbThumbMissing_;
    std::vector<KeyBinding> keyBindings_;
    bool keyCapturing_ = false;
    int keySelected_ = -1;
    int keyScroll_ = 0;
    int keyMaxScroll_ = 0;
    int keyVisibleRows_ = 0;
    RectF rKeyPanel_;
    std::vector<RectF> rKeyRows_;
    bool speedBoostActive_ = false;
    int boostVk_ = 0;
    double speedBoostPrev_ = 1.0;
    bool rightPending_ = false;
    bool rightBoost_ = false;
    double rightDownAt_ = 0.0;
    double rightBoostSpeed_ = 3.0;
    double rightPrevSpeed_ = 1.0;
    double autoNextAt_ = 0;
    float subX_ = 0.f, subY_ = 0.f;
    bool draggingSub_ = false;
    float subDragMouseX_ = 0, subDragMouseY_ = 0, subDragBaseX_ = 0, subDragBaseY_ = 0;
    RectF subDragRect_;
    bool autoNext_ = true;
    bool softwareDecode_ = false;
    bool hdrDisabled_ = false;
    bool thumbnails_ = true;
    bool showStats_ = false;
    int accentPreset_ = 0;
    double renderFps_ = 0.0;
    int renderFrames_ = 0;
    double lastFpsSample_ = 0.0;
    double processCpu_ = 0.0;
    double lastCpuSample_ = 0.0;
    uint64_t lastCpuKernel_ = 0, lastCpuUser_ = 0;
    size_t workingSet_ = 0, privateBytes_ = 0;
    bool systemStatsInit_ = false;
    bool bufferToast_ = false;
    bool bufferingPaused_ = false;
    std::wstring watchDir_;
    std::vector<std::wstring> watchNames_;
    double lastWatchScan_ = 0.0;
    double benchAsmMs_ = 0.0;
    double benchRefMs_ = 0.0;

    // layout rects
    RectF rTitleBar_, rMin_, rMax_, rClose_, rSettingsTop_, rOpenTop_;
    RectF rHero_, rSub_, rOpen_, rFolder_, rDrop_, rRecentPanel_;
    std::vector<RectF> rRecent_;
    RectF rVideo_, rBack_, rTitleText_, rStatus_, rPlay_, rPrev_, rNext_, rTime_, rMute_, rVolSlider_, rSpeed_,
        rPlaylistBtn_, rFullscreen_, rSeek_, rSettingsBtn_, rCenter_, rEndCard_, rReplay_, rNextUp_, rSubtitle_;
    RectF rPlaylistPanel_;
    std::vector<RectF> rPlaylistRows_;
    RectF rSettingsPanel_, rToast_, rStatsBtn_, rStatsTop_;
    std::vector<RectF> rSettingsRows_;
    RectF rSpeedMenu_;
    std::vector<RectF> rSpeedItems_;
    RectF rSubtitlePanel_, rSubOff_, rSubImport_, rSubStyleBtn_, rSubStylePanel_;
    RectF rSubMode_[3];
    std::vector<int> subtitleAllOrds_, subtitleRowOrds_;
    std::vector<RectF> subtitleRowRects_, rSubStyleRows_;
    RectF rFontPanel_, rFontList_, rFontDefault_;
    std::vector<RectF> rFontRows_;
    RectF rFbPanel_, rFbClose_, rFbUp_, rFbPath_, rFbDrivesTag_, rFbList_, rFbOpen_, rFbCancel_;
    std::vector<RectF> rFbDrive_, rFbRows_;
    std::vector<std::wstring> fbDrives_;
    std::vector<Hit> hits_;
};
