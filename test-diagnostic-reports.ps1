[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ExecutablePath,
    [string]$Python = 'python',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'artifacts\diagnostics-integration\generated-reports')
)
$ErrorActionPreference = 'Stop'
$executable = (Resolve-Path -LiteralPath $ExecutablePath).Path
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
[IO.Directory]::CreateDirectory($output) | Out-Null
$process = Start-Process -FilePath $executable -ArgumentList @('--report-self-test', ('"' + $output + '"')) -WindowStyle Hidden -PassThru
try {
    if (-not $process.WaitForExit(30000)) { throw 'Native report tests timed out.' }
    if ($process.ExitCode -ne 0) { throw "Native report tests failed: $($process.ExitCode)" }
}
finally { if (-not $process.HasExited) { Stop-Process -Id $process.Id }; $process.Dispose() }
& $Python (Join-Path $PSScriptRoot 'diagnostics\tools\test_report_contract.py') $output
if ($LASTEXITCODE -ne 0) { throw 'Native reports did not pass receiver validation. Install diagnostics/requirements.txt if jsonschema is missing.' }
& $Python (Join-Path $PSScriptRoot 'diagnostics\tools\test_intake_rejections.py') (Join-Path $output 'intake-results.json')
if ($LASTEXITCODE -ne 0) { throw 'Malformed diagnostic bundle rejection tests failed.' }
