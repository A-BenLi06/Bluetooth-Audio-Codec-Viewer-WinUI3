[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-native-common.ps1')
$taskMsbuild = Get-NativeMSBuild
& $taskMsbuild (Join-Path $PSScriptRoot 'tests/BluetoothDeviceProfilesTests.vcxproj') /p:Configuration=Release /p:Platform=x64 /v:quiet /nologo
if ($LASTEXITCODE -ne 0) { throw 'Device profile collector test build failed.' }
& (Join-Path $PSScriptRoot 'artifacts/device-profiles/probe/BluetoothDeviceProfilesTests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Device profile collector tests failed.' }
