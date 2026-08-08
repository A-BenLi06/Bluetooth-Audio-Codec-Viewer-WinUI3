[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '1.0.0',

    [ValidateSet('x64', 'arm64', 'all')]
    [string]$Architecture = 'all',

    [string]$CertificateThumbprint,

    [ValidateNotNullOrEmpty()]
    [string]$Manufacturer = 'BenLi06',

    [uri]$TimestampUrl = 'http://timestamp.digicert.com',

    [switch]$RequireSigning
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$appProject = Join-Path $root 'BluetoothAudioCodec.WinUI\BluetoothAudioCodec.WinUI.csproj'
$installerProject = Join-Path $root 'BluetoothAudioCodec.Installer\BluetoothAudioCodec.Installer.wixproj'
$artifactRoot = Join-Path $root 'artifacts'

if ($RequireSigning -and [string]::IsNullOrWhiteSpace($CertificateThumbprint)) {
    throw 'A trusted code-signing certificate thumbprint is required for a Store release.'
}

function Get-SignTool {
    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $candidate = Get-ChildItem -Path $kitsRoot -Filter signtool.exe -File -Recurse |
        Where-Object { $_.DirectoryName -match '\\x64$' } |
        Sort-Object { [version]$_.Directory.Parent.Name } -Descending |
        Select-Object -First 1

    if ($null -eq $candidate) {
        throw 'signtool.exe was not found in the Windows SDK.'
    }

    return $candidate.FullName
}

function Invoke-CodeSign([string]$Path, [string]$SignTool) {
    & $SignTool sign `
        /sha1 $CertificateThumbprint `
        /fd SHA256 `
        /tr $TimestampUrl.AbsoluteUri `
        /td SHA256 `
        $Path | Out-Host

    if ($LASTEXITCODE -ne 0) {
        throw "Signing failed for $Path."
    }

    $signature = Get-AuthenticodeSignature -FilePath $Path
    if ($signature.Status -ne 'Valid') {
        throw "The Authenticode signature is not valid for ${Path}: $($signature.StatusMessage)"
    }
}

$architectures = if ($Architecture -eq 'all') {
    @('x64', 'arm64')
} else {
    @($Architecture)
}

$signTool = if ([string]::IsNullOrWhiteSpace($CertificateThumbprint)) {
    $null
} else {
    Get-SignTool
}

$outputs = foreach ($targetArchitecture in $architectures) {
    $runtimeIdentifier = "win-$targetArchitecture"
    $publishDirectory = Join-Path $artifactRoot "publish\$runtimeIdentifier"

    dotnet publish $appProject `
        --configuration Release `
        --runtime $runtimeIdentifier `
        --self-contained true `
        -p:Platform=$targetArchitecture `
        -p:Version=$Version `
        -p:PublishSingleFile=true `
        -p:IncludeAllContentForSelfExtract=true `
        --output $publishDirectory | Out-Host

    if ($LASTEXITCODE -ne 0) {
        throw "Publishing failed for $targetArchitecture."
    }

    $application = Join-Path $publishDirectory 'BluetoothAudioCodec.WinUI.exe'
    if ($null -ne $signTool) {
        Invoke-CodeSign -Path $application -SignTool $signTool
    }

    dotnet build $installerProject `
        --configuration Release `
        -p:Platform=$targetArchitecture `
        -p:ProductVersion=$Version `
        -p:ProductManufacturer=$Manufacturer `
        -p:AppPublishDir=$publishDirectory | Out-Host

    if ($LASTEXITCODE -ne 0) {
        throw "MSI creation failed for $targetArchitecture."
    }

    $installer = Join-Path $artifactRoot "installer\$targetArchitecture\BluetoothAudioCodec-$Version-$targetArchitecture.msi"
    if ($null -ne $signTool) {
        Invoke-CodeSign -Path $installer -SignTool $signTool
    }

    [pscustomobject]@{
        Architecture = $targetArchitecture
        Application = $application
        Installer = $installer
        Signed = $null -ne $signTool
        SilentInstall = "msiexec.exe /i `"$installer`" /qn /norestart"
    }
}

if ($null -eq $signTool) {
    Write-Warning 'Unsigned local-test installers were created. They are not eligible for Microsoft Store submission.'
}

$outputs | Format-List
