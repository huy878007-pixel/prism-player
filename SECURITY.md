# Security policy

## Supported versions

The latest release on the `main` branch is supported. Older builds are not maintained.

## Reporting

Please report security issues privately through
[GitHub Security Advisories](https://github.com/huy878007-pixel/prism-player/security/advisories/new)
rather than a public issue.

Include:

- Prism version / commit
- Windows version and GPU driver
- The media file or a minimal reproduction (a malformed MKV/ASS sample is ideal)
- Steps to reproduce and the last lines of `%LOCALAPPDATA%\Prism\prism.log`

## Scope

Relevant areas include FFmpeg demux/decode input handling, subtitle file parsing, the in-app file
browser, and loading of local font/subtitle/audio sidecars. Prism does not open network streams.

## No warranty

Prism is a personal project distributed under AGPL-3.0 without warranty. Use it at your own risk.
