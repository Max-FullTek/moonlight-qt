# 目录导览(Architecture)

> 回答一个问题:每个目录是谁的、干什么的。上游同构的部分**不要随意改名/移动**
> ——那会破坏与 moonlight-stream/moonlight-qt 的 merge 能力(见 [upstream-sync.md](upstream-sync.md))。

## 上游继承(与上游同构,merge 面)

| 目录/文件 | 用途 |
|---|---|
| `app/` | 客户端主体:GUI(QML)、streaming 会话、backend、settings、输入、渲染器。fork 在此有大量增量(见下) |
| `moonlight-common-c/` | 协议库 submodule,**指向我们自己的 fork**(qiin2333/moonlight-common-c),其 `mic` 分支是实际消费线;上游同步走 PR |
| `qmdnsengine/` `h264bitstream/` `third-party/AMF` | vendored / submodule 依赖 |
| `AntiHooking/` | Windows 防 DLL 注入库(上游) |
| `config.tests/` | qmake 特性探测(SL=SteamLink SDK、EGL) |
| `scripts/` | 构建与打包脚本(部分上游、部分 fork);fork 的本地工具脚本也放这里 |
| `wix/` `setup-deps.ps1/.py` | Windows 安装包与预编译依赖拉取 |
| `moonlight-qt.pro` `globaldefs.pri` | 顶层构建:SUBDIRS 注册 + 公共配置 |

## fork 新增(上游不存在,零冲突)

| 目录/文件 | 用途 |
|---|---|
| `clipboard-helper/` | 剪贴板同步 helper(独立进程) |
| `windows-lite/` | Windows Lite 的无 Qt CMake 入口，构建原生库、输入 helper、音讯 pipeline、压缩影格组装、画面排程、统计文字与 SDL 影像输出；不包含仍依赖 Qt 的完整串流 app 与剪贴板 helper |
| `usb-helper/` | macOS USB/IP 后端 helper(moonlight-usbd,C++23 + CMake,内嵌自己的 third_party 子模块) |
| `file-mapping/` | 主机文件共享:mount / protocol / vfs 三层(`FileMapping` 命名空间);`smoke/` 是手工冒烟二进制(不在 CI) |
| `tests/` | 测试目录,CI 覆盖矩阵与约定见 [../tests/README.md](../tests/README.md) |
| `docs/` | 设计文档与流程 SOP(upstream-sync、architecture 等) |
| `projects/` | 业务项目材料(**仅本地,不入库**,见 CONTRIBUTING.md) |
| `scripts/run-test.bat` | 测试统一入口(CI 与本地共用) |

## 约定

- Windows Lite 的 Qt 移除与 Rust／GPUI 迁移遵循 [迁移开发准则](qt-migration-guidelines.md)，其中记录已确认的 SA 边界、需讨论的取舍与无 Qt 验收定义。新建置路线仍需由对应 Work 确立；以下 qmake 约定继续适用于现有 Qt 路线。
- **新增顶层模块默认 qmake** 并注册进 `moonlight-qt.pro` 的 SUBDIRS;仅当「上游不存在 + 依赖 CMake-only + 纯新增目录」三条件同时满足才允许 CMake(现例:usb-helper)。
- 语言标准:C++17 为 app 地板(SteamLink 工具链上限),usb-helper 为 C++23 孤岛,不再引入第三种。
- 依赖钉值:Linux 源码依赖在 build.yml;Win/Mac 预编译组在 setup-deps(v-tag);升级要与上游对齐(见 upstream-sync.md 的依赖双轨一节)。
- 本地产物(build/、libs/、IDE 目录、.DS_Store)一律 gitignore,不入库。

## Windows Lite 原生库建置

迁移新路径采用 Cargo（GPUI 前端）与 CMake／MSVC（原生核心），保留独立核心与 IPC 方向。旧 qmake 路径继续保留；`windows-lite/` 是此迁移路径的建置例外，不适用上面的新增模块默认 qmake 约定。

当前入口为 `./scripts/build-lite-native.ps1`，需要 Windows x64、VS 2022 C++ Build Tools、CMake 3.24+，以及既有 `libs/windows` 依赖。干净 checkout 可先执行 `./scripts/prepare-portable-deps.ps1`，按现有固定 SHA256 校验并解包；已有依赖目录时不要重复解包。脚本优先使用 PATH 中的 CMake，否则寻找 VS 随附版本，不安装工具。

入口构建 `moonlight-common-c`（含 ENet／nanors）、`h264bitstream`、`AntiHooking`、光标形状分类器与 SDL 音讯输出，执行协议库既有 CTest、原生链接测试，以及键盘状态、手把传感器频率、手写笔历史选择三项输入回归测试。键盘按键身份与鬆键重试状态已改用标准 C++ 容器，测试直接复用生产 helper；光标分类器改用标准 C++ 容器、double 与只读 BGRA 指针／长度，分类门槛及 Unknown 位图回退保留，原生测试覆盖既有形状与输入边界；Qt 调用处只传递现有位图地址与长度，不新增像素复制。完整 SDL 输入处理器仍依赖 Qt。默认产物放在 `build/lite-native`；可传入 `-BuildDirectory` 或 `-Configuration Debug`。独立 CI 为 `.github/workflows/lite-native.yml`，覆盖 Release 与 Debug。

固定版本的协议库解码测试在销毁已使用的佇列前遗漏关闭操作。`windows-lite/patches/common-c-decode-test-shutdown.patch` 只修正建置目录中的测试副本，生产源码与 submodule 指针不变；原 fork 远端没有本维护者的写入权限。测试数据与生产代码仍直接来自原 submodule，升级版本时必须重新核对补丁，无法套用即停止配置。测试另将 Debug CRT 断言输出为明确失败，避免等待交互对话框。原 fork 收录此修正后，应移除此测试补丁。

SDL 音讯输出与共用音讯 renderer 介面已不依赖 Qt；仍直接复用既有 `sdlaud.cpp`、固定 SDL2／SDL3 依赖与 common-c。`sdl_audio_renderer` 测试用 dummy 音讯驱动验证 stereo／5.1／7.1、5／10 ms PCM 送出与重复开关，不启动串流、不操作实际音讯装置；SDL 错误与断言会明确失败。真实音讯装置切换、影音同步与延迟未由此测试验收。

音讯 backend 建立与 Opus→PCM 解码／送出抽至无 Qt 的 `audio/audiopipeline.*`，Qt Session 直接调用，仍负责静音、decoder／renderer 生命周期及失败重试时序；`ML_AUDIO` 的解析保留原 Qt 路径。`audio_pipeline` 使用固定 Opus 实库验证 stereo／5.1／7.1、float／s16 PCM 与直接 Opus 解码输出一致，并覆盖输出失败、空 buffer 与后端清理，不新增 PCM 中转复制。

影像解码参数与解析度使用 `videodecoderoptions.h` 的原生型别；Qt 偏好设定保留原 Q_ENUM 名称与数值，在 Session 边界转换。`overlayrenderer.h` 只保留原生渲染通知。Windows 的生产 Pacer 与 DxVsyncSource 已纳入无 Qt CMake：标准 C++ 佇列、mutex／condition variable 与 atomic 停止旗标，保留三幀佇列上限、500 ms 历史、丢幀策略、SDL 渲染线程及延后一幀释放。停止转移与等待同步，空佇列等待抵御虚假唤醒；其他平台的 Wayland Qt 桥接仍保留。

统计／状态文字的生产 `OverlayManager` 已纳入无 Qt CMake，保留既有 SDL_ttf 格式解析、字型与 surface 拥有权。owner 以 `std::string` 移入字型资料；过渡期 Session 仍透过原 Qt `Path::readDataFile()` 选择相同档案／内嵌资源，不改变查找顺序。`overlay_text` 比对修改前 Qt 版本的五组固定像素结果，并验证 surface 交接、开关、多个 owner 与字型生命周期；不代表真实串流、DPI 或 HDR 已验收。

`video/decodebuffer.*` 复用原压缩包拼接、H.264 SPS 修正与 AV1 OBU 重排，由 Qt FFmpeg decoder 直接调用；可重用原生缓冲区在扩容时无需复制旧包，每包实际长度后明确补零，满足 FFmpeg padding 要求。原始探测影片样本与原生测试共用，十组 H.264／HEVC／AV1、8／10-bit 与 4:2:0／4:4:4 经分段组装后，以实际软件 decoder 验证像素与原始样本一致，并送入生产 Pacer。此路径使用测试 renderer，不证明 GPU 呈现、实体 V-sync、影音同步或真实游戏串流通过；完整串流视窗、Session 与网路仍有 Qt 相依；FFmpeg／Windows renderer 的原生建置见下文。

Windows Raw Input 触控板的接点集合与帧差异计算由无 Qt 的 `input/touchpadcontacts.h` 处理，保留五接点上限、HID 输入顺序、旧接点释放先于新 Down 事件；同一组 Up 内的 hash 迭代顺序不作为契约。Qt 输入处理器复用生产 helper，仍负责装置拥有权、按钮回退、擷取、传输与取消流程；SDL／macOS 接点容器保留。原生测试覆盖 1024 组新旧接点集合组合与边界，不代表真实触控板手势已完成验收。

浮动按钮的单轴边界、归一化与像素还原由无 Qt 的 `overlayplacementaxis.h` 处理，原 Qt 介面直接复用计算；QSettings 路径／格式、窗口与输入处理仍保留。原生测试验证坐标往返与边界，旧 Qt 位置测试另外比对原 Qt 公式及保存还原。喚醒合并与 toast 排程测试也已移除 Qt，相同生产状态 helper 由 CMake 与旧 qmake 测试共用；这不代表浮动介面已完成无 Qt 绘制。

Windows／MSVC 的 `streamutils.cpp` 也已纳入无 Qt 建置：复用既有 SDL 隐藏视窗探测、桌面模式／刷新率查询、比例／座标转换与 AES-NI 检测。原 Qt 异步日志计数的控制函数随日志实现留在 `main.cpp`，不会纳入原生库；其他平台／工具链的 Qt 平台与 DRM 路径保留。`stream_utils` 测试仅用 SDL dummy video，不操作实体显示器；不代表已验收真实 GPU、HDR、DPI 或全屏体验。

测试 PATH 仅包含原生依赖与 Windows 系统目录，不借用开发机 Qt 路径。

Windows Lite 的生产 SDL 影像 renderer 与 `SwFrameMapper` 也纳入无 Qt CMake，使用固定 swscale／avutil／SDL 依赖。`ISdlRendererHost` 只提供视窗事件同步清理、图层开关与更新后的 SDL surface；原 Qt decoder 透过 `SessionFFmpegDecoderHost` adapter 保留即时 Session 查询与既有清理流程。图层 surface 拥有权、texture 快取、位置、渲染线程选择与色彩转换不变，不增加每幀像素中转。完整 worker 仍由 Qt Session 协调；改由原生 owner 提供此介面后即可移除该 adapter，不需要在 renderer 模拟 QObject。其他平台 MMAL／CUDA 条件路径不属于此无 Qt 验收。

`sdl_video_renderer` 默认只验证原生 probe／格式协商，不启动 GPU。显式 `--render` 使用隐藏 Windows 视窗与实际 SDL backend：验证 YUV420／NV12／NV21、4:4:4 CPU 转换、图层更新／快取／开关、尺寸变化与比例，以保留的 SDL target texture 读回像素，避免依赖 Present 后可被丢弃的 window backbuffer。另复用六组 8-bit H.264／HEVC／AV1 的 4:2:0／4:4:4 样本，完成组包→软件解码→生产 Pacer／渲染线程→SDL 实际呈现。此测试不验收硬体解码／copyback、HDR、实体 V-sync 或游戏延迟；SDL 原本拒绝 HDR 的行为保留。

生产 D3D11VA renderer 也保持 C++，直接纳入无 Qt CMake；解码装置、共享 texture／fence、HDR shader 与厂商影像增强流程不改写为 Rust。`ID3D11RendererHost` 只提供 shader 二进位及图层状态／surface，过渡期 `session.cpp` 的 `SessionFFmpegDecoderHost` adapter 保留原 Qt 资源查找顺序；替换为原生 owner 后即可移除此 adapter。无每幀影像中转或额外程序。原生环境变数解析保留既有带正负号、空白、进位侦测与 int 范围；迁移时另与安装的 Qt 6.12.0 比对 37 组值。SDL 的环境快取不作为此查询来源，使用与原 Qt 路径一致的 C runtime 环境。

`d3d11_video_renderer` 的预设测试不启动 GPU，验证生产链接、环境覆盖及显式停用后的 fallback；`--probe app/shaders` 则建立隐藏 SDL 视窗、实际 D3D11 装置、五个既有 shader 与 FFmpeg 硬体 context，再清理资源。此检查尚不验收影片解码／呈现、HDR、厂商增强或游戏延迟。AMF 复用 `app.pro` 的同一组内嵌源码；新 CMake 仅对原 renderer 已有的 C4244 数值缩窄警告作局部豁免，不在解除 Qt 时顺便改动 HDR／texture 数值行为。

DXVA2 备援 renderer 同样保持 C++，由 `lite-dxva2-video` 直接构建生产源码；`IDxva2RendererHost` 只提供既有图层状态与 SDL surface，`session.cpp` 的过渡 adapter 保留即时 Session 查询。surface 交接、texture 快取、D3D9 装置／video processor、黑名单、quirk、decoder 选择顺序及 HDR 首轮回退不变。环境开关复用相同的原生解析器；x86／x64 判定使用编译器架构宏，不再依赖 Qt 宏。未来原生 owner 接手图层后即可移除 adapter，不新增影像复制或程序。

`dxva2_video_renderer` 默认仅验证原生链接、明确停用时的两轮 fallback、HDR 能力与首轮 HDR 回退，不启动 GPU；显式 `--probe` 建立隐藏 SDL 视窗、实际 DXVA2 装置／processor 及 FFmpeg 硬体 context，并执行图层 surface 交接、无新像素的更新与停用清理，错误日志即失败。探测未送入压缩影片、未读回呈现像素，不代表真实硬体解码、全萤幕、延迟或串流体验通过。无 Qt CMake 对既有顶点 int→float 转换仅作局部 C4244 豁免，保留原数值行为。

Vulkan／libplacebo renderer 与通用 FFmpeg hwaccel 分别由 `lite-vulkan-video`、`lite-generic-hwaccel` 直接编译生产源码，使用既有固定 libplacebo 374／Vulkan headers，不另找 SDK。`IPlVkRendererHost` 只提供原图层状态与 surface，保留异步上传的 surface 所有权、staging texture 与渲染线程锁。日志、Intel／软件 GPU 开关复用原生环境解析器；平台与 Debug 分支改用编译器宏。通用 hwaccel 保留原 FFmpeg 装置建立、context 引用及 read-back 语意，不新增影像中转。

`lite-ffmpeg-decoder` 直接构建生产 `ffmpeg.cpp`，同时链接 SDL、D3D11VA、DXVA2、Vulkan 和通用 hwaccel，未藉停用 renderer 达成无 Qt 编译。`IFFmpegDecoderHost` 提供原生 OverlayManager、renderer 图层／资源介面、当前频宽及 GPU 策略；原 Session 查询与 Qt 资源读取统一留在 `session.cpp` 的 `SessionFFmpegDecoderHost`，此 owner 必须比 decoder 活得更久。renderer 选择顺序、探测影格、解码线程、Pacer、统计更新与清理顺序保留。codec 名称表、ASCII 名称衍生的 AVOPTIONS／OMX 判定及环境 hint 使用标准型别；不改主机格式或用户设定。

`vulkan_video_renderer --probe` 验证实际 Vulkan 影格上传、两次呈现、图层更新／清理及通用 D3D11 硬体 context；未读回最终像素。`ffmpeg_video_decoder <repo-root> --probe` 则使用生产内嵌 H.264／HEVC／AV1 探测影格，分别走软件与硬体解码、原 renderer 格式映射，再清理资源。两者均须显式执行，默认 CTest 不启动 GPU；所有原生测试在不含 Qt 的 PATH 运行。这些验证不代表完整 worker 已无 Qt，或真实串流、HDR、全萤幕、影音同步及延迟已验收；Session、网路、设定、输入与 helper 仍需迁移。

Windows Lite 的 GPUI 串流入口改为附属于 SDL 串流视窗的有焦点放射选单，主控制页保持最小化，快捷键仍为 Ctrl+Alt+Shift+O；十字键／方向键选择、A／Enter 确认、B／Esc 返回。外圈与中央均为圆形，外圈只显示图示；滑鼠 hover 或键盘／手把选择时，中央显示名称、状态与说明。F2／手把 X 切换图示顺序编辑，依序选择来源与目的位置交换，编辑期间不执行串流操作；顺序写入独立 GPUI 设置。GPUI session 不建立 Qt 互动选单或浮动按钮；旧 Qt 前端路径保留。原选单位置设定仍保留在隔离 profile 中供旧路径使用，GPUI 设定页不再提供不适用的位置选项。

控制页以既有匿名 pipe 增加 `controls` 命令与状态／导航事件，不传递影像，也不增加程序或定时轮询。worker 收到 pipe 命令时以 SDL user event 唤醒主线程，过渡期由该事件处理既有 Qt 主线程命令。开页时明确释放远端按键／按钮／接点与手把状态、暂停输入计时器；关页时清除控制页期间排队的输入，再恢复原擷取状态，本机仍按住的键鼠／手把按钮必须先放开才可重新送往 host。Windows 两个既有程序透过 `AllowSetForegroundWindow` 交接前景权限，放射选单使用 GPUI PopUp 加少量 Win32 owner／位置／边框设定，不使用全域置顶、穿透、GPUI 源码修改或新增程序。固定的 GPUI 0.2.2 `activate_window()` 内部会合成 Alt 按键，所以必须先完成 worker 输入交接，才启用 GPUI 视窗。

「暂时断线」明确保留 host 工作阶段；「结束工作阶段」在断线后沿用 `NvHTTP::quitApp()` 的 `cancel` 请求，要求主机结束 Desktop／应用程式工作阶段。主机显示还原依 Sunshine／VDD 设定，client 不自行切换 host 萤幕；请求失败会回报错误。快速选单保留返回串流、手把滑鼠、暂时断线、结束工作阶段、统计与全萤幕六项；其他既有 worker 控制命令不因此移除。

既有 GPUI 位元率命令回覆契约仍保留，但目前六项快速选单不提供位元率按钮，也不显示常驻位元率数字。回覆不再触发游戏画面的 Qt toast。既有协议 1 的 `controls` 请求新增可选 `request_id`，核心在原 HTTP worker 完成后回报 `bitrate_result`（编号、请求 kbps、成功旗标及错误）；旧调用省略编号仍可使用。编号随原位元率槽位一起合并，前端不会以旧请求结果清除新请求的等待／错误状态，重复选择同一数值也适用；关闭控制页后结果只更新资料，不启用视窗或抢焦点。Session 结束即清理前端状态，bridge 不转发已结束 Session 的延迟结果。「请求完成」沿用既有 HTTP 完成判定，不是实测网络位元率，也未增加新的主机回复格式要求。旧 Qt 前端保留原 toast 行为。

此控制页尚需真实 Sunshine／Desktop 串流验证 Windows 焦点、全萤幕、手把输入交接及两种断线结果；模擬核心的介面检查不能代替此验收。GPUI 前端仍由其 Cargo 路径构建，串流 worker、被动 toast、字型资源查找与剪贴板 helper 仍需 Qt；现有 portable 封装仍走 qmake／windeployqt。不得把原生库或控制页编译通过视为 Windows Lite 已完成无 Qt 交付。

前端打包入口 `gpui-prototype/package.ps1` 要求设定 `CARGO_TARGET_DIR`，并先以固定 Rust 1.99.1 执行 locked release build，成功后才复制正式执行档及记录来源／杂凑；`cargo test` 的测试执行档不能代替此步骤。整合 portable 建置前先执行此前端打包入口，再核对部署目录的前端 SHA256 与该包一致，避免混入旧 release 执行档。

被动通知的替代模块 `video/nativenotification.*` 采用已确认的 C++／DirectComposition 路线，由 CMake 的 `lite-native-notification` 构建，目前尚未接入 Session，也尚未停用 Qt toast。模块在 owner 的既有 SDL HWND 合成小型半透明 DirectWrite 繁中文字层，独立于影片 swapchain／decoder，不读取或传输游戏影像。第一次显示才建立绘制装置；owner 负责消息与到期事件，模块不建立视窗、输入处理器、计时器、执行绪或轮询。owner 必须在同一执行绪调用，并先销毁模块再销毁 SDL 视窗；失败以 HRESULT 回报，不另开提示或静默改用其他窗口。尺寸／DPI 改变由 owner 呼叫 `refresh()`，不是持续重绘。

显式执行 `native_notification` 可验证实际 GPU 合成与 SDL 影片 swapchain 共存：只 Present 一次影片，隐藏后重新显示通知、释放影片装置后更新、缩放、期限替换与到期隐藏、跨执行绪拒绝、无效 owner 及销毁后重建。`--visible` 展示五个阶段便于目视确认繁中与半透明；不自动加入可能没有桌面合成器的 CTest。此验证不等于真实串流、全萤幕、HDR、跨萤幕 DPI 或效能验收；正式切换尚需完成这些检查与已确认的警告／操作回馈流程迁移。

## 主机控制 HTTP 原生传输

已确认采用 C++＋libcurl＋既有 OpenSSL 方向验证，保留 worker／IPC 边界。原探测中的传输逻辑已抽至 `app/backend/nativehttptransport.*`，由 `windows-lite/host-http` 的 `lite-native-host-http` 静态库构建；测试直接复用它，不另存一份 TLS 实作。模组以标准型别处理 GET／POST、原始回應、配对 leaf DER 及记忆体中的客戶端 PEM 身分，不访问 profile、私钥暂存档或系统凭证库。HTTP status 与 body 留给上层协议政策；取消、期限、超限、凭证不符及传输／设置错误分别回报，失败不交出部分 body。

`scripts/test-native-host-http.ps1` 验证官方 curl 8.23.0 ZIP 的固定 SHA256 `85477fe11408471285f38a601e7c6f53470d13be9a5a316919b071ad02e68213`，静态构建 libcurl，并只链接既有 OpenSSL 3.6.5。可传 `-Configuration Debug`，测试 PATH 不含 Qt；源码与产物留在忽略的 `build/`，不安装系统工具。来源：[官方 release](https://github.com/curl/curl/releases/tag/curl-8_23_0)。准备此来源后，也可给 `scripts/build-lite-native.ps1` 传 `-CurlSourceDirectory <source>`，在原生核心中构建同一模组；未提供时不启用，不在 CMake 中下载。Windows x64 portable 入口先准备此库并执行隔离测试，再以 `CONFIG+=lite-native-http` 接入生产 `NvHTTP`；其他平台及未启用此配置的旧建置继续使用 Qt Network。封装附带 curl `COPYING`，来源 ZIP 与两份静态库杂凑写入 provenance；缓存源码逐档与已验证 ZIP 核对。此切换不代表完整 Sunshine 配对、真实串流与显示／输入生命周期已验收。

`tests/native_host_http` 复用公开的剪贴板 TLS fixture，只访问随机 loopback port。完整 leaf DER 在 OpenSSL 握手验证 callback 中比对，不以公钥 pin 替代；匹配配对凭证才接受其自签／名称／有效期属性，行为目的与现有 `PairedCertificate` 相同。测试从伺服器端确认不同凭证与同公钥换证都未收到 HTTP 请求，并覆盖客戶端凭证、HTTP/1.1、禁止重新导向、忽略代理环境、期限取消及回應大小上限。探测显式禁止连接／TLS session 重用，避免自订验证策略跨请求遗留；参见 [libcurl SSL context callback](https://curl.se/libcurl/c/CURLOPT_SSL_CTX_FUNCTION.html)。这些 loopback 证据不代表真实 Sunshine 的完整配对、应用启停或持续连接已验收；不读取用户身分或旧设定。

传输在调用者选择的执行绪执行，使用 `curl_multi_poll` 的 socket／期限等待；跨执行绪取消以 `curl_multi_wakeup` 唤醒，不由模组增加执行绪或固定间隔轮询。取消 token 单调且同一时间只接受一个请求，注册／解除与唤醒同步，避免对已销毁的 multi handle 操作。回归另覆盖等待前取消、等待中取消、UTF-8 JSON、准确达到回應上限、无效 pin／不完整身分、连续请求切换 pin／移除客戶端身分，以及仅允许明确无身分的未配对 HTTP。阻塞入口的 token 测试不能视为现有 Session 已完成无 Qt 生命周期迁移；现有 NvHTTP 的应用关闭由下述过渡接点处理。

已有事件 dispatcher 的 owner 可改用 `perform(request, EventLoop&)`，共享相同的请求、TLS 与错误处理，不额外启动线程。此入口使用 libcurl 的 [socket action](https://curl.se/libcurl/c/curl_multi_socket_action.html) 与 [单次 timer callback](https://curl.se/libcurl/c/CURLMOPT_TIMERFUNCTION.html)，由 owner 等待 socket 就绪／期限，或回报关闭取消；不以固定间隔轮询，也不把阻塞的 `perform()` 放进 UI 线程。事件入口拒绝跨线程取消 token，取消必须透过 owner 的 dispatcher 唤醒并回报，避免只唤醒 curl 而漏掉外部事件等待。

`qtnativehttptransport.*` 是仅供现有 NvHTTP owner 接入的过渡接点：以 `QSocketNotifier`／单次 `QTimer` 保留 `ExcludeUserInputEvents` 与应用关闭取消，不使用 Qt Network。退出条件是 worker 不再使用 Qt dispatcher；它不是通用 Qt 相容层，Windows Lite 的 NvHTTP 在启用原生传输时使用此接点。测试脚本默认另跑无 Qt 的 socket owner；显式传 `-QtDirectory <SDK>` 才另外构建、运行此过渡接点。两种 owner 都验证部分回應取消／逾时后不交出 body、无限期限的 owner 取消、主机名称解析及 TLS／JSON／状态政策；Qt 测试分别在主线程及调用者拥有的工作线程确认等待期间其他计时事件可执行，实际 `QCoreApplication::quit()` 能退出请求。工作线程的 dispatcher 由原本的 `QEventLoop` 创建流程建立，接点不额外建立线程。此证据不取代正式 NvHTTP 的错误映射、配对／控制流程与封装验收。

生产 `NvHTTP` 的内部回應改为 body／HTTP status／配对验证结果，XML、JSON、box art 与受限 USB capability 不再直接消费 `QNetworkReply`；查询参数与上层协议语意保持原有实现。原生配置不创建或修改注入的 QNetworkAccessManager。`qtnativehttppolicy.h` 暂时将原生传输错误映射到现有 exception 种类，并保留 [Qt 的 HTTP 状态分类](https://github.com/qt/qtbase/blob/6.12/src/network/access/qhttpthreaddelegate.cpp)，特别是 ComputerManager 的 503 判断；本地 TLS 失败不会变成允许 HTTP recovery 的远端 XML 401。重新导向仍拒绝，取消与逾时仍对 owner 回报 TimeoutError；错误文字不包含 URL query 或 host 回應。此 exception／QSslCertificate／IdentityManager 仍依赖 Qt，不能宣称已移除整个 Qt Network 模组。

`tests/native_host_http/check_nvhttp.py` 分别链接生产新旧 NvHTTP、IdentityManager、NvComputer 与 common-c，在全新 INI 目录使用公开 fixture，验证 serverinfo、JSON 位元率、cancel 后 serverinfo、launch／resume 的查询与 RTSP 回應、图片、503／401、redirect、逾时／应用关闭、错误 JSON、配对 pin 拒绝，以及仅远端 XML 401 允许的 HTTP recovery。测试不改写用户旧设定，也不以 loopback 协定 fixture 代替真实 Sunshine 配对／连线验收。

## 配对协定的原生边界

`backend/nativepairing.*` 负责原有五阶段配对与 OpenSSL 运算，标准 C++ 型别可由 `lite-native-pairing` 独立构建，不需要 Qt、HTTP 实作、档案、计时器或线程。保留 Gen 6 的 SHA-1、Gen 7+ 的 SHA-256、AES-128-ECB 无 padding、SHA-256 签章与完整 leaf 配对目的；金钥取杂凑前 16 个二进位 bytes，不再经 C 字串截断。challenge 回應需包含完整 hash 与 16-byte server challenge，且密文必须是完整 AES block；畸形资料走既有 Failed／unpair 路径。加解密、随机数与签章的 OpenSSL 回传均检查，资源由 RAII 释放。

现有 `NvPairingManager` 仅保留 NvHTTP／XML、QSslCertificate、pairname 储存及错误呈现接点；不再另存一份 Qt 配对算法。HTTP 错误与原有 stage 标记继续抛给 owner；原生密码运算失败在此回报既有 Failed 状态。第一阶段成功但没有凭证仍回报 AlreadyInProgress；一般阶段失败按既有规则 unpair 一次，HTTP 例外不自动增加请求。pairname 的写入时机不变，serverCert 只在最后的已 pin HTTPS／mTLS pairchallenge 成功后发布给 owner，失败保留原本输出凭证。IdentityManager、QSslCertificate、QSettings 与主机持久化尚未解除 Qt，未改变资料格式／位置，也未触碰旧版 profile。

`native_pairing` 加入无 Qt 的 CTest，以公开 fixture 与独立 OpenSSL peer 验证两个世代、含零位元组的衍生金钥向量、五阶段顺序、双向 challenge／签章、错误 PIN、签章不符、短资料、占用状态、失败清理及 owner 例外。`check_pairing.py` 进一步经生产 NvPairingManager 与新旧 NvHTTP 路径跑 loopback HTTP＋已 pin mTLS，确认 XML 转换、繁中 pairname 及凭证发布时机；测试只写唯一 INI 测试目录。此证据不替代真实 Sunshine／GFE 配对、游戏连线与全部平台实机验收。

## 历史

- 2026-09 目录清理:移除空壳目录(src/、streaming/、include/、soundio/ 上游遗留)与误跟踪的 .DS_Store;filemapping-smoke 并入 file-mapping/smoke;DS5 探针脚本并入 scripts/。

客戶端身分的 OpenSSL 运算由 `backend/nativeidentity.*` 负责，可经 `lite-native-identity` 脱离 Qt 构建。保留 RSA-2048、X509 v3、自签 SHA-256、serial 0、原 CN、20 年效期、未加密 PKCS8 PEM 与无前导零的 64-bit 小写十六进位识别码；Darwin 的既有 PKCS1 私钥接点也保留。每一步 OpenSSL 失败均明确返回失败／抛出例外，资源由 RAII 回收，私钥读取不等待密码输入。`IdentityManager` 保留 QSettings、QSslCertificate／QSslKey／QSslConfiguration 接点与原本重新产生规则；更换凭证时会一起清除旧的两项 SSL 快取，避免有效旧证书搭配新私钥。此阶段不改变信任模型、储存位置或格式，也不代表完整身分管理已无 Qt。

XML／JSON 去 Qt 的能力验证入口为 `scripts/test-native-parsers.ps1 -QtDirectory <SDK>`。经 SA 确认评估 Expat 2.10.0 与 nlohmann/json 3.12.0，维持 C++ worker／IPC 职责；源码由官方 release 固定 SHA256 准备至忽略的 `build/native-parsers-source`，每次复用逐档核对归档，保留两者授权。CMake 配置不下载或安装套件；Expat 静态链接、JSON 为 header-only。`native_parser_probe` 不链接 Qt，Qt 只用于独立 reference executable；正式 worker、封装与数据格式尚未切换。

对照范围是既有 Qt 6.12 的 XML token／JSON object 语意及整数转换，覆盖繁中、配对字段、空标题、namespace、CDATA、实体、畸形输入与数值边界；不取代生产 NvHTTP 的提前返回、错误映射或完整协议验证。原生库与 Qt 对损坏 XML UTF-8、外部实体、实体展开上限、JSON 非法 escape／未配对 surrogate、超大整数表示及深度限制有差异。未限深的 JSON 原型在深层 Debug 资料树处理出现 stack overflow，能力原型已在解析期间限制 64 层；这是待 SA 确认的候选输入规则，不代表已授权改变正式产品。正式接入前需确认异常输入与 DTD 政策，并维持既有字段数值范围；不得将可构建或比较程序结束视为相容验收通过。
