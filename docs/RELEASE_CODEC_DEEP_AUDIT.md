# 已发布 C# 版编码检测深度审计

> 后续 C++ 实现已按本审计继续修复并加入可导出日志，见 [原生版修复记录](NATIVE_CODEC_FIXES.md)。下文保留各阶段调查当时的行为及限制；其中关于当前候选版取消检测等描述已由后续实现更新。


日期：2026-10-22。审计基线：本地 release `v1.0.3`，提交 `a217cf1e8b841a66f8c7222ec6734afb8d7c1cb2`。用户确认截图来自上一 release；不将未发布原生版的 TDH 调用、日志或重连取消规则套用到截图。

## 结论与证据边界

最强的待验证原因是：程序用一次 600ms 的应用层测试音，试图触发底层 A2DP 串流状态变化，但只消费带编码的串流汇总事件。持续播放、仍有音频客户端维持串流、HFP 或测试音播放失败，都可能使这次触发失效。新找到的本机驱动调用点支持这一机制，不再只依赖事件名字和字段猜测。

另外，实测发现 TraceEvent 3.2.6 在目标回调前存在静默解码/过滤路径；旧程序只记录回调内异常，所以默认 `No event` 文案不能区分“真的没事件”和“事件在回调之前被丢弃”。

未对受影响电脑进行真实 ETW 采集，不能给这些原因分配统计概率，也不能唯一确定截图的根因。本轮没有改生产检测逻辑；新增的代码均在忽略的 `artifacts/codec-deep-audit/` 中用于离线实验。

## 1. 驱动发送时机：静态调用点证据

读取本机 `C:\Windows\System32\drivers\btha2dp.sys`，版本 `10.0.26100.9711`，SHA-256 `C7AC7B6F8E3CA1AC82BB0A80C20292434F01FB6F80609AEFBD1163767780A8CB`。

从 PE 的 RSDS 信息提取 PDB 标识 `A23C6825-50FE-C89F-B547-1A03AAFFC005`、Age 1，并从微软符号服务器按该标识取得 `btha2dp.pdb`。PDB 自身的 Sig70 和 Age 与 PE 对应。PDB SHA-256 为 `5E1F42CA2C657C56221C5A1C5ED5D894997C14BD81E3BF0523FABAB682FCC947`。

使用现有 SDK 的 `dumpbin /disasm:nobytes` 和 `dbh` 做离线静态检查；没有加载、调试或执行驱动。不是 REA 原生引擎输出，未安装分析引擎或修改 REA 配置。

| 函数 | RVA | 观察到的指令和调用关系 |
| --- | --- | --- |
| `A2DP_Device::LogStreamingInformation` | 0x2D564 | 调用 `A2dpRole::GetCurrentSelectedCodec`；若为空直接跳过；读时钟并减去先前存储的时间；引用 A2dpStreaming 元数据并写出字段 |
| `A2DP_Device::LogStreamingStart` | 0x2D6B0 | 引用 A2dpStreamingStart 元数据；没有编码字段 |
| `A2DP_Device::EnterNewStreamingState` | 0x2C350 | 进入状态 1 时 SetStreaming(true)、记录时间并调用 LogStreamingStart；状态 4 和 9 分支调用 LogStreamingInformation |
| `A2DP_Device::SetStreamingState` | 0x31EE0 | 状态不变时跳过 EnterNewStreamingState；从状态 1 离开时 SetStreaming(false) |
| `A2DP_Device::OnAudioDrained` | 0x2DA94 | 一条状态分支可调用 SetStreamingState 进入状态 4，进而发汇总事件 |
| `A2DP_Device::StreamingStateWaitForDrainWorker` | 0x32EA0 | 有进入状态 4 的调用路径 |

这证明本机已找到的发送路径依赖驱动状态转换，不是“每收到一个新应用音频流就报告编码”的查询接口。持续保持状态 1 时，找到的 SetStreamingState 路径不会因为一次同状态调用再次发出汇总事件。没有完整状态类型信息，不能擅自给所有枚举值命名；也没有证明不同 Windows/驱动版本的调用点相同。

最值得实机复现的时间线：监听前已有蓝牙音乐 → 测试音混入既有音频 → 测试音结束但底层串流仍持续 → 30 秒内没有进入能发 A2dpStreaming 的状态 → 正常返回空结果。停止所有播放和关闭维持输出的音频客户端后，若随后出现汇总事件，即可验证这个分支。

`GetCurrentSelectedCodec` 为空时不发送汇总也是观察到的条件分支，但尚无证据表明受影响电脑进入了它，不能将它直接归因为某种耳机、offload 或第三方驱动。

取证文件：`pdb-identity.json`、`pdb-info.txt`、`streaming-symbols.txt`、`driver-callsite-evidence.json`。PDB 单独加载时 DBH 的 PdbUnmatched 标记不用于声称模块已匹配；匹配依据是显式核对 PE RSDS 与 PDB Sig70/Age。符号只含公开符号，没有源代码、行号或完整类型。

## 2. TraceEvent 3.2.6：实际库离线分发实验

库：NuGet `Microsoft.Diagnostics.Tracing.TraceEvent` 3.2.6，DLL SHA-256 `530946DC20E89754783F0AC76D86B7A4EEDF95326F255DB34C32FCBF5C3CE0FF`。

读取微软 `microsoft/perfview` 的 `v3.2.6` 源码并保存文件 blob SHA。重点代码：

- `TraceEvent.cs`：`AddCallbackForProviderEvent` 对 provider 和 event name 做区分大小写的精确匹配，事件名可包含 `/OpcodeName`。
- `RegisteredTraceEventParser.cs`：TDH 返回失败时不生成模板；ExternalTraceEventParser 会缓存已失败事件身份的空模板。
- `TraceEventSession.cs`：默认启用超时 0 为异步；实时会话 FlushTimer 为 1 秒；Dispose 可用于从另一线程停止 Process。
- `ETWTraceEventSource.cs`：Dispose 将 stopProcessing 置为 true，RawDispatch 在此标记为 true 时不再交付事件。

来源：[TraceEvent.cs](https://github.com/microsoft/perfview/blob/v3.2.6/src/TraceEvent/TraceEvent.cs)、[RegisteredTraceEventParser.cs](https://github.com/microsoft/perfview/blob/v3.2.6/src/TraceEvent/RegisteredTraceEventParser.cs)、[TraceEventSession.cs](https://github.com/microsoft/perfview/blob/v3.2.6/src/TraceEvent/TraceEventSession.cs)、[ETWTraceEventSource.cs](https://github.com/microsoft/perfview/blob/v3.2.6/src/TraceEvent/ETWTraceEventSource.cs)。

实验使用实际 3.2.6 DLL 的分发器和 Dynamic parser，将从本机驱动提取的真实元数据放进内存中的合成 EVENT_RECORD，填入人为构造的 LDAC 数值 `255 / 301 / 170`。没有创建 ETW 会话、注册仿冒 provider、播放声音或操作蓝牙。通过反射和仅用于测试的动态派生分发器注入记录，不是正常生产入口的端到端测试。

| 合成输入 | 旧版同样的精确回调次数 | 实验结论 |
| --- | --- | --- |
| 完整本机 provider traits + A2dpStreaming schema，opcode 0 | 1 | 名称正确，三个编码值正确；本机 schema 与旧版基础解码路径兼容 |
| 去掉事件 schema | 0 | 回调前可以静默失败 |
| 去掉 provider traits | 0 | 在本离线环境中无法解码并触发目标回调 |
| 保留 schema，但将 opcode 人为改为 Stop | 0 | Dynamic.All 能看到 A2dpStreaming/Stop，精确名称过滤拒绝 |
| A2dpStreamingStart 的真实 schema | 0 | 能解码，但既不是旧版目标事件，也没有编码字段 |
| 首条去掉 traits，下一条完整且同 schema / 固定事件身份 | 0 | 复现空模板缓存阻止本分发器再次尝试 |
| 相同失败后，完整记录使用另一事件身份 | 1 | 对照实验验证解码本身可恢复，问题与失败身份缓存有关 |

**不能把故障注入实验当成用户机器发生过的事。**本机真实 A2dpStreaming opcode 是 0；没有证据显示受影响电脑改成了 Stop。真实 TraceLogging 流会有事件身份修正；合成记录直接给定固定 ID。因此缓存实验只证明库内失败身份缓存机制，尚未证明某次真实驱动事件出现过元数据缺失或触发相同缓存路径。

不过，上述实验足以推翻“warnings 为空就说明底层没有收到事件”的推论。旧版只在自己的精确回调内部 enqueue 警告，库内解析或订阅过滤不进入这段 try/catch。

复现：`dotnet run ./artifacts/codec-deep-audit/offline-traceevent-probe.cs`。结果：`offline-traceevent-results.json`。

## 3. 已发布应用本身的静默路径

### 测试音失败

`DiagnosticTone.Play` 忽略 `PlaySoundW` 的 bool 返回值。因此音频 API 返回失败时，检测仍继续等到 30 秒结束，返回 Observation=null、Warnings 为空，显示截图里的默认 No event。这是明确的代码路径，是否在受影响电脑发生需记录返回值验证。

测试音也没有显式绑定到 `AudioEndpoint.TryGetDefaultRenderEndpoint` 刚查到的 IMMDevice；界面读取 eMultimedia 默认端点的名字，不能作为测试音实际流向该端点的证明。应用当前未记录实际测试音端点身份、音频会话或串流活动情况。

### HFP

Windows 11 合并 Classic A2DP/HFP 输出端点。耳机麦克风被打开，或播放流分类为 Communications 时可能选择 HFP；显示 WH-1000XM3 和 connected 无法区分。仅订阅 A2DP 事件的程序此时可能等不到目标事件。这是适用于该耳机的待测分支。LE Audio/LC3 不是解释该 WH-1000XM3 截图的优先方向。

来源：[Microsoft Bluetooth Classic Audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/bluetooth-classic-audio)、[Sony WH-1000XM3 specifications](https://www.sony.com/electronics/support/wireless-headphones-bluetooth-headphones/wh-1000xm3/specifications)。

### 窗口和结束边界

旧版异步 EnableProvider 后固定等 750ms，没有驱动就绪确认。慢启动存在错过一次性事件的可能。30 秒后直接 Dispose 会话及 source，晚到或结束附近待处理事件存在被丢弃的窗口。实时 FlushTimer 是 1 秒，因此“默认缓冲 60 秒导致 30 秒永远收不到”不符合这条实时配置；边界竞态也不足以单独解释反复在早期就产生的事件全部丢失。

## 4. 降低优先级或可排除的解释

| 怀疑 | 本轮判断 |
| --- | --- |
| Sony 编码名称缺失 | 五种已知编码有映射，未知 ID 也会显示 Unknown；不应直接变成默认 No event |
| GUID / provider 名称必定配错 | 本机元数据和实际 3.2.6 分发均证明旧值可以匹配；不能作为已确认根因 |
| MatchAnyKeyword=0 禁用了全部事件 | 对现代 TraceLogging provider，0 表示启用全部关键词；不能按普通位掩码直觉认定是禁用 |
| Verbose 级别太低 | 本机目标事件 Level=5，旧版使用 Verbose=5，匹配 |
| 固定备用索引在本机错位 | 本机 codec 字段确为 3、4、5；备用位置仍不稳健，但没有此次根因证据 |
| 管道 / UAC / DLL 缺失等显式异常 | 这些通常走错误或取消显示，截图走默认 No event，优先级降低 |
| Release 裁剪移除了 TraceEvent 功能 | release 项目 PublishTrimmed=false，没看到项目启用 NativeAOT；本轮探针的初始 AOT 配置问题已修正，不属于旧 release 的证据 |
| 原生版重连取消 | 不属于旧 release，不能用于解释截图 |

另外复核了 WAV 构造和生命周期：44.1kHz、单声道、16-bit PCM 的 RIFF/data 长度、平均字节率和 block align 相符；没有设置 SND_ASYNC，PlaySound 默认同步执行，GCHandle 在调用返回后才释放，因此未发现“缺少显式 SND_SYNC 导致提前释放声音缓冲”的问题。测试音 worker 与计时器同处一条任务：若 PlaySound 本身长时间阻塞，helper 可能触发外层响应超时并显示错误，而不是稳定走截图中的普通 No event。回调先原子写入 observation 再 StopProcessing，因此一旦该写入已发生，正常返回路径应优先显示结果；没有找到把已保存的 observation 主动清空的旧版代码。

MatchAnyKeyword 语义来源：[EnableTraceEx2](https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-enabletraceex2)。

## 5. 优先复测和修复顺序

1. 在受影响电脑记录采集层的原始交付量、两个 GUID、事件名/Opcode、TDH 状态码、Dynamic 分发量和精确回调量。不能只加回调内部日志，也不能把 Dynamic.All 当成原始交付总量。
2. 确认耳机麦克风无占用，比较“无其他播放，仅测试音”和“持续播放 → 检测 → 停止所有播放”。诊断采集应独立于 UI 的状态失效规则，跨越串流状态切换。
3. 记录 PlaySound 返回值和实际音频端点；将触发音绑定目标端点。成功播放仍不能当成重新协商的证明。
4. 核对受影响机 Windows build、btha2dp.sys 版本/哈希、适配器驱动、应用实际版本；本机驱动证据不能替代其配置。
5. 如需扩展事件来源，验证成功配置事件的结果、活动状态及设备地址后再使用；A2dpAvailableRemoteCodec 只是能力，不可直接当当前编码。

当前双 provider 补丁和固定字段防护没有改变串流触发条件，不能作为上述最强分支已解决的证明。还存在系统级第一个匹配事件未按 BTDeviceAddress / A2dpIsSink 归属到默认输出的问题；它主要造成错认，不解释空结果。
