# C++ / WinUI 3 迁移

GUI 已从 C# / .NET 10 迁移为 C++20 / C++/WinRT + WinUI 3。原有 `App.xaml`、
`MainWindow.xaml`、图标及九种语言资源继续使用。根目录可选的 `BluetoothAudioCodec.cs`
命令行工具保持独立，不参与 GUI 编译或发布。

## 实现

| 部分 | 原生实现 |
| --- | --- |
| 界面 | 原有 XAML + C++ 窗口代码，保留 Mica、卡片、技术详情和支持链接 |
| 蓝牙/音频设备监听 | 系统 WinRT DeviceWatcher，由 C++/WinRT 调用；弱引用回调、事件撤销及窗口关闭清理 |
| 编码检测 | Windows ETW `StartTraceW` / `EnableTraceEx2` / `OpenTraceW` / `ProcessTrace`，通过 TDH 读取事件元数据与字段 |
| 默认音频输出 | 系统 Core Audio COM；`com_ptr` 管理接口，释放 PROPVARIANT |
| 测试音 | WASAPI 共享模式绑定目标端点，使用混音格式生成 660 Hz / 600 ms 短音，有界播放并排空；记录失败及取消 |
| 提权 | 同一 EXE 的固定 `--elevated-helper` 模式；UI 保持普通权限，按需 `ShellExecuteExW(runas)` |
| 通信 | 当前用户 SID 的管道 ACL、拒绝远程连接、首个管道实例、系统随机 256 位令牌、校验助手 PID |
| 协议 | 系统 `Windows.Data.Json`；严格参数、消息大小限制、可取消的重叠 I/O |
| 本地化 | MRT Core + 原有 RESW；商店包每个架构切片保留全部九种语言 |
| 日志 | UTC JSONL、本地约 1 MiB 轮转及脱敏导出；记录采集阶段、驱动版本、播放错误和事件/解析统计；路径仍为 `%LOCALAPPDATA%\BluetoothAudioCodecViewer\Logs\runtime.log`，不自动上传 |

删除了 GUI 的 C# 源文件与 csproj；解决方案入口改为
`BluetoothAudioCodec.WinUI/BluetoothAudioCodec.WinUI.vcxproj`。
原生项目不引用 TraceEvent，不发布 CoreCLR、JIT、BCL、C# Windows SDK / WinUI 投影。

TDH 严格按 `A2dpStandardCodecId`、`A2dpVendorId`、`A2dpVendorCodecId` 查找标量整数字段，
不按固定位置猜测，不硬编码事件负载字节偏移。观察时间保留 ETW 的 UTC 时间戳。
未知编码 ID 继续显示原始数值。

当前采集策略、跨重连及失败提示、日志导出与回归覆盖见 [原生版修复记录](NATIVE_CODEC_FIXES.md)。

窗口关闭、用户取消、无事件超时都会结束检测。ETW 会话及消费句柄使用 RAII 清理；
关闭实时消费句柄可唤醒安静的事件源。助手只接受固定参数，超时范围 1–60 秒，
连接等待 15 秒；响应大小上限 256 KiB。助手断开或收到认证取消命令时停止检测。
即使用户手动以管理员权限启动 UI，ETW 也仍在独立助手中执行，避免窗口关闭时
UI 进程退出打断会话清理。后台 COM 初始化和 UI 调度器关闭也有对应的生命周期处理。

## 构建和发布

### 在 Visual Studio 2026 中测试

1. 打开 `BluetoothAudioCodec.WinUI/BluetoothAudioCodec.WinUI.slnx`。
2. 将 `BluetoothAudioCodec.WinUI` 设为启动项目，选择 **Debug / x64**。
3. 首次构建让 NuGet 自动还原；按 **F5 / 本地 Windows 调试器** 启动。
   Debug 默认 `WindowsPackageType=None`、`WindowsAppSDKSelfContained=true`，
   不生成/部署 MSIX，也不需要测试签名；依赖随构建复制到输出目录。
4. 在窗口内点击检测时批准 Windows UAC；普通启动界面不需要管理员权限。
   复现后通过 **Report a problem** 选择记录、预览并保存诊断 ZIP；近期脱敏运行日志可选附加。

默认 Debug 输出：`BluetoothAudioCodec.WinUI/bin/native/offline/x64/Debug/`。
若只运行主界面，Ctrl+F5 也可；断点建议放在 `MainWindow.xaml.cpp` 的
`OnDetectClicked`、`OnExportClicked`。ETW 在独立提升权限的同名 helper 中执行，
UI 的 F5 不会自动附加 helper；需要调试 `Detect`/`OnEvent` 时，在采集窗口内用有权限的
调试器附加该 helper PID，日志中的 `helper-launched` 会记录该 PID。
单纯复测检测成功与否无需附加 helper，也不需要修改程序参数。

如 VS 已缓存旧的“部署”配置，在配置管理器取消 Debug 项目的“部署”，
调试目标选择“本地 Windows 调试器”。项目已移除解决方案的默认 Debug 部署项。
ARM64 配置用于 ARM64 设备；在普通 x64 电脑上先使用 x64。

Debug 用于本机开发，Release 的商店打包默认配置保持不变。
部署方式参见 [Microsoft 非打包 WinUI 指南](https://learn.microsoft.com/windows/apps/package-and-deploy/unpackage-winui-app)
及 [自包含部署](https://learn.microsoft.com/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps)。

需要 Visual Studio 的 C++ 桌面开发、WinUI C++ 工具、Windows SDK 26100，
以及 ARM64 交叉编译器。VS 2026 使用 v145；项目也为 VS 2022 设置 v143。
实际验证的环境为 VS 2026 / MSVC 14.51、Windows SDK 26100。
本机缺失的 ARM64 编译工具及库从微软 Visual Studio 清单下载并校验 SHA-256，
仅解压到忽略的 `artifacts/native-toolchain/`；未修改系统工具链。
普通构建机器应通过 Visual Studio Installer 安装对应组件，不依赖这个本地路径。

GUI 不需要 .NET SDK；WiX 安装器构建仍通过 .NET SDK 运行。

```powershell
.\build-store-msix.ps1 -Version 1.0.3.0 -OutputDirectory artifacts\native\msix
.\build-store-installer.ps1 -Architecture all -Version 1.0.3 -OutputDirectory artifacts\native
```

商店脚本默认先清理两个架构。开发时可使用 `-Incremental`；资源、版本、架构、
框架依赖、符号及禁止发布 .NET 文件等校验仍然执行。
`-MSBuildArguments` 允许显式提供工具链覆盖参数，默认不使用任何本机专有路径。

EXE 文件版本由构建版本生成；未显式设置版本时取 MSIX 源清单。

| 发布方式 | 部署内容 |
| --- | --- |
| 商店 MSIX | 小型原生程序、资源与必要的原生依赖；由商店安装共享 Windows App Runtime 2.2+ 和清单声明的 VCLibs 依赖 |
| 离线 MSI | 完整 EXE + WinUI DLL / PRI / XBF / 图像目录；包含 app-local Windows App SDK 文件 |

离线版不再使用 .NET 自解压单文件。只复制 EXE 无法运行，应该安装 MSI，或复制完整发布目录。
主程序 Release 使用静态 C++ CRT；保留 SDK 产生的 WinUI 原生依赖文件。
打包不携带 PDB、LIB、EXP 或旧 .NET 文件。

商店构建把 XAML 作为 EmbeddedData 放入 `resources.pri`。校验脚本使用 `makepri dump`
确认 `App.xbf`、`MainWindow.xbf` 和九种语言均在包中，避免 SDK 语言拆包过滤掉翻译。
上传包保留 x64 / ARM64 的独立符号包；符号不属于用户安装内容。

## 验证范围

- x64 / ARM64 原生编译、链接和 MSIX 打包。
- x64 / ARM64 MSI 解包，分别比对完整目录的 99 个文件，全部 SHA-256 一致；主 EXE 架构及无 CLR 检查通过。
- x64 离线窗口启动、设备状态显示、技术详情展开；界面外观检查。
- 使用独立 `NativeSmoke` 身份注册并启动商店配置，确认从共享 Windows App Runtime 加载 WinUI，进程未加载 CoreCLR。
- 原生服务自检：编码名称、未知 ID、中文/Unicode 与时间戳序列化、令牌拒绝、参数约束、已取消请求。
- 蓝牙状态修复后的独立回归、周期刷新及线程退出检查，见 `BLUETOOTH_STATE_REVIEW.md`；早先的迁移安装包不含此后续修复。
- 真实命名管道测试：无权限助手返回认证错误并以 5 退出；无效参数以 2 退出，缺失管道以 3 退出。
- 测试身份已移除；原有商店版身份和版本保留。

可复现无提权测试：

```powershell
.\test-native.ps1 -ExecutablePath <完整离线发布目录>\BluetoothAudioCodec.WinUI.exe
# 已注册 MSIX 的已解包目录可额外传入 -Packaged
```

尚需实机验证：用户批准 UAC 后的真实蓝牙 ETW 抓取、进行中取消及无事件超时，
ARM64 设备运行，以及干净系统上的安装/升级。上述无提权测试不会触发 UAC、播放测试音或更改蓝牙设置。
构建产物未经代码签名，也未上传或发布。当前使用 1.0.3 版本测试，正式更新应提高发布版本。

体积数据见 `APP_SIZE.md`；机器可读结果保存在忽略的 `artifacts/native/` 中。
