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
    -p:AppxSymbolPackageEnabled=false

if ($LASTEXITCODE -ne 0) {
    throw 'MSIX StoreUpload build failed.'
}

$upload = Get-ChildItem -LiteralPath (Join-Path $root 'BluetoothAudioCodec.WinUI\AppPackages') `
        -Filter '*.msixupload' -File -Recurse |
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
