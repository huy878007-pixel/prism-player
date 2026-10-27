#include "engine.h"

#include <algorithm>
#include <filesystem>
#include <limits>

namespace {

double QpcSeconds() {
    static double freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return (double)f.QuadPart;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / freq;
}


struct GrowingIo {
    HANDLE file = INVALID_HANDLE_VALUE;
    std::atomic<bool>* quit = nullptr;
    std::atomic<bool>* seek = nullptr;
    std::atomic<bool>* waiting = nullptr;
    std::atomic<bool>* opening = nullptr;
    std::wstring path;
};

int GrowingRead(void* opaque, uint8_t* buf, int size) {
    auto* io = static_cast<GrowingIo*>(opaque);
    if (!io || io->file == INVALID_HANDLE_VALUE) return AVERROR(EIO);
    int waits = 0;
    for (;;) {
        if (io->quit->load()) return AVERROR_EXIT;
        if (io->seek->load()) return AVERROR(EAGAIN);
        DWORD got = 0;
        if (!ReadFile(io->file, buf, (DWORD)size, &got, nullptr)) return AVERROR(EIO);
        if (got > 0) { io->waiting->store(false); return (int)got; }
        io->waiting->store(true);
        if (GetFileAttributesW(io->path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            io->waiting->store(false);
            return AVERROR_EOF;
        }
        if (io->opening->load() && ++waits > 200) {
            io->waiting->store(false);
            return AVERROR_EOF;
        }
        Sleep(50);
    }
}

int64_t GrowingSeek(void* opaque, int64_t offset, int whence) {
    auto* io = static_cast<GrowingIo*>(opaque);
    if (!io || io->file == INVALID_HANDLE_VALUE) return -1;
    if (whence & AVSEEK_SIZE) {
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(io->file, &sz)) return -1;
        return sz.QuadPart;
    }
    whence &= ~AVSEEK_FORCE;
    DWORD method = 0;
    if (whence == SEEK_SET) method = FILE_BEGIN;
    else if (whence == SEEK_CUR) method = FILE_CURRENT;
    else if (whence == SEEK_END) method = FILE_END;
    else return -1;
    LARGE_INTEGER li{}; li.QuadPart = offset;
    if (!SetFilePointerEx(io->file, li, nullptr, method)) return -1;
    LARGE_INTEGER cur{};
    if (!SetFilePointerEx(io->file, LARGE_INTEGER{}, &cur, FILE_CURRENT)) return -1;
    return cur.QuadPart;
}

std::wstring ParseAssText(const char* ass) {
    std::string line = ass ? ass : "";
    std::vector<size_t> commas;
    for (size_t i = 0; i < line.size() && commas.size() < 10; ++i)
        if (line[i] == ',') commas.push_back(i);
    size_t textPos = 0;
    if (commas.size() >= 9) {
        // Full ASS dialogue: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text
        bool hasTime = line.find(':', commas[1]) != std::string::npos &&
                       line.find(':', commas[1]) < commas[2];
        if (hasTime) textPos = commas[8] + 1;
        else textPos = commas[7] + 1;  // ffmpeg's ReadOrder,Layer,Style,... prefix
    } else if (commas.size() >= 8) {
        textPos = commas[7] + 1;
    }
    std::string body = textPos ? line.substr(textPos) : line;
    std::string out;
    bool brace = false;
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '{') {
            brace = true;
            continue;
        }
        if (c == '}') {
            brace = false;
            continue;
        }
        if (brace) continue;
        if (c == '\\' && i + 1 < body.size()) {
            char n = body[i + 1];
            if (n == 'N' || n == 'n') {
                out += '\n';
                ++i;
                continue;
            }
            if (n == 'h') {
                out += ' ';
                ++i;
                continue;
            }
        }
        out += c;
    }
    return Widen(out);
}

bool IsTextSubtitle(const std::string& codec) {
    return codec == "subrip" || codec == "ass" || codec == "ssa" || codec == "mov_text" ||
           codec == "webvtt" || codec == "text";
}



AudioTrackInfo MakeAudioTrack(AVFormatContext* ctx, int idx, bool external) {
    AudioTrackInfo t;
    t.index = idx;
    t.external = external;
    if (!ctx || idx < 0 || idx >= (int)ctx->nb_streams) return t;
    AVStream* st = ctx->streams[idx];
    t.codec = avcodec_get_name(st->codecpar->codec_id);
    t.sampleRate = st->codecpar->sample_rate;
    t.channels = st->codecpar->ch_layout.nb_channels;
    t.bitRate = st->codecpar->bit_rate;
    if (st->codecpar->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) {
        char layout[64]{};
        av_channel_layout_describe(&st->codecpar->ch_layout, layout, sizeof(layout));
        t.channelLayout = layout;
    }
    if (AVDictionaryEntry* lang = av_dict_get(st->metadata, "language", nullptr, 0)) t.lang = lang->value;
    if (AVDictionaryEntry* title = av_dict_get(st->metadata, "title", nullptr, 0)) t.title = Widen(title->value);
    return t;
}


std::filesystem::path CleanMediaPath(const std::filesystem::path& p) {
    std::filesystem::path q = p;
    q.replace_filename(StripPartialName(p.filename().wstring()));
    return q;
}

std::wstring FindSidecarAudio(const std::wstring& media) {
    std::filesystem::path p = CleanMediaPath(std::filesystem::path(media));
    std::wstring stem = (p.parent_path() / p.stem()).wstring();
    static const wchar_t* exts[] = {L".mka", L".flac", L".wav",  L".aac", L".opus",
                                    L".m4a", L".ac3", L".dts", L".eac3", L".mp3"};
    std::error_code ec;
    for (const wchar_t* ext : exts) {
        std::wstring cand = stem + ext;
        if (std::filesystem::is_regular_file(cand, ec)) return cand;
    }
    // Episode.jpn.mka / Episode.eng.flac style suffixes.
    const std::wstring base = p.stem().wstring();
    for (const auto& e : std::filesystem::directory_iterator(p.parent_path(), ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        std::wstring name = e.path().filename().wstring();
        if (name.size() <= base.size() + 1) continue;
        if (_wcsnicmp(name.c_str(), base.c_str(), base.size()) != 0 || name[base.size()] != L'.') continue;
        std::wstring ext = e.path().extension().wstring();
        for (const wchar_t* want : exts)
            if (_wcsicmp(ext.c_str(), want) == 0) return e.path().wstring();
    }
    return {};
}

SubtitleBackend BackendOf(const std::string& codec) {
    if (codec == "ass" || codec == "ssa") return SubtitleBackend::Ass;
    if (IsTextSubtitle(codec)) return SubtitleBackend::Text;
    if (codec == "hdmv_pgs_subtitle" || codec == "dvd_subtitle" || codec == "dvb_subtitle" || codec == "xsub")
        return SubtitleBackend::Pgs;
    return SubtitleBackend::None;
}

// libass needs a complete script header. Matroska codec private data normally has
// one; fill in the missing pieces so ass_process_chunk can parse the events.
void EnsureAssHeader(std::string& header) {
    while (!header.empty() && header.back() == '\0') header.pop_back();
    if (header.find("[Script Info]") == std::string::npos)
        header.insert(0, "[Script Info]\nScriptType: v4.00+\n");
    const size_t events = header.find("[Events]");
    if (events == std::string::npos) {
        header += "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    } else if (header.find("Format:", events) == std::string::npos) {
        const size_t eol = header.find('\n', events);
        const char* fmt = "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
        if (eol == std::string::npos) header += std::string("\n") + fmt;
        else header.insert(eol + 1, fmt);
    }
    if (header.empty() || header.back() != '\n') header += '\n';
}

int SubtitleScore(const SubtitleTrackInfo& t) {
    if (!IsTextSubtitle(t.codec)) return -1;
    int score = 0;
    std::string lang = t.lang;
    for (char& c : lang)
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    std::wstring title = t.title;
    for (wchar_t& c : title)
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
    if (lang == "chi" || lang == "zho" || lang == "zh") score = 100;
    if (title.find(L"chinese") != std::wstring::npos) score = std::max(score, 90);
    if (title.find(L"simplified") != std::wstring::npos) score += 20;
    else if (title.find(L"traditional") != std::wstring::npos) score += 10;
    if (lang == "eng") score = std::max(score, 30);
    return score;
}

std::wstring DictGet(AVDictionary* dict, const char* key) {
    if (!dict) return {};
    AVDictionaryEntry* e = av_dict_get(dict, key, nullptr, 0);
    return e ? Widen(e->value) : std::wstring{};
}

const char* SpaceName(int space) {
    switch (space) {
        case AVCOL_SPC_BT709: return "BT.709";
        case AVCOL_SPC_BT470BG: return "BT.601";
        case AVCOL_SPC_SMPTE170M: return "SMPTE-170M";
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL: return "BT.2020";
        case AVCOL_SPC_SMPTE240M: return "SMPTE-240M";
        case AVCOL_SPC_FCC: return "FCC";
        default: return "未标记";
    }
}

}  // namespace

Engine::Engine() { avformat_network_init(); }

Engine::~Engine() { close(); }

std::string Engine::avError(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

double Engine::qpcNow() const { return QpcSeconds(); }



AVCodecContext* Engine::openAudioCodec(AVFormatContext* ctx, int idx) {
    if (!ctx || idx < 0 || idx >= (int)ctx->nb_streams) return nullptr;
    AVStream* st = ctx->streams[idx];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return nullptr;
    AVCodecContext* c = avcodec_alloc_context3(codec);
    if (!c) return nullptr;
    if (avcodec_parameters_to_context(c, st->codecpar) < 0 || avcodec_open2(c, codec, nullptr) < 0) {
        avcodec_free_context(&c);
        return nullptr;
    }
    c->pkt_timebase = st->time_base;
    c->thread_count = 0;
    return c;
}

std::wstring Engine::audioTrackName() const {
    if (info_.audioOrdinal < 0 || info_.audioOrdinal >= (int)info_.audioTracks.size()) return L"音轨";
    const AudioTrackInfo& t = info_.audioTracks[info_.audioOrdinal];
    if (!t.title.empty()) return t.title;
    std::wstring s;
    if (!t.lang.empty()) s = Widen(t.lang);
    if (s.empty()) s = L"音轨 " + std::to_wstring(info_.audioOrdinal + 1);
    if (t.channels > 0) s += L" · " + std::to_wstring(t.channels) + L"ch";
    return s;
}

bool Engine::selectAudio(int ordinal) {
    if (!fmt_ || ordinal < 0 || ordinal >= (int)info_.audioTracks.size()) return false;
    if (ordinal == info_.audioOrdinal) return true;
    const AudioTrackInfo& t = info_.audioTracks[ordinal];
    AVFormatContext* ctx = t.external ? afmt_ : fmt_;
    if (!ctx) return false;
    std::lock_guard<std::mutex> lk(audioMu_);
    AVCodecContext* next = openAudioCodec(ctx, t.index);
    if (!next) {
        Log("audio: cannot open track %d", ordinal);
        return false;
    }
    if (actx_) avcodec_free_context(&actx_);
    actx_ = next;
    if (t.external) audioExtIdx_ = t.index;
    else audioIdx_ = t.index;
    if (!rebuildSwr()) {
        Log("audio: swr rebuild failed for track %d", ordinal);
        return false;
    }
    audio_.flush(!paused_.load());
    audio_.setAbort(false);
    lastAudioPts_ = position();
    info_.audioOrdinal = ordinal;
    audioOrdinal_ = ordinal;
    AVStream* st = ctx->streams[t.index];
    StreamInfo& a = info_.audio;
    a.index = t.index;
    a.codec = t.codec;
    a.sampleRate = t.sampleRate;
    a.channels = t.channels;
    a.bitRate = t.bitRate;
    a.channelLayout = t.channelLayout;
    if (t.external) {
        audioExtSeekTime_ = lastAudioPts_;
        audioExtSeek_ = true;
    } else {
        avcodec_flush_buffers(actx_);
    }
    (void)st;
    Log("audio: switched to track %d (%s) %s", ordinal, t.codec.c_str(), Narrow(audioTrackName()).c_str());
    return true;
}

bool Engine::cycleAudio(int dir) {
    const int n = (int)info_.audioTracks.size();
    if (n < 2) return false;
    int cur = info_.audioOrdinal;
    if (cur < 0) cur = 0;
    int next = (cur + (dir >= 0 ? 1 : -1) + n) % n;
    return selectAudio(next);
}

bool Engine::open(const std::wstring& path, std::string& error) {
    return open(path, error, true);
}

bool Engine::openExternalAudio(const std::wstring& path) {
    std::string utf8 = Narrow(path);
    int r = avformat_open_input(&afmt_, utf8.c_str(), nullptr, nullptr);
    if (r < 0) {
        Log("external audio: cannot open %s -> %s", utf8.c_str(), avError(r).c_str());
        return false;
    }
    r = avformat_find_stream_info(afmt_, nullptr);
    if (r < 0) {
        Log("external audio: stream info failed -> %s", avError(r).c_str());
        avformat_close_input(&afmt_);
        return false;
    }
    int idx = av_find_best_stream(afmt_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (idx < 0) {
        Log("external audio: no audio stream in %s", utf8.c_str());
        avformat_close_input(&afmt_);
        return false;
    }
    actx_ = openAudioCodec(afmt_, idx);
    if (!actx_) {
        Log("external audio: decoder unavailable in %s", utf8.c_str());
        avformat_close_input(&afmt_);
        return false;
    }
    AVStream* st = afmt_->streams[idx];
    audioExtIdx_ = idx;
    audioExtActive_ = true;
    externalAudioPath_ = path;
    audioStartOffset_ = (afmt_->start_time != AV_NOPTS_VALUE && afmt_->start_time > 0)
                            ? (double)afmt_->start_time / AV_TIME_BASE
                            : 0.0;
    Log("external audio: %s stream=%d codec=%s %dHz/%dch", utf8.c_str(), idx,
        avcodec_get_name(st->codecpar->codec_id), st->codecpar->sample_rate,
        st->codecpar->ch_layout.nb_channels);
    return true;
}

bool Engine::open(const std::wstring& path, std::string& error, bool enableAudio) {
    close();
    const double openT0 = qpcNow();
    error_.clear();
    info_ = MediaInfo{};
    audioOrdinal_ = -1;
    subtitlesAsync_ = enableAudio;
    videoStarted_ = false;
    {
        std::lock_guard<std::mutex> lk(subtitleMu_);
        subtitleCues_.clear();
        assTrack_.reset();
        subtitleTrack_ = -1;
        subtitleJob_ = SubtitleJob{};
        subtitleHasJob_ = false;
        subtitleLoading_ = false;
        subtitleStop_ = false;
        subtitleSerial_ = 0;
        subtitleAbort_ = false;
    }
    info_.path = path;
    info_.filename = FileNameOf(path);
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (!ec) info_.fileSize = (int64_t)sz;

    const bool partial = IsPartialName(FileNameOf(path));
    partial_ = partial;
    std::string utf8 = Narrow(path);
    int r = 0;
    if (partial) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            error = "无法打开下载中的文件";
            Log("partial open failed: %ls", path.c_str());
            return false;
        }
        auto* gio = new GrowingIo{h, &quit_, &seekPending_, &waitingData_, &opening_, path};
        io_ = gio;
        uint8_t* buf = (uint8_t*)av_malloc(256 * 1024);
        avio_ = avio_alloc_context(buf, 256 * 1024, 0, gio, GrowingRead, nullptr, GrowingSeek);
        fmt_ = avformat_alloc_context();
        if (!avio_ || !fmt_) {
            error = "无法创建增量读取器";
            return false;
        }
        fmt_->pb = avio_;
        fmt_->flags |= AVFMT_FLAG_CUSTOM_IO;
        quit_ = false;
        seekPending_ = false;
        opening_ = true;
        r = avformat_open_input(&fmt_, nullptr, nullptr, nullptr);
        if (r >= 0) r = avformat_find_stream_info(fmt_, nullptr);
        opening_ = false;
        if (r < 0) {
            error = "下载中的文件头尚未就绪: " + avError(r);
            Log("partial open failed: %s", avError(r).c_str());
            return false;
        }
        Log("partial: growing file %ls", path.c_str());
    } else {
        r = avformat_open_input(&fmt_, utf8.c_str(), nullptr, nullptr);
        if (r < 0) {
            error = "无法打开文件: " + avError(r);
            Log("open failed: %s -> %s", utf8.c_str(), avError(r).c_str());
            return false;
        }
        r = avformat_find_stream_info(fmt_, nullptr);
        if (r < 0) {
            error = "无法读取媒体信息: " + avError(r);
            return false;
        }
    }

    info_.container = fmt_->iformat && fmt_->iformat->name ? fmt_->iformat->name : "?";
    info_.title = DictGet(fmt_->metadata, "title");
    info_.artist = DictGet(fmt_->metadata, "artist");
    info_.album = DictGet(fmt_->metadata, "album");

    startOffset_ = 0.0;
    if (fmt_->start_time != AV_NOPTS_VALUE && fmt_->start_time > 0)
        startOffset_ = (double)fmt_->start_time / AV_TIME_BASE;

    videoIdx_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIdx_ >= 0 && (fmt_->streams[videoIdx_]->disposition & AV_DISPOSITION_ATTACHED_PIC))
        videoIdx_ = -1;
    audioIdx_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (videoIdx_ >= 0) {
        // Hardware first, software on any failure. The device stays alive while
        // the decoder runs; selectFormat() decides what the codec actually gets.
        hwFailed_ = false;
        hwLogged_ = false;
        hwFormat_ = AV_PIX_FMT_NONE;
        hwFrames_ = 0;
        bool opened = false;
        if (softwareDecode_) {
            Log("hw: disabled by setting; software decode");
        } else {
            int hwErr = av_hwdevice_ctx_create(&hwDevice_, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
            if (hwErr < 0) {
                hwDevice_ = nullptr;
                Log("hw: D3D11VA device unavailable (%s); software decode", avError(hwErr).c_str());
            } else {
                Log("hw: D3D11VA device created");
            }
            opened = hwDevice_ && openVideoDecoder(true);
            if (!opened && hwDevice_) {
                Log("hw: D3D11VA decoder open failed; falling back to software");
                av_buffer_unref(&hwDevice_);
            }
        }
        if (!opened) opened = openVideoDecoder(false);
        if (!opened) {
            Log("video decoder unavailable for stream %d", videoIdx_);
            videoIdx_ = -1;
        }
    }
    if (audioIdx_ < 0 && videoIdx_ >= 0) {
        std::wstring sidecar = FindSidecarAudio(path);
        if (!sidecar.empty()) {
            if (!openExternalAudio(sidecar)) Log("external audio: fallback to silent video");
        }
    }
    if (audioIdx_ >= 0) {
        AVStream* st = fmt_->streams[audioIdx_];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (codec) {
            actx_ = avcodec_alloc_context3(codec);
            if (actx_ && avcodec_parameters_to_context(actx_, st->codecpar) >= 0) {
                actx_->pkt_timebase = st->time_base;
                actx_->thread_count = 0;
                if (avcodec_open2(actx_, codec, nullptr) < 0) avcodec_free_context(&actx_);
            } else if (actx_) {
                avcodec_free_context(&actx_);
            }
        }
        if (!actx_) {
            Log("audio decoder unavailable for stream %d", audioIdx_);
            audioIdx_ = -1;
        }
    }

    info_.hasVideo = videoIdx_ >= 0 && vctx_ != nullptr;
    info_.hasAudio = actx_ != nullptr && (audioIdx_ >= 0 || audioExtActive_);

    if (!info_.hasVideo && !info_.hasAudio) {
        error = "文件中没有可解码的音视频流";
        return false;
    }

    // Stream details
    if (info_.hasVideo) {
        AVStream* st = fmt_->streams[videoIdx_];
        StreamInfo& v = info_.video;
        v.index = videoIdx_;
        v.codec = avcodec_get_name(st->codecpar->codec_id);
        if (const AVCodecDescriptor* d = avcodec_descriptor_get(st->codecpar->codec_id))
            v.profile = d->long_name ? d->long_name : "";
        if (vctx_->profile >= 0) {
            const char* p = avcodec_profile_name(st->codecpar->codec_id, vctx_->profile);
            if (p) v.profile = p;
        }
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get((AVPixelFormat)st->codecpar->format);
        if (desc) {
            v.pixFmt = desc->name ? desc->name : "";
            v.bitDepth = desc->comp[0].depth;
        }
        v.width = st->codecpar->width;
        v.height = st->codecpar->height;
        AVRational fr = av_guess_frame_rate(fmt_, st, nullptr);
        v.fps = fr.den ? av_q2d(fr) : 0.0;
        if (v.fps <= 0) v.fps = 0;
        v.bitRate = st->codecpar->bit_rate;
        v.hdr = st->codecpar->color_trc == AVCOL_TRC_SMPTE2084 ||
                st->codecpar->color_trc == AVCOL_TRC_ARIB_STD_B67 ||
                st->codecpar->color_primaries == AVCOL_PRI_BT2020;
        v.fullRange = st->codecpar->color_range == AVCOL_RANGE_JPEG;
        for (int i = 0; i < st->codecpar->nb_coded_side_data; ++i) {
            const AVPacketSideData& sd = st->codecpar->coded_side_data[i];
            if (sd.type == AV_PKT_DATA_DISPLAYMATRIX && sd.size >= 9 * sizeof(int32_t)) {
                double theta = av_display_rotation_get((const int32_t*)sd.data);
                if (!std::isnan(theta)) {
                    int rot = (int)std::lround(-theta) % 360;
                    if (rot < 0) rot += 360;
                    if (rot == 90 || rot == 180 || rot == 270) v.rotation = rot;
                }
            }
        }
        frameDuration_ = v.fps > 0 ? 1.0 / v.fps : 1.0 / 30.0;
    }
    if (info_.hasAudio) {
        AVFormatContext* ast = audioExtActive_ ? afmt_ : fmt_;
        AVStream* st = ast->streams[audioExtActive_ ? audioExtIdx_ : audioIdx_];
        StreamInfo& a = info_.audio;
        a.index = audioIdx_;
        a.codec = avcodec_get_name(st->codecpar->codec_id);
        a.sampleRate = st->codecpar->sample_rate;
        a.channels = st->codecpar->ch_layout.nb_channels;
        a.bitRate = st->codecpar->bit_rate;
        char layout[64]{};
        if (st->codecpar->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_describe(&st->codecpar->ch_layout, layout, sizeof(layout));
        a.channelLayout = layout;
        if (st->codecpar->codec_id == AV_CODEC_ID_AAC) a.profile = "AAC";
    }

    // Audio track list from the active container (embedded or external MKA).
    if (actx_ && (audioIdx_ >= 0 || audioExtActive_)) {
        AVFormatContext* actx = audioExtActive_ ? afmt_ : fmt_;
        const int selected = audioExtActive_ ? audioExtIdx_ : audioIdx_;
        for (unsigned i = 0; i < actx->nb_streams; ++i) {
            if (actx->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
            info_.audioTracks.push_back(MakeAudioTrack(actx, (int)i, audioExtActive_));
            if ((int)i == selected) info_.audioOrdinal = (int)info_.audioTracks.size() - 1;
        }
        if (info_.audioOrdinal < 0 && !info_.audioTracks.empty()) info_.audioOrdinal = 0;
        audioOrdinal_ = info_.audioOrdinal;
        Log("audio: %zu track(s), selected %d", info_.audioTracks.size(), info_.audioOrdinal);
    }

    // Subtitles (text only for now)
    for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
        AVStream* st = fmt_->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;
        SubtitleTrackInfo t;
        t.index = (int)i;
        t.codec = avcodec_get_name(st->codecpar->codec_id);
        t.backend = BackendOf(t.codec);
        if (t.backend == SubtitleBackend::Pgs)
            Log("subtitles: track %d codec=%s is a bitmap/PGS track (not supported)", (int)i, t.codec.c_str());
        AVDictionaryEntry* lang = av_dict_get(st->metadata, "language", nullptr, 0);
        if (lang) t.lang = lang->value;
        AVDictionaryEntry* title = av_dict_get(st->metadata, "title", nullptr, 0);
        t.title = title ? Widen(title->value) : L"";
        if (t.title.empty()) {
            wchar_t buf[64];
            swprintf_s(buf, L"字幕轨 %d", (int)info_.subtitles.size() + 1);
            t.title = buf;
        }
        info_.subtitles.push_back(t);
    }

    // Duration
    if (fmt_->duration != AV_NOPTS_VALUE && fmt_->duration > 0) {
        info_.duration = (double)fmt_->duration / AV_TIME_BASE;
    } else {
        double maxDur = 0;
        for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
            AVStream* st = fmt_->streams[i];
            if (st->duration != AV_NOPTS_VALUE)
                maxDur = std::max(maxDur, st->duration * av_q2d(st->time_base));
        }
        info_.duration = maxDur;
    }
    if (info_.duration < 0) info_.duration = 0;
    if (info_.title.empty()) info_.title = info_.filename;

    // Audio device
    if (info_.hasAudio && enableAudio) {
        audioOk_ = audio_.open(exclusiveAudio_, info_.audio.sampleRate, info_.audio.channels);
        if (audioOk_) {
            appliedSpeed_ = speed_.load();
            if (!rebuildSwr()) {
                Log("swr init failed");
                audioOk_ = false;
            }
        }
    }

    int pick = -1;
    {
        int best = -1;
        for (int i = 0; i < (int)info_.subtitles.size(); ++i) {
            int score = SubtitleScore(info_.subtitles[i]);
            if (score > best) {
                best = score;
                pick = i;
            }
        }
    }
    const int sidecar = loadSidecarSubtitles();
    if (pick < 0) pick = sidecar;
    if (subtitlesAsync_) {
        // Live playback: never block opening on a full subtitle scan. The
        // worker reopens the file on its own context, so close() can cancel it
        // without touching the playback demuxer.
        subtitleThread_ = std::thread([this] { subtitleLoop(); });
        if (pick >= 0) requestSubtitleTrack(pick);
    } else if (pick >= 0) {
        loadSubtitleTrackNow(pick);  // headless/--render: cues must exist before the first frame
    }

    baseMedia_ = 0;
    baseQpc_ = qpcNow();
    pausedAt_ = 0;
    paused_ = true;
    eof_ = false;
    quit_ = false;
    seekPending_ = false;
    dropVideoUntil_ = -1;
    dropAudioUntil_ = -1;
    lastVideoPts_ = -1;
    lastAudioPts_ = -1;
    appliedSpeed_ = speed_.load();
    speedSerial_ = 0;
    lastPushQpc_ = 0;
    serial_ = 0;

    if (audioExtActive_ && audioOk_) audioThread_ = std::thread([this] { audioWorker(); });
    worker_ = std::thread([this] { worker(); });

    Log("opened: %s | %s | duration=%.2f video=%s %dx%d @%.3f audio=%s %dHz/%dch subs=%zu open=%.0fms async=%d",
        utf8.c_str(), info_.container.c_str(), info_.duration, info_.hasVideo ? info_.video.codec.c_str() : "none",
        info_.video.width, info_.video.height, info_.video.fps, info_.hasAudio ? info_.audio.codec.c_str() : "none",
        info_.audio.sampleRate, info_.audio.channels, info_.subtitles.size(), (qpcNow() - openT0) * 1000.0,
        subtitlesAsync_ ? 1 : 0);
    return true;
}

void Engine::close() {
    if (fmt_)
        Log("engine: closing at %.2fs, underruns=%llu starvations=%llu audio=%d | video frames=%llu avg=%.2fms "
            "dropped=%llu | hw=%d hwframes=%llu",
            position(), (unsigned long long)audio_.underruns(), (unsigned long long)audio_.starvations(),
            audioOk_ ? 1 : 0, (unsigned long long)framesDecoded_,
            framesDecoded_ ? decodeMs_ / framesDecoded_ : 0.0, (unsigned long long)framesDropped_,
            hwDecoding() ? 1 : 0, (unsigned long long)hwFrames_);
    quit_ = true;
    audio_.setAbort(true);
    {
        std::lock_guard<std::mutex> lk(subtitleMu_);
        subtitleStop_ = true;
        subtitleHasJob_ = false;
    }
    subtitleAbort_ = true;
    subtitleCv_.notify_all();
    queueCv_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (audioThread_.joinable()) audioThread_.join();
    if (subtitleThread_.joinable()) subtitleThread_.join();
    subtitlesAsync_ = false;
    audio_.close();
    clearQueue();
    if (swr_) {
        swr_free(&swr_);
        swr_ = nullptr;
    }
    if (sws_) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (vctx_) avcodec_free_context(&vctx_);
    if (actx_) avcodec_free_context(&actx_);
    if (afmt_) avformat_close_input(&afmt_);
    afmt_ = nullptr;
    audioExtActive_ = false;
    audioExtIdx_ = -1;
    audioExtSeek_ = false;
    audioStartOffset_ = 0.0;
    externalAudioPath_.clear();
    if (hwDevice_) av_buffer_unref(&hwDevice_);
    if (fmt_) {
        if (avio_) fmt_->pb = nullptr;
        avformat_close_input(&fmt_);
    }
    if (avio_) {
        av_freep(&avio_->buffer);
        avio_context_free(&avio_);
    }
    if (io_) {
        auto* gio = static_cast<GrowingIo*>(io_);
        if (gio->file != INVALID_HANDLE_VALUE) CloseHandle(gio->file);
        delete gio;
        io_ = nullptr;
    }
    fmt_ = nullptr;
    partial_ = false;
    waitingData_ = false;
    {
        std::lock_guard<std::mutex> lk(subtitleMu_);
        subtitleCues_.clear();
        assTrack_.reset();
        subtitleTrack_ = -1;
        subtitleJob_ = SubtitleJob{};
        subtitleHasJob_ = false;
        subtitleLoading_ = false;
        subtitleSerial_++;
    }
    videoStarted_ = false;
    audioOk_ = false;
    hwFormat_ = AV_PIX_FMT_NONE;
    hwFailed_ = false;
    hwFrames_ = 0;
    hwLogged_ = false;
    {
        std::lock_guard<std::mutex> lk(poolMu_);
        free_.clear();
        pool_.clear();
    }
    info_ = MediaInfo{};
    audioOrdinal_ = -1;
}

bool Engine::rebuildSwr() {
    if (!actx_ || !audioOk_) return false;
    if (swr_) swr_free(&swr_);
    AVChannelLayout out{};
    av_channel_layout_default(&out, std::max(1, audio_.channels()));
    AVChannelLayout in = actx_->ch_layout;
    if (in.nb_channels <= 0) av_channel_layout_default(&in, 2);
    int inRate = (int)std::lround(actx_->sample_rate * speed_.load());
    if (inRate < 1000) inRate = actx_->sample_rate;
    int r = swr_alloc_set_opts2(&swr_, &out, AV_SAMPLE_FMT_FLT, audio_.rate(), &in, actx_->sample_fmt, inRate, 0,
                                nullptr);
    av_channel_layout_uninit(&in);
    if (r < 0) {
        Log("swr_alloc_set_opts2 failed: %s", avError(r).c_str());
        return false;
    }
    if (swr_init(swr_) < 0) {
        Log("swr_init failed");
        swr_free(&swr_);
        return false;
    }
    return true;
}

double Engine::position() {
    if (waitingData_.load()) return lastVideoPts_ > 0 ? lastVideoPts_ : pausedAt_;
    if (seekPending_.load()) return pendingSeek_.load();
    if (paused_.load()) return pausedAt_;
    if (audioOk_ && audio_.active()) {
        double t = audio_.mediaTime();
        if (!std::isnan(t)) {
            lastAudioTime_ = t;
            audioWasActive_ = true;
            return std::clamp(t, 0.0, std::max(0.0, info_.duration));
        }
    }
    if (audioOk_ && audioWasActive_) {
        return std::clamp(lastAudioTime_, 0.0, std::max(0.0, info_.duration));
    }
    double t = baseMedia_ + (qpcNow() - baseQpc_) * speed_.load();
    return std::clamp(t, 0.0, std::max(0.0, info_.duration));
}

void Engine::play() {
    if (!fmt_ || !paused_.exchange(false)) return;
    baseQpc_ = qpcNow();
    baseMedia_ = pausedAt_;
    audioWasActive_ = false;
    if (audioOk_) audio_.play();
    queueCv_.notify_all();
}

void Engine::pause() {
    if (!fmt_ || paused_.load()) return;
    double t = position();
    pausedAt_ = t;
    paused_ = true;
    if (audioOk_) audio_.pause();
}

void Engine::toggle() {
    if (paused_.load())
        play();
    else
        pause();
}

void Engine::seek(double seconds) {
    if (!fmt_) return;
    double t = std::clamp(seconds, 0.0, std::max(0.0, info_.duration - 0.01));
    pendingSeek_ = t;
    seekPending_ = true;
    if (paused_) pausedAt_ = t;
    if (audioOk_) audio_.setAbort(true);
    queueCv_.notify_all();
}

void Engine::setSpeed(double speed) {
    speed = std::clamp(speed, 0.25, 4.0);
    if (std::fabs(speed - speed_.load()) < 0.001) return;
    double t = position();
    speed_ = speed;
    speedSerial_++;
    baseMedia_ = t;
    baseQpc_ = qpcNow();
    pausedAt_ = t;
    queueCv_.notify_all();
}

void Engine::setVolume(float v) { audio_.setVolume(v); }

void Engine::doSeek() {
    double t = pendingSeek_.load();
    seekPending_ = false;
    {
        std::lock_guard<std::mutex> lk(audioMu_);
        if (audioOk_) {
            audio_.flush(!paused_.load());
            audio_.setAbort(false);
        }
        if (actx_ && !audioExtActive_) avcodec_flush_buffers(actx_);
    }
    clearQueue();
    pausedAt_ = t;
    baseMedia_ = t;
    baseQpc_ = qpcNow();
    audioWasActive_ = false;
    lastAudioTime_ = t;
    lastPushQpc_ = qpcNow();
    dropVideoUntil_ = t > 0.05 ? t - 0.02 : 0.0;
    dropAudioUntil_ = t > 0.05 ? t - 0.05 : 0.0;
    eof_ = false;
    lastVideoPts_ = t - frameDuration_;
    lastAudioPts_ = t;

    int idx = videoIdx_ >= 0 ? videoIdx_ : audioIdx_;
    if (idx >= 0) {
        AVStream* st = fmt_->streams[idx];
        int64_t ts = (int64_t)llround((t + startOffset_) * AV_TIME_BASE);
        ts = av_rescale_q(ts, AV_TIME_BASE_Q, st->time_base);
        int r = av_seek_frame(fmt_, idx, ts, AVSEEK_FLAG_BACKWARD);
        if (r < 0) {
            int64_t us = (int64_t)llround((t + startOffset_) * 1e6);
            r = avformat_seek_file(fmt_, -1, INT64_MIN, us, INT64_MAX, 0);
        }
        if (r < 0) Log("seek to %.2f failed: %s", t, avError(r).c_str());
        if (vctx_) avcodec_flush_buffers(vctx_);
        if (audioExtActive_) {
            audioExtSeekTime_ = t;
            audioExtSeek_ = true;
        }
    }
    serial_++;
    Log("seek -> %.2f (serial %llu)", t, (unsigned long long)serial_.load());
}

bool Engine::openVideoDecoder(bool withHw) {
    AVStream* st = fmt_->streams[videoIdx_];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return false;
    vctx_ = avcodec_alloc_context3(codec);
    if (!vctx_) return false;
    if (avcodec_parameters_to_context(vctx_, st->codecpar) < 0) {
        avcodec_free_context(&vctx_);
        return false;
    }
    vctx_->pkt_timebase = st->time_base;
    vctx_->thread_count = 0;
    vctx_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    vctx_->opaque = this;
    vctx_->get_format = &Engine::selectFormat;
    if (withHw) {
        vctx_->hw_device_ctx = av_buffer_ref(hwDevice_);
        if (!vctx_->hw_device_ctx) {
            avcodec_free_context(&vctx_);
            return false;
        }
    }
    int r = avcodec_open2(vctx_, codec, nullptr);
    if (r < 0) {
        Log("hw: %s decoder open failed: %s", withHw ? "D3D11VA" : "software", avError(r).c_str());
        avcodec_free_context(&vctx_);
        return false;
    }
    return true;
}

AVPixelFormat Engine::selectFormat(AVCodecContext* ctx, const AVPixelFormat* fmt) {
    Engine* self = ctx && ctx->opaque ? (Engine*)ctx->opaque : nullptr;
    const bool tryHw = self && self->hwDevice_;
    if (tryHw) {
        for (const AVPixelFormat* p = fmt; *p != AV_PIX_FMT_NONE; ++p) {
            if (*p == AV_PIX_FMT_D3D11) {
                self->hwFormat_ = AV_PIX_FMT_D3D11;
                if (!self->hwLogged_.exchange(true)) Log("hw: decoder picked AV_PIX_FMT_D3D11");
                return AV_PIX_FMT_D3D11;
            }
        }
    }
    AVPixelFormat pick = AV_PIX_FMT_NONE;
    for (const AVPixelFormat* p = fmt; *p != AV_PIX_FMT_NONE; ++p) {
        const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(*p);
        if (!(d && (d->flags & AV_PIX_FMT_FLAG_HWACCEL))) {
            pick = *p;
            break;
        }
    }
    if (pick == AV_PIX_FMT_NONE) pick = fmt[0];
    if (self) {
        self->hwFormat_ = pick;
        if (tryHw && !self->hwLogged_.exchange(true)) {
            const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(pick);
            Log("hw: D3D11VA not offered, decoder uses %s", d && d->name ? d->name : "?");
        }
    }
    return pick;
}

void Engine::fallbackToSoftware(const char* reason) {
    if (hwFailed_) return;
    hwFailed_ = true;
    Log("hw: %s; disabling hardware decode for the rest of this file", reason);
    hwFormat_ = AV_PIX_FMT_NONE;
    avcodec_free_context(&vctx_);
    if (hwDevice_) av_buffer_unref(&hwDevice_);
    if (!openVideoDecoder(false)) {
        Log("hw: software decoder reopen failed; stopping video");
        eof_ = true;
        return;
    }
    Log("hw: software decoder active");
    // The hardware decoder's reference frames are gone; restart from a keyframe.
    seek(lastVideoPts_ > 0.05 ? lastVideoPts_ : 0.0);
}

bool Engine::decodeVideo(const AVPacket* pkt) {
    if (!vctx_) return true;
    const double t0 = qpcNow();
    int produced = 0;
    int sent = avcodec_send_packet(vctx_, pkt);
    if (sent < 0 && sent != AVERROR(EAGAIN)) {
        if (hwTrying() && hwFrames_ == 0) {
            fallbackToSoftware("hardware packet decode failed");
            return true;
        }
        return false;
    }
    AVFrame* f = av_frame_alloc();
    if (!f) return false;
    AVStream* st = fmt_->streams[videoIdx_];
    const char* hwFailure = nullptr;
    while (true) {
        int r = avcodec_receive_frame(vctx_, f);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
        if (r < 0) {
            if (hwTrying() && hwFrames_ == 0) hwFailure = "hardware decode error";
            break;
        }
        double pts;
        if (f->best_effort_timestamp != AV_NOPTS_VALUE)
            pts = f->best_effort_timestamp * av_q2d(st->time_base) - startOffset_;
        else
            pts = lastVideoPts_ + frameDuration_;
        lastVideoPts_ = pts;
        if (dropVideoUntil_ >= 0) {
            if (pts + 0.0005 < dropVideoUntil_) {
                av_frame_unref(f);
                continue;
            }
            dropVideoUntil_ = -1;
        }
        AVFrame* cpu = nullptr;
        AVFrame* src = f;
        if (f->format == AV_PIX_FMT_D3D11) {
            cpu = av_frame_alloc();
            if (!cpu) break;
            int transfer = av_hwframe_transfer_data(cpu, f, 0);
            if (transfer < 0) {
                Log("hw: frame transfer failed: %s", avError(transfer).c_str());
                av_frame_free(&cpu);
                av_frame_unref(f);
                hwFailure = "hardware frame transfer failed";
                break;
            }
            ++hwFrames_;
            if (hwFrames_ == 1) {
                const AVPixFmtDescriptor* d = av_pix_fmt_desc_get((AVPixelFormat)cpu->format);
                Log("hw: first frame %dx%d d3d11 -> %s", f->width, f->height, d && d->name ? d->name : "?");
            }
            src = cpu;
        }
        VideoFrame* frame = allocFrame(src->width, src->height);
        if (frame) {
            frame->pts = pts;
            frame->rotation = info_.video.rotation;
            frame->serial = serial_.load();
            if (convertFrame(src, frame)) {
                pushFrame(frame);
            } else {
                recycleFrame(frame);
            }
        }
        if (cpu) av_frame_free(&cpu);
        ++produced;
        av_frame_unref(f);
        if (quit_.load() || seekPending_.load()) break;
    }
    av_frame_free(&f);
    if (hwFailure) {
        fallbackToSoftware(hwFailure);
        return true;
    }
    if (produced > 0) {
        framesDecoded_ += (uint64_t)produced;
        decodeMs_ += (qpcNow() - t0) * 1000.0;
    }
    return true;
}

bool Engine::decodeAudio(const AVPacket* pkt) {
    std::lock_guard<std::mutex> lk(audioMu_);
    if (!actx_ || !swr_) return true;
    if (pkt) {
        const int active = audioExtActive_ ? audioExtIdx_ : audioIdx_;
        if (pkt->stream_index != active) return true;
    }
    int sent = avcodec_send_packet(actx_, pkt);
    if (sent < 0 && sent != AVERROR(EAGAIN)) return false;
    AVFrame* f = av_frame_alloc();
    if (!f) return false;
    AVFormatContext* ast = audioExtActive_ ? afmt_ : fmt_;
    const int aidx = audioExtActive_ ? audioExtIdx_ : audioIdx_;
    if (!ast || aidx < 0) { av_frame_free(&f); return false; }
    AVStream* st = ast->streams[aidx];
    const double basePts = audioExtActive_ ? audioStartOffset_ : startOffset_;
    while (true) {
        int r = avcodec_receive_frame(actx_, f);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
        if (r < 0) break;
        double pts;
        if (f->best_effort_timestamp != AV_NOPTS_VALUE)
            pts = f->best_effort_timestamp * av_q2d(st->time_base) - basePts;
        else
            pts = lastAudioPts_ > 0 ? lastAudioPts_ : 0.0;
        lastAudioPts_ = pts + (double)f->nb_samples / std::max(1, actx_->sample_rate);
        if (dropAudioUntil_ >= 0) {
            if (pts + (double)f->nb_samples / std::max(1, actx_->sample_rate) < dropAudioUntil_) {
                av_frame_unref(f);
                continue;
            }
            dropAudioUntil_ = -1;
        }
        int outMax = swr_get_out_samples(swr_, f->nb_samples);
        if (outMax <= 0) {
            av_frame_unref(f);
            continue;
        }
        audioScratch_.resize((size_t)outMax * std::max(1, audio_.channels()));
        uint8_t* out[1] = {(uint8_t*)audioScratch_.data()};
        const uint8_t* const* in = (const uint8_t* const*)f->extended_data;
        int got = swr_convert(swr_, out, outMax, in, f->nb_samples);
        if (got > 0) {
            double spf = appliedSpeed_ / (double)audio_.rate();
            if (!audio_.write(audioScratch_.data(), got, pts, spf)) {
                av_frame_unref(f);
                av_frame_free(&f);
                return false;
            }
        }
        av_frame_unref(f);
        if (quit_.load() || seekPending_.load()) break;
    }
    av_frame_free(&f);
    return true;
}


void Engine::audioWorker() {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) return;
    while (!quit_.load()) {
        if (audioExtSeek_.exchange(false)) {
            const double t = audioExtSeekTime_.load();
            AVStream* st = afmt_->streams[audioExtIdx_];
            int64_t ts = (int64_t)llround((t + audioStartOffset_) * AV_TIME_BASE);
            ts = av_rescale_q(ts, AV_TIME_BASE_Q, st->time_base);
            int r = av_seek_frame(afmt_, audioExtIdx_, ts, AVSEEK_FLAG_BACKWARD);
            if (r < 0) Log("external audio seek to %.2f failed: %s", t, avError(r).c_str());
            avcodec_flush_buffers(actx_);
            lastAudioPts_ = t;
            dropAudioUntil_ = t > 0.05 ? t - 0.05 : 0.0;
        }
        if (speedSerial_.load() != appliedSerial_.load()) {
            appliedSpeed_ = speed_.load();
            appliedSerial_ = speedSerial_.load();
            std::lock_guard<std::mutex> lk(audioMu_);
            audio_.flush(!paused_.load());
            audio_.setAbort(false);
            rebuildSwr();
        }
        if (paused_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        int r = av_read_frame(afmt_, pkt);
        if (r == AVERROR_EOF) {
            decodeAudio(nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (r == AVERROR(EAGAIN)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (r < 0) {
            Log("external audio read error: %s", avError(r).c_str());
            break;
        }
        if (pkt->stream_index == audioExtIdx_) decodeAudio(pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

void Engine::worker() {
    AVPacket* pkt = av_packet_alloc();
    // When the video frame queue is full we must not stop reading packets, or
    // audio starves behind a congested video decoder. Stage video packets and
    // keep feeding audio; the staged packets are decoded as frames are consumed.
    std::deque<AVPacket*> pendingVideo;
    size_t pendingBytes = 0;
    constexpr size_t kPendingPackets = 240;
    constexpr size_t kPendingBytes = 96ull * 1024 * 1024;
    auto clearPending = [&] {
        for (AVPacket* p : pendingVideo) av_packet_free(&p);
        pendingVideo.clear();
        pendingBytes = 0;
    };

    while (!quit_.load()) {
        if (seekPending_.load()) {
            clearPending();
            doSeek();
            continue;
        }
        if (speedSerial_.load() != appliedSerial_.load()) {
            appliedSpeed_ = speed_.load();
            appliedSerial_ = speedSerial_.load();
            if (audioOk_ && !audioExtActive_) {
                std::lock_guard<std::mutex> lk(audioMu_);
                audio_.flush(!paused_.load());
                audio_.setAbort(false);
                rebuildSwr();
            }
        }
        if (eof_.load()) {
            std::unique_lock<std::mutex> lk(queueMu_);
            queueCv_.wait_for(lk, std::chrono::milliseconds(50),
                              [this] { return quit_.load() || seekPending_.load(); });
            continue;
        }

        const bool isPaused = paused_.load();
        const size_t limit = isPaused ? 2 : 8;
        size_t qs = queueSize();

        if (!pendingVideo.empty() && qs < limit) {
            AVPacket* p = pendingVideo.front();
            pendingVideo.pop_front();
            pendingBytes -= (size_t)p->size;
            decodeVideo(p);
            av_packet_free(&p);
            continue;
        }
        if (isPaused && qs >= limit) {
            std::unique_lock<std::mutex> lk(queueMu_);
            queueCv_.wait_for(lk, std::chrono::milliseconds(15), [this] {
                return quit_.load() || seekPending_.load() || queue_.size() < 2;
            });
            continue;
        }
        if (pendingVideo.size() >= kPendingPackets || pendingBytes >= kPendingBytes) {
            std::unique_lock<std::mutex> lk(queueMu_);
            queueCv_.wait_for(lk, std::chrono::milliseconds(10), [this] {
                return quit_.load() || seekPending_.load() || queue_.size() < 8;
            });
            continue;
        }
        if (!pkt) break;

        int r = av_read_frame(fmt_, pkt);
        if (r == AVERROR_EOF) {
            if (!pendingVideo.empty()) {
                if (queueSize() >= limit) {
                    std::unique_lock<std::mutex> lk(queueMu_);
                    queueCv_.wait_for(lk, std::chrono::milliseconds(10), [this] {
                        return quit_.load() || seekPending_.load() || queue_.size() < 8;
                    });
                    continue;
                }
                AVPacket* p = pendingVideo.front();
                pendingVideo.pop_front();
                pendingBytes -= (size_t)p->size;
                decodeVideo(p);
                av_packet_free(&p);
                continue;
            }
            if (vctx_) {
                AVPacket p{};
                avcodec_send_packet(vctx_, &p);
                AVFrame* f = av_frame_alloc();
                bool hwDrainFailed = false;
                while (avcodec_receive_frame(vctx_, f) >= 0) {
                    AVFrame* cpu = nullptr;
                    AVFrame* src = f;
                    bool ok = true;
                    if (f->format == AV_PIX_FMT_D3D11) {
                        cpu = av_frame_alloc();
                        if (!cpu) {
                            Log("hw: frame transfer failed at eof: alloc failed");
                            ok = false;
                        } else {
                            int tr = av_hwframe_transfer_data(cpu, f, 0);
                            if (tr < 0) {
                                Log("hw: frame transfer failed at eof: %s", avError(tr).c_str());
                                ok = false;
                            } else {
                                ++hwFrames_;
                                src = cpu;
                            }
                        }
                    }
                    if (ok && dropVideoUntil_ < 0 && src->width > 0) {
                        VideoFrame* frame = allocFrame(src->width, src->height);
                        if (frame) {
                            frame->pts = lastVideoPts_ + frameDuration_;
                            frame->rotation = info_.video.rotation;
                            frame->serial = serial_.load();
                            if (convertFrame(src, frame)) {
                                lastVideoPts_ = frame->pts;
                                pushFrame(frame);
                            } else {
                                recycleFrame(frame);
                            }
                        }
                    }
                    if (cpu) av_frame_free(&cpu);
                    av_frame_unref(f);
                    if (!ok) {
                        hwDrainFailed = true;
                        break;
                    }
                }
                av_frame_free(&f);
                if (hwDrainFailed) fallbackToSoftware("hardware frame transfer failed at eof");
            }
            if (actx_ && audioOk_ && swr_) {
                AVPacket p{};
                avcodec_send_packet(actx_, &p);
                AVFrame* f = av_frame_alloc();
                while (avcodec_receive_frame(actx_, f) >= 0) {
                    int outMax = swr_get_out_samples(swr_, f->nb_samples);
                    if (outMax > 0) {
                        audioScratch_.resize((size_t)outMax * std::max(1, audio_.channels()));
                        uint8_t* out[1] = {(uint8_t*)audioScratch_.data()};
                        int got = swr_convert(swr_, out, outMax, (const uint8_t* const*)f->extended_data,
                                              f->nb_samples);
                        if (got > 0)
                            audio_.write(audioScratch_.data(), got, lastAudioPts_,
                                         appliedSpeed_ / (double)audio_.rate());
                    }
                    av_frame_unref(f);
                }
                av_frame_free(&f);
            }
            eof_ = true;
            Log("eof at %.2f", position());
            continue;
        }
        if (r == AVERROR(EAGAIN)) {
            Sleep(5);
            continue;
        }
        if (r < 0) {
            error_ = avError(r);
            Log("read error: %s", error_.c_str());
            eof_ = true;
            continue;
        }
        bool ok = true;
        if (pkt->stream_index == videoIdx_) {
            if (qs >= limit || !pendingVideo.empty()) {
                AVPacket* copy = av_packet_clone(pkt);
                if (copy) {
                    pendingVideo.push_back(copy);
                    pendingBytes += (size_t)copy->size;
                }
            } else {
                ok = decodeVideo(pkt);
            }
        } else if (pkt->stream_index == audioIdx_) {
            ok = decodeAudio(pkt);
        }
        av_packet_unref(pkt);
        if (!ok) {
            if (audioOk_) audio_.setAbort(false);
            if (seekPending_.load()) continue;
        }
    }
    clearPending();
    av_packet_free(&pkt);
}

VideoFrame* Engine::allocFrame(int w, int h) {
    if (w <= 0 || h <= 0) return nullptr;
    std::lock_guard<std::mutex> lk(poolMu_);
    VideoFrame* f = nullptr;
    if (!free_.empty()) {
        f = free_.back();
        free_.pop_back();
    } else {
        pool_.push_back(std::make_unique<VideoFrame>());
        f = pool_.back().get();
    }
    f->width = w;
    f->height = h;
    f->stride = w * 4;
    f->hdr = false;
    f->yStride = 0;
    f->uvStride = 0;
    size_t need = (size_t)f->stride * h;
    if (f->pixels.size() < need) f->pixels.resize(need);
    return f;
}

void Engine::recycleFrame(VideoFrame* f) {
    if (!f) return;
    std::lock_guard<std::mutex> lk(poolMu_);
    free_.push_back(f);
}

void Engine::pushFrame(VideoFrame* f) {
    {
        std::lock_guard<std::mutex> lk(queueMu_);
        queue_.push_back(f);
    }
    lastPushQpc_ = qpcNow();
    if (!videoStarted_) {
        videoStarted_ = true;
        Log("video: first frame pts=%.3f", f->pts);
    }
    queueCv_.notify_all();
}

size_t Engine::queueSize() {
    std::lock_guard<std::mutex> lk(queueMu_);
    return queue_.size();
}

void Engine::clearQueue() {
    std::vector<VideoFrame*> drop;
    {
        std::lock_guard<std::mutex> lk(queueMu_);
        while (!queue_.empty()) {
            drop.push_back(queue_.front());
            queue_.pop_front();
        }
    }
    for (auto* f : drop) recycleFrame(f);
}

bool Engine::convertFrame(AVFrame* src, VideoFrame* dst) {
    if (!src || src->width <= 0 || src->height <= 0) return false;
    const int w = src->width, h = src->height;
    dst->width = w;
    dst->height = h;
    dst->rotation = 0;
    if (info_.video.hdr && (src->format == AV_PIX_FMT_YUV420P10LE || src->format == AV_PIX_FMT_P010LE)) {
        const int cw = (w + 1) / 2, ch = (h + 1) / 2;
        const int ys = w * 2, cs = cw * 2;
        dst->hdr = true;
        dst->yStride = ys;
        dst->uvStride = cs;
        dst->planes.resize((size_t)ys * h + 2 * (size_t)cs * ch);
        uint8_t* py = dst->planes.data();
        uint8_t* pu = py + (size_t)ys * h;
        uint8_t* pv = pu + (size_t)cs * ch;
        if (src->format == AV_PIX_FMT_P010LE) {
            // D3D11VA downloads 10-bit as P010, which keeps the 10-bit code in
            // the high bits. The scRGB shader expects the bare code yuv420p10le
            // stores, so shift it down and split the interleaved chroma.
            const uint16_t* sy = (const uint16_t*)src->data[0];
            const uint16_t* suv = (const uint16_t*)src->data[1];
            for (int row = 0; row < h; ++row) {
                const uint16_t* in = sy + (size_t)row * src->linesize[0] / 2;
                uint16_t* out = (uint16_t*)(py + (size_t)row * ys);
                for (int x = 0; x < w; ++x) out[x] = in[x] >> 6;
            }
            for (int row = 0; row < ch; ++row) {
                const uint16_t* in = suv + (size_t)row * src->linesize[1] / 2;
                uint16_t* uo = (uint16_t*)(pu + (size_t)row * cs);
                uint16_t* vo = (uint16_t*)(pv + (size_t)row * cs);
                for (int x = 0; x < cw; ++x) {
                    uo[x] = in[2 * x] >> 6;
                    vo[x] = in[2 * x + 1] >> 6;
                }
            }
        } else {
            for (int row = 0; row < h; ++row)
                memcpy(py + (size_t)row * ys, src->data[0] + (size_t)row * src->linesize[0], (size_t)w * 2);
            for (int row = 0; row < ch; ++row) {
                memcpy(pu + (size_t)row * cs, src->data[1] + (size_t)row * src->linesize[1], (size_t)cw * 2);
                memcpy(pv + (size_t)row * cs, src->data[2] + (size_t)row * src->linesize[2], (size_t)cw * 2);
            }
        }
        dst->pixels.clear();
        return true;
    }
    dst->hdr = false;
    dst->stride = w * 4;
    dst->pixels.resize((size_t)dst->stride * h);

    prism::Matrix matrix = prism::Matrix::BT709;
    switch (src->colorspace) {
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M: matrix = prism::Matrix::BT601; break;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL: matrix = prism::Matrix::BT2020; break;
        case AVCOL_SPC_BT709: matrix = prism::Matrix::BT709; break;
        default: matrix = h >= 720 ? prism::Matrix::BT709 : prism::Matrix::BT601; break;
    }
    const bool full = src->color_range == AVCOL_RANGE_JPEG;

    switch (src->format) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            prism::Yuv420pToBgra(dst->pixels.data(), dst->stride, src->data[0], src->data[1], src->data[2],
                                 src->linesize[0], src->linesize[1], w, h, matrix, full);
            break;
        case AV_PIX_FMT_NV12:
            prism::Nv12ToBgra(dst->pixels.data(), dst->stride, src->data[0], src->data[1], src->linesize[0],
                              src->linesize[1], w, h, matrix, full);
            break;
        case AV_PIX_FMT_YUV420P10LE:
            prism::Yuv420p10ToBgra(dst->pixels.data(), dst->stride, (const uint16_t*)src->data[0],
                                   (const uint16_t*)src->data[1], (const uint16_t*)src->data[2],
                                   src->linesize[0] / 2, src->linesize[1] / 2, w, h, matrix, full);
            break;
        case AV_PIX_FMT_P010LE:
            prism::P010ToBgra(dst->pixels.data(), dst->stride, (const uint16_t*)src->data[0],
                              (const uint16_t*)src->data[1], src->linesize[0] / 2, src->linesize[1] / 2, w, h,
                              matrix, full);
            break;
        default: {
            sws_ = sws_getCachedContext(sws_, w, h, (AVPixelFormat)src->format, w, h, AV_PIX_FMT_BGRA,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws_) return false;
            uint8_t* dstData[4] = {dst->pixels.data(), nullptr, nullptr, nullptr};
            int dstStride[4] = {dst->stride, 0, 0, 0};
            sws_scale(sws_, src->data, src->linesize, 0, h, dstData, dstStride);
            break;
        }
    }
    return true;
}

VideoFrame* Engine::acquireFrame() {
    std::vector<VideoFrame*> drop;
    VideoFrame* pick = nullptr;
    double t = position();
    {
        std::lock_guard<std::mutex> lk(queueMu_);
        if (queue_.empty()) return nullptr;
        if (paused_.load()) {
            pick = queue_.front();
            queue_.pop_front();
        } else {
            while (!queue_.empty() && queue_.front()->pts <= t + 0.001) {
                if (pick) {
                    drop.push_back(pick);
                    ++framesDropped_;
                }
                pick = queue_.front();
                queue_.pop_front();
            }
            if (!pick && !queue_.empty() && queue_.front()->pts <= t + 0.08) {
                pick = queue_.front();
                queue_.pop_front();
            }
        }
    }
    for (auto* f : drop) recycleFrame(f);
    if (pick) queueCv_.notify_all();
    return pick;
}

void Engine::releaseFrame(VideoFrame* frame) {
    recycleFrame(frame);
    queueCv_.notify_all();
}

EngineStats Engine::stats() {
    EngineStats s;
    s.frames = framesDecoded_;
    s.dropped = framesDropped_;
    s.hwFrames = hwFrames_;
    s.avgDecodeMs = framesDecoded_ ? decodeMs_ / (double)framesDecoded_ : 0.0;
    s.queue = queueSize();
    s.hw = hwDecoding();
    s.partial = partial_;
    s.buffering = buffering();
    s.bufferedAhead = bufferedAhead();
    s.underruns = audio_.underruns();
    s.starvations = audio_.starvations();
    s.audioBufferFrames = audio_.bufferedFrames();
    {
        std::lock_guard<std::mutex> lk(subtitleMu_);
        s.subtitleCues = (int)subtitleCues_.size();
        s.subtitleTrack = subtitleTrack_;
        if (subtitleTrack_ >= 0 && subtitleTrack_ < (int)info_.subtitles.size())
            s.subtitleBackend = info_.subtitles[subtitleTrack_].backend;
    }
    s.audioTracks = (int)info_.audioTracks.size();
    s.audioOrdinal = info_.audioOrdinal;
    return s;
}

bool Engine::buffering() {
    if (waitingData_.load()) return true;
    if (paused_.load() || eof_.load()) return false;
    if (queueSize() > 0) return false;
    double last = lastPushQpc_.load();
    if (last <= 0) return true;
    return (qpcNow() - last) > 0.35;
}

bool Engine::ended() {
    if (!eof_.load()) return false;
    if (queueSize() > 0) return false;
    if (audioOk_ && audio_.bufferedFrames() > 0) return false;
    if (info_.duration <= 0) return false;
    return position() >= info_.duration - 0.4;
}

double Engine::bufferedAhead() const {
    if (!audioOk_) return 0;
    return (double)audio_.bufferedFrames() / std::max(1, audio_.rate());
}

std::wstring Engine::statusLine() const {
    std::wstring s;
    if (info_.hasVideo) {
        s += Widen(info_.video.codec);
        if (!info_.video.profile.empty()) {
            s += L" ";
            s += Widen(info_.video.profile);
        }
        if (info_.video.bitDepth > 8) {
            s += L" · ";
            s += std::to_wstring(info_.video.bitDepth);
            s += L"-bit";
        }
        if (info_.video.hdr) s += L" · HDR";
        s += L" · ";
        s += std::to_wstring(info_.video.width);
        s += L"×";
        s += std::to_wstring(info_.video.height);
        if (info_.video.fps > 0) {
            wchar_t buf[32];
            swprintf_s(buf, L" · %.3g fps", info_.video.fps);
            s += buf;
        }
    }
    if (info_.hasAudio) {
        if (!s.empty()) s += L" · ";
        s += Widen(info_.audio.codec);
        s += L" ";
        s += std::to_wstring(info_.audio.channels);
        s += L"ch";
    }
    return s;
}


bool Engine::makeSubtitleJob(int trackIndex, SubtitleJob& job) const {
    if (trackIndex < 0 || trackIndex >= (int)info_.subtitles.size()) return false;
    job = SubtitleJob{};
    job.ordinal = trackIndex;
    job.track = info_.subtitles[trackIndex];
    job.mediaPath = job.track.external ? job.track.path : info_.path;
    job.startOffset = startOffset_;
    return true;
}

Engine::SubtitleDecodeResult Engine::decodeSubtitleTrack(const SubtitleJob& job,
                                                         const std::atomic<bool>* abort) const {
    SubtitleDecodeResult out;
    if (abort && abort->load()) return out;
    const double t0 = qpcNow();
    const int streamIndex = job.track.index;
    AVFormatContext* subFmt = nullptr;
    std::string path = Narrow(job.mediaPath);
    if (avformat_open_input(&subFmt, path.c_str(), nullptr, nullptr) < 0) {
        Log("subtitles: reopen failed");
        return out;
    }
    avformat_find_stream_info(subFmt, nullptr);
    if (streamIndex < 0 || streamIndex >= (int)subFmt->nb_streams) {
        avformat_close_input(&subFmt);
        return out;
    }
    AVStream* st = subFmt->streams[streamIndex];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext* ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0 ||
        avcodec_open2(ctx, codec, nullptr) < 0) {
        if (ctx) avcodec_free_context(&ctx);
        avformat_close_input(&subFmt);
        return out;
    }
    out.opened = true;
    ctx->pkt_timebase = st->time_base;
    // ASS/SSA: rebuild a complete script for libass from codec private data plus
    // the Matroska-style event packets (the decoder keeps those bytes verbatim).
    const bool assTrack = job.track.backend == SubtitleBackend::Ass;
    std::shared_ptr<AssTrackData> ass;
    if (assTrack) {
        ass = std::make_shared<AssTrackData>();
        if (st->codecpar->extradata && st->codecpar->extradata_size > 0)
            ass->header.assign((const char*)st->codecpar->extradata, (size_t)st->codecpar->extradata_size);
        EnsureAssHeader(ass->header);
    }
    AVPacket* pkt = av_packet_alloc();
    size_t encoded = 0;
    while (av_read_frame(subFmt, pkt) >= 0) {
        if (abort && abort->load()) break;
        if (pkt->stream_index == streamIndex) {
            AVSubtitle sub{};
            int got = 0;
            if (avcodec_decode_subtitle2(ctx, &sub, &got, pkt) >= 0 && got) {
                std::wstring text;
                for (unsigned i = 0; i < sub.num_rects; ++i) {
                    AVSubtitleRect* r = sub.rects[i];
                    if (!r) continue;
                    std::wstring piece;
                    if (r->text && r->text[0]) piece = Widen(r->text);
                    else if (r->ass && r->ass[0]) piece = ParseAssText(r->ass);
                    if (piece.empty()) continue;
                    if (!text.empty()) text += L"\n";
                    text += piece;
                }
                double base = 0;
                if (pkt->pts != AV_NOPTS_VALUE)
                    base = pkt->pts * av_q2d(st->time_base) - job.startOffset;
                else if (sub.pts != AV_NOPTS_VALUE)
                    base = (double)sub.pts / AV_TIME_BASE;
                double s = base + sub.start_display_time / 1000.0;
                double e = base + sub.end_display_time / 1000.0;
                if (!text.empty() && e > s && out.cues.size() < 20000) {
                    out.cues.push_back({s, e, text});
                    ++encoded;
                }
                if (ass && ass->events.size() < 20000 && sub.num_rects > 0 && sub.rects[0] &&
                    sub.rects[0]->ass && sub.rects[0]->ass[0]) {
                    double dur = e - s;
                    if (dur <= 0) dur = pkt->duration > 0 ? pkt->duration * av_q2d(st->time_base) : 5.0;
                    ass->events.push_back({std::max(0.0, s), dur, sub.rects[0]->ass});
                }
                avsubtitle_free(&sub);
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&subFmt);
    std::sort(out.cues.begin(), out.cues.end(),
              [](const SubtitleCue& a, const SubtitleCue& b) { return a.start < b.start; });
    out.ass = std::move(ass);
    const bool aborted = abort && abort->load();
    Log("subtitles: track %d codec=%s cues=%zu (%zu packets)%s in %.1f ms%s", job.ordinal,
        job.track.codec.c_str(), out.cues.size(), encoded,
        out.ass ? (", ass events=" + std::to_string(out.ass->events.size())).c_str() : "",
        (qpcNow() - t0) * 1000.0, aborted ? " aborted" : "");
    return out;
}

bool Engine::requestSubtitleTrack(int trackIndex) {
    SubtitleJob job;
    if (!makeSubtitleJob(trackIndex, job)) return false;
    const std::string codec = job.track.codec;
    {
        std::lock_guard<std::mutex> lk(subtitleMu_);
        job.serial = ++subtitleSerial_;
        subtitleAbort_ = true;  // cancel the in-flight scan, if any
        subtitleJob_ = std::move(job);
        subtitleHasJob_ = true;
        subtitleLoading_ = true;
        subtitleCues_.clear();
        subtitleTrack_ = trackIndex;
        assTrack_.reset();
    }
    subtitleCv_.notify_all();
    Log("subtitles: request track %d codec=%s (async)", trackIndex, codec.c_str());
    return true;
}

bool Engine::loadSubtitleTrackNow(int trackIndex) {
    SubtitleJob job;
    if (!makeSubtitleJob(trackIndex, job)) return false;
    uint64_t serial = 0;
    {
        std::lock_guard<std::mutex> lk(subtitleMu_);
        serial = ++subtitleSerial_;
        subtitleAbort_ = true;  // an async scan of an older track is now stale
        subtitleHasJob_ = false;
        subtitleLoading_ = true;
        subtitleCues_.clear();
        subtitleTrack_ = trackIndex;
        assTrack_.reset();
    }
    SubtitleDecodeResult result = decodeSubtitleTrack(job, nullptr);
    const bool hasCues = !result.cues.empty();
    {
        std::lock_guard<std::mutex> lk(subtitleMu_);
        if (serial == subtitleSerial_) {
            subtitleCues_ = std::move(result.cues);
            assTrack_ = std::move(result.ass);
            subtitleTrack_ = result.opened ? trackIndex : -1;
            subtitleLoading_ = false;
        }
    }
    return hasCues;
}

bool Engine::loadSubtitleTrack(int trackIndex) {
    if (subtitlesAsync_) return requestSubtitleTrack(trackIndex);
    return loadSubtitleTrackNow(trackIndex);
}

bool Engine::cycleSubtitle(int dir) {
    const int n = (int)info_.subtitles.size();
    if (n == 0) return false;
    int cur = -1;
    {
        std::lock_guard<std::mutex> lk(subtitleMu_);
        cur = subtitleTrack_;
    }
    for (int step = 1; step <= n; ++step) {
        int i = ((cur + dir * step) % n + n) % n;
        if (!IsTextSubtitle(info_.subtitles[i].codec)) continue;
        if (loadSubtitleTrack(i)) return true;
    }
    return false;
}

std::wstring Engine::subtitleName() const {
    int track = -1;
    {
        std::lock_guard<std::mutex> lk(subtitleMu_);
        track = subtitleTrack_;
    }
    if (track < 0 || track >= (int)info_.subtitles.size()) return L"";
    return info_.subtitles[track].title;
}

bool Engine::selectSubtitle(int ordinal) {
    if (ordinal < 0 || ordinal >= (int)info_.subtitles.size()) return false;
    if (!IsTextSubtitle(info_.subtitles[ordinal].codec)) return false;
    return loadSubtitleTrack(ordinal);
}

bool Engine::addExternalSubtitle(const std::wstring& path, std::string& error, int& firstOrdinal) {
    AVFormatContext* f = nullptr;
    std::string p = Narrow(path);
    if (avformat_open_input(&f, p.c_str(), nullptr, nullptr) < 0) {
        error = "无法打开字幕文件";
        return false;
    }
    avformat_find_stream_info(f, nullptr);
    int added = 0;
    firstOrdinal = -1;
    for (unsigned i = 0; i < f->nb_streams; ++i) {
        AVStream* st = f->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;
        const char* codec = avcodec_get_name(st->codecpar->codec_id);
        if (!IsTextSubtitle(codec)) continue;
        SubtitleTrackInfo t;
        t.index = (int)i;
        t.external = true;
        t.path = path;
        t.codec = codec;
        t.backend = BackendOf(codec);
        AVDictionaryEntry* lang = av_dict_get(st->metadata, "language", nullptr, 0);
        if (lang) t.lang = lang->value;
        AVDictionaryEntry* title = av_dict_get(st->metadata, "title", nullptr, 0);
        if (title) t.title = Widen(title->value);
        if (t.title.empty()) t.title = std::filesystem::path(path).filename().wstring();
        if (firstOrdinal < 0) firstOrdinal = (int)info_.subtitles.size();
        info_.subtitles.push_back(t);
        ++added;
    }
    avformat_close_input(&f);
    if (!added) {
        error = "该文件没有文本字幕轨";
        return false;
    }
    return true;
}

bool Engine::loadExternalSubtitle(const std::wstring& path, std::string& error) {
    int first = -1;
    if (!addExternalSubtitle(path, error, first)) return false;
    if (first < 0) return false;
    // SRT/ASS imports are small: decode on the UI thread so cues are ready
    // immediately. This also invalidates any async scan of the old track.
    return loadSubtitleTrackNow(first);
}


int SidecarSubScore(const std::wstring& path) {
    std::wstring n = std::filesystem::path(path).filename().wstring();
    for (wchar_t& c : n) c = (wchar_t)towlower(c);
    int score = 0;
    if (n.find(L".sc.") != std::wstring::npos || n.find(L".chs.") != std::wstring::npos ||
        n.find(L"zh-cn") != std::wstring::npos || n.find(L"zh_cn") != std::wstring::npos ||
        n.find(L"简") != std::wstring::npos)
        score += 100;
    else if (n.find(L".tc.") != std::wstring::npos || n.find(L".cht.") != std::wstring::npos ||
             n.find(L"zh-tw") != std::wstring::npos || n.find(L"繁") != std::wstring::npos)
        score += 80;
    if (n.find(L".ass") != std::wstring::npos || n.find(L".ssa") != std::wstring::npos) score += 20;
    if (n.find(L"jpn") != std::wstring::npos || n.find(L".jp.") != std::wstring::npos ||
        n.find(L"eng") != std::wstring::npos || n.find(L".en.") != std::wstring::npos)
        score -= 60;
    return score;
}

int Engine::loadSidecarSubtitles() {
    if (info_.path.empty()) return -1;
    std::filesystem::path p = CleanMediaPath(std::filesystem::path(info_.path));
    std::error_code ec;
    std::filesystem::path dir = p.parent_path();
    std::wstring stem = p.stem().wstring();
    std::vector<std::wstring> cands;
    auto addCand = [&](const std::filesystem::path& f) {
        std::wstring ext = f.extension().wstring();
        for (wchar_t& c : ext) c = (wchar_t)towlower(c);
        if (ext != L".srt" && ext != L".ass" && ext != L".ssa" && ext != L".vtt") return;
        std::wstring w = f.wstring();
        if (std::find(cands.begin(), cands.end(), w) == cands.end()) cands.push_back(w);
    };
    for (const wchar_t* ext : {L".srt", L".ass", L".ssa", L".vtt"})
        addCand(dir / (stem + ext));
    // VCB/fansub convention: Episode.sc.ass / Episode.tc.ass / Episode.CHS.ass.
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        std::wstring name = e.path().filename().wstring();
        if (name.size() <= stem.size() + 1) continue;
        if (_wcsnicmp(name.c_str(), stem.c_str(), stem.size()) != 0 || name[stem.size()] != L'.') continue;
        addCand(e.path());
    }
    std::sort(cands.begin(), cands.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    int bestOrdinal = -1, bestScore = INT_MIN;
    for (const std::wstring& path : cands) {
        std::string err;
        int first = -1;
        if (!addExternalSubtitle(path, err, first)) continue;
        int score = SidecarSubScore(path);
        if (score > bestScore) {
            bestScore = score;
            bestOrdinal = first;
        }
        Log("subtitles: sidecar %s -> ordinal %d (score %d)", Narrow(path).c_str(), first, score);
    }
    return bestOrdinal;
}

void Engine::subtitleLoop() {
    for (;;) {
        SubtitleJob job;
        {
            std::unique_lock<std::mutex> lk(subtitleMu_);
            subtitleCv_.wait(lk, [this] { return subtitleStop_ || subtitleHasJob_; });
            if (subtitleStop_) return;
            job = std::move(subtitleJob_);
            subtitleHasJob_ = false;
            subtitleAbort_ = false;
            subtitleLoading_ = true;
        }
        SubtitleDecodeResult result = decodeSubtitleTrack(job, &subtitleAbort_);
        {
            std::lock_guard<std::mutex> lk(subtitleMu_);
            if (subtitleStop_) return;
            if (job.serial == subtitleSerial_) {
                subtitleCues_ = std::move(result.cues);
                assTrack_ = std::move(result.ass);
                subtitleTrack_ = result.opened ? job.ordinal : -1;
                subtitleLoading_ = false;
            }
        }
    }
}

bool Engine::hasSubtitles() const {
    std::lock_guard<std::mutex> lk(subtitleMu_);
    return !subtitleCues_.empty();
}

int Engine::subtitleTrack() const {
    std::lock_guard<std::mutex> lk(subtitleMu_);
    return subtitleTrack_;
}

SubtitleBackend Engine::subtitleBackend() const {
    std::lock_guard<std::mutex> lk(subtitleMu_);
    const int t = subtitleTrack_;
    if (t < 0 || t >= (int)info_.subtitles.size()) return SubtitleBackend::None;
    return info_.subtitles[t].backend;
}

std::shared_ptr<const AssTrackData> Engine::assTrack() const {
    std::lock_guard<std::mutex> lk(subtitleMu_);
    return assTrack_;
}

bool Engine::subtitlePending() const {
    std::lock_guard<std::mutex> lk(subtitleMu_);
    return subtitleLoading_;
}

std::wstring Engine::subtitleAt(double t) const {
    std::lock_guard<std::mutex> lk(subtitleMu_);
    if (subtitleCues_.empty()) return {};
    auto it = std::upper_bound(subtitleCues_.begin(), subtitleCues_.end(), t,
                               [](double value, const SubtitleCue& cue) { return value < cue.start; });
    while (it != subtitleCues_.begin()) {
        --it;
        if (t >= it->start && t < it->end) return it->text;
    }
    return {};
}

bool Engine::warmup(double seconds) {
    if (!fmt_) return false;
    seek(seconds);
    double t = seconds;
    for (int i = 0; i < 400 && !quit_.load(); ++i) {
        if (queueSize() > 0) {
            // Wait for the seek target to be reached.
            std::lock_guard<std::mutex> lk(queueMu_);
            if (!queue_.empty() && queue_.front()->pts + 0.001 >= t) return true;
        }
        Sleep(5);
    }
    return queueSize() > 0;
}
