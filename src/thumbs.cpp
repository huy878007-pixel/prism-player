#include "thumbs.h"

#include <shobjidl.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {

constexpr int kThumbW = 160;
constexpr int kThumbH = 90;
// SIIGBF_BIGGERSIZEOK may hand back a shell-cache image larger than requested.
// Cap it at 2x the requested size so the cache stays a few MB, not hundreds.
constexpr int kMaxW = 320;
constexpr int kMaxH = 180;
constexpr size_t kMaxEntries = 64;

std::wstring FileKey(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return path + L"\x1f?";
    const uint64_t size = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    const uint64_t mtime =
        ((uint64_t)fad.ftLastWriteTime.dwHighDateTime << 32) | fad.ftLastWriteTime.dwLowDateTime;
    return path + L'\x1f' + std::to_wstring(size) + L'\x1f' + std::to_wstring(mtime);
}

void Downscale(std::vector<uint8_t>& px, int& w, int& h) {
    if (w <= kMaxW && h <= kMaxH) return;
    const float scale = std::min((float)kMaxW / (float)w, (float)kMaxH / (float)h);
    const int nw = std::max(1, (int)std::lround(w * scale));
    const int nh = std::max(1, (int)std::lround(h * scale));
    std::vector<uint8_t> dst((size_t)nw * nh * 4);
    for (int y = 0; y < nh; ++y) {
        float sy = (y + 0.5f) * (float)h / (float)nh - 0.5f;
        sy = std::clamp(sy, 0.f, (float)(h - 1));
        const int y0 = (int)sy, y1 = std::min(y0 + 1, h - 1);
        const float fy = sy - (float)y0;
        for (int x = 0; x < nw; ++x) {
            float sx = (x + 0.5f) * (float)w / (float)nw - 0.5f;
            sx = std::clamp(sx, 0.f, (float)(w - 1));
            const int x0 = (int)sx, x1 = std::min(x0 + 1, w - 1);
            const float fx = sx - (float)x0;
            const uint8_t* p00 = &px[((size_t)y0 * w + x0) * 4];
            const uint8_t* p10 = &px[((size_t)y0 * w + x1) * 4];
            const uint8_t* p01 = &px[((size_t)y1 * w + x0) * 4];
            const uint8_t* p11 = &px[((size_t)y1 * w + x1) * 4];
            uint8_t* out = &dst[((size_t)y * nw + x) * 4];
            for (int c = 0; c < 4; ++c) {
                const float top = p00[c] + (p10[c] - p00[c]) * fx;
                const float bot = p01[c] + (p11[c] - p01[c]) * fx;
                out[c] = (uint8_t)std::lround(top + (bot - top) * fy);
            }
        }
    }
    px.swap(dst);
    w = nw;
    h = nh;
}

std::shared_ptr<const std::vector<uint8_t>> BitmapToBgra(HBITMAP hbmp, int& width, int& height) {
    BITMAP bm{};
    if (!hbmp || GetObjectW(hbmp, sizeof(bm), &bm) <= 0 || bm.bmWidth <= 0) return nullptr;
    width = bm.bmWidth;
    height = bm.bmHeight < 0 ? -bm.bmHeight : bm.bmHeight;
    if (width <= 0 || height <= 0) return nullptr;

    std::vector<uint8_t> px((size_t)width * height * 4);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;  // top-down, matching Direct2D
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bi.bmiHeader.biSizeImage = (DWORD)px.size();
    HDC dc = GetDC(nullptr);
    const int rows = dc ? GetDIBits(dc, hbmp, 0, (UINT)height, px.data(), &bi, DIB_RGB_COLORS) : 0;
    if (dc) ReleaseDC(nullptr, dc);
    if (rows != height) return nullptr;

    // Shell thumbnails are usually opaque DDBs whose alpha comes back zero.
    // Icons carry real alpha; premultiply only if the pixels look straight.
    bool anyAlpha = false, straight = false;
    for (size_t i = 0; i < (size_t)width * height; ++i) {
        const uint8_t a = px[i * 4 + 3];
        if (!a) continue;
        anyAlpha = true;
        if (px[i * 4] > a + 1 || px[i * 4 + 1] > a + 1 || px[i * 4 + 2] > a + 1) straight = true;
    }
    if (!anyAlpha) {
        for (size_t i = 0; i < (size_t)width * height; ++i) px[i * 4 + 3] = 255;
    } else if (straight) {
        for (size_t i = 0; i < (size_t)width * height; ++i) {
            const uint32_t a = px[i * 4 + 3];
            for (int c = 0; c < 3; ++c) px[i * 4 + c] = (uint8_t)((px[i * 4 + c] * a + 127) / 255);
        }
    }
    Downscale(px, width, height);
    return std::make_shared<const std::vector<uint8_t>>(std::move(px));
}

std::shared_ptr<const std::vector<uint8_t>> LoadShellImage(const std::wstring& path, int& width, int& height,
                                                           bool allowIcon = true) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))) || !item) return nullptr;
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(item.As(&factory)) || !factory) return nullptr;

    SIZE size{kThumbW, kThumbH};
    const SIIGBF modes[] = {(SIIGBF)(SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY), SIIGBF_ICONONLY};
    const int modeCount = allowIcon ? 2 : 1;
    for (int modeIndex = 0; modeIndex < modeCount; ++modeIndex) {
        SIIGBF mode = modes[modeIndex];
        HBITMAP hbmp = nullptr;
        HRESULT hr = factory->GetImage(size, mode, &hbmp);
        if (FAILED(hr) || !hbmp) {
            if (hbmp) DeleteObject(hbmp);
            continue;
        }
        auto bgra = BitmapToBgra(hbmp, width, height);
        DeleteObject(hbmp);
        if (bgra) return bgra;
    }
    return nullptr;
}


std::shared_ptr<const std::vector<uint8_t>> LoadFfmpegImage(const std::wstring& path, int& width, int& height) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, Narrow(path).c_str(), nullptr, nullptr) < 0 || !fmt) return nullptr;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return nullptr;
    }
    int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) {
        avformat_close_input(&fmt);
        return nullptr;
    }
    AVStream* st = fmt->streams[idx];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext* ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0 || avcodec_open2(ctx, codec, nullptr) < 0) {
        if (ctx) avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return nullptr;
    }
    // Pick a representative frame instead of a potentially black first frame.
    int64_t ts = av_rescale_q(AV_TIME_BASE, AV_TIME_BASE_Q, st->time_base);
    av_seek_frame(fmt, idx, ts, AVSEEK_FLAG_BACKWARD);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool got = false;
    if (pkt && frame) {
        for (int i = 0; i < 240; ++i) {
            int r = av_read_frame(fmt, pkt);
            if (r == AVERROR_EOF) break;
            if (r < 0) continue;
            if (pkt->stream_index != idx) {
                av_packet_unref(pkt);
                continue;
            }
            if (avcodec_send_packet(ctx, pkt) >= 0) {
                if (avcodec_receive_frame(ctx, frame) >= 0) {
                    got = true;
                    av_packet_unref(pkt);
                    break;
                }
            }
            av_packet_unref(pkt);
        }
        if (!got) {
            avcodec_send_packet(ctx, nullptr);
            got = avcodec_receive_frame(ctx, frame) >= 0;
        }
    }
    std::shared_ptr<const std::vector<uint8_t>> out;
    if (got && frame->width > 0 && frame->height > 0) {
        SwsContext* sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                                         kThumbW, kThumbH, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws) {
            std::vector<uint8_t> px((size_t)kThumbW * kThumbH * 4, 255);
            uint8_t* dst[4] = {px.data(), nullptr, nullptr, nullptr};
            int dstStride[4] = {kThumbW * 4, 0, 0, 0};
            sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
            for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
            width = kThumbW;
            height = kThumbH;
            out = std::make_shared<const std::vector<uint8_t>>(std::move(px));
            sws_freeContext(sws);
        }
    }
    if (pkt) av_packet_free(&pkt);
    if (frame) av_frame_free(&frame);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return out;
}

}  // namespace

ThumbnailCache::~ThumbnailCache() { stop(); }

void ThumbnailCache::ensureStartedLocked() {
    if (started_ || quit_) return;
    started_ = true;
    thread_ = std::thread([this] { worker(); });
}

void ThumbnailCache::worker() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [&] { return quit_ || !jobs_.empty(); });
            if (quit_) break;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            ++inFlight_;
            auto want = wanted_.find(job.path);
            if (job.gen != gen_ || want == wanted_.end() || want->second != job.gen) {
                --inFlight_;
                if (job.gen == gen_) asked_.erase(job.path);
                continue;
            }
        }

        const std::wstring key = FileKey(job.path);
        std::shared_ptr<const std::vector<uint8_t>> bgra;
        int w = 0, h = 0;
        bool cached = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = cache_.find(job.path);
            if (it != cache_.end() && it->second.key == key) {
                bgra = it->second.bgra;
                w = it->second.width;
                h = it->second.height;
                cached = true;
            }
        }
        if (!cached) {
            bgra = LoadShellImage(job.path, w, h, false);
            if (!bgra) bgra = LoadFfmpegImage(job.path, w, h);
            if (!bgra) bgra = LoadShellImage(job.path, w, h, true);
            std::lock_guard<std::mutex> lock(mu_);
            cache_.erase(job.path);
            while (cache_.size() >= kMaxEntries && !cacheOrder_.empty()) {
                std::wstring old = std::move(cacheOrder_.front());
                cacheOrder_.pop_front();
                cache_.erase(old);
            }
            cache_.emplace(job.path, Entry{key, w, h, bgra});
            cacheOrder_.push_back(job.path);
        }

        {
            std::lock_guard<std::mutex> lock(mu_);
            --inFlight_;
            auto want = wanted_.find(job.path);
            if (job.gen == gen_ && want != wanted_.end() && want->second == job.gen) {
                done_.push_back(ThumbResult{job.path, key, w, h, bgra});
            } else if (job.gen == gen_) {
                asked_.erase(job.path);
            }
        }
    }
    if (SUCCEEDED(co)) CoUninitialize();
}

void ThumbnailCache::request(const std::vector<std::wstring>& paths) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        ensureStartedLocked();
        wanted_.clear();
        for (const auto& path : paths) wanted_[path] = gen_;
        for (const auto& path : paths) {
            if (asked_.insert(path).second) jobs_.push_back(Job{path, gen_});
        }
    }
    cv_.notify_all();
}

void ThumbnailCache::cancel() {
    std::lock_guard<std::mutex> lock(mu_);
    ++gen_;
    wanted_.clear();
    asked_.clear();
    jobs_.clear();
    done_.clear();
}

std::vector<ThumbResult> ThumbnailCache::poll() {
    std::vector<ThumbResult> out;
    std::lock_guard<std::mutex> lock(mu_);
    out.swap(done_);
    return out;
}

void ThumbnailCache::stop() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        quit_ = true;
        wanted_.clear();
        jobs_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    started_ = false;
}
