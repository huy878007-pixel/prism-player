# Changelog

## 1.0.0 (2026-10-27)

Initial public source snapshot.

### Playback

- FFmpeg 8.1 decode, D3D11VA hardware decode with software fallback.
- Native HDR10/PQ output through a 16F scRGB swapchain; SDR Hable fallback.
- 4K HEVC 10-bit HDR, AV1, VP9, ProRes, MPEG-2/4, H.264 and common audio formats.
- WASAPI shared/exclusive audio with automatic fallback.
- External audio sidecars (`.mka/.flac/...`) and multi-track switching.
- Growing-file playback for `.qkdownloading/.part` downloads.

### Subtitles

- Embedded text subtitle extraction on a background thread.
- Automatic sidecar discovery (`.srt/.ass/.ssa/.vtt`, `.sc/.tc/.chs/.cht` suffixes).
- libass native ASS/SSA rendering, system-font picker, drag, 0.5–5.0× scaling, delay.
- Secondary text-only subtitle layer (`Alt+V`).

### Interface

- Direct2D custom UI: home, player, playlist, settings, file browser, statistics.
- Windows Shell video thumbnails in the file browser.
- Folder playlist with natural sort and live refresh.
- Graphical shortcut editor (`F1`) and `keys.ini`.
- Per-format circular file icons and `RegisterFileTypes.ps1`.

### Known limitations

- PGS/DVD/DVB bitmap subtitles are not rendered yet.
- Hardware decode downloads P010 back to the CPU; zero-copy is not implemented.
- Dual subtitles use a plain-text secondary layer, not libass effects.
- Dolby Vision uses the HDR10 base layer; dynamic RPU metadata is ignored.
