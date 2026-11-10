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

`native_host_http` 直接链接生产 `nativehttptransport.*`，运行 `scripts/test-native-host-http.ps1`（另可传 `-Configuration Debug`）。脚本验证固定 curl 来源杂凑、构建并以不含 Qt 的 PATH 执行 Python loopback TLS fixture；覆盖完整凭证比对先于 HTTP、同公钥换证拒绝、记忆体 mTLS、禁止重新导向、代理绕过、期限和回應上限，并验证取消等待的唤醒、UTF-8 JSON、HTTP 错误 status／body、连续身分切换及未配对 HTTP 的明确开关。只复用公开测试金钥，不连接真实主机或操作用户配对资料。此模组也可透过原生核心的 `-CurlSourceDirectory` 选项构建，现有 Qt worker／portable 尚未切换。

脚本也以无 Qt 的 socket owner 测试事件入口：只依 socket／libcurl 单次期限驱动，不持续轮询；验证部分回應的取消／逾时、无限期限取消、主机名称解析及原有 TLS／HTTP 政策。加 `-QtDirectory build/tools/qt/6.12.0/msvc2022_64` 可额外构建 `native_host_http_qt_owner`，直接复用生产过渡接点 `qtnativehttptransport.*`，分别在主线程与测试拥有的工作线程确认等待中其他 Qt 计时事件继续执行、应用 `quit()` 中断无限期限请求，并拒绝隐藏的 stderr 警告。只有此额外测试的 PATH 加入 Qt；原生测试仍排除 Qt。过渡接点尚未用于现有 NvHTTP，不等同正式配对／串流关闭已验收。

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
