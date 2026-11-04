# CI/CD

项目使用 GitHub Actions。工作流提交并推送到 GitHub 后生效，不需要配置 Secrets。

## 持续集成

`CI` 在分支推送、PR 和手动运行时触发，复用 `native-build.yml`：

- 使用 `windows-2025`、Visual Studio C++ 工具和 Windows SDK 26100。
- 分别构建 x64、ARM64 Release 的 WinUI 自包含程序、WiX MSI 和原生 CLI。
- 在 x64 上执行连接状态、设备识别、CLI、原生内核及诊断报告契约/拒收测试。
- ARM64 由 x64 runner 交叉编译，不执行 ARM64 二进制；发布前仍需真机验证。
- `packages-x64` / `packages-ARM64` 保存 MSI、CLI ZIP 和包含签名状态、哈希的发布清单。
- `diagnostics-*` 保存 MSBuild binlog、PDB 和测试结果，即使构建失败也尝试上传。保留 14 天。

普通 CI 使用占位版本 `0.0.0`。所有自动构建均不签名。
本地可用相同入口验证（需要 README 所列的 C++/WinUI、.NET SDK 及 Python 环境）：

```powershell
python -m pip install -r diagnostics/requirements.txt
./scripts/ci/build.ps1 -Platform x64 -Version 1.1.0
./scripts/ci/build.ps1 -Platform ARM64 -Version 1.1.0
```

## 发布交付

将工作流和版本代码提交、推送后，创建新的三段版本标签：

```powershell
git tag v1.1.1
git push origin v1.1.1
```

`Release` 校验版本，重新执行同一构建和测试流程，仅在两个架构均成功后创建
GitHub Release **草稿**，附带两个 MSI、两个 CLI ZIP、各架构发布清单和 `SHA256SUMS.txt`。
版本的 major、minor 不超过 255，patch 不超过 65535，不接受前导零或预发布后缀。
示例使用尚未占用的新标签；不要重推或移动已有的 `v1.1.0` 等发布标签。
也可在 Actions 中手动运行 `Release`，但必须选择已有版本标签，选择分支会失败。
已有同名 Release 会使创建步骤失败，不自动覆盖。

草稿产物未签名。发布者需完成签名（如需要）、安装/升级验证及 ARM64 真机验证，
再发布草稿；若替换已签名的产物，须重新计算发布清单中的应用/MSI 哈希和 `SHA256SUMS.txt`。
Store 签名和 Partner Center 提交流程见 [STORE_SUBMISSION.md](STORE_SUBMISSION.md)。
当前自动化交付目标为 GitHub Release 草稿，Store 提交仍按该文档操作。

## 权限和维护

构建和 PR 任务仅有 `contents: read`；只有创建草稿的任务拥有 `contents: write`。
checkout 不保留凭据，不使用 `pull_request_target`。Actions 固定到提交 SHA，Dependabot 每周检查更新。
可在 GitHub 分支保护中将两个架构的 CI 构建任务设为必需检查。
蓝牙硬件 ETW 捕获、UAC 和交互 UI 不属于无人值守测试。

参考：[Windows runner 软件清单](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md)。
