$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$outDir = Join-Path $repoRoot 'build/gpui-package'
New-Item -ItemType Directory -Force $outDir | Out-Null
$exe = Join-Path $env:CARGO_TARGET_DIR 'release/moonlight-lite-gpui-prototype.exe'
Copy-Item -LiteralPath $exe -Destination $outDir
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $outDir
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Cargo.lock') -Destination $outDir
$commit = (git -C $repoRoot rev-parse HEAD).Trim()
$rust = (rustc +1.99.1 --version).Trim()
@{ commit = $commit; rust = $rust; gpui = '0.2.2'; target = 'x86_64-pc-windows-msvc'; tests = @('settings validation and persistence round trip', 'native recommended bitrate presets', 'worker protocol validation', 'stream settings mapping'); scope = 'Native GPUI frontend. Pairing and streaming require the matching Moonlight.exe worker from the integrated portable package.' } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outDir 'build-provenance.json') -Encoding utf8
$metadata = cargo +1.99.1 metadata --locked --format-version 1 --manifest-path (Join-Path $PSScriptRoot 'Cargo.toml') | ConvertFrom-Json
if ($LASTEXITCODE) { throw 'Cannot collect dependency provenance' }
$licenseText = [Collections.Generic.List[string]]::new()
$licenseText.Add("Source: https://github.com/Max-FullTek/moonlight-qt/tree/$commit/gpui-prototype`nPrototype: GPL-3.0-only. Dependencies retain their own licenses.`n")
foreach ($package in ($metadata.packages | Sort-Object name, version)) {
    $licenseText.Add("`n===== $($package.name) $($package.version) | $($package.license) | $($package.repository) =====`n")
    $manifestDir = Split-Path $package.manifest_path -Parent
    foreach ($licenseFile in (Get-ChildItem -LiteralPath $manifestDir -File | Where-Object { $_.Name -match '^(LICENSE|LICENCE|COPYING|NOTICE)([._-]|$)' })) {
        $licenseText.Add([IO.File]::ReadAllText($licenseFile.FullName))
    }
}
[IO.File]::WriteAllLines((Join-Path $outDir 'THIRD-PARTY-NOTICES.txt'), $licenseText, [Text.UTF8Encoding]::new($false))
$hash = (Get-FileHash -LiteralPath (Join-Path $outDir 'moonlight-lite-gpui-prototype.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  moonlight-lite-gpui-prototype.exe" | Set-Content -LiteralPath (Join-Path $outDir 'SHA256SUMS.txt') -Encoding ascii
