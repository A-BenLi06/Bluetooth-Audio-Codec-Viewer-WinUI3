# CLI

原生 C++20 控制台入口，使用从 UI 提取的共享 Core，无需 .NET 或 WinUI Runtime。

在仓库根目录运行 `./build-cli.ps1`；支持 `-Configuration Debug` 和 `-Platform ARM64`。
需要与 UI 相同的 Visual Studio C++ 工具链和 Windows SDK。

| 参数 | 行为 |
| --- | --- |
| `--help` | 参数说明 |
| `--timeout 1..60` | 单次监听秒数，默认 15 |
| `--json` | 每次检测输出一行 UTF-8 JSON，数字厂商 ID 和 FILETIME 用十进制字符串避免精度丢失 |
| `--no-tone` | 不播放测试音，仍使用共享媒体暂停与恢复流程 |
| `--watch` | 重复完整检测，间隔 1 秒；可能每次弹出 UAC |
| `--export-report PATH [--include-runtime-log]` | 导出最近记录或应用上下文 ZIP |
| `--export-diagnostics PATH` | 导出兼容 TXT 日志 |
| `--self-test` | 运行共享内核内置测试，使用隔离的诊断测试目录 |

退出码：0 成功，2 参数错误，3 未检测到 codec，4 取消（含 UAC 取消），10 运行错误。
`--watch` 的退出码取最后一次结果；Ctrl+C 返回 4。

检测通过同一个 EXE 的内部 helper 模式提权。Ctrl+C 请求取消并等待媒体恢复；强制结束进程
无法保证清理。JSON 包含设备显示名称，仅输出到当前终端；分享前自行检查。
CLI 和 UI 共用本机日志、诊断记录与保留策略。

CLI 不包含 UI 的窗口、本地化、报告预览与文件选择器；导出前请自行检查报告再分享。
旧 C# CLI 的实现位于 `archive/legacy-cli/`，其旧参数契约不作为新版兼容承诺。
