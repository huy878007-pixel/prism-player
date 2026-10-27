# Third-party notices

Prism bundles and links against the following third-party software:

## FFmpeg 8.1 (master build)

- Project: https://ffmpeg.org/
- Binary source: BtbN FFmpeg-Builds, `ffmpeg-master-latest-win64-gpl-shared.zip`
- License: GNU General Public License v3 (the build is configured with `--enable-gpl --enable-version3`
  and includes GPL components such as libx264/libx265).
- The full license text is distributed with the binary as `LICENSE-FFMPEG.txt`.
- Prism's source, including all modifications, is provided in this repository. When redistributing
  Prism binaries, comply with the GPL (source availability, notices, no additional restrictions).

## libass 0.17.5 (native ASS/SSA subtitles)

- Project: https://github.com/libass/libass
- Source: `libass-0.17.5.tar.xz` from the official GitHub release.
- License: ISC. The full license text is in `_vendor/libass/LICENSE-libass.txt` and is
  distributed as `LICENSE-LIBASS.txt`.
- Binary: `_vendor/libass/libass-9.dll`, a 64-bit Windows build made with MinGW-w64 GCC 16.1
  (UCRT), NASM 3.01 and the DirectWrite font provider. It is loaded at runtime with
  `LoadLibrary`/`GetProcAddress`; Prism does not link against it.
- The DLL statically contains these dependencies (license texts in `_vendor/libass/licenses/`):
  - FreeType 2.13.3 — FreeType License (FTL), https://freetype.org/
  - FriBidi 1.0.16 — GNU LGPL v2.1+, https://github.com/fribidi/fribidi
  - HarfBuzz 10.4.0 — Old MIT, https://github.com/harfbuzz/harfbuzz
  - libunibreak 6.1 — zlib-style license, https://github.com/adah1972/libunibreak
- FriBidi is linked statically under LGPL-2.1+. The build is reproducible from the upstream
  sources and versions above; see `_vendor/libass/README.md`. If you redistribute Prism,
  keep these notices and make the corresponding library sources available.

## Microsoft Windows SDK / DirectX

- Direct3D 11, Direct2D, DirectWrite, Windows Imaging Component (WIC), WASAPI are used through the
  Windows SDK. Redistribution is governed by the Microsoft Windows SDK license; the relevant runtime
  components ship with Windows.

Fonts used by the UI (Segoe UI Variable, Microsoft YaHei UI, Consolas) are system fonts and are not
redistributed with Prism.
