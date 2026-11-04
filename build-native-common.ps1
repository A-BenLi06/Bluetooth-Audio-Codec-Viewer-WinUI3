function Get-NativeMSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer (vswhere.exe) was not found.' }
    $installations = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
    $installation = $installations | Where-Object {
        @(Get-ChildItem -Path (Join-Path $_.installationPath 'MSBuild\Microsoft\VC\*\Application Type\Windows Store\10.0\Platforms\x64\Platform.props') -File -ErrorAction SilentlyContinue).Count -gt 0
    } | Sort-Object { [version]$_.installationVersion } -Descending | Select-Object -First 1 -ExpandProperty installationPath
    if ([string]::IsNullOrWhiteSpace($installation)) { throw 'Install Visual Studio 2022/2026 with Desktop development with C++, WinUI C++ tools, and the ARM64 build tools.' }
    $candidate = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
    if (-not (Test-Path -LiteralPath $candidate)) { throw "MSBuild was not found: $candidate" }
    return $candidate
}

function Assert-NativePayload([string]$Directory, [switch]$Offline, [switch]$Packaged) {
    $requiredFiles = @('BluetoothAudioCodec.WinUI.exe')
    if (-not $Packaged) { $requiredFiles += @('App.xbf', 'MainWindow.xbf') }
    foreach ($required in $requiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $Directory $required))) { throw "Native output is missing $required." }
    }
    if (@(Get-ChildItem -LiteralPath $Directory -Filter '*.pri' -File).Count -eq 0) { throw 'Native output is missing its resource index.' }
    foreach ($forbidden in @('coreclr.dll', 'clrjit.dll', 'System.Private.CoreLib.dll', 'Microsoft.Windows.SDK.NET.dll', 'Microsoft.WinUI.dll', 'Microsoft.Diagnostics.Tracing.TraceEvent.dll')) {
        if (Test-Path -LiteralPath (Join-Path $Directory $forbidden)) { throw "Native output unexpectedly contains $forbidden." }
    }
    if ($Offline -and -not (Test-Path -LiteralPath (Join-Path $Directory 'Microsoft.ui.xaml.dll'))) { throw 'The offline output is missing the WinUI runtime.' }
}
