[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-native-common.ps1')
$msbuild = Get-NativeMSBuild
& $msbuild (Join-Path $PSScriptRoot 'tests/BluetoothConnectionTests.vcxproj') /p:Configuration=Release /p:Platform=x64 /v:quiet /nologo
if ($LASTEXITCODE -ne 0) { throw 'Bluetooth regression test build failed.' }
& (Join-Path $PSScriptRoot 'artifacts/bluetooth-state-tests/x64/BluetoothConnectionTests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Bluetooth regression tests failed.' }
