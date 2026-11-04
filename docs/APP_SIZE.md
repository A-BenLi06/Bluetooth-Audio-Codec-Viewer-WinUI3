# 应用体积与发布依赖

WinUI 项目使用 `Microsoft.WindowsAppSDK.WinUI 2.2.1`，这是 Windows App SDK
2.2.0 总包采用的 WinUI 组件版本。它会传递引用 Foundation 2.1.0、
InteractiveExperiences 2.0.15 和 Base 2.0.4，提供窗口管理、WinUI 与 MRT Core。

直接引用 WinUI 组件可以避开应用未使用的 AI、ML、Widgets 和独立 DWrite
组件，避免将 ONNX Runtime、DirectML 等文件一起发布。WebView2 是 WinUI 的
传递依赖，原生版保留 SDK 输出的 Core DLL 和元数据；应用没有携带固定版本浏览器运行时。

当前 GUI 已迁移为 C++20 / C++/WinRT + WinUI 3，不再发布 .NET、C# WinRT 投影或
TraceEvent。XAML、九种语言、设备监听、取消/超时与按需提权流程保留。
商店 MSIX 默认依赖共享 Windows App Runtime 2.2 或更高版本，由商店处理依赖安装；
离线 MSI 包含 WinUI 原生运行文件，采用目录布局，不再是 .NET 自解压单文件 EXE。
根目录可选的 `BluetoothAudioCodec.cs` CLI 仍独立使用 .NET 10，不进入 GUI 发布包。

迁移实现和验证范围见 [NATIVE_MIGRATION.md](NATIVE_MIGRATION.md)。

## 当前：C++ / WinUI 3（2026-10-21）

实际构建产物，单位为 MiB（1,048,576 字节）：

| 产物 | x64 | ARM64 |
| --- | ---: | ---: |
| 商店 MSIX 主架构包（压缩） | 1.129 | 1.153 |
| 商店 MSIX 主架构包（解包文件合计） | 2.226 | 2.573 |
| 离线 MSI 安装包 | 15.137 | 13.840 |
| 离线完整安装目录 | 49.047 | 52.019 |
| 离线目录中的主 EXE（不能单独分发） | 1.018 | 1.064 |

x64 商店主架构包相对上一轮 C# 的 47.48 MiB 减少约 **97.6%**；离线 MSI 相对
组件精简后的 50.25 MiB 减少约 **69.9%**。两种 GUI 发布方式都已移除 .NET。

双架构 `.msixupload` 为 **32.359 MiB**，其中包含较大的原生调试符号包，供商店诊断使用；
符号不下载到用户设备。上传包还含缩放资源切片，主架构包大小也不等于商店实际网络下载量。
共享 Windows App Runtime / VCLibs 首次安装下载量另计，不能把 1.129 MiB 解释为干净设备的全部下载量。

两种 MSI 均解包核对了 **99 个文件**，与对应发布目录的路径和 SHA-256 全部一致。
EXE 的 CLR 目录为空、处理器架构正确、AppContainer 标志关闭；静态 DLL 导入未发现
未打包的非系统依赖（基于本机 System32 和 Windows API set 名称检查，不能替代干净系统实测）。
两种 MSIX 均检查了内嵌 XAML、九种语言、共享框架依赖及独立符号。

产物及可复查记录：

- `artifacts/native/installer/{x64,arm64}/BluetoothAudioCodec-1.0.3-{x64,arm64}.msi`
- `artifacts/native/msix/BluetoothAudioCodec.WinUI_1.0.3.0_x64_ARM64_bundle.msixupload`
- `artifacts/native/artifact-audit.json`、`test-results.json`、`msi-check/x64/test-results.json`

当前安装包未签名，尚未发布。真实 UAC 后的蓝牙 ETW 检测、ARM64 运行和干净系统
安装/升级仍需实机验证。以下两轮数据是迁移前 C# 版本的历史基线。

## 第一轮：组件精简结果

以下为组件精简前后的 x64 结果，单位为 MiB（1,048,576 字节）：

| 产物 | 精简前 | 精简后 | 减少 |
| --- | ---: | ---: | ---: |
| 单文件 EXE | 218.22 | 166.60 | 23.7% |
| MSI 安装包 | 67.60 | 50.25 | 25.7% |
| 单架构 MSIX 压缩包 | 86.98 | 65.95 | 24.2% |
| 单架构 MSIX 文件解压合计 | 220.39 | 168.96 | 23.3% |
| x64 + ARM64 MSIX 上传包 | 172.40 | 130.96 | 24.0% |

精简后的 ARM64 EXE 为 180.88 MiB，MSI 为 47.32 MiB，单架构 MSIX 为
64.14 MiB，MSIX 文件解压合计为 184.18 MiB。

EXE 对比使用同一 SDK、源码和发布参数分别构建，只改变组件引用。MSIX 对比
使用本地已有的 1.0.3.0 上传包；此前的构建时间与本次不同，因此该差值也可能
包含小幅生成代码差异。MSI 基线是原有的 GitHub 1.0.3 发布文件。

这些都是文件逻辑长度。MSIX 上传包包含两种处理器架构，不等于单台设备实际
下载量；单文件 MSI 应用运行时还会释放内容，EXE 大小也不等于全部磁盘占用。

本次测试产物和校验记录位于 `artifacts/size-reduction/`，不替代正式发布版本。
测试安装包未签名。发布时应提升版本，并执行对应渠道的签名和认证流程。

## 第二轮：商店 MSIX 使用共享运行时

2026-10-19 构建，仍使用 Windows App SDK 2.2.0 对应组件，.NET 仍随应用发布。

| 产物 | 第一轮自包含 | 本轮共享运行时 | 相对第一轮减少 |
| --- | ---: | ---: | ---: |
| x64 MSIX 压缩包 | 65.95 MiB | 47.48 MiB | 28.0% |
| x64 MSIX 解压内容 | 168.96 MiB | 121.09 MiB | 28.3% |
| ARM64 MSIX 压缩包 | 64.14 MiB | 47.03 MiB | 26.7% |
| ARM64 MSIX 解压内容 | 184.18 MiB | 133.68 MiB | 27.4% |
| 双架构上传包 | 130.96 MiB | 95.36 MiB | 27.2% |

原始 x64 MSIX 为 86.98 MiB，当前应用包累计减少约 45.4%。新结果位于
`artifacts/size-reduction/shared-runtime/`，包括 `size-comparison.json` 和隔离测试
记录；原有商店安装未被替换，测试包身份已移除。

这是应用专属包体积的减少，依赖本身仍需要存在于设备上。以 SDK 附带的 2.2.0
x64 Framework 包为例，其压缩大小约 41.93 MiB；若设备没有共享运行时，粗略的
首次下载合计可达到约 89.41 MiB（47.48 + 41.93），大于上一轮的 65.95 MiB。
商店可能交付更新的运行时版本，实际网络下载量本次未测量。共享模式的优势在于
已安装 Runtime 的设备直接复用，以及运行时由商店集中维护。

项目分别为 MSIX / MSI 选择 Windows App SDK 分发方式。`build-store-installer.ps1`
显式设置 `WindowsAppSDKSelfContained=true`；两个架构的离线单文件重新发布通过，
内部文件清单与第一轮相同，除应用 DLL 外的内嵌文件哈希相同；x64 离线窗口启动
和 helper 参数校验通过，保留原有无需额外安装 Runtime 的离线使用方式。

应用只使用 Framework API，不使用 Main/Singleton 功能，因此按微软建议关闭
`WindowsAppSdkDeploymentManagerInitialize`，避免在 UI 与 helper 启动时进行
额外运行时包部署。[微软自动初始化说明](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/project-properties)

本轮验证：

- 正式身份的双架构 StoreUpload 构建与脚本内包校验通过，零警告、零错误。
- 声明 `Microsoft.WindowsAppRuntime.2`、微软 Publisher 和最低版本 2.2.0.0；
  应用包保留 .NET，且没有嵌入 WinUI 原生 runtime、ONNX 或 DirectML。
- 使用单独测试身份注册开发包，经 AppModel 激活后创建响应正常的窗口，确认
  `Microsoft.UI.Xaml.dll` 来自本机共享 Windows App Runtime 2.5.1.0。
- helper 拒绝不完整参数；普通权限 helper 通过命名管道返回带正确 token 的
  权限错误并正常退出，未请求 UAC 或启动 ETW。
- 测试身份在验证后移除；真实蓝牙、提升权限后的 helper 和 ARM64 实机仍待验收。

## MSI 构建的缓存保护

WiX 6 的增量构建读取上一次构建的绑定路径。安装器项目现在将当前 EXE、图标和
预处理参数文本加入构建输入，切换 `AppPublishDir`、版本或厂家信息时会重新绑定。

WiX 默认还可能将输出 MSI 硬链接到中间文件。项目关闭了该选项，让安装器输出
成为独立文件，避免后续构建中间文件时影响已有安装包快照。

验证 MSI 时应读取 `File` 表，确认 `BluetoothAudioCodecExe` 的 `FileSize` 与
当前发布 EXE 一致；在发布验收中还应提取载荷并比较 SHA-256。

## 构建与验证

原有发布入口继续使用：

```powershell
# 离线 MSI：参数决定 x64 / ARM64 / all。
.\build-store-installer.ps1 -Architecture all -Version 1.0.3

# 商店 MSIX：版本需与 Package.appxmanifest 一致。
.\build-store-msix.ps1 -Version 1.0.3.0
```

上述命令使用默认正式输出目录。进行前后对比时，应给 `dotnet publish` 设置独立
`--output`，给 WiX 设置独立 `OutputPath`，给 MSIX 设置独立 `AppxPackageDir`。

本次已验证：

- x64、ARM64 的自包含单文件发布和 MSI 构建成功。
- 从两种 MSI 提取的 EXE 的 SHA-256 均与对应发布 EXE 一致；新 MSI 输出为独立文件，旧版发布 MSI 的 SHA-256 与原发布清单一致。
- 双架构 MSIX StoreUpload 构建成功，包含两份非空 `.appxsym`。
- 依赖图和 EXE / MSIX 文件清单不含 AI、ML、Widgets、独立 DWrite 与历史构建目录。
- MSIX 保留两套运行时、Bluetooth / runFullTrust / allowElevation 声明，以及与基线相同的资源包结构。
- 九种语言 `.resw` 仍是 PRI 构建输入；没有进行全部语言的端到端运行验证。
- x64 精简 EXE 可以创建响应正常的窗口；helper 拒绝不完整参数并返回预期退出码。

真实蓝牙检测、完整 UAC 同意/拒绝/取消流程、干净 Windows 10/11 安装升级以及
ARM64 实机运行仍应在正式发布前验证。
