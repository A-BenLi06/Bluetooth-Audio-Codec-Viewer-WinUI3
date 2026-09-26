[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string]$Version = '1.0.0.0',

    [switch]$AllowPlaceholderIdentity
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$project = Join-Path $root 'BluetoothAudioCodec.WinUI\BluetoothAudioCodec.WinUI.csproj'
$manifest = Join-Path $root 'BluetoothAudioCodec.WinUI\Package.appxmanifest'

[xml]$manifestXml = Get-Content -Raw -LiteralPath $manifest
$namespace = New-Object System.Xml.XmlNamespaceManager($manifestXml.NameTable)
$namespace.AddNamespace('f', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
$identity = $manifestXml.SelectSingleNode('/f:Package/f:Identity', $namespace)

if ($null -eq $identity) {
    throw 'Package.appxmanifest does not contain a package Identity element.'
}

$parsedVersion = [Version]$Version
if ($parsedVersion.Revision -ne 0) {
    throw "Microsoft Store package versions must have a zero revision. Use a version such as 1.0.1.0 instead of $Version."
}

if ($identity.Version -ne $Version) {
    throw "Package.appxmanifest specifies version $($identity.Version), but the requested build version is $Version. Update the manifest so they match."
}

if (-not $AllowPlaceholderIdentity -and
    ($identity.Name -eq 'BluetoothAudioCodec.WinUI' -or
     $identity.Publisher -eq 'CN=PlaceholderPublisher')) {
    throw @'
The package still uses placeholder identity values. In Partner Center, open
Product management > Product identity, then copy Package/Identity/Name and
Package/Identity/Publisher into Package.appxmanifest. Use
-AllowPlaceholderIdentity only for a local, non-submittable packaging test.
'@
}

$capabilities = @(
    $manifestXml.Package.Capabilities.Capability |
        ForEach-Object { $_.Name }
)

foreach ($requiredCapability in @('runFullTrust', 'allowElevation')) {
    if ($requiredCapability -notin $capabilities) {
        throw "The package manifest is missing $requiredCapability."
    }
}

$deviceCapabilities = @(
    $manifestXml.Package.Capabilities.DeviceCapability |
        ForEach-Object { $_.Name }
)

if ('bluetooth' -notin $deviceCapabilities) {
    throw 'The package manifest is missing the bluetooth device capability.'
}

$buildStartedAt = [DateTime]::UtcNow

# dotnet MSBuild does not inherit Visual Studio's installation root, so the
# MSIX targets can otherwise look for mspdbcmf.exe under the .NET SDK folder.
# Resolve the converter explicitly to keep Store symbol generation reliable.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found. Install Visual Studio or Build Tools with the C++ build tools workload.'
}

$vsInstallRoot = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ([string]::IsNullOrWhiteSpace($vsInstallRoot)) {
    throw 'No Visual Studio installation with the C++ build tools was found.'
}

$pdbCmfX64 = Get-ChildItem -Path (Join-Path $vsInstallRoot 'VC\Tools\MSVC\*\bin\Hostx64\x64\mspdbcmf.exe') `
        -File -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1
$pdbCmfX86 = Get-ChildItem -Path (Join-Path $vsInstallRoot 'VC\Tools\MSVC\*\bin\Hostx64\x86\mspdbcmf.exe') `
        -File -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1

if ($null -eq $pdbCmfX64 -or $null -eq $pdbCmfX86) {
    throw "mspdbcmf.exe was not found under $vsInstallRoot. Install the C++ build tools and Windows application packaging components."
}

# PackageVersion is not always propagated into an architecture child build
# when that architecture has stale intermediates from the previous release.
foreach ($platform in @('x64', 'ARM64')) {
    dotnet clean $project `
        --configuration Release `
        --verbosity quiet `
        -p:Platform=$platform

    if ($LASTEXITCODE -ne 0) {
        throw "Failed to clean the $platform Release intermediates."
    }
}

dotnet build $project `
    --configuration Release `
    --no-incremental `
    -p:Platform=x64 `
    -p:PackageVersion=$Version `
    -p:AppxPackageVersion=$Version `
    -p:AppxBundle=Always `
    '-p:AppxBundlePlatforms=x64|ARM64' `
    -p:UapAppxPackageBuildMode=StoreUpload `
    -p:AppxPackageSigningEnabled=false `
    -p:AppxSymbolPackageEnabled=true `
    "-p:PdbCmfx64ExeFullPath=$($pdbCmfX64.FullName)" `
    "-p:PdbCmfx86ExeFullPath=$($pdbCmfX86.FullName)"

if ($LASTEXITCODE -ne 0) {
    throw @'
MSIX StoreUpload build failed. If the build output mentions mspdbcmf.exe,
install the Windows SDK Debugging Tools; symbol-less uploads are intentionally
rejected because Partner Center could not resolve their crash stacks.
'@
}

$upload = Get-ChildItem -LiteralPath (Join-Path $root 'BluetoothAudioCodec.WinUI\AppPackages') `
        -Filter '*.msixupload' -File -Recurse |
    Where-Object { $_.LastWriteTimeUtc -ge $buildStartedAt.AddSeconds(-5) } |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1

if ($null -eq $upload) {
    throw 'The build completed but no .msixupload file was found.'
}

Add-Type -AssemblyName System.IO.Compression

function Copy-ZipEntryToMemory([System.IO.Compression.ZipArchiveEntry]$Entry) {
    $memory = [System.IO.MemoryStream]::new()
    $entryStream = $Entry.Open()
    try {
        $entryStream.CopyTo($memory)
        $memory.Position = 0
        return $memory
    }
    finally {
        $entryStream.Dispose()
    }
}

$uploadArchive = [System.IO.Compression.ZipFile]::OpenRead($upload.FullName)
try {
    $bundleEntry = @($uploadArchive.Entries |
        Where-Object { $_.FullName.EndsWith('.msixbundle', [StringComparison]::OrdinalIgnoreCase) })

    if ($bundleEntry.Count -ne 1) {
        throw 'The .msixupload must contain exactly one .msixbundle.'
    }

    $symbolEntries = @($uploadArchive.Entries |
        Where-Object { $_.FullName.EndsWith('.appxsym', [StringComparison]::OrdinalIgnoreCase) })

    if ($symbolEntries.Count -ne 2 -or @($symbolEntries | Where-Object { $_.Length -eq 0 }).Count -ne 0) {
        throw @'
The .msixupload must contain non-empty x64 and ARM64 .appxsym files so Partner
Center can symbolicate crash reports. Install the Windows SDK Debugging Tools
(including mspdbcmf.exe), then rebuild the Store upload.
'@
    }

    $symbolArchitectures = @($symbolEntries | ForEach-Object {
        if ($_.FullName -notmatch '_(x64|arm64)\.appxsym$') {
            throw "Unexpected symbol package name: $($_.FullName)"
        }

        $Matches[1].ToLowerInvariant()
    })

    if (($symbolArchitectures | Sort-Object -Unique) -join ',' -ne 'arm64,x64') {
        throw 'The upload package does not contain symbol packages for both x64 and ARM64.'
    }

    $bundleStream = Copy-ZipEntryToMemory $bundleEntry[0]
    try {
        $bundleArchive = [System.IO.Compression.ZipArchive]::new(
            $bundleStream,
            [System.IO.Compression.ZipArchiveMode]::Read)
        try {
            # Scale-qualified visual assets can be emitted as neutral resource
            # packages. Validate the two executable architecture packages here;
            # resource packages do not contain the app runtime files below.
            $packageEntries = @($bundleArchive.Entries |
                Where-Object {
                    $_.FullName -match '_(x64|arm64)\.msix$'
                })

            if ($packageEntries.Count -ne 2) {
                throw 'The bundle must contain exactly two architecture packages.'
            }

            $validatedArchitectures = foreach ($packageEntry in $packageEntries) {
                $packageStream = Copy-ZipEntryToMemory $packageEntry
                try {
                    $packageArchive = [System.IO.Compression.ZipArchive]::new(
                        $packageStream,
                        [System.IO.Compression.ZipArchiveMode]::Read)
                    try {
                        $appManifestEntry = $packageArchive.GetEntry('AppxManifest.xml')
                        if ($null -eq $appManifestEntry) {
                            throw "$($packageEntry.FullName) has no AppxManifest.xml."
                        }

                        $appManifestStream = $appManifestEntry.Open()
                        try {
                            [xml]$packagedManifest = New-Object System.Xml.XmlDocument
                            $packagedManifest.Load($appManifestStream)
                        }
                        finally {
                            $appManifestStream.Dispose()
                        }

                        $packagedNamespace = New-Object System.Xml.XmlNamespaceManager(
                            $packagedManifest.NameTable)
                        $packagedNamespace.AddNamespace(
                            'f',
                            'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
                        $packagedNamespace.AddNamespace(
                            'rescap',
                            'http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities')

                        $packagedIdentity = $packagedManifest.SelectSingleNode(
                            '/f:Package/f:Identity',
                            $packagedNamespace)
                        $packagedCapabilities = @($packagedManifest.SelectNodes(
                            '/f:Package/f:Capabilities/rescap:Capability',
                            $packagedNamespace) | ForEach-Object { $_.Name })

                        foreach ($requiredCapability in @('runFullTrust', 'allowElevation')) {
                            if ($requiredCapability -notin $packagedCapabilities) {
                                throw "$($packageEntry.FullName) is missing $requiredCapability."
                            }
                        }

                        $packagedDeviceCapabilities = @($packagedManifest.SelectNodes(
                            '/f:Package/f:Capabilities/f:DeviceCapability',
                            $packagedNamespace) | ForEach-Object { $_.Name })
                        if ('bluetooth' -notin $packagedDeviceCapabilities) {
                            throw "$($packageEntry.FullName) is missing the bluetooth device capability."
                        }

                        foreach ($runtimeFile in @(
                            'BluetoothAudioCodec.WinUI.exe',
                            'Microsoft.WindowsAppRuntime.dll',
                            'Microsoft.ui.xaml.dll')) {
                            if ($null -eq $packageArchive.GetEntry($runtimeFile)) {
                                throw "$($packageEntry.FullName) is missing $runtimeFile."
                            }
                        }

                        [pscustomobject]@{
                            Architecture = $packagedIdentity.ProcessorArchitecture
                            IdentityName = $packagedIdentity.Name
                            Publisher = $packagedIdentity.Publisher
                            Version = $packagedIdentity.Version
                        }
                    }
                    finally {
                        $packageArchive.Dispose()
                    }
                }
                finally {
                    $packageStream.Dispose()
                }
            }
        }
        finally {
            $bundleArchive.Dispose()
        }
    }
    finally {
        $bundleStream.Dispose()
    }
}
finally {
    $uploadArchive.Dispose()
}

if (@($validatedArchitectures.Architecture | Sort-Object -Unique) -join ',' -ne 'arm64,x64') {
    throw 'The upload package does not contain both x64 and ARM64 packages.'
}

$unexpectedVersions = @($validatedArchitectures |
    Where-Object { $_.Version -ne $Version })
if ($unexpectedVersions.Count -ne 0) {
    $details = $unexpectedVersions |
        ForEach-Object { "$($_.Architecture)=$($_.Version)" }
    throw "The upload package contains a stale internal version ($($details -join ', ')); expected $Version for every architecture. Run dotnet clean for the affected platform and rebuild."
}

Write-Host ''
Write-Host 'Partner Center upload package:'
Write-Host $upload.FullName
Write-Host "SHA-256: $((Get-FileHash -LiteralPath $upload.FullName -Algorithm SHA256).Hash)"
$validatedArchitectures | Format-Table -AutoSize

if ($AllowPlaceholderIdentity) {
    Write-Warning 'This package uses a placeholder identity and cannot be submitted to Partner Center.'
}
