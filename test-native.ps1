[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$ExecutablePath,
    [string]$ResultPath,
    [switch]$Packaged
)
$ErrorActionPreference = 'Stop'
$executable = (Resolve-Path -LiteralPath $ExecutablePath).Path
. (Join-Path $PSScriptRoot 'build-native-common.ps1')
Assert-NativePayload -Directory (Split-Path $executable) -Packaged:$Packaged

function Invoke-NativeCheck([string[]]$Arguments, [int]$Expected) {
    $process = Start-Process -FilePath $executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    try {
        if (-not $process.WaitForExit(20000)) { throw "Native check timed out: $($Arguments[0])" }
        if ($process.ExitCode -ne $Expected) { throw "Native check $($Arguments[0]) returned $($process.ExitCode), expected $Expected." }
    }
    finally { if (-not $process.HasExited) { Stop-Process -Id $process.Id }; $process.Dispose() }
}

Invoke-NativeCheck @('--self-test') 0
Invoke-NativeCheck @('--export-diagnostics') 2
$reportDirectory = Join-Path ([IO.Path]::GetTempPath()) ('BluetoothAudioCodec-tests-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($reportDirectory) | Out-Null
try {
    $reportPath = Join-Path $reportDirectory 'diagnostics report 测试.txt'
    Invoke-NativeCheck @('--export-diagnostics', ('"' + $reportPath + '"')) 0
    $report = [IO.File]::ReadAllText($reportPath, [Text.Encoding]::UTF8)
    foreach ($expected in @('Bluetooth Audio Codec diagnostic report', 'engine=native-cpp', 'app-version=', 'windows=', 'btha2dp.sys=', 'runtime.log')) {
        if (-not $report.Contains($expected)) { throw "Diagnostic export missing $expected." }
    }
    if ($report -match '(?i)[A-Z]:[\\/]+Users[\\/]+(?!\[user\])[^\\/\r\n"]+' -or
        $report -match '(?i)\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b' -or
        $report -match '(?i)\b[0-9a-f]{32,}\b' -or $report.Length -gt 6000000) {
        throw 'Diagnostic export privacy or size check failed.'
    }
    [IO.File]::WriteAllText($reportPath, 'existing report')
    Invoke-NativeCheck @('--export-diagnostics', ('"' + $reportPath + '"')) 0
    if (-not ([IO.File]::ReadAllText($reportPath)).StartsWith('Bluetooth Audio Codec diagnostic report')) {
        throw 'Replacing an existing diagnostic report failed.'
    }
    if (@(Get-ChildItem -LiteralPath $reportDirectory -Filter '*.tmp').Count) { throw 'Diagnostic export left temporary files.' }
    $activeLog = Join-Path $env:LOCALAPPDATA 'BluetoothAudioCodecViewer/Logs/runtime.log'
    Invoke-NativeCheck @('--export-diagnostics', ('"' + $activeLog + '"')) 10
    if (-not (Test-Path -LiteralPath $activeLog)) { throw 'Active log was removed.' }
    Invoke-NativeCheck @('--export-diagnostics', ('"' + (Join-Path $reportDirectory 'absent/report.txt') + '"')) 10
}
finally {
    $resolvedTestDirectory = [IO.Path]::GetFullPath($reportDirectory)
    $expectedTestRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (-not $resolvedTestDirectory.StartsWith($expectedTestRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to clean a test directory outside the temporary directory.'
    }
    Remove-Item -LiteralPath $resolvedTestDirectory -Recurse -Force
}
Invoke-NativeCheck @('--elevated-helper') 2
$token = [Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
foreach ($seconds in @('0', '61', 'NaN', '1x')) {
    Invoke-NativeCheck @('--elevated-helper', '--pipe', 'test', '--token', $token, '--timeout-seconds', $seconds) 2
}
Invoke-NativeCheck @('--elevated-helper', '--pipe', '../invalid', '--token', $token, '--timeout-seconds', '1') 2
Invoke-NativeCheck @('--elevated-helper', '--pipe', 'test', '--token', 'invalid', '--timeout-seconds', '1') 2
Invoke-NativeCheck @('--elevated-helper', '--pipe', ('absent-' + [Guid]::NewGuid().ToString('N')), '--token', $token, '--timeout-seconds', '1') 3

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$permissionCheck = 'skipped: test process is elevated'
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $name = 'BluetoothAudioCodec-native-test-' + [Guid]::NewGuid().ToString('N')
    $pipe = [IO.Pipes.NamedPipeServerStream]::new($name, [IO.Pipes.PipeDirection]::InOut, 1,
        [IO.Pipes.PipeTransmissionMode]::Byte,
        ([IO.Pipes.PipeOptions]::Asynchronous -bor [IO.Pipes.PipeOptions]::CurrentUserOnly))
    $reader = $null
    $helper = $null
    try {
        $connection = $pipe.WaitForConnectionAsync()
        $helper = Start-Process -FilePath $executable -ArgumentList @('--elevated-helper', '--pipe', $name,
            '--token', $token, '--timeout-seconds', '1') -WindowStyle Hidden -PassThru
        if (-not $connection.Wait(15000)) { throw 'Native helper failed to connect.' }
        $reader = [IO.StreamReader]::new($pipe, [Text.Encoding]::UTF8, $true, 1024, $true)
        $response = $reader.ReadLineAsync()
        if (-not $response.Wait(15000)) { throw 'Native helper failed to respond.' }
        $message = $response.Result | ConvertFrom-Json
        if ($message.Token -ne $token -or $message.Error -ne 'The codec helper was not started with administrator access.') {
            throw 'Native helper did not authenticate its permission error.'
        }
        if (-not $helper.WaitForExit(5000) -or $helper.ExitCode -ne 5) { throw 'Native helper did not exit with the expected permission code.' }
        $permissionCheck = 'passed: authenticated response, exit 5'
    }
    finally {
        if ($reader) { $reader.Dispose() }
        $pipe.Dispose()
        if ($helper) { if (-not $helper.HasExited) { Stop-Process -Id $helper.Id }; $helper.Dispose() }
    }
}
$result = [ordered]@{
    executable = $executable
    sha256 = (Get-FileHash -LiteralPath $executable).Hash
    serviceSelfTests = 'passed: real TDH synthetic TraceLogging records, both provider GUIDs, opcode suffix/case, metadata failure recovery, missing/reordered/type-checked fields, target address/source direction filtering, capability rejection, failed/successful configuration plus stream start, codec catalog, IPC Unicode/timestamp, token validation, strict arguments, cancellation, polling/revisions/shutdown, privacy and actual file rotation'
    diagnosticExport = 'passed: UTF-8 path with spaces/Chinese, environment and event log content, privacy/size checks, replacement, temporary cleanup, active-log protection, write error exit'
    invalidHelperArguments = 'passed'
    playbackControl = 'passed: global media control scope, all-owned-session recovery, originally paused exclusion, idempotent recovery, late pause completion, pause/play rejection and exceptions; synthetic controls only'
    pauseRecoveryNotifications = 'passed: own pause transitions and Changing notifications permit recovery; later manual play-pause prevents it; timeline position and range are not recovery guards'
    mediaIdentity = 'passed: known title/artist/album/track changes are detected; missing or backfilled fields are not changes; normalized HMAC is stable within an attempt and different across random attempt keys'
    helperProbeHandshake = 'passed: token and attempt validation, begin-probe gating, duplicate and unknown command rejection, cancel after begin-probe'
    missingPipe = 'passed: exit 3'
    unelevatedHelper = $permissionCheck
    note = 'No UAC requested or Bluetooth settings changed. Hardware ETW capture needs a separate elevated device test.'
}
if ($ResultPath) { $result | ConvertTo-Json | Set-Content -LiteralPath $ResultPath -Encoding utf8 }
$result | ConvertTo-Json
