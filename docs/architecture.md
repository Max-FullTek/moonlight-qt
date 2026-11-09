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
| `windows-lite/` | Windows Lite 的无 Qt CMake 入口，构建原生库、输入 helper、音讯 pipeline、压缩影格组装与画面排程；不包含仍依赖 Qt 的完整串流 app 与剪贴板 helper |
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

影像解码参数与解析度使用 `videodecoderoptions.h` 的原生型别；Qt 偏好设定保留原 Q_ENUM 名称与数值，在 Session 边界转换。`overlayrenderer.h` 只保留原生渲染通知，文字／字型载入仍由 Qt OverlayManager 负责。Windows 的生产 Pacer 与 DxVsyncSource 已纳入无 Qt CMake：标准 C++ 佇列、mutex／condition variable 与 atomic 停止旗标，保留三幀佇列上限、500 ms 历史、丢幀策略、SDL 渲染线程及延后一幀释放。停止转移与等待同步，空佇列等待抵御虚假唤醒；其他平台的 Wayland Qt 桥接仍保留。

`video/decodebuffer.*` 复用原压缩包拼接、H.264 SPS 修正与 AV1 OBU 重排，由 Qt FFmpeg decoder 直接调用；可重用原生缓冲区在扩容时无需复制旧包，每包实际长度后明确补零，满足 FFmpeg padding 要求。原始探测影片样本与原生测试共用，十组 H.264／HEVC／AV1、8／10-bit 与 4:2:0／4:4:4 经分段组装后，以实际软件 decoder 验证像素与原始样本一致，并送入生产 Pacer。此路径使用测试 renderer，不证明 GPU 呈现、实体 V-sync、影音同步或真实游戏串流通过；完整 FFmpeg orchestration、GPU renderer、视窗与 Session 仍有 Qt 相依。

Windows Raw Input 触控板的接点集合与帧差异计算由无 Qt 的 `input/touchpadcontacts.h` 处理，保留五接点上限、HID 输入顺序、旧接点释放先于新 Down 事件；同一组 Up 内的 hash 迭代顺序不作为契约。Qt 输入处理器复用生产 helper，仍负责装置拥有权、按钮回退、擷取、传输与取消流程；SDL／macOS 接点容器保留。原生测试覆盖 1024 组新旧接点集合组合与边界，不代表真实触控板手势已完成验收。

浮动按钮的单轴边界、归一化与像素还原由无 Qt 的 `overlayplacementaxis.h` 处理，原 Qt 介面直接复用计算；QSettings 路径／格式、窗口与输入处理仍保留。原生测试验证坐标往返与边界，旧 Qt 位置测试另外比对原 Qt 公式及保存还原。喚醒合并与 toast 排程测试也已移除 Qt，相同生产状态 helper 由 CMake 与旧 qmake 测试共用；这不代表浮动介面已完成无 Qt 绘制。

Windows／MSVC 的 `streamutils.cpp` 也已纳入无 Qt 建置：复用既有 SDL 隐藏视窗探测、桌面模式／刷新率查询、比例／座标转换与 AES-NI 检测。原 Qt 异步日志计数的控制函数随日志实现留在 `main.cpp`，不会纳入原生库；其他平台／工具链的 Qt 平台与 DRM 路径保留。`stream_utils` 测试仅用 SDL dummy video，不操作实体显示器；不代表已验收真实 GPU、HDR、DPI 或全屏体验。

测试 PATH 仅包含原生依赖与 Windows 系统目录，不借用开发机 Qt 路径。

这仅完成原生库建置阶段：GPUI 前端仍由其 Cargo 路径构建，串流 worker、浮动介面与剪贴板 helper 仍需 Qt；现有 portable 封装仍走 qmake／windeployqt。不得把本入口通过视为 Windows Lite 已完成无 Qt 交付。

## 历史

- 2026-09 目录清理:移除空壳目录(src/、streaming/、include/、soundio/ 上游遗留)与误跟踪的 .DS_Store;filemapping-smoke 并入 file-mapping/smoke;DS5 探针脚本并入 scripts/。
