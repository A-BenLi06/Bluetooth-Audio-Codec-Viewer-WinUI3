[CmdletBinding()]
param([string]$ExecutablePath = (Join-Path $PSScriptRoot 'BluetoothAudioCodec.Cli/bin/x64/Debug/BluetoothAudioCodec.Cli.exe'))
$ErrorActionPreference = 'Stop'
$taskCli = (Resolve-Path -LiteralPath $ExecutablePath).Path
function Assert-CliExit([string[]]$CliArguments, [int]$Expected) {
    & $taskCli @CliArguments
    if ($LASTEXITCODE -ne $Expected) { throw "CLI $CliArguments returned $LASTEXITCODE; expected $Expected." }
}
Assert-CliExit @('--help') 0
Assert-CliExit @('--self-test') 0
foreach ($value in @('0', '61', '-1', 'NaN', '1x')) {
    Assert-CliExit @('--timeout', $value) 2
}
Assert-CliExit @('--timeout') 2
Assert-CliExit @('--unknown') 2
Assert-CliExit @('--elevated-helper') 2
Assert-CliExit @('--export-report') 2
Assert-CliExit @('--export-report', 'unused.zip', '--unknown') 2
$taskReport = Join-Path $PSScriptRoot 'artifacts/cli-tests/diagnostics 测试.txt'
[IO.Directory]::CreateDirectory((Split-Path $taskReport)) | Out-Null
Assert-CliExit @('--export-diagnostics', $taskReport) 0
if (-not ([IO.File]::ReadAllText($taskReport)).StartsWith('Bluetooth Audio Codec diagnostic report')) {
    throw 'CLI diagnostic export is invalid.'
}
Write-Host 'CLI shared-core, argument and UTF-8 export checks passed. No UAC or hardware capture requested.'
