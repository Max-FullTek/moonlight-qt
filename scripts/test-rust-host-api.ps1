param(
    [string]$QtDirectory,
    [string]$NativeHttpProbe,
    [string]$QtParserReference,
    [string]$PairingPeer,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'set-rust-host-api-env.ps1')
if (!$QtDirectory -or !$NativeHttpProbe -or !$QtParserReference) {
    throw 'Provide QtDirectory, NativeHttpProbe (public fixture generator) and QtParserReference from the existing capability builds.'
}
foreach ($inputPath in @($QtDirectory, $NativeHttpProbe, $QtParserReference)) {
    if (!(Test-Path -LiteralPath $inputPath)) { throw "Missing capability input: $inputPath" }
}
$manifest = Join-Path $repoRoot 'tests/rust_host_api/Cargo.toml'
if (!$env:CARGO_TARGET_DIR) { throw 'Set CARGO_TARGET_DIR to an existing build/cache location.' }
$savedIncremental = [Environment]::GetEnvironmentVariable('CARGO_INCREMENTAL', 'Process')
try {
    $env:CARGO_INCREMENTAL = '0'
    $profileArgs = @()
    $profile = 'debug'
    if ($Configuration -eq 'Release') { $profileArgs = @('--release'); $profile = 'release' }
    & cargo +1.99.1 test --locked --manifest-path $manifest @profileArgs
    if ($LASTEXITCODE -ne 0) { throw 'Rust API probe unit checks failed' }
    & cargo +1.99.1 test --locked --manifest-path $manifest -p moonlight-host-api @profileArgs
    if ($LASTEXITCODE -ne 0) { throw 'Production Rust API unit checks failed' }
    & cargo +1.99.1 build --locked --manifest-path $manifest @profileArgs
    if ($LASTEXITCODE -ne 0) { throw 'Rust API probe build failed' }
    $python = (Get-Command python.exe -ErrorAction Stop).Source
    $work = Join-Path $repoRoot ('build/rust-host-api/' + $Configuration + '-' + [guid]::NewGuid().ToString('N'))
    & $python (Join-Path $repoRoot 'tests/rust_host_api/check.py') `
        (Join-Path $env:CARGO_TARGET_DIR "$profile/rust-host-api-probe.exe") `
        $NativeHttpProbe $QtParserReference (Join-Path $QtDirectory 'bin') $work
    if ($LASTEXITCODE -ne 0) { throw 'Rust API capability scenarios failed' }
    if ($PairingPeer) {
        & $python (Join-Path $repoRoot 'tests/native_host_http/check_pairing.py') `
            (Join-Path $env:CARGO_TARGET_DIR "$profile/rust-host-api-probe.exe") $PairingPeer `
            (Join-Path $repoRoot 'tests/clipboard_payload_routing/fixtures') `
            ($work + '-pairing') --extended-rust
        if ($LASTEXITCODE -ne 0) { throw 'Rust pairing scenarios failed' }
    }
} finally {
    [Environment]::SetEnvironmentVariable('CARGO_INCREMENTAL', $savedIncremental, 'Process')
}
