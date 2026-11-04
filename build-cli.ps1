[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('x64', 'ARM64')][string]$Platform = 'x64'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-native-common.ps1')
$taskMsbuild = Get-NativeMSBuild
& $taskMsbuild (Join-Path $PSScriptRoot 'BluetoothAudioCodec.Cli/BluetoothAudioCodec.Cli.vcxproj') /restore "/p:Configuration=$Configuration" "/p:Platform=$Platform" /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'CLI build failed.' }
Write-Host "CLI: $PSScriptRoot/BluetoothAudioCodec.Cli/bin/$Platform/$Configuration/BluetoothAudioCodec.Cli.exe"
