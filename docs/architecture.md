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
| `windows-lite/` | Windows Lite 的无 Qt CMake 入口，目前仅构建原生库；不包含仍依赖 Qt 的串流 app 与剪贴板 helper |
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

入口构建 `moonlight-common-c`（含 ENet／nanors）、`h264bitstream` 与 `AntiHooking`，执行协议库既有 CTest 和原生链接测试。默认产物放在 `build/lite-native`；可传入 `-BuildDirectory` 或 `-Configuration Debug`。独立 CI 为 `.github/workflows/lite-native.yml`，覆盖 Release 与 Debug。

固定版本的协议库解码测试在销毁已使用的佇列前遗漏关闭操作。`windows-lite/patches/common-c-decode-test-shutdown.patch` 只修正建置目录中的测试副本，生产源码与 submodule 指针不变；原 fork 远端没有本维护者的写入权限。测试数据与生产代码仍直接来自原 submodule，升级版本时必须重新核对补丁，无法套用即停止配置。测试另将 Debug CRT 断言输出为明确失败，避免等待交互对话框。原 fork 收录此修正后，应移除此测试补丁。

测试 PATH 仅包含原生依赖与 Windows 系统目录，不借用开发机 Qt 路径。

这仅完成原生库建置阶段：GPUI 前端仍由其 Cargo 路径构建，串流 worker、浮动介面与剪贴板 helper 仍需 Qt；现有 portable 封装仍走 qmake／windeployqt。不得把本入口通过视为 Windows Lite 已完成无 Qt 交付。

## 历史

- 2026-09 目录清理:移除空壳目录(src/、streaming/、include/、soundio/ 上游遗留)与误跟踪的 .DS_Store;filemapping-smoke 并入 file-mapping/smoke;DS5 探针脚本并入 scripts/。
