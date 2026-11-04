param(
    [string]$SourceRoot = (Split-Path $PSScriptRoot -Parent),
    [string]$ArchivePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$target = Join-Path $SourceRoot 'libs/windows'
$dependency = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'portable-dependencies.json') -Raw | ConvertFrom-Json
$expected = $dependency.sha256
$url = $dependency.officialUrl
$archive = if ($ArchivePath) { [IO.Path]::GetFullPath($ArchivePath) } else {
    Join-Path $SourceRoot "build/dependency-cache/portable-deps-$($dependency.version)-x64.zip"
}
$source = 'verified-local-cache'

if (Test-Path -LiteralPath $target) {
    throw 'Dependency destination already exists. Use a fresh checkout; nothing is deleted or overwritten.'
}
New-Item -ItemType Directory -Path (Join-Path $SourceRoot 'build') -Force | Out-Null
if (!(Test-Path -LiteralPath $archive)) {
    New-Item -ItemType Directory -Path (Split-Path $archive -Parent) -Force | Out-Null
    if ($env:DEPENDENCY_MIRROR_TOKEN) {
        $api = "https://api.github.com/repos/$($dependency.mirrorRepository)/releases/tags/$($dependency.mirrorTag)"
        $headers = @{ Authorization = "Bearer $env:DEPENDENCY_MIRROR_TOKEN"; Accept = 'application/vnd.github+json' }
        $release = Invoke-RestMethod -Uri $api -Headers $headers
        $assets = @($release.assets | Where-Object name -CEQ $dependency.asset)
        if ($assets.Count -ne 1) { throw 'Expected exactly one pinned mirror asset.' }
        $url = $assets[0].url
        # Prevent an unexpected metadata URL from receiving the repository token.
        if ($url -notmatch '^https://api\.github\.com/repos/Max-FullTek/moonlight-qt-deps/releases/assets/[0-9]+$') {
            throw 'Unexpected dependency mirror asset URL.'
        }
        $headers.Accept = 'application/octet-stream'
        Invoke-WebRequest -Uri $url -Headers $headers -OutFile $archive
        $source = 'private-github-mirror'
    } else {
        Invoke-WebRequest -Uri $url -OutFile $archive
        $source = 'official-upstream'
    }
}
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $expected) {
    throw "Dependency SHA256 mismatch: expected $expected, got $actual"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName -match '(^[/\\]|(^|[/\\])\.\.([/\\]|$)|:)') {
            throw "Unsafe archive path: $($entry.FullName)"
        }
    }
} finally {
    $zip.Dispose()
}
Expand-Archive -LiteralPath $archive -DestinationPath $target
& (Join-Path $PSScriptRoot 'verify-pe-machine.ps1') -Path (Join-Path $target 'lib/x64') -ExpectedMachine 0x8664
@{ url = $url; officialUrl = $dependency.officialUrl; source = $source; sha256 = $actual; tag = $dependency.version; kind = 'third-party-prebuilt' } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $SourceRoot 'build/portable-deps.json')
