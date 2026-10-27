// Playback engine: FFmpeg demux/decode, A/V clock, seeking, speed, frame queue.
#pragma once

#include "asm_c.h"
#include "audio.h"
#include "common.h"

// Subtitle rendering backend chosen per track. ASS/SSA goes through libass,
// bitmap formats (PGS/DVD/DVB) are identified but not rendered yet.
enum class SubtitleBackend { None, Text, Ass, Pgs };

struct SubtitleTrackInfo {
    int index = -1;
    std::wstring title;
    std::string lang;
    std::string codec;
    SubtitleBackend backend = SubtitleBackend::None;
    bool external = false;
    std::wstring path;
};

// One Matroska-style ASS event chunk (ReadOrder,Layer,Style,...,Text) with the
// container timestamps the header itself does not carry.
struct AssEvent {
    double start = 0;
    double duration = 0;
    std::string data;
};

struct AssTrackData {
    std::string header;  // codec private data: [Script Info] + styles + [Events] format
    std::vector<AssEvent> events;
};

struct StreamInfo {
    int index = -1;
    std::string codec;
    std::string profile;
    std::string pixFmt;
    std::string channelLayout;
    int width = 0, height = 0, bitDepth = 8;
    int rotation = 0;
    int sampleRate = 0, channels = 0;
    double fps = 0;
    int64_t bitRate = 0;
    bool hdr = false;
    bool fullRange = false;
};

struct AudioTrackInfo {
    int index = -1;
    std::string codec;
    std::string lang;
    std::string channelLayout;
    std::wstring title;
    int sampleRate = 0, channels = 0;
    int64_t bitRate = 0;
    bool external = false;
};

struct EngineStats {
    uint64_t frames = 0, dropped = 0, hwFrames = 0;
    double avgDecodeMs = 0;
    size_t queue = 0;
    bool hw = false;
    bool partial = false;
    bool buffering = false;
    double bufferedAhead = 0;
    uint64_t underruns = 0, starvations = 0;
    int audioBufferFrames = 0;
    int subtitleCues = 0;
    SubtitleBackend subtitleBackend = SubtitleBackend::None;
    int subtitleTrack = -1;
    int audioTracks = 0;
    int audioOrdinal = -1;
};

struct MediaInfo {
    std::wstring path;
    std::wstring filename;
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::string container;
    int64_t fileSize = 0;
    double duration = 0;
    bool hasVideo = false;
    bool hasAudio = false;
    StreamInfo video;
    StreamInfo audio;
    std::vector<AudioTrackInfo> audioTracks;
    int audioOrdinal = -1;
    std::vector<SubtitleTrackInfo> subtitles;
};

struct SubtitleCue {
    double start = 0;
    double end = 0;
    std::wstring text;
};

struct VideoFrame {
    std::vector<uint8_t> pixels;   // BGRA, SDR path
    std::vector<uint8_t> planes;   // 16-bit planar Y/U/V, HDR path
    int width = 0, height = 0, stride = 0, rotation = 0;
    int yStride = 0, uvStride = 0;
    bool hdr = false;
    double pts = 0;
    uint64_t serial = 0;
};

class Engine {
public:
    Engine();
    ~Engine();

    void setExclusiveAudio(bool on) { exclusiveAudio_ = on; }
    void setSoftwareDecode(bool on) { softwareDecode_ = on; }
    EngineStats stats();
    bool open(const std::wstring& path, std::string& error);
    bool open(const std::wstring& path, std::string& error, bool enableAudio);
    void close();
    bool externalAudio() const { return audioExtActive_; }
    const std::wstring& externalAudioPath() const { return externalAudioPath_; }

    const MediaInfo& info() const { return info_; }
    const std::wstring& path() const { return info_.path; }
    bool hasVideo() const { return info_.hasVideo; }
    bool hasAudio() const { return info_.hasAudio; }
    int audioTrack() const { return info_.audioOrdinal; }
    int audioTrackCount() const { return (int)info_.audioTracks.size(); }
    std::wstring audioTrackName() const;
    bool selectAudio(int ordinal);
    bool cycleAudio(int dir);
    bool audioDevice() const { return audioOk_; }
    double duration() const { return info_.duration; }

    double position();
    void play();
    void pause();
    void toggle();
    bool paused() const { return paused_.load(); }
    bool eof() const { return eof_.load(); }
    bool ended();
    bool buffering();
    bool partial() const { return partial_; }
    double bufferedAhead() const;

    void seek(double seconds);
    void setSpeed(double speed);
    double speed() const { return speed_.load(); }
    void setVolume(float v);
    float volume() const { return audio_.volume(); }

    uint64_t serial() const { return serial_.load(); }
    VideoFrame* acquireFrame();
    bool hasSubtitles() const;
    std::wstring subtitleAt(double t) const;
    int subtitleTrack() const;
    SubtitleBackend subtitleBackend() const;
    std::shared_ptr<const AssTrackData> assTrack() const;
    bool subtitlePending() const;
    bool loadSubtitleTrack(int trackIndex);
    bool selectSubtitle(int ordinal);
    bool loadExternalSubtitle(const std::wstring& path, std::string& error);
    bool cycleSubtitle(int dir);
    int loadSidecarSubtitles();
    bool addExternalSubtitle(const std::wstring& path, std::string& error, int& firstOrdinal);
    std::wstring subtitleName() const;
    void releaseFrame(VideoFrame* frame);

    float peak() const { return audio_.peak(); }
    const std::string& lastError() const { return error_; }
    std::wstring statusLine() const;

    // Diagnostics for --render/--verify: decode the next N frames synchronously.
    bool warmup(double seconds);

private:
    struct SubtitleJob {
        int ordinal = -1;
        SubtitleTrackInfo track;
        std::wstring mediaPath;
        double startOffset = 0;
        uint64_t serial = 0;
    };
    struct SubtitleDecodeResult {
        std::vector<SubtitleCue> cues;
        std::shared_ptr<AssTrackData> ass;
        bool opened = false;
    };

    void worker();
    bool openVideoDecoder(bool withHw);
    void fallbackToSoftware(const char* reason);
    bool hwDecoding() const { return hwDevice_ != nullptr && hwFormat_.load() == AV_PIX_FMT_D3D11; }
    bool hwTrying() const {
        if (!hwDevice_) return false;
        const int fmt = hwFormat_.load();
        return fmt == AV_PIX_FMT_NONE || fmt == AV_PIX_FMT_D3D11;
    }
    static AVPixelFormat selectFormat(AVCodecContext* ctx, const AVPixelFormat* fmt);
    void subtitleLoop();
    bool makeSubtitleJob(int trackIndex, SubtitleJob& job) const;
    SubtitleDecodeResult decodeSubtitleTrack(const SubtitleJob& job, const std::atomic<bool>* abort) const;
    bool requestSubtitleTrack(int trackIndex);
    bool loadSubtitleTrackNow(int trackIndex);
    void doSeek();
    bool decodeVideo(const AVPacket* pkt);
    bool decodeAudio(const AVPacket* pkt);
    bool openExternalAudio(const std::wstring& path);
    void audioWorker();
    AVCodecContext* openAudioCodec(AVFormatContext* ctx, int idx);
    void drainDecoders();
    bool convertFrame(AVFrame* src, VideoFrame* dst);
    VideoFrame* allocFrame(int w, int h);
    void recycleFrame(VideoFrame* f);
    void pushFrame(VideoFrame* f);
    size_t queueSize();
    void clearQueue();
    bool rebuildSwr();
    double qpcNow() const;
    void updateStartOffset();
    static std::string avError(int err);

    MediaInfo info_;
    AVFormatContext* fmt_ = nullptr;
    AVIOContext* avio_ = nullptr;
    void* io_ = nullptr;
    bool partial_ = false;
    std::atomic<bool> waitingData_{false};
    std::atomic<bool> opening_{false};
    AVFormatContext* afmt_ = nullptr;             // external audio container (.mka/.flac/...)
    int audioExtIdx_ = -1;
    std::thread audioThread_;
    mutable std::mutex audioMu_;
    int audioOrdinal_ = -1;
    std::atomic<bool> audioExtActive_{false};
    std::atomic<bool> audioExtSeek_{false};
    std::atomic<double> audioExtSeekTime_{0.0};
    double audioStartOffset_ = 0.0;
    std::wstring externalAudioPath_;
    AVCodecContext* vctx_ = nullptr;
    AVCodecContext* actx_ = nullptr;
    SwsContext* sws_ = nullptr;
    SwrContext* swr_ = nullptr;
    AVBufferRef* hwDevice_ = nullptr;          // D3D11VA device, shared by vctx_
    std::atomic<int> hwFormat_{AV_PIX_FMT_NONE};  // format get_format picked last
    std::atomic<bool> hwLogged_{false};
    bool hwFailed_ = false;                   // runtime fallback happened for this file
    uint64_t hwFrames_ = 0;
    int videoIdx_ = -1;
    int audioIdx_ = -1;
    double startOffset_ = 0.0;
    double frameDuration_ = 1.0 / 30.0;

    AudioOut audio_;
    bool audioOk_ = false;
    bool exclusiveAudio_ = false;
    bool softwareDecode_ = false;

    std::thread worker_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> paused_{true};
    std::atomic<bool> eof_{false};
    std::atomic<bool> seekPending_{false};
    std::atomic<double> pendingSeek_{0.0};
    std::atomic<double> speed_{1.0};
    std::atomic<uint64_t> serial_{0};
    std::atomic<double> lastFrameTime_{0.0};

    double baseMedia_ = 0.0;
    double pausedAt_ = 0.0;
    double baseQpc_ = 0.0;
    double dropVideoUntil_ = -1.0;
    double dropAudioUntil_ = -1.0;
    double lastVideoPts_ = 0.0;
    double lastAudioPts_ = 0.0;
    double lastAudioTime_ = 0.0;
    bool audioWasActive_ = false;
    double appliedSpeed_ = 1.0;
    std::atomic<uint64_t> speedSerial_{0};
    std::atomic<uint64_t> appliedSerial_{0};

    std::mutex queueMu_;
    std::condition_variable queueCv_;
    std::deque<VideoFrame*> queue_;
    std::mutex poolMu_;
    std::vector<std::unique_ptr<VideoFrame>> pool_;
    std::vector<VideoFrame*> free_;
    std::atomic<double> lastPushQpc_{0.0};

    std::vector<SubtitleCue> subtitleCues_;
    std::shared_ptr<const AssTrackData> assTrack_;
    int subtitleTrack_ = -1;
    std::thread subtitleThread_;
    mutable std::mutex subtitleMu_;
    std::condition_variable subtitleCv_;
    SubtitleJob subtitleJob_;
    bool subtitleHasJob_ = false;
    bool subtitleLoading_ = false;
    bool subtitleStop_ = false;
    bool subtitlesAsync_ = false;
    uint64_t subtitleSerial_ = 0;
    std::atomic<bool> subtitleAbort_{false};
    bool videoStarted_ = false;
    uint64_t framesDecoded_ = 0;
    uint64_t framesDropped_ = 0;
    double decodeMs_ = 0.0;
    std::vector<uint8_t> convScratch_;
    std::vector<float> audioScratch_;
    std::string error_;
};
