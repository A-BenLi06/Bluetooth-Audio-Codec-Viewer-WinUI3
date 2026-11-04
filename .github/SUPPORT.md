# Report a problem / 报告问题

Use **Report a problem**, choose the affected detection or application/startup context, optionally include recent runtime logs, review every file and save one ZIP. Then use **Open GitHub** to create an issue. No detection is required for application/startup context. Older TXT-only exports remain a compatibility path.

使用“报告问题”，选择出错的检测记录或应用／启动问题，按需附加近期运行日志，检查并保存一个 ZIP，再点击“打开 GitHub”。应用／启动问题无需先检测。附加日志可能跨多次检测，并包含导出时状态，分享前需人工检查；旧 TXT 仅作兼容。

**GitHub issues and their attachments are public. Selecting a file uploads it immediately, before submitting the issue.** Anyone can access a public attachment without signing in. A GitHub account is needed to submit an issue.

**GitHub 问题及附件是公开的。选择文件时就会立即上传，早于提交问题。** 公开附件无需登录即可访问；提交问题需要 GitHub 账号。

1. Describe what happened and what you expected. Include reproduction steps, the headset product model, and whether playback or the microphone was in use. Avoid personal device nicknames and raw device identifiers.
2. Optionally attach the reviewed `bac-report-...zip` (project limit: 5 MiB). If export failed, submit text without a file.
3. Keep the issue URL after submission. Opening GitHub from the app does not submit the report or confirm that a maintainer received it.

请说明现象、预期、复现步骤、耳机型号，以及当时是否播放声音或使用麦克风。不要填写带姓名的设备昵称或原始设备标识。导出失败也可以只提交文字。

New reports include available Windows product model/manufacturer properties and default-output matching in `snapshots.json` and the summary. These are not user-confirmed; Windows may omit them or report a generic PnP vendor. Confirm or correct the product brand/model in the issue text. Do not infer the active codec from the product model.

新报告的快照和摘要包含可读取的 Windows 产品型号、厂商及默认输出匹配关系。Windows 属性尚未经用户确认，可能缺失或返回通用 PnP 厂商；请在问题正文补充或更正确认过的品牌、型号，不能用产品型号推断正在使用的编解码器。

Do not attach raw ETL, crash/hang dumps, registry exports, unfiltered PnP records or account details to a public issue. The standard ZIP does not contain those files. If a maintainer needs restricted data, first agree on a dedicated private destination and verify who has access; otherwise keep it locally. This project does not offer anonymous private uploads.

原始 ETL、dump、未过滤的设备/注册表记录不能直接上传到公开问题。确需这些资料时，先与维护者确认私有收件位置和访问范围；没有私密通道则保留在本地。当前没有匿名私密上传服务。

Submitting an issue does not grant permission to republish the original attachment as a test dataset. Any reuse of device samples is agreed separately.

Store users use the same issue flow. Ratings and reviews are for product feedback; do not paste logs or device identifiers into reviews. The app does not change Windows diagnostic-data settings. Store installation or purchase issues may need Microsoft support.
