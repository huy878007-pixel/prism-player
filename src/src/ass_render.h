// Native ASS/SSA rendering through libass, loaded at runtime from libass-9.dll.
// The engine reconstructs the ASS track (header + Matroska-style event chunks) and
// this class turns them into a premultiplied BGRA bitmap for the D2D subtitle layer.
#pragma once

#include "common.h"
#include "engine.h"

#include <ass/ass.h>

class AssRenderer {
public:
    ~AssRenderer();

    // Loads libass-9.dll next to the exe and creates the library/renderer.
    // Returns false (and fills error()) when native ASS is unavailable.
    bool init();
    bool available() const { return renderer_ != nullptr; }
    const std::string& error() const { return error_; }
    const std::string& version() const { return version_; }

    // Rebuilds the ASS track from the engine's header + event chunks.
    bool setTrack(const std::shared_ptr<const AssTrackData>& data);
    // Render target size in pixels (the on-screen video rect, not the file resolution).
    void setFrameSize(int width, int height);
    // Renders the subtitle frame at `time` seconds. Returns true when the bitmap changed.
    bool render(double time);

    const uint8_t* pixels() const { return pixels_.data(); }
    int width() const { return frameW_; }
    int height() const { return frameH_; }
    int stride() const { return frameW_ * 4; }
    uint64_t serial() const { return serial_; }

private:
    template <typename T>
    bool resolve(T& fn, const char* name);
    void releaseTrack();
    void clearPixels();
    void composite(const ASS_Image* image);

    HMODULE dll_ = nullptr;
    ASS_Library* library_ = nullptr;
    ASS_Renderer* renderer_ = nullptr;
    ASS_Track* track_ = nullptr;
    std::shared_ptr<const AssTrackData> data_;

    std::vector<uint8_t> pixels_;
    int frameW_ = 0, frameH_ = 0;
    uint64_t serial_ = 0;
    bool rendered_ = false;  // libass frame at the current size has been composited
    bool empty_ = true;
    std::string version_;
    std::string error_;

    ASS_Library* (*fnLibraryInit_)(void) = nullptr;
    void (*fnLibraryDone_)(ASS_Library*) = nullptr;
    int (*fnLibraryVersion_)(void) = nullptr;
    void (*fnSetMessageCb_)(ASS_Library*, void (*)(int, const char*, va_list, void*), void*) = nullptr;
    ASS_Renderer* (*fnRendererInit_)(ASS_Library*) = nullptr;
    void (*fnRendererDone_)(ASS_Renderer*) = nullptr;
    ASS_Track* (*fnNewTrack_)(ASS_Library*) = nullptr;
    void (*fnFreeTrack_)(ASS_Track*) = nullptr;
    void (*fnProcessCodecPrivate_)(ASS_Track*, const char*, int) = nullptr;
    void (*fnProcessChunk_)(ASS_Track*, const char*, int, long long, long long) = nullptr;
    void (*fnSetFrameSize_)(ASS_Renderer*, int, int) = nullptr;
    void (*fnSetStorageSize_)(ASS_Renderer*, int, int) = nullptr;
    void (*fnSetMargins_)(ASS_Renderer*, int, int, int, int) = nullptr;
    void (*fnSetUseMargins_)(ASS_Renderer*, int) = nullptr;
    void (*fnSetPixelAspect_)(ASS_Renderer*, double) = nullptr;
    void (*fnSetFonts_)(ASS_Renderer*, const char*, const char*, int, const char*, int) = nullptr;
    ASS_Image* (*fnRenderFrame_)(ASS_Renderer*, ASS_Track*, long long, int*) = nullptr;
};
