#include "audio.h"

#include <ksmedia.h>
#include <propsys.h>

#include <algorithm>
#include <limits>

namespace {
constexpr UINT32 kBufferMs = 40;
constexpr size_t kRingSeconds = 4;

bool IsFloat(const WAVEFORMATEX* f) {
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (f->wFormatTag != WAVE_FORMAT_EXTENSIBLE) return false;
    const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
    return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
}
void WriteFloatWav(const std::wstring& path, const std::vector<float>& data, int channels, int rate);
}  // namespace

AudioOut::~AudioOut() { close(); }

bool AudioOut::open(bool exclusive, int preferredRate, int preferredChannels) {
    close();
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_));
    if (FAILED(hr)) {
        Log("audio: CoCreateInstance(MMDeviceEnumerator) 0x%08X", (unsigned)hr);
        return false;
    }
    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(hr)) {
        Log("audio: no default render device 0x%08X", (unsigned)hr);
        return false;
    }
    auto activate = [&]() -> bool {
        if (client_) {
            client_->Release();
            client_ = nullptr;
        }
        return SUCCEEDED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client_));
    };
    if (!activate()) {
        Log("audio: Activate(IAudioClient) failed");
        return false;
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_) return false;

    bool ready = false;
    bool usedExclusive = false;
    if (exclusive) {
        int rates[3] = {preferredRate > 0 ? preferredRate : 48000, 48000, 44100};
        int chans[2] = {preferredChannels > 0 ? preferredChannels : 2, 2};
        for (int ri = 0; ri < 3 && !ready; ++ri) {
            for (int ci = 0; ci < 2 && !ready; ++ci) {
                int ch = chans[ci];
                if (ch != 1 && ch != 2 && ch != 6) ch = 2;
                for (int fi = 0; fi < 3 && !ready; ++fi) {
                    int bits = fi == 0 ? 32 : (fi == 1 ? 24 : 16);
                    WAVEFORMATEXTENSIBLE wf{};
                    wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
                    wf.Format.nChannels = (WORD)ch;
                    wf.Format.nSamplesPerSec = (DWORD)rates[ri];
                    wf.Format.wBitsPerSample = (WORD)bits;
                    wf.Format.nBlockAlign = (WORD)(ch * bits / 8);
                    wf.Format.nAvgBytesPerSec = (DWORD)(rates[ri] * wf.Format.nBlockAlign);
                    wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
                    wf.Samples.wValidBitsPerSample = (WORD)bits;
                    wf.dwChannelMask = ch == 6
                                           ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
                                              SPEAKER_LOW_FREQUENCY | SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT)
                                           : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
                    wf.SubFormat = bits == 32 ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
                    HRESULT s = client_->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wf.Format, nullptr);
                    if (s != S_OK) continue;
                    hr = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 1000000,
                                             0, &wf.Format, nullptr);
                    if (FAILED(hr)) {
                        if (!activate()) break;
                        continue;
                    }
                    auto* copy = (WAVEFORMATEXTENSIBLE*)CoTaskMemAlloc(sizeof(wf));
                    if (!copy) break;
                    *copy = wf;
                    mix_ = &copy->Format;
                    ready = true;
                    usedExclusive = true;
                    Log("audio: exclusive format %d Hz %d ch %d-bit", rates[ri], ch, bits);
                }
            }
        }
        if (!ready) Log("audio: exclusive mode unavailable, falling back to shared");
    }

    if (!ready) {
        if (mix_) {
            CoTaskMemFree(mix_);
            mix_ = nullptr;
        }
        hr = client_->GetMixFormat(&mix_);
        if (FAILED(hr) || !mix_) {
            Log("audio: GetMixFormat 0x%08X", (unsigned)hr);
            return false;
        }
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, kBufferMs * 10000, 0,
                                 mix_, nullptr);
        if (FAILED(hr)) {
            Log("audio: Initialize(shared) 0x%08X", (unsigned)hr);
            return false;
        }
        Log("audio: shared format %u Hz %u ch %u-bit", mix_->nSamplesPerSec, mix_->nChannels, mix_->wBitsPerSample);
    }

    rate_ = (int)mix_->nSamplesPerSec;
    channels_ = mix_->nChannels;
    bytesPerSample_ = mix_->wBitsPerSample / 8;
    formatTag_ = 0;
    const bool isFloat = IsFloat(mix_);
    if (isFloat && mix_->wBitsPerSample == 32) formatTag_ = 1;
    else if (!isFloat && mix_->wBitsPerSample == 16) formatTag_ = 2;
    else if (!isFloat && mix_->wBitsPerSample == 32) formatTag_ = 3;
    else if (!isFloat && mix_->wBitsPerSample == 24) formatTag_ = 4;
    if (!formatTag_) {
        Log("audio: unsupported format tag=%u bits=%u", mix_->wFormatTag, mix_->wBitsPerSample);
        return false;
    }
    hr = client_->SetEventHandle(event_);
    if (FAILED(hr)) {
        Log("audio: SetEventHandle 0x%08X", (unsigned)hr);
        return false;
    }
    hr = client_->GetBufferSize(&bufferFrames_);
    if (FAILED(hr)) return false;
    hr = client_->GetService(IID_PPV_ARGS(&render_));
    if (FAILED(hr) || !render_) return false;

    ringFrames_ = (size_t)rate_ * kRingSeconds;
    ring_.assign(ringFrames_ * (size_t)channels_, 0.f);
    scratch_.assign((size_t)bufferFrames_ * channels_, 0.f);
    buffered_ = 0;
    readPos_ = writePos_ = 0;
    totalWritten_ = 0;
    framesReleased_ = 0;
    played_ = 0;
    padding_ = 0;
    underruns_ = 0;
    starvations_ = 0;
    anchors_.clear();
    clock_.store(std::numeric_limits<double>::quiet_NaN());
    quit_.store(false);
    abort_.store(false);
    playing_.store(false);
    wantPlay_.store(false);
    active_.store(false);
    primeFrames_ = std::max(1024, rate_ / 10);
    fadeTotal_ = std::max(1, rate_ / 100);
    fadePos_ = fadeTotal_;
    currentVolume_ = targetVolume_.load();

    wchar_t dump[8]{};
    dumpEnabled_ = GetEnvironmentVariableW(L"PRISM_AUDIO_DUMP", dump, 8) > 0;
    dumpLimit_ = (size_t)rate_ * 5;
    dump_.clear();
    dump_.reserve(dumpLimit_ * (size_t)channels_);

    primeSilenceLocked();
    thread_ = std::thread([this] { threadProc(); });
    Log("audio: opened %d Hz, %d ch, %u-frame buffer, format %d, prime %d, exclusive=%d", rate_, channels_,
        bufferFrames_, formatTag_, primeFrames_, usedExclusive ? 1 : 0);
    return true;
}

void AudioOut::primeSilenceLocked() {
    if (!render_ || bufferFrames_ == 0) return;
    BYTE* data = nullptr;
    if (FAILED(render_->GetBuffer(bufferFrames_, &data))) return;
    render_->ReleaseBuffer(bufferFrames_, AUDCLNT_BUFFERFLAGS_SILENT);
}

void AudioOut::close() {
    {
        std::lock_guard<std::mutex> api(apiMu_);
        playing_.store(false);
        wantPlay_.store(false);
        if (client_) client_->Stop();
    }
    quit_.store(true);
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (event_) {
        CloseHandle(event_);
        event_ = nullptr;
    }
    render_ = nullptr;
    client_ = nullptr;
    if (mix_) {
        CoTaskMemFree(mix_);
        mix_ = nullptr;
    }
    device_ = nullptr;
    enumerator_ = nullptr;
    {
        std::lock_guard<std::mutex> lk(mu_);
        ring_.clear();
        ringFrames_ = 0;
        buffered_ = 0;
        anchors_.clear();
    }
    if (dumpEnabled_ && !dump_.empty()) {
        std::wstring path = DataDir() + L"\\audio-dump.wav";
        WriteFloatWav(path, dump_, channels_, rate_);
        Log("audio: dumped %zu samples to %ls", dump_.size(), path.c_str());
    }
    dump_.clear();
    active_.store(false);
    clock_.store(std::numeric_limits<double>::quiet_NaN());
}

void AudioOut::play() {
    if (!client_) return;
    wantPlay_.store(true);
    fadePos_ = 0;
    fadeTotal_ = std::max(1, rate_ / 100);
}

void AudioOut::pause() {
    if (!client_) return;
    std::lock_guard<std::mutex> api(apiMu_);
    playing_.store(false);
    wantPlay_.store(false);
    client_->Stop();
    active_.store(false);
}

void AudioOut::flush(bool keepPlaying) {
    {
        std::lock_guard<std::mutex> api(apiMu_);
        playing_.store(false);
        wantPlay_.store(keepPlaying);
        if (client_) {
            client_->Stop();
            client_->Reset();
            primeSilenceLocked();
        }
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        buffered_ = 0;
        readPos_ = writePos_ = 0;
        totalWritten_ = 0;
        framesReleased_ = 0;
        played_ = 0;
        padding_ = 0;
        anchors_.clear();
    }
    clock_.store(std::numeric_limits<double>::quiet_NaN());
    active_.store(false);
    fadePos_ = 0;
    fadeTotal_ = std::max(1, rate_ / 100);
    cv_.notify_all();
}

bool AudioOut::write(const float* interleaved, int frames, double pts, double secondsPerFrame) {
    if (!client_ || frames <= 0) return false;
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [&] {
        return quit_.load() || abort_.load() || (size_t)buffered_ + frames <= ringFrames_;
    });
    if (quit_.load() || abort_.load()) return false;
    if (buffered_ < 0) buffered_ = 0;

    size_t pos = writePos_;
    size_t remaining = (size_t)frames;
    const float* src = interleaved;
    size_t ch = (size_t)channels_;
    while (remaining > 0) {
        size_t untilEnd = ringFrames_ - pos;
        size_t n = std::min(remaining, untilEnd);
        memcpy(&ring_[pos * ch], src, n * ch * sizeof(float));
        pos = (pos + n) % ringFrames_;
        src += n * ch;
        remaining -= n;
    }
    writePos_ = pos;
    anchors_.push_back({totalWritten_, pts, secondsPerFrame, (uint32_t)frames});
    totalWritten_ += frames;
    buffered_ += frames;
    lk.unlock();
    cv_.notify_all();
    return true;
}

void AudioOut::setAbort(bool abort) {
    abort_.store(abort);
    cv_.notify_all();
}

bool AudioOut::readRing(float* dst, int frames, uint32_t& got, uint32_t& underrun) {
    std::lock_guard<std::mutex> lk(mu_);
    int n = std::min(frames, buffered_);
    size_t pos = readPos_;
    size_t remaining = (size_t)n;
    size_t ch = (size_t)channels_;
    while (remaining > 0) {
        size_t untilEnd = ringFrames_ - pos;
        size_t take = std::min(remaining, untilEnd);
        memcpy(dst, &ring_[pos * ch], take * ch * sizeof(float));
        pos = (pos + take) % ringFrames_;
        dst += take * ch;
        remaining -= take;
    }
    readPos_ = pos;
    buffered_ -= n;
    got = (uint32_t)n;
    underrun = (n < frames) ? (uint32_t)(frames - n) : 0;
    if (n > 0) cv_.notify_all();
    return n > 0;
}

void AudioOut::updateClock(uint64_t playedFrames) {
    std::lock_guard<std::mutex> lk(mu_);
    played_ = playedFrames;
    if (anchors_.empty()) return;
    while (anchors_.size() > 1 && anchors_[1].start <= playedFrames) anchors_.pop_front();
    const Anchor& a = anchors_.front();
    uint64_t offset = playedFrames > a.start ? playedFrames - a.start : 0;
    clock_.store(a.pts + (double)offset * a.secPerFrame);
    while (anchors_.size() > 2 && anchors_[1].start + 30000 < playedFrames) anchors_.pop_front();
}

void AudioOut::convertAndCopy(float* src, UINT32 frames, BYTE* dst, UINT32 dstBytes) {
    const float vol = targetVolume_.load();
    const int ch = channels_;
    float gain = currentVolume_;
    const float step = frames > 0 ? (vol - gain) / (float)frames : 0.f;
    float peak = peak_.load();
    const float fadeStep = fadeTotal_ > 0 ? 1.f / (float)fadeTotal_ : 1.f;
    int fade = fadePos_;
    auto shape = [&](UINT32 i, float& outFade) {
        outFade = 1.f;
        if (fade < fadeTotal_) {
            outFade = (float)fade * fadeStep;
            ++fade;
        }
    };
    switch (formatTag_) {
        case 1: {
            float* out = (float*)dst;
            for (UINT32 i = 0; i < frames; ++i) {
                float f;
                shape(i, f);
                float g = (gain + step * (float)i) * f;
                for (int c = 0; c < ch; ++c) {
                    float v = std::clamp(src[i * ch + c] * g, -1.f, 1.f);
                    float av = std::fabs(v);
                    if (av > peak) peak = av;
                    out[i * ch + c] = v;
                }
            }
            break;
        }
        case 2: {
            int16_t* out = (int16_t*)dst;
            for (UINT32 i = 0; i < frames; ++i) {
                float f;
                shape(i, f);
                float g = (gain + step * (float)i) * f;
                for (int c = 0; c < ch; ++c) {
                    float v = std::clamp(src[i * ch + c] * g, -1.f, 1.f);
                    float av = std::fabs(v);
                    if (av > peak) peak = av;
                    out[i * ch + c] = (int16_t)std::lrint(v * 32767.f);
                }
            }
            break;
        }
        case 3: {
            int32_t* out = (int32_t*)dst;
            for (UINT32 i = 0; i < frames; ++i) {
                float f;
                shape(i, f);
                float g = (gain + step * (float)i) * f;
                for (int c = 0; c < ch; ++c) {
                    float v = std::clamp(src[i * ch + c] * g, -1.f, 1.f);
                    float av = std::fabs(v);
                    if (av > peak) peak = av;
                    out[i * ch + c] = (int32_t)llround((double)v * 2147483647.0);
                }
            }
            break;
        }
        case 4: {
            uint8_t* out = dst;
            for (UINT32 i = 0; i < frames; ++i) {
                float f;
                shape(i, f);
                float g = (gain + step * (float)i) * f;
                for (int c = 0; c < ch; ++c) {
                    float v = std::clamp(src[i * ch + c] * g, -1.f, 1.f);
                    float av = std::fabs(v);
                    if (av > peak) peak = av;
                    int32_t s = (int32_t)llround((double)v * 8388607.0);
                    out[(i * ch + c) * 3 + 0] = (uint8_t)(s & 0xFF);
                    out[(i * ch + c) * 3 + 1] = (uint8_t)((s >> 8) & 0xFF);
                    out[(i * ch + c) * 3 + 2] = (uint8_t)((s >> 16) & 0xFF);
                }
            }
            break;
        }
        default:
            memset(dst, 0, dstBytes);
            break;
    }
    if (dumpEnabled_ && formatTag_ == 1 && dump_.size() < dumpLimit_ * (size_t)channels_) {
        size_t room = dumpLimit_ * (size_t)channels_ - dump_.size();
        size_t n = std::min(room, (size_t)frames * (size_t)channels_);
        dump_.insert(dump_.end(), (const float*)dst, (const float*)dst + n);
    }
    fadePos_ = fade;
    currentVolume_ = vol;
    peak_.store(peak);
}

void AudioOut::threadProc() {
    while (!quit_.load()) {
        if (wantPlay_.load() && !playing_.load()) {
            bool ready = false;
            {
                std::lock_guard<std::mutex> api(apiMu_);
                ready = wantPlay_.load() && !playing_.load() && bufferedFrames() >= primeFrames_;
                if (ready && client_) {
                    fadePos_ = 0;
                    fadeTotal_ = std::max(1, rate_ / 100);
                    if (SUCCEEDED(client_->Start())) {
                        playing_.store(true);
                        active_.store(true);
                        Log("audio: started, %d frames buffered", bufferedFrames());
                    }
                }
            }
            if (ready) continue;
        }
        if (!playing_.load()) {
            WaitForSingleObject(event_, 20);
            continue;
        }
        DWORD wait = WaitForSingleObject(event_, 100);
        if (quit_.load()) break;
        if (wait != WAIT_OBJECT_0) continue;

        std::lock_guard<std::mutex> api(apiMu_);
        if (!playing_.load() || !client_) continue;
        UINT32 padding = 0;
        if (FAILED(client_->GetCurrentPadding(&padding))) continue;
        padding_ = padding;
        if (framesReleased_ >= padding) {
            updateClock(framesReleased_ - padding);
            active_.store(true);
        }
        UINT32 avail = bufferFrames_ - padding;
        if (avail == 0) continue;

        int buffered = bufferedFrames();
        if (buffered <= 0) {
            // Underrun: stop the endpoint instead of pushing silence (which
            // would desync the media clock), then restart once primed.
            client_->Stop();
            playing_.store(false);
            active_.store(false);
            ++starvations_;
            Log("audio: starve (released=%llu padding=%u)", (unsigned long long)framesReleased_, padding);
            continue;
        }

        UINT32 want = std::min<UINT32>(avail, (UINT32)std::max(0, buffered));
        BYTE* data = nullptr;
        if (FAILED(render_->GetBuffer(want, &data))) continue;
        uint32_t got = 0, under = 0;
        readRing(scratch_.data(), (int)want, got, under);
        convertAndCopy(scratch_.data(), got, data, want * bytesPerSample_ * channels_);
        render_->ReleaseBuffer(got, 0);
        framesReleased_ += got;
        if (under) underruns_ += under;
    }
    active_.store(false);
}

namespace {
void WriteFloatWav(const std::wstring& path, const std::vector<float>& data, int channels, int rate) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
    const uint32_t dataBytes = (uint32_t)(data.size() * sizeof(float));
    const uint16_t ch = (uint16_t)channels;
    const uint16_t bits = 32;
    const uint32_t byteRate = (uint32_t)rate * ch * bits / 8;
    const uint16_t blockAlign = (uint16_t)(ch * bits / 8);
    fwrite("RIFF", 1, 4, f);
    uint32_t riffSize = 36 + dataBytes;
    fwrite(&riffSize, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f);
    uint16_t format = 3;  // IEEE float
    fwrite(&format, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    uint32_t r = (uint32_t)rate;
    fwrite(&r, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataBytes, 4, 1, f);
    fwrite(data.data(), 1, dataBytes, f);
    fclose(f);
}
}  // namespace

double AudioOut::mediaTime() const { return clock_.load(); }

bool AudioOut::active() const {
    return active_.load() && playing_.load() && !std::isnan(clock_.load());
}

float AudioOut::peak() const { return peak_.load(); }

uint64_t AudioOut::underruns() const { return underruns_; }

uint64_t AudioOut::starvations() const { return starvations_; }

int AudioOut::bufferedFrames() const {
    std::lock_guard<std::mutex> lk(mu_);
    return buffered_;
}
