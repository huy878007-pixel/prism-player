// WASAPI shared-mode audio output with a lock-protected float ring buffer and a
// media-time clock derived from the device playhead.
#pragma once

#include "common.h"

#include <audioclient.h>
#include <limits>
#include <mmdeviceapi.h>

class AudioOut {
public:
    ~AudioOut();

    bool open(bool exclusive = false, int preferredRate = 0, int preferredChannels = 0);
    void close();
    bool ok() const { return client_ && render_; }
    int rate() const { return rate_; }
    int channels() const { return channels_; }

    void play();
    void pause();
    // Discards queued audio and resets the stream. keepPlaying re-arms the output
    // so it starts again automatically once enough audio has been buffered.
    void flush(bool keepPlaying);

    // Producer side. Blocks while the ring is full. Returns false when aborted.
    bool write(const float* interleaved, int frames, double pts, double secondsPerFrame);
    void setAbort(bool abort);

    // Consumer side.
    double mediaTime() const;  // NaN until the device has started presenting data
    bool active() const;
    float peak() const;
    uint64_t underruns() const;
    uint64_t starvations() const;
    int bufferedFrames() const;

    void setVolume(float v) { targetVolume_.store(std::clamp(v, 0.f, 2.f)); }
    float volume() const { return targetVolume_.load(); }

private:
    void threadProc();
    bool readRing(float* dst, int frames, uint32_t& got, uint32_t& underrun);
    void configureConversion(const WAVEFORMATEX* fmt);
    void updateClock(uint64_t playedFrames);
    void convertAndCopy(float* src, UINT32 frames, BYTE* dst, UINT32 dstBytes);
    void primeSilenceLocked();

    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* client_ = nullptr;
    IAudioRenderClient* render_ = nullptr;
    HANDLE event_ = nullptr;
    std::thread thread_;
    WAVEFORMATEX* mix_ = nullptr;
    UINT32 bufferFrames_ = 0;
    int rate_ = 48000;
    int channels_ = 2;
    int bytesPerSample_ = 4;
    int formatTag_ = 0;  // 1 = float32, 2 = s16, 3 = s32, 4 = s24
    int primeFrames_ = 4800;
    int fadeTotal_ = 0;
    int fadePos_ = 0;

    mutable std::mutex mu_;  // ring buffer + anchors
    std::mutex apiMu_;    // serializes WASAPI Start/Stop/Reset/GetBuffer
    std::condition_variable cv_;
    std::vector<float> ring_;
    std::vector<float> scratch_;
    size_t ringFrames_ = 0;
    size_t readPos_ = 0;
    size_t writePos_ = 0;
    int buffered_ = 0;
    uint64_t totalWritten_ = 0;

    struct Anchor {
        uint64_t start;
        double pts;
        double secPerFrame;
        uint32_t frames;
    };
    std::deque<Anchor> anchors_;
    uint64_t framesReleased_ = 0;
    uint64_t played_ = 0;
    uint32_t padding_ = 0;
    uint64_t underruns_ = 0;
    uint64_t starvations_ = 0;

    std::atomic<double> clock_{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<bool> playing_{false};
    std::atomic<bool> wantPlay_{false};
    std::atomic<bool> quit_{false};
    std::atomic<bool> abort_{false};
    std::atomic<bool> active_{false};
    std::atomic<float> peak_{0.f};
    std::atomic<float> targetVolume_{1.f};
    float currentVolume_ = 1.f;
    bool dumpEnabled_ = false;
    size_t dumpLimit_ = 0;
    std::vector<float> dump_;
};
