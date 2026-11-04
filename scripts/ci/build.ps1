[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')][string]$Platform = 'x64',
    [ValidatePattern('^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$')][string]$Version = '0.0.0',
    [ValidateSet('unknown', 'github-release')][string]$Distribution = 'unknown'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
Push-Location $root
try {
    $parsed = [version]$Version
    if ($parsed.Major -gt 255 -or $parsed.Minor -gt 255 -or $parsed.Build -gt 65535) {
        throw 'MSI versions must be within 255.255.65535.'
    }
    $logs = Join-Path $root 'artifacts/ci/logs'
    $packages = Join-Path $root 'artifacts/ci/packages'
    $native = Join-Path $root 'artifacts/ci/native'
    New-Item -ItemType Directory -Path $logs, $packages -Force | Out-Null
    . ./build-native-common.ps1
    $msbuild = Get-NativeMSBuild
    Write-Host "MSBuild: $msbuild"

    ./build-store-installer.ps1 -Version $Version -Architecture $Platform.ToLowerInvariant() `
        -Distribution $Distribution -OutputDirectory $native `
        -MSBuildArguments @("/bl:$(Join-Path $logs 'winui.binlog')")
    & $msbuild ./BluetoothAudioCodec.Cli/BluetoothAudioCodec.Cli.vcxproj /restore /nologo /v:minimal `
        /p:Configuration=Release "/p:Platform=$Platform" "/bl:$(Join-Path $logs 'cli.binlog')"
    if ($LASTEXITCODE -ne 0) { throw 'CLI build failed.' }

    $architecture = $Platform.ToLowerInvariant()
    $manifestPath = Join-Path $native "installer/BluetoothAudioCodec-$Version-release.json"
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $payload = Join-Path $native $manifest.packages[0].applicationDirectory
    $cliDirectory = Join-Path $root "BluetoothAudioCodec.Cli/bin/$Platform/Release"
    if ($Platform -eq 'x64') {
        ./test-bluetooth-state.ps1
        ./test-device-profiles.ps1
        ./test-cli.ps1 -ExecutablePath (Join-Path $cliDirectory 'BluetoothAudioCodec.Cli.exe')
        ./test-native.ps1 -ExecutablePath (Join-Path $payload 'BluetoothAudioCodec.WinUI.exe') `
            -ResultPath (Join-Path $logs 'native-tests.json')
        ./test-diagnostic-reports.ps1 -ExecutablePath (Join-Path $payload 'BluetoothAudioCodec.WinUI.exe')
    }
    else {
        Write-Host 'ARM64 is cross-compiled; runtime tests execute in the x64 job.'
    }

    Copy-Item -LiteralPath (Join-Path $native "installer/$architecture/BluetoothAudioCodec-$Version-$architecture.msi") -Destination $packages
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $packages "BluetoothAudioCodec-$Version-$architecture-release.json")
    $cliStage = Join-Path $root "artifacts/ci/cli-$architecture"
    New-Item -ItemType Directory -Path $cliStage -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $cliDirectory 'BluetoothAudioCodec.Cli.exe') -Destination $cliStage
    Copy-Item -LiteralPath ./LICENSE -Destination $cliStage
    Copy-Item -LiteralPath ./BluetoothAudioCodec.Cli/README.md -Destination $cliStage
    Compress-Archive -Path "$cliStage/*" -DestinationPath (Join-Path $packages "BluetoothAudioCodec-Cli-$Version-$architecture.zip") -Force
}
finally {
    Pop-Location
}
