# Microsoft Store 商店資訊 — zh-TW

## 產品名稱

Bluetooth Audio Codec

## 簡短描述

查看 Windows 與藍牙耳機協商使用的 Bluetooth Classic A2DP 編解碼器，包括
SBC、AAC、aptX 系列、LDAC 及部分廠商編解碼器。

## 描述

Bluetooth Audio Codec 是一款簡潔的 Windows 工具，用來查看 Windows 與藍牙耳機
或喇叭目前協商使用的 Bluetooth Classic A2DP 編解碼器。

從現代化桌面介面開始偵測後，軟體會監聽 Windows 提供的藍牙 A2DP 串流事件。偵測
到事件時，會顯示編解碼器名稱、輸出裝置、標準編解碼器 ID、廠商 ID、廠商編解碼器
ID 與偵測時間。

軟體可辨識 SBC、AAC、aptX Classic、aptX HD、aptX Low Latency、LDAC、Samsung
Scalable Codec、LHDC 等已知編解碼器。遇到未知值時仍會顯示其數字識別碼，方便
疑難排解。

所有偵測都在本機完成。軟體不會錄製音訊、修改藍牙設定、安裝驅動程式或服務，也
不會傳送裝置資訊。由於 Windows 將藍牙編解碼器追蹤限制為系統管理員程序，只有在
使用者主動開始偵測時才會要求系統管理員核准。偵測時會播放一段短暫且安靜的提示音，
協助 Windows 建立新的 A2DP 串流。

本工具只偵測 Bluetooth Classic A2DP 播放。免持通話（HFP）和 Bluetooth LE Audio
使用不同的編解碼器路徑，不在偵測範圍內。

可選的 Ko-fi 和愛發電連結會在預設瀏覽器中開啟。支持開發完全自願，不會解鎖應用
程式功能或數位內容。

## 產品功能

1. 偵測目前使用的 Bluetooth Classic A2DP 編解碼器
2. 辨識 SBC、AAC、aptX 系列、LDAC 及部分廠商編解碼器
3. 顯示編解碼器 ID、廠商 ID、輸出裝置和偵測時間
4. 完全在本機執行，不會修改藍牙設定
5. 只在開始偵測時要求系統管理員核准
6. 簡潔現代的 Windows 介面

## 其他系統需求

1. Windows 10 版本 2004（組建 19041）或更新版本
2. x64 或 ARM64 處理器
3. 使用 Bluetooth Classic A2DP 的藍牙音訊裝置
4. 開始編解碼器偵測時需要系統管理員核准

## 搜尋字詞

1. 藍牙編解碼器
2. A2DP
3. aptX
4. LDAC
5. SBC
6. 藍牙耳機
7. 音訊診斷

## 螢幕擷取畫面說明

1. 查看 Windows 目前協商使用的編解碼器和輸出裝置。
2. 從簡潔的主畫面開始一次新的本機 A2DP 偵測。

## 此版本的新增功能

第一次提交請留空。

## 適用授權條款

MIT 授權條款。Copyright 2026 BenLi06。完整條款請參閱專案的 LICENSE 檔案。

## 版權和商標資訊

Copyright 2026 BenLi06。Bluetooth 商標歸 Bluetooth SIG, Inc. 所有；其他產品
名稱和商標歸各自權利人所有。

## 開發者

BenLi06
