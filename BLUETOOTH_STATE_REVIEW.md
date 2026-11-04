# 蓝牙状态残留检查

检查日期：2026-10-21。对象：当前 C++ / WinUI 3 源码及 x64 测试程序。

## 实机复现

在不扫描、不连接设备、不切换蓝牙设置的条件下，独立原生诊断程序同时读取到：

| 数据源 | 结果 |
| --- | --- |
| `Windows.Devices.Radios.Radio` | Bluetooth 的 `State` 为 `Off`（枚举值 2） |
| 已配对 Classic Bluetooth AEP | `WH-CH710N` 的 `System.Devices.Aep.IsConnected` 仍为 `true`，Major 为 AudioVideo |
| `BluetoothFindFirstRadio` | `ERROR_NO_MORE_ITEMS`，没有当前可枚举的本地蓝牙无线电 |
| 当前 PnP AudioEndpoint | 只有本机扬声器、麦克风，无当前在线的蓝牙音频端点 |

这证明本次误报可以来自 Windows 配对记录中的残留属性；关闭、重开应用并不能可靠地消除它。
应用没有把蓝牙连接列表写入磁盘并在启动时恢复，也没有硬编码这个耳机名称。需要纠正状态的来源和失效规则，而不是删除配对信息。

## 原来的失效路径

1. 应用把 AEP 的 `IsConnected` 直接保存在设备表中；增量更新没有该字段时一直沿用旧值。
2. 监视器停止只设置 `unavailable`，但发布状态时优先判断缓存的 connected，旧耳机仍可显示为连接。
3. 音频 Added / Updated / Removed 只重新发布原设备表，没有核实蓝牙连接；没有定期校准，漏掉事件后可以一直残留。
4. 多个事件线程生成的通知缺少版本约束，界面不能拒绝晚到的旧状态。
5. 连接或输出变化没有使旧编码结果失效。检测完成会写回检测开始时的输出名称；取消、失败时恢复界面也包含旧输出名称。

## 修复后的规则

- 使用 Windows 自带 `bthprops.cpl` / `Bthprops.lib`，遍历当前无线电及其 connected 设备，以设备地址和 AudioVideo 类别建立完整快照。与原界面一样，这里指 Classic Bluetooth 音频设备连接，并不表示它一定是默认输出或正在传送 A2DP 音频。
- `fReturnConnected = TRUE`，`fIssueInquiry = FALSE`；不读取 remembered-only 记录作为连接证据，不扫描周边设备，不建立连接。
- 没有无线电或没有连接设备时发布 Disconnected，并清空名称与地址。任一无线电或设备枚举失败时丢弃整个部分结果，发布 Unavailable；不继续显示旧的“已连接”。
- AEP / 音频事件只请求重新查询。单个后台工作线程串行执行查询，每两秒也会复核，覆盖漏事件、无线电变化和休眠恢复。查询异常同样发布 Unavailable。
- 状态携带递增 revision；界面忽略旧通知。断开、重新连接、设备地址变化或默认音频端点 ID 变化会更新 generation，使旧检测结果和可恢复界面失效。
- 输出设备始终从当前 Core Audio 端点读取；同名输出通过端点 ID 区分。检测结果中的历史输出名称不再覆盖当前输出。
- 窗口关闭时撤销事件、唤醒并等待工作线程退出；关闭后的界面回调不再应用状态。

这没有增加第三方运行时依赖，也没有删除 Windows 中的配对记录。

## 验证

`test-bluetooth-state.ps1` 编译并运行独立 C++ 回归测试，以可注入的 API 模拟以下情况：

- 无无线电、连接后断开、忽略 remembered/disconnected 或非音频设备；
- 多个无线电、同一地址重复出现、不同设备同名、名称缺失；
- 无线电及设备枚举开始或中途失败，丢弃部分结果，所有句柄正确关闭；
- 晚到的连接通知、断开后重连、同名设备地址替换和默认输出 ID 改变；
- 设备仍连接但默认输出为扬声器时，保留连接状态；没有实际变化的定期刷新不会让有效检测失效。

`test-native.ps1` 的自检还实际启动 watcher，验证无事件时的周期刷新、revision 顺序、重复 Stop 和工作线程结束，再运行原有服务、参数、命名管道与权限检查。

本机 x64 修复窗口已确认显示 `Bluetooth audio disconnected`，输出为 `Speakers (Realtek(R) Audio)`，编码、技术 ID 和观察时间均为 `—`。Windows AEP 同时仍返回耳机旧连接属性，因此该实机用例覆盖了这次误报。

x64、ARM64 Release 原生编译和链接均通过，编译日志无警告或错误。x64 独立回归及完整原生服务测试通过。

复现命令：

```powershell
.\test-bluetooth-state.ps1
.\test-native.ps1 -ExecutablePath artifacts\bluetooth-state-fix\app-x64\BluetoothAudioCodec.WinUI.exe
```

诊断快照、编译日志及测试结果放在忽略的 `artifacts/bluetooth-state-investigation/` 和 `artifacts/bluetooth-state-fix/`。本次测试程序是完整离线目录；只复制 EXE 无法运行。之前迁移任务生成的 MSI / MSIX 不包含本次修复。

尚未进行真实耳机开关、多个物理适配器、休眠恢复、检测期间断开，以及 ARM64 设备运行测试；这些场景目前由模拟回归或编译检查覆盖。未操作用户蓝牙设置，也没有触发 UAC 或播放测试音。

API 参考：[BluetoothFindFirstRadio](https://learn.microsoft.com/en-us/windows/win32/api/bluetoothapis/nf-bluetoothapis-bluetoothfindfirstradio)、[BLUETOOTH_DEVICE_SEARCH_PARAMS](https://learn.microsoft.com/en-us/windows/win32/api/bluetoothapis/ns-bluetoothapis-bluetooth_device_search_params)、[BLUETOOTH_DEVICE_INFO](https://learn.microsoft.com/en-us/windows/win32/api/bluetoothapis/ns-bluetoothapis-bluetooth_device_info_struct)。
