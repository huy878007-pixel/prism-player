#include "common.h"

#include <cstdlib>

bool g_hdrOutput = false;

std::wstring Widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::string Narrow(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring FileExtOf(const std::wstring& path) {
    size_t p = path.find_last_of(L'.');
    size_t s = path.find_last_of(L"\\/");
    if (p == std::wstring::npos || (s != std::wstring::npos && p < s)) return {};
    std::wstring e = path.substr(p + 1);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

std::wstring FormatTime(double seconds, bool withMillis) {
    if (!(seconds > 0) || !std::isfinite(seconds)) seconds = 0;
    int64_t total = (int64_t)(seconds + 0.0005);
    int h = (int)(total / 3600);
    int m = (int)((total / 60) % 60);
    int s = (int)(total % 60);
    wchar_t buf[32];
    if (withMillis) {
        int ms = (int)((seconds - (double)total) * 1000.0);
        if (ms < 0) ms = 0;
        if (h > 0) swprintf_s(buf, L"%d:%02d:%02d.%03d", h, m, s, ms);
        else swprintf_s(buf, L"%02d:%02d.%03d", m, s, ms);
    } else if (h > 0) {
        swprintf_s(buf, L"%d:%02d:%02d", h, m, s);
    } else {
        swprintf_s(buf, L"%02d:%02d", m, s);
    }
    return buf;
}

std::wstring FormatBitrate(int64_t bps) {
    wchar_t buf[64];
    if (bps >= 10000000) swprintf_s(buf, L"%.1f Mbps", bps / 1e6);
    else if (bps >= 1000000) swprintf_s(buf, L"%.2f Mbps", bps / 1e6);
    else swprintf_s(buf, L"%lld kbps", (long long)(bps / 1000));
    return buf;
}

std::wstring FormatSize(int64_t bytes) {
    wchar_t buf[64];
    if (bytes >= (int64_t)1 << 30) swprintf_s(buf, L"%.2f GB", bytes / 1073741824.0);
    else if (bytes >= (int64_t)1 << 20) swprintf_s(buf, L"%.1f MB", bytes / 1048576.0);
    else swprintf_s(buf, L"%lld KB", (long long)(bytes / 1024));
    return buf;
}

bool IsVideoExt(const std::wstring& e) {
    static const wchar_t* k[] = {L"mkv", L"mp4", L"mov", L"avi", L"wmv", L"flv", L"ts", L"m2ts", L"mts",
                                 L"webm", L"ogv", L"rmvb", L"rm", L"mpg", L"mpeg", L"vob", L"3gp", L"m4v",
                                 L"f4v", L"asf", L"divx", L"mxf", L"m2v", L"ogm", L"dv", L"m3u8", L"mpd"};
    for (auto* x : k) if (e == x) return true;
    return false;
}

bool IsAudioExt(const std::wstring& e) {
    static const wchar_t* k[] = {L"mp3", L"flac", L"wav", L"aac", L"m4a", L"ogg", L"opus", L"wma",
                                 L"ape", L"dts", L"ac3", L"mka", L"alac", L"aiff", L"aif", L"m4b"};
    for (auto* x : k) if (e == x) return true;
    return false;
}

bool IsMediaExt(const std::wstring& e) { return IsVideoExt(e) || IsAudioExt(e); }

bool IsSubtitleExt(const std::wstring& e) {
    return e == L"srt" || e == L"ass" || e == L"ssa" || e == L"vtt" || e == L"sub";
}

bool IsPartialName(const std::wstring& name) {
    static const wchar_t* k[] = {L".qkdownloading", L".part", L".download", L".crdownload",
                                 L".tmp", L".td", L".!qb"};
    for (const wchar_t* s : k) {
        const size_t n = wcslen(s);
        if (name.size() > n && _wcsicmp(name.c_str() + name.size() - n, s) == 0) return true;
    }
    return false;
}

std::wstring StripPartialName(const std::wstring& name) {
    std::wstring s = name;
    while (IsPartialName(s)) {
        size_t p = s.find_last_of(L'.');
        if (p == std::wstring::npos) break;
        s.resize(p);
    }
    return s;
}

bool IsPlayableMediaName(const std::wstring& name) {
    return IsMediaExt(FileExtOf(StripPartialName(name)));
}

std::wstring& AppDirRef() {
    static std::wstring dir = [] {
        wchar_t buf[MAX_PATH]{};
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring p = buf;
        size_t s = p.find_last_of(L"\\/");
        return s == std::wstring::npos ? std::wstring(L".") : p.substr(0, s);
    }();
    return dir;
}

const std::wstring& AppDir() { return AppDirRef(); }

const std::wstring& DataDir() {
    static std::wstring dir = [] {
        PWSTR p = nullptr;
        std::wstring out;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) out = p;
        CoTaskMemFree(p);
        if (out.empty()) out = AppDirRef();
        out += L"\\Prism";
        std::filesystem::create_directories(out);
        return out;
    }();
    return dir;
}

void Log(const char* fmt, ...) {
    static std::mutex mu;
    static FILE* fp = [] {
        std::wstring path = DataDir() + L"\\prism.log";
        wchar_t alt[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableW(L"PRISM_LOG", alt, MAX_PATH);
        if (n > 0 && n < MAX_PATH) path.assign(alt, n);
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"ab") != 0) return (FILE*)nullptr;
        return f;
    }();
    std::lock_guard<std::mutex> lock(mu);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (fp) {
        fprintf(fp, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fflush(fp);
    }
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

std::string ReadTextFile(const std::wstring& path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string s((size_t)std::max(0L, n), '\0');
    if (n > 0 && fread(s.data(), 1, (size_t)n, f) != (size_t)n) s.resize(0);
    fclose(f);
    return s;
}

bool WriteTextFile(const std::wstring& path, const std::string& text) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}
