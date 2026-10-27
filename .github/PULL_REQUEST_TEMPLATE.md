## What changed

<!-- One paragraph; link the issue if there is one. -->

## Verification

<!-- Exact commands and results. For UI changes, attach the rendered PNG. -->

```powershell
.\build.ps1 -Clean
build\bin\Prism.exe --render output\check.png --screen player --media media\demo-subtitled.mkv --time 3
```

## Tested on

- Windows:
- GPU / display:
- Audio device:
- Media (container / codec / HDR / tracks):

## Checklist

- [ ] Header changes were followed by a clean build.
- [ ] No `_vendor/`, `build/`, `dist/`, `media/` or `output/` files are committed.
- [ ] New dependencies and licenses are reflected in `THIRD_PARTY_NOTICES.md`.
- [ ] The change is licensed under AGPL-3.0-only.
