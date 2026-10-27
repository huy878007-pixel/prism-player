#include "ass_render.h"

namespace {

void AssLog(int level, const char* fmt, va_list args, void*) {
    // libass levels: 0 fatal, 1 error, 2 warning, 4 info, 6 verbose, 7 debug.
    // Skip the per-event verbose chatter; keep what helps diagnose fonts/files.
    if (level > 4) return;
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    Log("libass: %s", buf);
}

}  // namespace

AssRenderer::~AssRenderer() {
    releaseTrack();
    if (renderer_) {
        fnRendererDone_(renderer_);
        renderer_ = nullptr;
    }
    if (library_) {
        fnLibraryDone_(library_);
        library_ = nullptr;
    }
    if (dll_) {
        FreeLibrary(dll_);
        dll_ = nullptr;
    }
}

template <typename T>
bool AssRenderer::resolve(T& fn, const char* name) {
    fn = reinterpret_cast<T>(GetProcAddress(dll_, name));
    if (!fn) {
        error_ = std::string("libass 缺少导出函数 ") + name;
        return false;
    }
    return true;
}

bool AssRenderer::init() {
    if (renderer_) return true;
    std::wstring path = AppDir() + L"\\libass-9.dll";
    dll_ = LoadLibraryW(path.c_str());
    if (!dll_) dll_ = LoadLibraryW(L"libass-9.dll");  // PATH fallback
    if (!dll_) {
        error_ = "无法加载 libass-9.dll（错误码 " + std::to_string(GetLastError()) + "）";
        Log("libass: %s", error_.c_str());
        return false;
    }
    bool ok = resolve(fnLibraryInit_, "ass_library_init") &&
              resolve(fnLibraryDone_, "ass_library_done") &&
              resolve(fnLibraryVersion_, "ass_library_version") &&
              resolve(fnRendererInit_, "ass_renderer_init") &&
              resolve(fnRendererDone_, "ass_renderer_done") &&
              resolve(fnNewTrack_, "ass_new_track") && resolve(fnFreeTrack_, "ass_free_track") &&
              resolve(fnProcessCodecPrivate_, "ass_process_codec_private") &&
              resolve(fnProcessChunk_, "ass_process_chunk") &&
              resolve(fnSetFrameSize_, "ass_set_frame_size") &&
              resolve(fnSetStorageSize_, "ass_set_storage_size") &&
              resolve(fnSetMargins_, "ass_set_margins") &&
              resolve(fnSetUseMargins_, "ass_set_use_margins") &&
              resolve(fnSetPixelAspect_, "ass_set_pixel_aspect") &&
              resolve(fnSetFonts_, "ass_set_fonts") && resolve(fnRenderFrame_, "ass_render_frame");
    if (!ok) {
        Log("libass: %s", error_.c_str());
        FreeLibrary(dll_);
        dll_ = nullptr;
        return false;
    }
    // Optional: message callback appeared long ago but stay tolerant of old DLLs.
    resolve(fnSetMessageCb_, "ass_set_message_cb");

    library_ = fnLibraryInit_();
    if (!library_) {
        error_ = "libass 初始化失败";
        Log("libass: %s", error_.c_str());
        return false;
    }
    if (fnSetMessageCb_) fnSetMessageCb_(library_, &AssLog, nullptr);
    renderer_ = fnRendererInit_(library_);
    if (!renderer_) {
        error_ = "libass 渲染器初始化失败";
        Log("libass: %s", error_.c_str());
        return false;
    }
    // DirectWrite system font lookup; the family is only the fallback for missing styles.
    fnSetFonts_(renderer_, nullptr, "Microsoft YaHei", ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);

    const int v = fnLibraryVersion_ ? fnLibraryVersion_() : 0;
    char buf[64];
    snprintf(buf, sizeof(buf), "0.17.5 (api 0x%X)", v);
    version_ = buf;
    Log("libass: loaded %ls version=%s provider=DirectWrite", path.c_str(), version_.c_str());
    return true;
}

void AssRenderer::releaseTrack() {
    if (track_) {
        fnFreeTrack_(track_);
        track_ = nullptr;
    }
    data_.reset();
    rendered_ = false;
    empty_ = true;
    clearPixels();
}

bool AssRenderer::setTrack(const std::shared_ptr<const AssTrackData>& data) {
    releaseTrack();
    if (!renderer_ || !data || data->header.empty()) return false;
    data_ = data;
    track_ = fnNewTrack_(library_);
    if (!track_) return false;
    fnProcessCodecPrivate_(track_, data->header.data(), (int)data->header.size());
    wchar_t dumpFlag[8]{};
    FILE* dump = nullptr;
    if (GetEnvironmentVariableW(L"PRISM_DUMP_ASS", dumpFlag, 8))
        _wfopen_s(&dump, (AppDir() + L"\\ass-track.txt").c_str(), L"wb");
    if (dump) {
        fwrite(data->header.data(), 1, data->header.size(), dump);
        fprintf(dump, "\n#EVENTS#\n");
    }
    for (const AssEvent& e : data->events) {
        if (e.data.empty()) continue;
        if (dump) fprintf(dump, "%lld %lld %s\n", (long long)llround(e.start * 1000.0),
                          (long long)llround(e.duration * 1000.0), e.data.c_str());
        fnProcessChunk_(track_, e.data.c_str(), (int)e.data.size(), (long long)llround(e.start * 1000.0),
                        (long long)llround(e.duration * 1000.0));
    }
    if (dump) fclose(dump);
    if (GetEnvironmentVariableW(L"PRISM_DUMP_ASS", dumpFlag, 8)) {
        Log("libass: playres=%dx%d events=%d format=%s", track_->PlayResX, track_->PlayResY, track_->n_events,
            track_->event_format ? track_->event_format : "(null)");
        for (int i = 0; i < track_->n_events && i < 8; ++i) {
            const ASS_Event& ev = track_->events[i];
            Log("libass: ev%d layer=%d start=%lld dur=%lld style=%d text=%.70s", i, ev.Layer,
                (long long)ev.Start, (long long)ev.Duration, ev.Style, ev.Text ? ev.Text : "(null)");
        }
    }
    Log("libass: track header=%zu bytes events=%d parsed=%d", data->header.size(), (int)data->events.size(),
        track_->n_events);
    return true;
}

void AssRenderer::setFrameSize(int width, int height) {
    width = std::clamp(width, 1, 8192);
    height = std::clamp(height, 1, 8192);
    if (width == frameW_ && height == frameH_ && renderer_) return;
    frameW_ = width;
    frameH_ = height;
    pixels_.assign((size_t)frameW_ * frameH_ * 4, 0);
    rendered_ = false;
    empty_ = true;
    serial_++;
    if (!renderer_) return;
    fnSetFrameSize_(renderer_, frameW_, frameH_);
    fnSetStorageSize_(renderer_, frameW_, frameH_);
    fnSetMargins_(renderer_, 0, 0, 0, 0);
    fnSetUseMargins_(renderer_, 0);
    fnSetPixelAspect_(renderer_, 1.0);
}

void AssRenderer::clearPixels() { std::fill(pixels_.begin(), pixels_.end(), 0); }

void AssRenderer::composite(const ASS_Image* img) {
    if (!img || !img->bitmap || img->w <= 0 || img->h <= 0) return;
    int x0 = img->dst_x, y0 = img->dst_y;
    int sx = 0, sy = 0, w = img->w, h = img->h;
    if (x0 < 0) {
        sx = -x0;
        w += x0;
        x0 = 0;
    }
    if (y0 < 0) {
        sy = -y0;
        h += y0;
        y0 = 0;
    }
    w = std::min(w, frameW_ - x0);
    h = std::min(h, frameH_ - y0);
    if (w <= 0 || h <= 0) return;
    // ASS_Image alpha is inverted for VSFilter compatibility: 0 = opaque, 255 = clear.
    const int ca = 255 - (int)(img->color & 0xFF);
    if (ca <= 0) return;
    const int cr = (int)((img->color >> 24) & 0xFF);
    const int cg = (int)((img->color >> 16) & 0xFF);
    const int cb = (int)((img->color >> 8) & 0xFF);
    for (int y = 0; y < h; ++y) {
        const uint8_t* mask = img->bitmap + (size_t)(sy + y) * img->stride + sx;
        uint8_t* row = pixels_.data() + ((size_t)(y0 + y) * frameW_ + x0) * 4;
        for (int x = 0; x < w; ++x) {
            const int sa = mask[x] * ca / 255;
            if (sa == 0) continue;
            const int ia = 255 - sa;
            uint8_t* dst = row + x * 4;
            dst[0] = (uint8_t)((cb * sa + dst[0] * ia + 127) / 255);
            dst[1] = (uint8_t)((cg * sa + dst[1] * ia + 127) / 255);
            dst[2] = (uint8_t)((cr * sa + dst[2] * ia + 127) / 255);
            dst[3] = (uint8_t)(sa + (dst[3] * ia + 127) / 255);
        }
    }
}

bool AssRenderer::render(double time) {
    if (!renderer_ || !track_ || frameW_ <= 0 || frameH_ <= 0) return false;
    int detect = 0;
    ASS_Image* images =
        fnRenderFrame_(renderer_, track_, (long long)llround(std::max(0.0, time) * 1000.0), &detect);
    if (!images) {
        if (rendered_ && empty_) return false;
        clearPixels();
        rendered_ = true;
        empty_ = true;
        serial_++;
        return true;
    }
    if (rendered_ && !empty_ && detect == 0) return false;
    clearPixels();
    wchar_t flag[8]{};
    const bool dbg = GetEnvironmentVariableW(L"PRISM_DUMP_ASS", flag, 8) != 0;
    if (dbg) {
        int n = 0;
        for (const ASS_Image* im = images; im && n < 6; im = im->next, ++n)
            Log("libass: img%d %dx%d stride=%d dst=(%d,%d) color=%08X type=%d", n, im->w, im->h, im->stride,
                im->dst_x, im->dst_y, im->color, im->type);
    }
    for (const ASS_Image* img = images; img; img = img->next) composite(img);
    rendered_ = true;
    empty_ = false;
    serial_++;
    return true;
}
