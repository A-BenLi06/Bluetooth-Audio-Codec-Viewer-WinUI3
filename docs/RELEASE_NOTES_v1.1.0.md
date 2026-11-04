v1.1.0 将应用从 .NET/C# 全面迁移至原生 C++20，新增共享检测内核和 CLI 工具。

## 变更

- 界面和检测逻辑迁移至 C++20 / C++/WinRT + WinUI 3，移除 .NET Runtime 依赖
- 提取共享原生内核 `BluetoothAudioCodec.Core`，CLI 和 UI 编译同一份源码
- 新增原生 CLI 工具 `BluetoothAudioCodec.Cli`，支持 `--json` 输出、`--watch` 循环检测和诊断导出
- 检测时自动暂停正在播放的系统媒体会话，完成或取消后恢复
- 改进目标设备匹配、蓝牙连接状态判断和 ETW 事件验证
- 新增统一"报告问题"入口，支持预览、脱敏诊断 ZIP 导出和可选运行日志附加
- 改进设备型号和厂商信息采集

## 安装

下载对应架构的离线 MSI：
- x64 (Intel/AMD)：`BluetoothAudioCodec-1.1.0-x64.msi`
- ARM64 (Snapdragon 等)：`BluetoothAudioCodec-1.1.0-arm64.msi`

安装包未签名，首次运行可能触发 SmartScreen 提示。安装包内置 WinUI 运行库，不需要 .NET。

也可通过 [Microsoft Store](https://apps.microsoft.com/detail/9P8G2CQW77JT) 安装。此 Release 不代表商店已更新至 v1.1.0。

CLI 需从源码构建（`build-cli.ps1`），MSI 仅包含 GUI。

## 范围

Windows 10 build 19041+ 和 Windows 11。检测 Bluetooth Classic A2DP，不支持 HFP 和 LE Audio。检测需要一次 UAC 授权。

## 校验

SHA-256 见附件 `SHA256SUMS.txt`。
