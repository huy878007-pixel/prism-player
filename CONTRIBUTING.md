# Contributing to Prism

Prism is a Windows-native C++20 player. Small, focused patches are preferred over broad refactors.

## Before you start

- Build the project from source (see [README](README.md#从源码构建)). The repository intentionally
  does not contain FFmpeg binaries or `_vendor/`.
- Use Visual Studio 2022, CMake, Ninja and NASM.
- If you change a header, do a clean build: `.\build.ps1 -Clean`. MSVC dependency tracking can miss
  header changes in localized environments.

## Pull requests

- Keep the change scoped to one problem.
- Include the exact commands and results you used to verify it (headless `--render` is usually enough
  for UI changes; mention the rendered file).
- For playback/decoder changes, say which file, codec, HDR/SDR state and GPU/audio device you tested.
- Do not commit `_vendor/`, `build/`, `dist/`, `media/`, `output/` or downloaded media.
- Do not add a new binary dependency without updating `THIRD_PARTY_NOTICES.md`.

## License

By contributing you agree that your contribution is licensed under the
**GNU Affero General Public License v3.0 only**, the same license as the project.
