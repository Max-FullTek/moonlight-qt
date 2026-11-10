# tests/

各子目录为独立可执行测试，既有测试经 `qmake && make` 后直接运行，退出码非零即失败；Windows Lite 原生测试另有 CMake 入口，见下文。
CI 统一经 `scripts/run-test.bat`(Windows)调用,法规约见脚本头注释;新测试加入
CI 时在 build.yml 对应步骤加一行 `call scripts\run-test.bat <目录> <TARGET>`。

断言风格尚未统一(require 累积 / CHECK 返回行号 / qFatal 混用),新测试建议用
`require(bool, QString)` 累积风格(clipboard_payload_routing 是范本),保持
main() 短、被测逻辑走生产源文件直编(见各 .pro 的 SOURCES)。

## CI 覆盖(.github/workflows/build.yml)

| 目录 | 验证内容 | CI 平台 |
|---|---|---|
| derive_version | scripts/derive-version.py 版本号推导 | Ubuntu(unittest) |
| cursor_shape_classification | 光标位图 → 形状分类器的判定矩阵 | Windows |
| clipboard_payload_routing | 剪贴板状态、复合内容、文件保护、IPC 分帧和本地 TLS 传输回归（offscreen） | Windows |
| clipboard_helper_lifecycle | helper 异常退出、响应超时和进程管道背压 | Windows |
| overlay_button_position | 悬浮按钮归一化/还原坐标的跨分辨率往返 | Windows |
| file_mapping_websocket_framing | 文件映射 WS 帧解析(fin/分片/ping-pong) | Windows |
| shared_helpers | 配对证书策略、版本比较与 GFE 兼容性回退 | Windows |
| file_mapping_transport | 本地 TLS 配对/缓存连接身份/重定向校验，WS Upgrade 与 RPC ID/重入回归 | Windows |
| file_mapping_mirror_e2e | 主机文件镜像端到端(FakeRemoteVfs + 挂载提供方) | Windows |
| usb_forwarding_capability | helper 能力 JSON 解析(端口/token/畸形输入) | Windows |
| usb_forwarding_environment | USB 转发就绪态 → 错误文案映射(--require-ready 走活探测) | Windows |
| usb_forwarding_backend_list | moonlight-usbd `list --json` → 设备表解析 | Windows |
| usb_forwarding_backend_sysfs | Linux sysfs 枚举 → 设备表解析（夹具目录，跳过 hub/接口/根 hub） | Linux |
| ds5_ir_renderer | DualSense 触觉 IR 渲染 | Windows |
| keyboard_state | 扩展键区分、国际键标志、失焦松键失败重试 | Windows |
| gamepad_sensor_rate | 低传感器频率周期存储、禁用值、全频率范围限速上界 | Windows |
| pen_history_selection | 手写笔历史选择 | Windows |
| stylus_replay | 手写笔录制回放 | Windows |
| overlay_event_wake_state | 悬浮菜单事件唤醒状态 | Windows |
| overlay_button_native_wake | 悬浮按钮原生唤醒 | Windows + macOS + Linux(xvfb) |
| overlay_toast_event_state | 悬浮 toast 事件状态 | Windows |
| overlay_menu_navigation | 悬浮菜单键盘/鼠标导航(QTest 事件,需 offscreen) | Windows |
| settings_localization | 中文翻译 .qm + 设置页布局(qmltestrunner) | Windows |
| linux_display_event_monitor | Linux 显示事件唤醒 | Linux |

Windows Lite 原生库另有无 Qt 入口 `scripts/build-lite-native.ps1`（CI：`lite-native.yml`），覆盖 Release／Debug，构建并执行协议库已有 CTest、`lite_native` 的实际静态链接／API／H.264 NAL 解析检查，以及 `keyboard_state`、`gamepad_sensor_rate`、`pen_history_selection`、`cursor_shape_classification`。四项输入测试复用现有源文件，键盘与光标分类测试本身已不再依赖 Qt；原 `.pro` 入口保留。光标分类测试覆盖 1x／2x 形状、保守回退、空指针、输入长度／尺寸、alpha 门槛及热点边界，也加入 portable 回归入口。此入口不需要 qmake，不连接真实主机，不读取用户设置或凭证；范围与前提见 [架构文档](../docs/architecture.md#windows-lite-原生库建置)。另有仅 CMake 的 `sdl_audio_renderer`：直接编译生产 SDL renderer 并链接实际 common-c，dummy 驱动覆盖 stereo／5.1／7.1、5／10 ms PCM、空帧与重复开关；SDL 错误／断言导致失败，不使用实体音讯装置。浮动介面另覆盖 `overlay_placement_axis`（仅 CMake）、`overlay_event_wake_state` 与 `overlay_toast_event_state`（保留 qmake 入口）；直接复用生产 helper，不启动视窗。旧 `overlay_button_position` 仍覆盖 Qt 包装及 QSettings，并比对原 Qt 公式与无 Qt 计算。`stream_utils` 为仅 CMake 的 Windows／MSVC 测试，直接编译生产源文件，以 SDL dummy 隐藏视窗覆盖 Vulkan 探测失败回退、桌面模式／刷新率、Windows DRM 空结果、比例／座标转换与 AES-NI 查询，不接触实体显示器。`audio_pipeline`（仅 CMake）使用实际 Opus encoder／decoder 比对多声道 float／s16 输出，验证输出缓冲区大小、空 buffer、错误包、输出失败及 dummy 后端建立／清理；`touchpad_contacts`（仅 CMake）直接测试生产帧差异 helper 的 1024 组接点转移、完整 32-bit ID、五点上限、重复 ID 与接点释放。均不连接串流主机或使用实体输入／音讯装置。其余 qmake 测试仍属于旧路径。

`video_pacer`（仅 CMake）直接编译生产 Pacer／Windows DxVsyncSource，以 SDL dummy 视窗及可控制的 V-sync source 验证主线程／SDL 渲染线程、三幀上限、严格／宽松丢幀、无缓冲 renderer、延后释放、失败回退与反复启停；用实际 AVFrame buffer 回调检查释放，检查渲染及清理的线程一致性。`video_decode_path`（仅 CMake）复用生产探测样本，十组 H.264／HEVC／AV1 的 8／10-bit、4:2:0／4:4:4 经三段组装、实际 FFmpeg 软件解码后，与原包解码逐像素比对，再送入生产 Pacer 的测试 renderer；另覆盖超过 1 MiB 的扩容、缩包重用、FFmpeg 零 padding、AV1 metadata 重排及 H.264 SPS 原样／修正。两者不使用 Qt、实体显示器或主机，不验收 GPU 呈现、实体 V-sync、延迟或游戏体验。

`sdl_video_renderer`（仅 CMake）默认验证不启动视窗的 probe、像素格式协商、HDR 拒绝及视窗变更旗标。显式执行 `build/lite-native/Release/sdl_video_renderer.exe --render`（使用自订 build 目录时对应替换；PATH 需包含固定 `libs/windows/lib/x64`）会建立隐藏 Windows 视窗并使用实际 SDL backend：读回保留的 target texture 验证 YUV420／NV12／NV21、4:4:4 CPU 色彩转换、尺寸／比例、debug／status 图层的位置、更新、快取、开关，以及资源释放和事件清理委派。另让六组生产 8-bit 样本经组包、真实 FFmpeg 软件解码、生产 Pacer 的 SDL 渲染线程到实际 SDL 呈现。SDL 错误日志或断言导致失败；此运行模式需要可用 Windows 图形环境，不纳入无 GPU 的默认 CTest，不验证硬体解码／copyback、HDR、实体 V-sync 或延迟。


## 仅本地/手动

`native_host_http` 直接链接生产 `nativehttptransport.*`，运行 `scripts/test-native-host-http.ps1`（另可传 `-Configuration Debug`）。脚本验证固定 curl 来源杂凑、构建并以不含 Qt 的 PATH 执行 Python loopback TLS fixture；覆盖完整凭证比对先于 HTTP、同公钥换证拒绝、记忆体 mTLS、禁止重新导向、代理绕过、期限和回應上限，并验证取消等待的唤醒、UTF-8 JSON、HTTP 错误 status／body、连续身分切换及未配对 HTTP 的明确开关。只复用公开测试金钥，不连接真实主机或操作用户配对资料。此模组也可透过原生核心的 `-CurlSourceDirectory` 选项构建，Windows x64 portable 会启用此传输；其他旧建置仍可使用 Qt Network。

脚本也以无 Qt 的 socket owner 测试事件入口：只依 socket／libcurl 单次期限驱动，不持续轮询；验证部分回應的取消／逾时、无限期限取消、主机名称解析及原有 TLS／HTTP 政策。加 `-QtDirectory build/tools/qt/6.12.0/msvc2022_64` 可额外构建 `native_host_http_qt_owner`，直接复用生产过渡接点 `qtnativehttptransport.*`，分别在主线程与测试拥有的工作线程确认等待中其他 Qt 计时事件继续执行、应用 `quit()` 中断无限期限请求，并拒绝隐藏的 stderr 警告。只有此额外测试的 PATH 加入 Qt；原生测试仍排除 Qt。过渡接点已供 Windows Lite 的 NvHTTP 使用，仍不等同完整配对／真实串流关闭已验收。

指定 QtDirectory 时也构建 `nvhttp_native`／`nvhttp_legacy`，共用生产 NvHTTP、IdentityManager、NvComputer 与 common-c；`check_nvhttp.py` 在唯一 INI 测试目录用公开 mTLS fixture 比较 serverinfo、JSON 位元率、结束工作阶段、launch／resume 查询与 RTSP 回應、box art、503／401、redirect、逾时／应用关闭、错误 JSON、pin 不符及仅远端 XML 401 允许的 HTTP recovery。正式封装脚本先执行这组测试，并把 curl 授权与来源／静态库杂凑纳入交付。无真实 host、旧 profile 或用户私钥参与；完整配对与真实游戏连线仍需后续验收。

`native_pairing` 随原生核心 CTest 执行，无 Qt／host／profile 需求，只读取公开 PEM fixture；使用独立 peer 验证 Gen 6／7、二进位 key 含零向量、challenge／签章、错误 PIN、畸形资料、清理与例外契约。`test-native-host-http.ps1 -QtDirectory <SDK>` 另执行 `check_pairing.py`：把此 peer 接成 loopback HTTP／mTLS host，分别走生产 NvPairingManager＋新旧 NvHTTP，验证完整五阶段、繁中 pairname、失败不发布凭证及最终 TLS 阶段回覆拒绝后的 unpair。未连接真实 Sunshine，也不写旧 profile；真实主机互通仍待验收。

`vulkan_video_renderer` 与 `ffmpeg_video_decoder`（仅 CMake）默认不启动 GPU，验证生产链接／owner 生命周期、格式与视窗状态契约，以及通用 hwaccel 能力覆盖。显式 `vulkan_video_renderer.exe --probe` 使用实际 Vulkan 上传、呈现与图层，并建立通用 D3D11 context；`ffmpeg_video_decoder.exe <repo-root> --probe` 复用生产探测影格，分别验证 H.264／HEVC／AV1 的软件及硬体解码和 renderer 映射。两者的 PATH 均需固定 `libs/windows/lib/x64`，无 Qt；未读回最终呈现像素，不验收持续串流、HDR、全萤幕或延迟。探测不纳入默认 CTest。

`dxva2_video_renderer`（仅 CMake）默认验证生产链接、明确停用的两轮 fallback，以及 HDR 首轮回退，不启动 GPU。显式 `build/lite-native/Release/dxva2_video_renderer.exe --probe`（自订 build 目录时替换路径；PATH 包含固定 `libs/windows/lib/x64`）建立隐藏 SDL 视窗、DXVA2 装置／video processor、FFmpeg 硬体 context，并执行图层更新／快取／停用清理；错误日志导致失败。此探测不解码影片、不验证最终像素、全萤幕或真实串流，不纳入默认 CTest。

| 目录 | 说明 |
|---|---|
| usb_forwarding_tunnel | USB/IP 隧道手动联调探针:需要 10 个 CLI 参数和真实 usbipd 服务端,不能进 CI。用法看 main.cpp 头注释 |
| `file-mapping/smoke/` | 文件映射冒烟二进制,复用 app 源码,手工构建运行。刻意**不注册**进 moonlight-qt.pro 的 SUBDIRS:它链接 app 后端与 OpenSSL,注册会给全部平台构建(含 SteamLink 老工具链)平添失败面,而它没有自动化验证价值 |

## 约定

- 测试**不得**把 Makefile/.o/可执行文件提交进仓库(.gitignore 已覆盖
  `tests/*/{Makefile*,.qmake.stash,*.o,release,debug}`),历史上曾误提交过
  Linux 二进制毒化 Windows 构建(.qmake.stash 被 MSVC qmake 加载直接报错)。
- 被测生产代码有平台分支的(如 clipboardsync.cpp 的 macx 段),跨平台跑法在
  .pro 里用条件 SOURCES 处理,别在测试里复制逻辑。

剪贴板 TLS 回归仅访问本机随机端口。`clipboard_payload_routing/fixtures/` 中的证书和私钥是公开测试素材，不能用于实际配对；证书有效期至 2036 年，届时需要更新。测试不会访问用户配对凭据，也不使用系统剪贴板。macOS 的原生复合写入路径仍需实机验证。

`gpui_control_owner` 是手动可见视窗 fixture，不加入无人值守 CTest。以原生 CMake 构建此 target，将 exe 复制为隔离目录的 `Moonlight.exe`，与正式 GPUI 前端、SDL2.dll、SDL3.dll 放在一起；执行 fixture 会启动前端并把 LOCALAPPDATA 限定在该目录的 `isolated-profile`。它只模拟协议 1 的串流／控制事件，不连接网络或 host，也不发送远端输入。Ctrl+Alt+Shift+O 开启选单，F10／F11 分别切换 SDL 无边框／独占全萤幕。用于检查同一 SDL 视窗上的选单与焦点，不替代真实 worker 的输入隔离、手把、HDR、DPI 或 Sunshine 验收。圆形六项菜单的最终 UI 与全萤幕返回仍须人工验证。

`native_identity` 经无 Qt CMake／CTest 验证生产身分生成：RSA 位数、私钥与证书配对、自签签章、CN／serial／效期、PKCS8／PKCS1 往返、不可读／加密私钥不等待输入，以及识别码格式。`check_nvhttp.py` 的隔离 QSettings 场景另经生产 IdentityManager 确认首次生成可由 Qt SSL 接点读取、重启不改写既有身分档案、不可读测试私钥触发原有重新产生流程时不沿用旧证书快取，并保留无关设定与识别码；这些场景不读取或修改用户 profile。

`scripts/test-native-parsers.ps1 -QtDirectory <SDK> [-Configuration Debug]` 比较无 Qt 的 `native_parser_probe` 与只依赖 Qt Core 的 `qt_parser_reference`。39 个公开输入包含主机 XML／JSON、繁中、空值、namespace／CDATA、实体、UTF-8、非法 escape／surrogate、整数字段及深度边界；原生执行 PATH 不含 Qt，比较报告写入 build 下的 `comparison-<Configuration>.json`。该入口成功表示比较程序已执行，不表示差异已接受；当前候选规则有 7 项差异，正式 worker 不会加载此原型。64 层 JSON 限制用于避免已重现的 Debug 深层树 stack overflow，正式限制与 XML DTD／异常输入政策仍须 SA 确认。`prepare.py` 固定官方 Expat 2.10.0／nlohmann/json 3.12.0 发布归档及 SHA256，缓存修改会中止；两份原始授权随来源保留，不读取用户 profile、不连接 host，也不解析外部实体的文件／URL。

`tests/rust_host_api` 直接复用正式 `windows-lite/host-api` 的 Rust HTTP／TLS，不复制传输逻辑。先按既有入口准备 `native_host_http.exe`（只复用其公开同公钥换证 fixture 生成器）、`qt_parser_reference.exe` 与 Qt SDK，再设置 Cargo／MSVC 环境和 `CARGO_TARGET_DIR`，运行 `scripts/test-rust-host-api.ps1 -QtDirectory <SDK> -NativeHttpProbe <exe> -QtParserReference <exe> [-Configuration Debug]`。入口也执行正式 crate 的 JSON、显示名称 fallback、数值范围与诊断隐藏凭据测试。固定 Rust 1.99.1、Cargo.lock 与 `--locked`，Debug／Release 都停用 incremental；不删除现有快取。产品依赖授权由既有 GPUI `package.ps1` 的 Cargo metadata／原始授权收集流程保留。

该入口复用 `native_host_http/check.py` 的 loopback TLS 情境，并新增 TLS 1.2／1.3、部分 body 逾时／取消及 localhost 覆盖；C++ 与 Rust 共用上层错误分类断言，不比较 curl 专属错误码。Rust 执行 PATH 不含 Qt，仅 Qt reference 单独加入 SDK。另复用 `native_parsers/check.py` 的 39 案并断言 7 项已记录差异，报告在唯一的 `build/rust-host-api/<configuration>-<id>/parser-comparison.json`。SA 已确认标准解析规则：严格 UTF-8／JSON、禁止 XML DTD、保留标准／数字实体、serde_json 默认 128 层与原字段范围；这取代前述 C++ 原型的待确认候选，C++ 解析库不会因此接入生产。

`python tests/rust_host_api/check_bridge.py <worker-exe> <runtime-dll-directory> <qt-sdk-plugins-directory> <unique-work-directory>` 启动本机 HTTP／mTLS fixture，并显式执行 GPUI 的 ignored 测试 `core::tests::rust_displays_through_real_worker_context`。使用真实 Rust Client 与 worker，经过隔离 QSettings、匿名 pipe context 与正式 Rust API，验证繁中／emoji、空名称 fallback、查询身分与单次 `/displays`，确认 worker 未发出这次请求、身分未进入诊断且正常结束。runtime 目录提供既有 DLL，SDK plugins 提供 portable 未部署的 offscreen plugin；不修改系统或既有部署。一般 `cargo test` 不自动启动 worker；前端常规测试另覆盖取消、过期 context 与已经排队的旧回覆。`scripts/smoke-gpui-core.py` 也验证 context 的缺少主机、无效编号、离线主机与过期配对提交拒绝。

Rust 配对验证使用同一生产 crate。Cargo／上述 bridge 验证前先 dot-source `scripts/set-rust-host-api-env.ps1`，使用 repository OpenSSL 3.6.5，不需安装系统套件。前述回归入口加 `-PairingPeer <native_pairing_peer.exe>` 会运行 `native_host_http/check_pairing.py --extended-rust` 的 22 案：世代 6／7、错误 PIN、忙碌、损坏证书／签章、短及非 block 回应、各阶段普通拒绝／HTTP 异常、非法 XML 与首次等待取消，检查清理次数及提前 TLS。peer 来自既有 `native-host-http` CMake target；未加此参数不宣称配对验收。正式 crate 单元检查另验证 OpenSSL runtime 版本、含零位元组的衍生金钥，以及不可读／加密私钥在网络前失败且不等待输入。

设置 `RUST_DISPLAY_HTTP_ERROR=1` 再执行显示清单 bridge，可验证 HTTP 503 即使带有效清单 body 仍回空清单，保留 NvHTTP 的失败行为。

bridge 命令末端加 `<native_pairing_peer.exe> <ok|wrong-pin|final-reject>`，显式执行 `core::tests::rust_pairing_through_real_worker_context`。测试透过真实 PIN 事件供独立 peer 验证完整协议，检查繁中 pairname 即时用于后续请求、每次 nonce 不重复、只有最终成功储存憑证、失败保留空 pin、身分不进诊断与正常结束。PIN 文件／公开 fixture 仅放唯一 build 目录，LOCALAPPDATA／worker profile 全部隔离。一般前端测试另验证过期配对 context 被忽略及 worker 拒绝后可重试。上述不代表真实 Sunshine、新配对后影音串流、Session 重连、吞吐或延迟已验收。

回归入口加 `-HostReference <nvhttp_native.exe>`，执行 `rust_host_api/check_hosts.py`。35 案包含生产 NvHTTP／NvComputer 的 18 组实际字段比较（非复制 Qt 解析器）：主机能力、版本、MAC、port、数值、display modes、繁中／namespace／CDATA、应用顺序、空标题／不完整项与嵌套字段；另验证已知／探测 port、未配对、pinned XML 401 恢复、TLS 错误不降级、HTTP 错误／redirect、取消／逾时、已确认的严格 XML 及 IPv6 loopback。scoped loopback URL 在现有 native HTTP 与 Rust 都失败，仅核对既有结果，不宣称新增支援。Qt 仅给 reference PATH，Rust 不借用 Qt。

`python tests/rust_host_api/check_monitor.py <worker-exe> <runtime-dll-directory> <qt-sdk-plugins-directory> <unique-work-directory> [large]` 运行真实 `core::tests::rust_monitor_through_real_worker`。隔离 profile 预置自订主机名、两条位址及应用偏好；fixture 同时提供错误 UUID 的 HTTP／TLS peer、正确 mTLS peer、首次失败／空清单及后续成功。验证原主机名、顺序、hidden／direct_launch、两轮位址重试、离线／恢复仍保留快取、删除后不继续请求或复活及正常关闭。`large` 使用 1,203 项清单并实际等待十轮（约 30 秒）确认更新间隔，亦覆盖超过普通控制命令 64 KiB 的 typed IPC。一般前端测试另验证 generation 替换与取消。测试不连接用户 host、不读取旧 profile；尚未测试真实 Sunshine／游戏与效能。

`-HostReference` 同时执行 `rust_host_api/check_quit.py` 的 14 项工作阶段结束测试：9 项对照生产 NvHTTP 的结果与请求顺序，另验证错误 pin、严格 XML、/cancel 与查证阶段取消及查证逾时；确认取消最多一次、无 redirect／TLS 降级。`check_bridge.py` 最后加 `quit` 执行真实 `core::tests::rust_quit_through_real_worker_context`，在隔离 profile／本机 mTLS 验证未完成前 busy、过期 quit_complete 不生效、非拥有者失败提示、失败后重试、查证后清空 current_app、空闲短路及请求尚未完成时关闭；日志不得出现 C++ Quit response 或凭据。该测试覆盖控制页 quit，不是 Session 串流 teardown／quit-after，也不取代真实 Sunshine 验收。
