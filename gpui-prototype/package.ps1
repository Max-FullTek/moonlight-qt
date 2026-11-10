$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $repoRoot 'scripts/set-rust-host-api-env.ps1')
if ([string]::IsNullOrWhiteSpace($env:CARGO_TARGET_DIR)) {
    throw 'Set CARGO_TARGET_DIR before packaging the GPUI frontend.'
}
# Tests produce a separate test binary. Always build the shipping executable
# before recording provenance or copying a possibly stale release executable.
& cargo +1.99.1 build --release --locked --manifest-path (Join-Path $PSScriptRoot 'Cargo.toml')
if ($LASTEXITCODE -ne 0) { throw 'Cannot build the GPUI release executable; package was not updated.' }
$outDir = Join-Path $repoRoot 'build/gpui-package'
New-Item -ItemType Directory -Force $outDir | Out-Null
$exe = Join-Path $env:CARGO_TARGET_DIR 'release/moonlight-lite-gpui-prototype.exe'
Copy-Item -LiteralPath $exe -Destination $outDir
foreach ($cryptoDll in @('libcrypto-3-x64.dll', 'libssl-3-x64.dll')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "libs/windows/lib/x64/$cryptoDll") -Destination $outDir
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $outDir
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Cargo.lock') -Destination $outDir
$commit = (git -C $repoRoot rev-parse HEAD).Trim()
$rust = (rustc +1.99.1 --version).Trim()
@{ commit = $commit; rust = $rust; gpui = '0.2.2'; target = 'x86_64-pc-windows-msvc'; tests = @('settings validation and persistence round trip', 'native recommended bitrate presets', 'worker protocol validation', 'stream settings mapping', 'display preferences migration and validation', 'three UI catalogs and user text preservation', 'hidden and direct launch app selection'); scope = 'Native GPUI frontend. Pairing and streaming require the matching Moonlight.exe worker from the integrated portable package.' } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outDir 'build-provenance.json') -Encoding utf8
$metadata = cargo +1.99.1 metadata --locked --format-version 1 --manifest-path (Join-Path $PSScriptRoot 'Cargo.toml') | ConvertFrom-Json
if ($LASTEXITCODE) { throw 'Cannot collect dependency provenance' }
$licenseText = [Collections.Generic.List[string]]::new()
$licenseText.Add("Source: https://github.com/Max-FullTek/moonlight-qt/tree/$commit/gpui-prototype`nPrototype: GPL-3.0-only. Dependencies retain their own licenses.`n")
$licenseText.Add("`n===== Native OpenSSL 3.6.5 | https://github.com/openssl/openssl/tree/openssl-3.6.5 =====`n")
foreach ($opensslNotice in @('LICENSE.txt', 'AUTHORS.md')) {
    $licenseText.Add([IO.File]::ReadAllText((Join-Path $repoRoot "third-party/openssl/$opensslNotice")))
}
foreach ($package in ($metadata.packages | Sort-Object name, version)) {
    $licenseText.Add("`n===== $($package.name) $($package.version) | $($package.license) | $($package.repository) =====`n")
    $manifestDir = Split-Path $package.manifest_path -Parent
    foreach ($licenseFile in (Get-ChildItem -LiteralPath $manifestDir -File | Where-Object { $_.Name -match '^(LICENSE|LICENCE|COPYING|NOTICE)([._-]|$)' })) {
        $licenseText.Add([IO.File]::ReadAllText($licenseFile.FullName))
    }
}
[IO.File]::WriteAllLines((Join-Path $outDir 'THIRD-PARTY-NOTICES.txt'), $licenseText, [Text.UTF8Encoding]::new($false))
$hashes = foreach ($binary in @('moonlight-lite-gpui-prototype.exe', 'libcrypto-3-x64.dll', 'libssl-3-x64.dll')) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $outDir $binary) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $binary"
}
$hashes | Set-Content -LiteralPath (Join-Path $outDir 'SHA256SUMS.txt') -Encoding ascii
