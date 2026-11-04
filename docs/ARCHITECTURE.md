# 仓库架构与维护规则

UI 当前的 C++ 原生功能为基线。业务内核只有一份，CLI 和 UI 各自负责输入与呈现。

```text
BluetoothAudioCodec.slnx
├── BluetoothAudioCodec.WinUI/   UI host：XAML、窗口状态、本地化、MSIX
├── BluetoothAudioCodec.Cli/     CLI host：参数、Ctrl+C、终端与 JSON
└── BluetoothAudioCodec.Core/    两个 host 导入同一个 vcxitems 源码清单
    └── Services/               ETW、音频、媒体恢复、设备身份、诊断、IPC
```

Core 以共享源码项目导入两个可执行程序，分别静态编入，不新增运行时 DLL。
Core 的头文件与实现使用 Windows SDK / C++/WinRT，不引用 XAML、WinUI 或窗口类。
`Services/pch.h` 是 Core 的公共 Windows/STL 依赖头，按普通头编译；UI 单独保留其预编译头。
两个 host 提供 `Native::Localize`：UI 使用资源管理器，CLI 提供 Core 所需的英语展示标签。

共享检测入口是 `Native::DetectWithElevation`。它负责输出设备选择、UAC helper、认证管道、
监听与测试音时序、播放暂停/恢复、结束后目标核对和诊断记录。CLI 不得改用底层
`Detect` 绕过这些步骤。helper 使用当前 host EXE，host 必须首先分派 `--elevated-helper`。

| 功能 | Core | UI | CLI |
| --- | --- | --- | --- |
| codec 解析、过滤、原始 ID | 实现 | 展示 | 文本 / JSON |
| 媒体暂停、测试音、恢复、取消 | 实现 | 进度与交互 | Ctrl+C、恢复告警 |
| 设备连接与身份查询 | 实现 | 持续监听并刷新 | 检测时查询 |
| 日志、诊断记录、脱敏、ZIP | 实现 | 选择、预览、保存 | 参数指定路径导出 |
| 九种语言、窗口与布局 | 无 | 实现 | 无 |

新增业务功能先进入 Core，再让两个 host 接入。新增 Core 文件必须登记在
`BluetoothAudioCodec.Core.vcxitems`，禁止把检测代码复制到 CLI 或 UI。
UI 发布脚本及原解决方案入口保持可用；根解决方案方便同时开发两个版本。
安装器仅打包 UI，CLI 通过 `build-cli.ps1` 单独构建。

测试入口：`test-bluetooth-state.ps1`、`test-device-profiles.ps1`、
`test-native.ps1 -ExecutablePath <UI EXE>`、`test-diagnostic-reports.ps1`，以及 CLI 的
`--self-test` 和 `test-cli.ps1`。硬件 codec 捕获另需真实蓝牙设备与 UAC 验证。

`docs/` 内的迁移、审计、上架记录保留其历史语境；其中旧 C# 路径描述历史版本，
当前开发以本文和根 README 为准。`archive/legacy-cli/` 不参与构建。
