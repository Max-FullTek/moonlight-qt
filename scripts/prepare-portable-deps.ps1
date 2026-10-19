param(
    [string]$SourceRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$target = Join-Path $SourceRoot 'libs/windows'
$archive = Join-Path $SourceRoot 'build/portable-deps-v19-x64.zip'
$url = 'https://github.com/moonlight-stream/moonlight-qt-deps/releases/download/v19/Windows-x64.zip'
# GitHub release asset digest, reviewed before adding this workflow.
$expected = '65ff1bd439222e1e2750054f960dcd6fac46190027da16db7403e345c295f10f'

if ((Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath $archive)) {
    throw 'Dependency destination or archive already exists. Use a fresh checkout; nothing is deleted or overwritten.'
}
New-Item -ItemType Directory -Path (Join-Path $SourceRoot 'build') -Force | Out-Null
Invoke-WebRequest -Uri $url -OutFile $archive
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
@{ url = $url; sha256 = $actual; tag = 'v19'; kind = 'third-party-prebuilt' } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $SourceRoot 'build/portable-deps.json')
