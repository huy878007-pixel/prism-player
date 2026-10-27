# Prism · AI 交接与优化说明书

> 面向下一个接手本项目的 AI/工程师。目标不是“重新介绍产品”，而是让你在最少上下文下安全地继续改、
> 跑测试、定位问题、避免把已经验证过的 HDR/音频/字幕/目录逻辑改坏。
>
> 当前版本：**1.0.2**（源码 main，Release v1.0.2）
> 最后构建：`build\bin\Prism.exe`，878,080 bytes，2026-10-27 12:38:39
> 构建目录：`D:\Document\ChatGPT\SOI\player`；Git 工作副本：`D:\Document\ChatGPT\SOI\.prism-upload`

---

## 1. 项目是什么

Prism（棱镜播放器）是一个 **Windows 原生 C++20 本地视频/音频播放器**：

- Win32 窗口 + Direct2D 1.1 自绘 UI，无 Electron/Qt/WPF。
- Direct3D 11 负责视频上屏；HDR 走 **16F scRGB** 交换链。
- FFmpeg 8.1 shared 负责解封装/解码；D3D11VA 优先，失败回退软解。
- WASAPI 负责音频输出（共享/独占尝试）。
- libass 0.17.5 负责 ASS/SSA 原生字幕（运行时 `LoadLibrary`，不是链接依赖）。
- 自写 AVX2 内核 + NASM 路径做 10-bit 打包/颜色转换。

产品定位：**个人使用、按真实片源打磨**。不是商业播放器；没有自动更新、网络流、遥测。

---

## 2. 当前仓库与发布

- GitHub（公开）：https://github.com/huy878007-pixel/prism-player
- 许可：**AGPL-3.0-only**（`LICENSE`、`NOTICE`）
- 最新 Release：**v1.0.2**，Windows x64 zip
  - `Prism-1.0.2-win64.zip`，约 74 MB
  - SHA256 `91A33E6FF10DA58598F3B1465F4D4A41DEC4396F0A504CDB9806A214DB1FBE6B`
- GitHub 上只有源码、文档、截图、图标和注册脚本；**不含 `_vendor/`、`dist/`、媒体文件**。
- 本地 Git 工作副本在 `D:\Document\ChatGPT\SOI\.prism-upload`；实际构建/运行目录是
  `D:\Document\ChatGPT\SOI\player`。两者需要手动同步源码后提交。
- 仓库是 **public**；不要提交个人路径、密钥、下载的影视资源或商业字体。

### 目录约定

```text
player/
  src/                 源码
  resources/           manifest、icon、prism.rc、file-icons/、RegisterFileTypes.ps1
  docs/                benchmark、格式验证、截图、本文档
  media/               本地测试媒体（不提交）
  samples/             小样例（不提交）
  build/               CMake/Ninja 输出（不提交）
  dist/Prism/          本地运行包（不提交；Release 另发）
  _vendor/             FFmpeg/libass（不提交）
  output/              headless 渲染输出（不提交）
  build.ps1            MSVC/Ninja 构建
  CMakeLists.txt
  README.md
  LICENSE / NOTICE / THIRD_PARTY_NOTICES.md
```

---

## 3. 构建与运行

### 依赖

- Windows 10/11 x64
- Visual Studio 2022 C++、CMake、Ninja、NASM
- BtbN FFmpeg 8.1 **shared win64 GPL**：
  `_vendor/ffmpeg/ffmpeg-master-latest-win64-gpl-shared/`（include + lib + bin）
- libass 0.17.5 x64：
  `_vendor/libass/include/ass/` + `_vendor/libass/libass-9.dll`
- 运行包还需要 FFmpeg DLL（`avcodec-63`、`avformat-63`、`avutil-61`、`swresample-7`、`swscale-10`）。

### 命令

```powershell
cd D:\Document\ChatGPT\SOI\player
.\build.ps1 -Clean        # 改头文件/结构体/枚举后必须 clean
.\build.ps1 -Fast         # 只改 .cpp 时可以用
.\build.ps1 -Fast -Dist   # 重新打包 dist\Prism
```

**关键坑**：MSVC 的 `/showIncludes` 在中文 VS 环境下可能让 Ninja 头文件依赖失效。任何
`src/*.h` 改动后都用 `-Clean`，否则会出现对象布局不一致导致的堆损坏/退出崩溃（本项目真实踩过）。

### 运行

```powershell
.\dist\Prism\Prism.exe
```

或 `dist\Prism\create desktop shortcut.cmd` / `RegisterFileTypes.ps1`。

### Headless 渲染（最重要的回归方式）

```powershell
build\bin\Prism.exe --render output\check.png --screen player `
  --media media\demo-subtitled.mkv --time 3 --width 1440 --height 900

build\bin\Prism.exe --render output\settings.png --screen player `
  --media media\demo-subtitled.mkv --time 3 --popup settings --width 1440 --height 900

build\bin\Prism.exe --render output\stats.png --screen player `
  --media media\demo-subtitled.mkv --time 3 --stats --width 1440 --height 900

build\bin\Prism.exe --render output\secondary.png --screen player `
  --media media\demo-subtitled.mkv --time 3 --secondary 1 --width 1440 --height 900
```

`--popup` 支持：`settings`、`playlist`、`speed`、`subtitles`、`substyle`、`font`、`files`、
`subfiles`、`keys`。`--playlist <dir>`、`--sub-mode auto|original|custom`、`--stats`、
`--secondary N` 也可用。

**PowerShell 注意**：Prism 是 GUI 子系统，`& exe` 不会等待；读 ExitCode 用
`Start-Process -Wait -PassThru`。

---

## 4. 源码地图

| 文件 | 行数 | 职责 |
|---|---:|---|
| `src/ui.cpp` | 2521 | App 状态机、设置、播放列表、字幕菜单、文件浏览器、快捷键编辑器、系统统计 |
| `src/engine.cpp` | 1963 | FFmpeg demux/decode、D3D11VA、A/V 时钟、seek、字幕任务、外挂音轨、部分文件 IO |
| `src/draw.cpp` | 1397 | Home/Player/Controls/Panels/Stats/Thumbnails/Toast 绘制 |
| `src/gfx.cpp` | 673 | D3D11/D2D/DXGI、scRGB HDR、YUV/P010 着色器、尺寸/设备丢失 |
| `src/audio.cpp` | 500 | WASAPI、环形缓冲、重采样、时钟、预填充/淡入/限幅 |
| `src/ui.h` | 416 | App 字段、Popup/命中 ID、设置/缓存/快捷键状态 |
| `src/paint.cpp` | 356 | Direct2D 立即模式、文本格式缓存、BGRA 位图 |
| `src/entry.cpp` | 318 | Win32 窗口、消息循环、headless CLI、崩溃诊断 |
| `src/engine.h` | 294 | Public Engine API、MediaInfo/EngineStats、字幕类型 |
| `src/thumbs.cpp` | 292 | Windows Shell 缩略图 + FFmpeg 兜底解码 |
| `src/asm_c.cpp` | 257 | AVX2 颜色内核、P010/10-bit 打包 |
| `src/ass_render.cpp` | 222 | libass 动态加载、ASS 合成 BGRA |
| `src/common.cpp` | 168 | 路径/扩展名/时间/日志/部分文件判断 |
| `src/asm/kernels.asm` | 109 | NASM 内核 |

UI 没有控件树：`layout()` 每次根据 `W_/H_` 计算 rect，`hits_` 存命中区，`activate(id)` 分发；
`draw()` 立即模式重画。所有坐标是 DIP，`gfx_->scale()` 处理 DPI。

---

## 5. 数据流

### 5.1 打开文件

```text
App::openPath / openFiles
  └─ Engine::open
       ├─ avformat_open_input / find_stream_info
       ├─ D3D11VA device + video decoder（失败回退软解）
       ├─ 音频流 / 外挂 MKA（FindSidecarAudio）
       ├─ 字幕轨道枚举 / 外挂字幕侧载（loadSidecarSubtitles）
       ├─ 部分文件：自定义 AVIO + GrowingIo
       ├─ worker_ 视频/音频解复用线程
       └─ subtitleThread_ 主字幕异步扫描
  └─ UI playlist、HDR 交换链切换、设置/记忆位置
```

### 5.2 视频

`worker()` 读包 → `decodeVideo()` → D3D11VA 帧 `av_hwframe_transfer_data` 到 CPU →
`convertFrame()`：
- HDR + P010：解交织 UV、`>>6` 成 10-bit 低位平面。
- SDR P010：`Pack16To8<8>`。
- 普通 YUV：`asm_c`/C++ 转 BGRA。
→ `queue_` → UI `acquireFrame()` → `Gfx::updateVideo...` → D3D11 shader（PQ/HLG→线性→
BT.2020→709→scRGB `/80`，或 Hable）→ D2D 绘制到窗口/headless target。

### 5.3 音频

`AudioOut` 内部 WASAPI 线程 + 4 秒环形缓冲；`decodeAudio()` 写入 `audio_.write()`；
外挂音轨由 `audioWorker()` 在独立线程读第二个 `AVFormatContext`，seek 通过
`audioExtSeek_` 与视频同步。`position()` 优先使用音频时钟，暂停/等待下载时冻结。

### 5.4 字幕

主字幕：`subtitleThread_` 异步打开媒体/外挂文件 → `decodeSubtitleTrack()` → 文本 cues 或
`AssTrackData`；UI 每帧 `subtitleAt()`。ASS 只有在需要时才 `ensureAssRenderer()` 加载
libass-9.dll；主字幕可以走原生 ASS 位图，副字幕是纯文本层。
双字幕：`Alt+V`，`Engine::loadSecondarySubtitle()` **同步**解码一条纯文本轨，显示在主字幕上方。

### 5.5 缩略图

两个 `ThumbnailCache` 实例（文件浏览器、Home 最近播放），各自一个 STA 工作线程：
1. `IShellItemImageFactory::GetImage`（先 THUMBNAILONLY，失败 ICONONLY）。
2. 若仍无图，`LoadFfmpegImage()` seek 到约 1 秒，解一帧，`sws_scale` 到 160×90 BGRA。
3. UI 线程 `CreateBitmapFromBgra()`，缓存 bitmap。

### 5.6 线程清单

- UI 主线程：消息、布局、绘制、设置、按钮。
- `Engine::worker_`：demux、视频解码、内嵌音频解码。
- `Engine::audioThread_`：仅外挂音轨。
- `Engine::subtitleThread_`：主字幕异步扫描。
- WASAPI 音频回调线程（`AudioOut`）。
- 两个缩略图 STA 线程。
- 注意：`subtitleCues_`/`subtitleCues2_`/`secondaryTrack_` 都受 `subtitleMu_` 保护；
  `queue_` 受 `queueMu_`；`audio_` 内部锁。

---

## 6. 关键设计（不要随手改坏）

### HDR

- HDR 显示器：`DXGI_FORMAT_R16G16B16A16_FLOAT` 交换链 +
  `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709`（scRGB）。
- UI 颜色 `Hex()` 在 HDR 输出时 sRGB→线性×`203/80`（SDR 白 203 nits）。
- shader：PQ/BT.2020 → 线性 nits → BT.709 → `/80`；SDR 显示器走 Hable。
- 为什么不用 10-bit PQ 交换链：D2D 在这个驱动上不支持
  `R10G10B10A2_UNORM` target（`0x88982F80`），所以 16F scRGB 是可行路径。
- `Engine::MediaInfo.video.hdr` 目前把 **HLG（ARIB STD B67）也标记为 hdr**，但 shader 只做
  PQ 解码——HLG 是已知缺口。
- 杜比视界只使用 HDR10 兼容基底；动态 RPU 未解析。

### 硬解

- `av_hwdevice_ctx_create(D3D11VA)` → `get_format` 选 `AV_PIX_FMT_D3D11`；
- 失败/transfer 失败 → `fallbackToSoftware()`，seek 到 `lastVideoPts_`，`hwFailed_` 锁存；
- 不是零拷贝：每帧 P010 回读 CPU，再解交织上屏。4K HDR CPU 约 3.9%，但 4K60/零拷贝未做。
- `ass_render.cpp` 与 FFmpeg 不共享 D3D 设备。

### 音频

- 共享 48k/2ch/32f；独占会先探测 48k/44.1k × 5.1/2ch × 32f/24/16，失败回退共享；
  本机设备通常拒绝独占。
- 5.1 在共享下混立体声。
- 启动有静音预填充、10 ms 淡入、限幅；seek 使用 `audio_.flush(keepPlaying)`。
- 外挂 MKA 多轨：`audioTracks` + `selectAudio()`/`cycleAudio()`；`A`/`Shift+A`。

### 字幕

- 文本：`SubtitleCue {start,end,text}`；ASS 文本解析会去标签。
- ASS：`AssTrackData {header, events}`，libass 动态加载；字体用 **系统字体**，不加载 MKV 字体附件。
- 自动侧载：同名/`.sc/.tc/.chs/.cht` 后缀；评分简体优先。
- 双字幕副轨是纯文本，不能保留 ASS 特效。
- 字幕位置 `subX_/subY_` 相对屏幕；点“位置”预设会重置偏移，避免拖丢。

### 部分文件（边下边看）

- `IsPartialName()` 识别 `.qkdownloading/.part/.download/.crdownload/.tmp/.td/.!qb`。
- 自定义 `AVIOContext`：`GrowingRead` 阻塞等待文件增长，`GetFileAttributesW` 消失视为下载器改名
  → EOF；打开阶段 10 秒 header 超时。
- UI 在 `buffering()` 时暂停播放并显示“缓冲中”，恢复后自动继续。
- 只对后缀命名的部分文件生效；**预分配成完整大小但内容是 0 的 final-name 文件不识别**。
- MP4 `moov` 在尾部时，索引到达前无法播放。

### 设置/统计/快捷键

- `settings.txt` 键：音量、静音、续播、动效、字幕样式/位置/字体、独占、`auto_next`、
  `soft_decode`、`hdr_off`、`thumbs`、`accent`、`last_dir`、最近播放。
- `keys.ini`：`[ctrl+][shift+][alt+]key = action [arg]`。
- 右方向键 `seek_boost`：短按 +5s，按住 ≥0.32s 3×，松开恢复；旧 `right=boost` 自动迁移。
- `F1` 图形快捷键编辑器；`F2` 重载；`I` 统计；`Alt+V` 副字幕。
- Settings 面板 760×640；右侧媒体数据；底部默认应用/快捷键/日志/统计。
- 顶部柱状按钮也是统计入口。

### 文件/默认应用/图标

- 全部媒体/文件夹/字幕导入使用 Prism 自绘 FileBrowser，不调用 Explorer `IFileOpenDialog`。
- `RegisterFileTypes.ps1 -Associate` 写 HKCU、注册 29 个格式图标；Windows 10/11 的 UserChoice
  是受保护哈希，脚本不能静默覆盖，必须由用户在默认应用里确认。
- 图标只在 Prism 是该扩展名默认应用时由 Explorer 使用。

---

## 6.1 关键 API（避免重新发明）

```cpp
// Engine
bool open(path, error, enableAudio = true);
void close();
void play(); void pause(); void toggle();
void seek(double seconds);
double position(); double duration();
void setSpeed(double); double speed();
void setSoftwareDecode(bool); void setExclusiveAudio(bool);
bool selectAudio(int ordinal); bool cycleAudio(int dir);
bool loadSubtitleTrack(int ordinal); bool cycleSubtitle(int dir);
std::wstring subtitleAt(double t);
bool loadSecondarySubtitle(int ordinal); std::wstring subtitleAtSecondary(double t);
VideoFrame* acquireFrame(); void releaseFrame(VideoFrame*);
const MediaInfo& info(); EngineStats stats();
```

`VideoFrame` 的 BGRA 路径是 `pixels`，HDR 路径是 `planes`（Y/U/V 16-bit planar）；
`hdr` 决定 Gfx 走哪个上传路径。`EngineStats` 包含 `frames/dropped/hwFrames/avgDecodeMs/queue/
hw/partial/buffering/bufferedAhead/underruns/starvations/audioBufferFrames/subtitleCues/
subtitleBackend/subtitleTrack/audioTracks/audioOrdinal`。

UI 侧关键入口：`App::openPath`、`App::closeMedia`、`App::tick`、`App::layout`、`App::draw`、
`App::onKeyDown/onKeyUp/onChar`、`App::activate`、`App::toggleSecondarySubtitle`、
`App::headlessSetup/headlessSave/headlessSetPopup/headlessWarmThumbnails`。

## 7. 已有功能清单（可用于回归）

- HDR10/PQ → scRGB；SDR Hable；SDR/HDR 切换；全屏 resize。
- D3D11VA 硬解 + 自动软解；H.264/HEVC 8/10bit、AV1/VP9 软解、ProRes 等。
- WASAPI；外挂 `.mka` 自动配对；多音轨切换；0 underrun 目标。
- 内嵌/外挂 SRT/ASS/SSA/VTT；后台提取；简体优先；系统字体选择；拖动/5×/延迟；双字幕。
- 边下边看；等待/恢复；下载完成改名。
- 文件夹扫描/自然排序/自动下一集/0.8s 增量刷新。
- 自绘文件浏览器 + Shell/FFmpeg 缩略图；Home 最近播放缩略图。
- 设置双栏、媒体/系统数据、播放统计浮层、图形快捷键编辑器。
- 29 个格式图标 + HKCU 注册。
- 4K HDR 实测约 2.55 ms/帧、0 丢帧、约 1.3 GB；硬解 CPU 约 3.9%。

---

## 8. 已知问题 / 风险（按优先级）

### P0：用户报告“快进之后卡着”（未完全闭环）

- 现象：右键快进/seek 后卡住。
- 已做：右键短按只 seek、长按才 boost；seek 后 `audio_.flush`、`audioMu_` 串行；
  完整 demo + JoJo 4K 外挂音轨测试 seek 后 0 underrun/0 starvation（前台窗口）。
- 未复现：带窗口在后台时曾看到 position 只前进 0.6s 的现象，可能是 UI/队列节流。
- 高概率诱因：**下载中的文件/预分配空洞文件** seek 到未下载区域，自定义 IO 会等待数据；
  或旧构建的右键立即 boost 让用户误判。
- 下一步：让用户提供具体文件 + `prism.log`；在 `seek` 后加 watchdog（2s 无新帧时记录
  queue/audio clock）；测试 hwdecode seek、P010 HDR、外挂 MKA、10 分钟长片。
- 注意：`prism.log` 被 Prism 独占打开，运行中无法用普通 `Get-Content` 读；可以加共享读
  或用 debugger，或先关闭 Prism。

### P1：字幕/画面

- **PGS/DVD/DVB 位图字幕未实现**；`SubtitleBackend::Pgs` 只识别、不渲染。
- 双字幕副轨是纯文本，非 libass 特效；同步加载大文件可能卡几百毫秒。
- 字体只用系统字体；release 的 MKV 字体附件（如 JoJo `[Fonts].7z` 里的 wqy）需要用户安装。
- HLG 未单独映射；DV 动态 RPU 忽略。
- 1080p 放大是 D2D cubic，不是 FSR/Lanczos；帧节奏未专门实现，HUD 30 可能来自外部 overlay。

### P1：性能/解码

- D3D11VA 回读 P010，**零拷贝未实现**；4K60 未验证。
- 4K HDR 内存约 1.1–1.4 GB；帧池/纹理仍可优化。
- 字幕 `subtitleAt()` 每帧 O(log n)+复制；可缓存当前 cue。
- 文件夹刷新是 0.8s 轮询，不是 `ReadDirectoryChangesW`；超大目录有成本。
- 播放列表非当前项时长为 0（没有后台 probe）。

### P1：音频

- 本机独占模式常被系统拒绝；共享 48k/2ch。
- 5.1 共享设备下混；没有音频延迟/声道映射 UI。
- 外挂音轨多轨只靠 `A`/设置行，没有完整音轨菜单。
- `audioWorker` 与外挂 seek、`audio_.flush` 的锁竞争需要压力测试。

### P2：UI/工程

- Settings 面板 640 高，极小窗口（<700px 高）可能被裁。
- `Home` 最近卡片用两个 worker；Shell 缩略图 handler 卡住会让关闭 join 变慢。
- 顶部统计图标/设置图标在极窄窗口可能挤到窗口按钮。
- 没有自动化 CI/单元测试，回归靠 headless PNG + 本机实测。
- `ui.cpp` 已 2500 行，后续建议拆 `settings_ui.cpp`/`file_browser.cpp`。
- 发布包 162 MB；依赖 FFmpeg 共享 DLL，无法单 exe 绿色版。
- 日志无共享读；崩溃时只有 WER/GS 诊断，没有 minidump。

---

## 9. 给下一任 AI 的建议顺序

1. **先稳定**：seek 卡住、日志共享读、50GB 长片/疯狂 seek/切轨、0 underrun、内存。
2. **PGS 位图字幕**：FFmpeg `AVSubtitle` rect → BGRA cue → D2D 图层；和文本/ASS 共存。
3. **双字幕升级**：副轨也走 libass 或至少独立样式/位置；异步加载。
4. **零拷贝 D3D11VA**：D3D11 纹理直接给 shader；保留软解回退和逐像素一致。
5. **帧节奏/更高阶缩放**：按显示器刷新，FSR/EASU/Lanczos；先做可切换选项。
6. **主题/密度/音轨菜单**；最后清理 `ui.cpp`。

---

## 10. 不要破坏的契约

- HDR 显示器：scRGB 16F，UI 203 nits；SDR 显示器：Hable，不黑屏。
- 硬解失败必须回退软解，不能崩；seek 后 A/V 时钟不能死锁。
- 音频启动不能爆音；seek 后不能永久静音；外挂 MKA 与视频时钟对齐。
- 字幕异步加载不能阻塞打开；字幕位置不能因为拖动永久丢失。
- 文件夹扫描/自动下一集/侧载字幕/缩略图不能因新功能退化。
- 文件/字幕导入不能再回到会卡住的原生 Explorer 对话框。
- 右方向键：短按 +5s、长按 boost；`keys.ini` 兼容旧 `right=boost`。
- AGPL-3.0-only；FFmpeg GPLv3/libass ISC 声明不能删。

---

## 11. 调试入口

- 日志：`%LOCALAPPDATA%\Prism\prism.log`（运行中独占；关闭后可读）。
- 设置：`%LOCALAPPDATA%\Prism\settings.txt`
- 快捷键：`%LOCALAPPDATA%\Prism\keys.ini`
- 字体缓存：`%LOCALAPPDATA%\Prism\icons`、系统字体目录
- 关联备份：`%LOCALAPPDATA%\Prism\assoc-backup.json`
- headless 截图：`output\*.png`
- `PRISM_LOG=<path>` 环境变量可改日志路径。
- `PRISM_DUMP_ASS=1` 会把 libass `ASS_Image` 元数据写日志（仅调试用）。
- `--render` 是 GUI 子系统，PowerShell 用 `Start-Process -Wait -PassThru` 读 ExitCode。
- 修改头文件后 clean build；否则可能堆损坏而不是编译错误。

---

## 12.5 可直接复制给下一个 AI 的开场提示

```text
请先完整阅读 D:\Document\ChatGPT\SOI\player\docs\AI_HANDOFF.md，再动代码。
不要重写 HDR/音频/字幕主链路；先在 D:\Document\ChatGPT\SOI\player 用 build.ps1 -Clean 构建，
用 --render 跑现有截图回归。当前 P0 是“seek 后卡住”的未闭环复现，P1 是 PGS、双字幕 ASS、
零拷贝、帧节奏。改头文件后必须 clean build；提交前同步到 .prism-upload 并 push。
```

## 13. 一页结论

Prism 的主链路（4K HDR、硬解回退、WASAPI、ASS/文本字幕、外挂音轨、边下边看、文件夹、
自绘 UI、缩略图、统计、快捷键）已经能真实使用；它现在的主要短板是 **seek 卡住的未闭环问题、
PGS、零拷贝、双字幕特效、帧节奏**，以及缺少自动化回归。下一个 AI 应先复现/修 seek 稳定性，
再按上面的顺序扩展，不要重写已经验证过的 HDR/音频/字幕基础。
