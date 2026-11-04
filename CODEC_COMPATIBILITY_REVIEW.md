# WH-1000XM3 编码检测兼容性检查

> 后续 C++ 实现已按本审计继续修复并加入可导出日志，见 [原生版修复记录](NATIVE_CODEC_FIXES.md)。下文保留各阶段调查当时的行为及限制；其中关于当前候选版取消检测等描述已由后续实现更新。


检查日期：2026-10-21。对象：当前 C++ / WinUI 3 工作区及独立 C# 命令行程序。

### 2026-10-22：截图版本归属更正

第三轮已补充公开符号和反汇编调用点、TraceEvent 3.2.6 实际库的离线分发及故障注入实验，详见 [RELEASE_CODEC_DEEP_AUDIT.md](RELEASE_CODEC_DEEP_AUDIT.md)。该轮没有修改生产检测逻辑。

用户确认截图来自上一个已发布 release，而不是当前未发布的原生工作区。本地最近 release 标签为 `v1.0.3`（`a217cf1`，C# / TraceEvent GUI）；其设备状态显示与截图一致。下面涉及当前原生版的新日志、TDH 解析、generation 失效规则及测试结果，均不能直接作为截图版执行过这些代码的证据。

对已发布 C# GUI 代码的复核表明：它同样只订阅 `8776…`，只消费 `A2dpStreaming`，等待 750ms 后播放短测试音，并在 30 秒后结束采集。因此“事件触发时机、持续播放复用串流、HFP、驱动/TraceEvent 解码兼容性”仍是适用于截图版的调查方向。

但该 release 的 `ApplyBluetoothStatus` 只刷新显示，没有当前原生版的 generation 机制，也不会因耳机断开重连而主动取消检测。上一轮提到的“检测期间重连会取消”仅适用于当前原生候选版，不能解释旧 release 的失败，也不应作为旧版复测限制。`a2dp-capture` 统计同样是本次加入原生候选版的功能，旧版日志中没有这些统计。已构建的原生候选版尚未作为 release 发布，不能视为已修复用户安装的旧版。

## 截图能确认的情况

第二张截图显示默认输出为 `耳机 (WH-1000XM3)`，设备连接状态为 connected，检测已超时并显示 `No event`。这与设备名称无法识别、UI 永久卡在 listening 是不同的问题：失败发生在编码事件采集或解析阶段。截图没有 Windows build、蓝牙驱动版本、应用版本、事件数据或诊断日志，不能据此确定该电脑的唯一根因。

Sony 官方说明 WH-1000XM3 支持 SBC、AAC、aptX、aptX HD、LDAC，通过 A2DP 播放。这五种格式均已有编码映射；没有发现 WH-1000XM3 专属映射缺失。耳机支持某格式，并不表示 Windows 当前一定协商了该格式。

来源：[Sony Supported codecs](https://helpguide.sony.net/mdr/wh1000xm3/v1/en/contents/TP0001703175.html)。

## 已修改的兼容性防护（不等于已确认根因）

### 1. Provider 订阅和身份判断

原实现仅启用 `8776AD1E-5022-4451-A566-F47E708B9075`，随后只接受名称为 `Microsoft.Windows.Bluetooth.BthA2dp` 的事件。微软发布的跟踪配置同时列出：

| Provider | GUID |
| --- | --- |
| Microsoft.Windows.Bluetooth.Telemetry.BthA2dp | 8776AD1E-5022-4451-A566-F47E708B9075 |
| Microsoft.Windows.Bluetooth.BthA2dp | DDB6DA39-08A7-4579-8D0C-68011146E205 |

现在同时启用这两个 GUID，按事件头 GUID 验证来源，保留 `A2dpStreaming` 事件名/任务名检查。C# 命令行入口也改为两个 GUID，并使用 GUID 过滤。这扩大了监听覆盖，但不能单凭配置文件认定原来订阅和名称不一致。

**深挖后的更正：**本机 `btha2dp.sys` 的 TraceLogging provider 元数据明确将 `8776…` GUID 与 `Microsoft.Windows.Bluetooth.BthA2dp` 名称放在同一结构中。微软 WPRP 的 `Id` 是配置标签，不能直接当作事件携带的 provider name。因此，第一轮将 GUID/名称不一致列为主要原因的判断证据不足；至少在本机驱动上，旧代码这两个值是匹配的。双 provider/GUID 过滤修改属于兼容性防护，尚不能称为该截图问题的根因修复。

来源：[Microsoft BluetoothStack.wprp](https://github.com/microsoft/busiotools/blob/master/bluetooth/tracing/BluetoothStack.wprp)。

### 2. 取消按固定字段位置猜测编码

原实现找不到字段名时，读取第 4、5、6 个字段。如果系统调整 schema，就可能把其他数字误当作 codec ID，甚至显示错误的 SBC。

现在仅按 `A2dpStandardCodecId`、`A2dpVendorId`、`A2dpVendorCodecId` 读取，支持顺序变化和字段名大小写变化。缺失字段明确报 schema 不支持，保留未知 ID 的原始数字显示行为。

本机驱动的三个字段仍正好位于索引 3、4、5，名称也与旧代码一致。因此，这项修改避免潜在误读，但没有证据表明它造成了截图中的超时。

### 3. 诊断与超时提示

- 原生测试音的播放返回值不再被忽略；失败会写日志，并在没有编码结果时作为警告返回。
- 每次原生采集结束写入 `a2dp-capture` 统计：启用的 provider 数、收到的事件数、元数据不可用数、streaming 事件数及是否识别成功。没有新增网络上传。
- 九种语言的默认超时信息改为“未收到匹配的 A2DP 编码事件”，并提示暂停其他播放、选择耳机输出后重试。没有收到事件并不能证明 Windows 没有建立过音频流。

## 验证结果

- x64 Release 编译、链接通过。
- ARM64 Release 使用工作区已有的 `artifacts/native-toolchain/common-build-arguments.json` 工具链配置交叉编译、链接通过；本机默认 VS ARM64 配置缺组件，直接构建会报 SDK 错误。
- x64 原生服务自检及 helper 参数、身份校验、权限错误测试通过。
- 新增自检覆盖两个 provider GUID、无关 GUID、字段重排与大小写、缺失字段、空元数据、非法属性数量，以及 WH-1000XM3 支持的五种编码映射。
- 既有蓝牙连接状态回归通过：断开、多个无线电、部分枚举失败、句柄释放、晚到通知和旧检测结果失效。
- `dotnet run ./BluetoothAudioCodec.cs -- --help` 编译及运行通过。

构建日志和服务测试 JSON 位于 `artifacts/codec-compatibility/`。本次没有触发 UAC、播放测试音或修改蓝牙设置；未在 WH-1000XM3 或 ARM64 实机执行 ETW 采集。以上是源码修复和回归证据，不是该耳机问题已经实机解决的证明。

## 复测入口与限制

完整离线测试目录：`artifacts/codec-compatibility/app-x64/` 和 `app-arm64/`。运行目录内的 `BluetoothAudioCodec.WinUI.exe`，保留其他文件；旧 MSI / MSIX 不包含本次修复。

建议先关闭会议/通话应用和耳机麦克风占用，暂停其他播放，确认默认输出是 WH-1000XM3，再开始检测并批准 UAC。若依旧失败，记录受影响电脑的 Windows build、蓝牙适配器及驱动、应用版本，并查看该机 `%LOCALAPPDATA%\BluetoothAudioCodecViewer\Logs\runtime.log` 的 `a2dp-capture` 行及警告。

当前检测只覆盖 Classic A2DP；HFP 和 LE Audio 仍不支持。持续播放可能复用原串流，测试音不保证重新协商。第三方蓝牙/音频驱动也可能不产生所需事件。

此外，当前原生连接状态保护会在检测期间设备连接或默认输出改变时取消检测，避免套用旧结果。因此如需重新连接耳机，应在重新连接并稳定后重新点击检测；不能承诺在同一次检测中跨断开/重连捕获。这是现有行为限制，本次未改变其结果归属规则。

## 第二轮：驱动元数据证据与根因分层

对本机系统驱动执行只读静态元数据解码，未加载驱动副本、播放音频、修改蓝牙或启动 ETW。使用的是 PowerShell 字节检查，非 REA 分析结果。REA CLI 6.7.0 的 doctor 显示客户端注册陈旧且缺少原生分析引擎；未修改其配置或安装组件。

- 路径：`C:\Windows\System32\drivers\btha2dp.sys`
- 文件版本：`10.0.26100.9711 (WinBuild.160101.0800)`
- SHA-256：`C7AC7B6F8E3CA1AC82BB0A80C20292434F01FB6F80609AEFBD1163767780A8CB`
- 格式依据：本机 Windows SDK 26100 的 `shared/TraceLoggingProvider.h`，其中 `_tlgProviderMetadata_t`、`_tlgEventMetadata_t` 明确规定 GUID、名称、事件字段和类型的布局。检查脚本会验证布局，不只提取相邻字符串。
- 复现脚本与结果：`artifacts/codec-compatibility/inspect-driver-metadata.ps1`、`driver-metadata.json`。

| 事件 | 本机元数据中的相关字段 | 对检测的意义 |
| --- | --- | --- |
| A2dpStreamingStart | SessionId、A2dpIsSink、BTDeviceAddress；没有 codec ID | 仅看到开始事件仍不能按当前算法报告编码 |
| A2dpStreaming | DurationInMilliseconds、三个 codec ID、A2dpIsSink、BTDeviceAddress | 当前实现只消费此事件；字段大小为 1、4、2 字节，与原生读取规则一致 |
| AvdtpSetConfigInfo | ResultCode、三个 codec ID、SampleRate、ChannelCount、远端地址 | 是值得进一步验证的配置事件；不能忽略结果/活动状态直接当成功协商 |
| A2dpAvailableRemoteCodec / A2dpAvailableOffloadedCodec | 三个 codec ID、设备地址等 | 是能力信息，不能把 available 直接当 active |

`A2dpStreaming` 带持续时间而 `A2dpStreamingStart` 不带编码，支持“编码事件与一段串流的汇总/结束有关”的假设。**静态 schema 本身不能证明具体发送时刻或执行过的调用路径**，需真实 ETW 时序或发送函数调用点证据验证。

最优先验证的因果链是：检测开始前已有持续 A2DP 播放 → 测试音混入既有输出 → 600ms 测试音结束但底层 A2DP 串流继续 → 30 秒内没有带 codec 的目标事件 → 超时。这个场景不要求耳机、蓝牙连接或 codec 本身有故障，且与截图相容；尚未在受影响电脑复现。

第二个待验证分支是 HFP：Windows 11 合并 A2DP/HFP 输出端点，打开耳机麦克风或 Communications 类输出可使系统选择 HFP；因此显示 `耳机 (WH-1000XM3)` 和 connected 不能证明当时在走 A2DP。来源：[Microsoft Bluetooth Classic Audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/bluetooth-classic-audio)。

另外两个明确的程序限制：

1. UI 在 helper 启动前已显示 listening；`EnableTraceEx2` 当前使用异步启用（Timeout=0），随后固定等 750ms 播放测试音。配置成功或固定延时都不证明驱动已准备好发送目标事件。来源：[EnableTraceEx2](https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-enabletraceex2)。
2. 当前回调接受系统级 provider 的第一个匹配事件，没有将 `BTDeviceAddress` / `A2dpIsSink` 与默认输出对应的远端匹配。多设备场景存在结果归属风险；这不能解释 No event，但影响正确性。默认输出显示名是独立 Core Audio 查询结果，不是从该事件验证出来的设备身份。

当前日志能帮助分层，但不足以确定所有根因：`events=0` 表示采集窗口内两个已知 GUID 没有交付事件；`events>0` 且 `streaming-events=0` 表示未成功走到目标事件解析；`metadata-unavailable>0` 表示部分事件无法取得元数据；`streaming-events>0` 且 `detected=false` 则应优先看字段读取警告。`providers-enabled=2` 只表示启用 API 成功，不证明两个 provider 均已注册、均在发事件，或耳机走的是该驱动路径。

要确认受影响电脑根因，需要在耳机连接稳定、无麦克风占用时记录完整事件时序，对比“原本无播放，仅测试音”和“持续播放期间检测，再停止所有播放”的事件；同时记录每个 GUID、事件名、解码错误及设备地址匹配。诊断记录应跨越状态变化，避免被 UI 当前的检测取消逻辑截断。第一轮的编译与自检没有覆盖这项真实驱动集成验证。
