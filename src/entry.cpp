// Prism Player - window, message loop and command line entry point.
#include "common.h"
#include "gfx.h"
#include "ui.h"

namespace {

Gfx g_gfx;
App* g_app = nullptr;
volatile LONG g_gsTraced = 0;

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

// ---------------------------------------------------------------------------
// Diagnostics: with PRISM_TRACE_GS=1, patch __report_gsfailure so a stack
// cookie failure prints a stack trace before the process dies. Debug only.
using CaptureFn = USHORT(WINAPI*)(ULONG, ULONG, PVOID*, PULONG);

void __cdecl GsTracer(uintptr_t cookie) {
    InterlockedExchange(&g_gsTraced, 1);
    HMODULE base = GetModuleHandleW(nullptr);
    auto capture = (CaptureFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlCaptureStackBackTrace");
    void* frames[48]{};
    ULONG hash = 0;
    USHORT n = capture ? capture(1, 48, frames, &hash) : 0;
    Log("=== GS FAILURE cookie=%p frames=%u base=%p", (void*)cookie, n, (void*)base);
    for (USHORT i = 0; i < n; ++i) {
        uintptr_t addr = (uintptr_t)frames[i];
        Log("  #%02u %p rva=0x%llX", i, frames[i], (unsigned long long)(addr - (uintptr_t)base));
    }
    TerminateProcess(GetCurrentProcess(), 99);
}

void InstallGsTracer() {
    wchar_t flag[8]{};
    if (!GetEnvironmentVariableW(L"PRISM_TRACE_GS", flag, 8)) return;
    std::string mapText = ReadTextFile(AppDir() + L"\\Prism.map");
    size_t pos = mapText.find("__report_gsfailure");
    if (pos == std::string::npos) {
        Log("gs tracer: symbol not found");
        return;
    }
    size_t lineEnd = mapText.find('\n', pos);
    if (lineEnd == std::string::npos) lineEnd = mapText.size();
    std::string line = mapText.substr(pos, lineEnd - pos);
    size_t addrPos = line.find("0000000140");
    if (addrPos == std::string::npos) {
        Log("gs tracer: address not found");
        return;
    }
    uint64_t va = strtoull(line.c_str() + addrPos, nullptr, 16);
    uintptr_t target = (uintptr_t)GetModuleHandleW(nullptr) + (uintptr_t)(va - 0x140000000ull);
    DWORD old = 0;
    if (!VirtualProtect((void*)target, 12, PAGE_EXECUTE_READWRITE, &old)) {
        Log("gs tracer: VirtualProtect failed");
        return;
    }
    unsigned char patch[12] = {0x48, 0xB8};
    void* hook = (void*)&GsTracer;
    memcpy(patch + 2, &hook, 8);
    patch[10] = 0xFF;
    patch[11] = 0xE0;
    memcpy((void*)target, patch, 12);
    FlushInstructionCache(GetCurrentProcess(), (void*)target, 12);
    Log("gs tracer installed at %p (rva 0x%llX)", (void*)target, (unsigned long long)(va - 0x140000000ull));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_app) {
        LRESULT out = 0;
        if (g_app->handleMessage(hwnd, msg, wParam, lParam, out)) return out;
    }
    switch (msg) {
        case WM_NCCALCSIZE: {
            if (!wParam) break;
            if (IsZoomed(hwnd)) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                UINT dpi = GetDpiForWindow(hwnd);
                int bx = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                int by = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                p->rgrc[0].left += bx;
                p->rgrc[0].right -= bx;
                p->rgrc[0].top += by;
                p->rgrc[0].bottom -= by;
            }
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            UINT dpi = GetDpiForWindow(hwnd);
            mmi->ptMinTrackSize.x = MulDiv(1200, dpi, 96);
            mmi->ptMinTrackSize.y = MulDiv(760, dpi, 96);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            if (g_app) g_app->shutdown();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int RunWindow(const std::wstring& initialMedia) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(8, 9, 13));
    wc.lpszClassName = L"PrismPlayerWindow";
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    UINT dpi = GetDpiForSystem();
    int w = MulDiv(1440, dpi, 96);
    int h = MulDiv(900, dpi, 96);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int workW = work.right - work.left, workH = work.bottom - work.top;
    w = std::min(w, workW - MulDiv(48, dpi, 96));
    h = std::min(h, workH - MulDiv(48, dpi, 96));
    int x = work.left + (workW - w) / 2;
    int y = work.top + (workH - h) / 2;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Prism", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr,
                                nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        Log("CreateWindowEx failed: %lu", GetLastError());
        return 1;
    }
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    int corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
    COLORREF border = 0xFFFFFFFE;  // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));

    RECT client{};
    GetClientRect(hwnd, &client);
    if (!g_gfx.initWindow(hwnd, client.right - client.left, client.bottom - client.top, dpi / 96.f)) {
        Log("Gfx init failed");
        return 2;
    }
    auto appStorage = std::make_unique<App>();
    App& app = *appStorage;
    g_app = &app;
    if (!app.init(&g_gfx, hwnd, false)) {
        Log("App init failed");
        return 3;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    if (!initialMedia.empty()) app.openPath(initialMedia, true);

    double last = QpcSeconds();
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        double now = QpcSeconds();
        double dt = now - last;
        last = now;
        if (IsIconic(hwnd)) {
            Sleep(16);
            continue;
        }
        app.tick(now, dt);
        if (app.wantsRender(now)) app.render(now);
        DWORD wait = app.wantsRender(now) ? 4 : 16;
        MsgWaitForMultipleObjectsEx(0, nullptr, wait, QS_ALLINPUT, 0);
    }
    g_app = nullptr;
    app.shutdown();
    g_gfx.shutdown();
    return 0;
}

struct Options {
    std::wstring render;
    std::wstring screen = L"home";
    std::wstring media;
    std::wstring popup;
    std::wstring playlistDir;
    std::wstring initialMedia;
    std::wstring subMode;
    bool stats = false;
    int secondary = -1;
    double time = 0;
    int width = 1440;
    int height = 900;
    int scale = 100;
};

bool ParseArgs(int argc, wchar_t** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&](std::wstring& out) {
            if (i + 1 < argc) out = argv[++i];
        };
        if (a == L"--render") next(o.render);
        else if (a == L"--screen") next(o.screen);
        else if (a == L"--media") next(o.media);
        else if (a == L"--popup") next(o.popup);
        else if (a == L"--stats") o.stats = true;
        else if (a == L"--secondary") { std::wstring v; next(v); o.secondary = _wtoi(v.c_str()); }
        else if (a == L"--playlist") next(o.playlistDir);
        else if (a == L"--sub-mode") next(o.subMode);
        else if (a == L"--time") {
            std::wstring v;
            next(v);
            o.time = _wtof(v.c_str());
        } else if (a == L"--width") {
            std::wstring v;
            next(v);
            o.width = _wtoi(v.c_str());
        } else if (a == L"--height") {
            std::wstring v;
            next(v);
            o.height = _wtoi(v.c_str());
        } else if (a == L"--scale") {
            std::wstring v;
            next(v);
            o.scale = _wtoi(v.c_str());
        } else if (!a.empty() && a[0] != L'-') {
            if (o.initialMedia.empty()) o.initialMedia = a;
        }
    }
    return !o.render.empty();
}

int RunHeadless(const Options& o) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    float scale = o.scale / 100.f;
    if (!g_gfx.initDevice()) return 2;
    if (!g_gfx.initHeadless(o.width, o.height, scale)) return 3;

    bool ok = false;
    {
        auto appStorage = std::make_unique<App>();
        App& app = *appStorage;
        g_app = &app;
        if (!app.init(&g_gfx, nullptr, true)) return 4;

        Screen screen = o.screen == L"player" ? Screen::Player : Screen::Home;
        if (!app.headlessSetup(o.media, o.time, screen)) Log("headless setup failed");
        if (o.subMode == L"auto") app.headlessSetSubtitleMode(0);
        else if (o.subMode == L"original") app.headlessSetSubtitleMode(1);
        else if (o.subMode == L"custom") app.headlessSetSubtitleMode(2);
        if (!o.playlistDir.empty()) {
            std::vector<std::wstring> files;
            std::error_code ec;
            for (auto& e : std::filesystem::directory_iterator(o.playlistDir, ec)) {
                if (!e.is_regular_file()) continue;
                std::wstring ext = FileExtOf(e.path().wstring());
                if (IsMediaExt(ext)) files.push_back(e.path().wstring());
            }
            std::sort(files.begin(), files.end());
            if (!files.empty()) app.headlessSetPlaylist(files);
        }
        int popup = 0;
        if (o.popup == L"settings") popup = 1;
        else if (o.popup == L"playlist") popup = 2;
        else if (o.popup == L"speed") popup = 3;
        else if (o.popup == L"subtitles") popup = 4;
        else if (o.popup == L"substyle") popup = 5;
        else if (o.popup == L"font") popup = 6;
        else if (o.popup == L"files") popup = 7;
        else if (o.popup == L"subfiles") popup = 8;
        else if (o.popup == L"keys") popup = 9;
        if (popup) app.headlessSetPopup(popup);
        if (o.stats) app.headlessShowStats();
        if (o.secondary >= 0) app.headlessSetSecondary(o.secondary);
        app.headlessWarmThumbnails();
        app.render(QpcSeconds());
        std::filesystem::path outPath(o.render);
        if (outPath.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(outPath.parent_path(), ec);
        }
        ok = app.headlessSave(o.render);
        app.shutdown();
        g_app = nullptr;
    }
    g_gfx.shutdown();
    // The process has finished all work and saved its output; exit directly
    // rather than unwinding the large headless frame.
    ExitProcess(ok ? 0 : 5);
    return ok ? 0 : 5;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    InstallGsTracer();
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    auto optionsStorage = std::make_unique<Options>();
    Options& options = *optionsStorage;
    bool headless = ParseArgs(argc, argv, options);
    int rc = headless ? RunHeadless(options) : RunWindow(options.initialMedia);
    if (argv) LocalFree(argv);
    return rc;
}
