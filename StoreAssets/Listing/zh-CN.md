# Microsoft Store 商品页 — zh-CN

## 产品名称

Bluetooth Audio Codec

## 简短说明

查看 Windows 与蓝牙耳机协商使用的 Bluetooth Classic A2DP 编解码器，包括
SBC、AAC、aptX 系列、LDAC 及部分厂商编解码器。

## 说明

Bluetooth Audio Codec 是一款简洁的 Windows 工具，用于查看 Windows 与蓝牙耳机
或音箱当前协商使用的 Bluetooth Classic A2DP 编解码器。

从现代化桌面界面启动检测后，软件会监听 Windows 提供的蓝牙 A2DP 串流事件。检测
到事件时，会显示编解码器名称、输出设备、标准编解码器 ID、厂商 ID、厂商编解码器
ID 和检测时间。

软件可识别 SBC、AAC、aptX Classic、aptX HD、aptX Low Latency、LDAC、Samsung
Scalable Codec、LHDC 等已知编解码器。遇到未知值时仍会显示其数字标识，便于排查。

所有检测均在本机完成。软件不会录制音频、修改蓝牙设置、安装驱动或服务，也不会
上传设备信息。由于 Windows 将蓝牙编解码器跟踪限制为管理员进程，只有在用户主动
开始检测时才会请求管理员批准。检测时会播放一段短促安静的提示音，帮助 Windows
建立新的 A2DP 串流。

本工具仅检测 Bluetooth Classic A2DP 播放。免提通话（HFP）和 Bluetooth LE Audio
使用不同的编解码器路径，不在检测范围内。

可选的 Ko-fi 和爱发电链接会在默认浏览器中打开。支持开发完全自愿，不会解锁应用
功能或数字内容。

## 产品功能

1. 检测当前使用的 Bluetooth Classic A2DP 编解码器
2. 识别 SBC、AAC、aptX 系列、LDAC 及部分厂商编解码器
3. 显示编解码器 ID、厂商 ID、输出设备和检测时间
4. 完全在本机运行，不会修改蓝牙设置
5. 仅在开始检测时请求管理员批准
6. 简洁现代的 Windows 界面

## 其他系统要求

1. Windows 10 版本 2004（内部版本 19041）或更高版本
2. x64 或 ARM64 处理器
3. 使用 Bluetooth Classic A2DP 的蓝牙音频设备
4. 开始编解码器检测时需要管理员批准

## 搜索词

1. 蓝牙编解码器
2. A2DP
3. aptX
4. LDAC
5. SBC
6. 蓝牙耳机
7. 音频诊断

## 截图说明

1. 查看 Windows 当前协商使用的编解码器和输出设备。
2. 从简洁的主界面启动一次新的本地 A2DP 检测。

## 此版本中的新增功能

首次提交请留空。

## 适用许可条款

MIT 许可证。Copyright 2026 BenLi06。完整条款请参阅项目的 LICENSE 文件。

## 版权和商标信息

Copyright 2026 BenLi06。Bluetooth 商标归 Bluetooth SIG, Inc. 所有；其他产品
名称和商标归各自权利人所有。

## 开发者

BenLi06
