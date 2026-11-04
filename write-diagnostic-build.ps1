[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputPath,
    [Parameter(Mandatory)][ValidateSet('x64','ARM64')][string]$Architecture,
    [Parameter(Mandatory)][ValidateSet('Debug','Release')][string]$Configuration,
    [ValidateSet('msix','msi','portable')][string]$Packaging = 'portable',
    [ValidateSet('microsoft-store','github-release','development','unknown')][string]$Distribution = 'unknown'
)
$ErrorActionPreference = 'Stop'
$commit = ''
$dirty = 1
if (Get-Command git -ErrorAction SilentlyContinue) {
    $revision = & git -C $PSScriptRoot rev-parse HEAD 2>$null
    if ($LASTEXITCODE -eq 0 -and $revision -match '^[0-9a-f]{40}$') {
        $commit = $revision
        $state = & git -C $PSScriptRoot status --porcelain --untracked-files=normal 2>$null
        if ($LASTEXITCODE -eq 0 -and -not $state) { $dirty = 0 }
    }
}
$buildId = '{0}-{1}-{2}-{3}' -f $Configuration.ToLowerInvariant(), $Architecture.ToLowerInvariant(),
    [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'), [Guid]::NewGuid().ToString('N')
$destination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
$source = @"
#pragma once
#define BAC_BUILD_ID L"$buildId"
#define BAC_GIT_COMMIT L"$commit"
#define BAC_BUILD_DIRTY $dirty
#define BAC_PACKAGING L"$Packaging"
#define BAC_DISTRIBUTION L"$Distribution"
"@
[IO.File]::WriteAllText($destination, $source, [Text.UTF8Encoding]::new($false))
