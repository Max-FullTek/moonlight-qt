# Dot-source before Cargo entry points: use only the repository's OpenSSL SDK.
$rustApiRoot = Split-Path $PSScriptRoot -Parent
$rustApiInclude = Join-Path $rustApiRoot 'libs/windows/include/x64'
$rustApiLib = Join-Path $rustApiRoot 'libs/windows/lib/x64'
$rustApiHeader = Join-Path $rustApiInclude 'openssl/opensslv.h'
if (!(Test-Path -LiteralPath $rustApiHeader) -or
    !((Get-Content -LiteralPath $rustApiHeader -Raw) -match 'OPENSSL_VERSION_TEXT\s+"OpenSSL 3\.6\.5\b')) {
    throw 'Repository OpenSSL 3.6.5 headers are required for Rust host control.'
}
foreach ($rustApiFile in @('libcrypto.lib', 'libssl.lib', 'libcrypto-3-x64.dll', 'libssl-3-x64.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $rustApiLib $rustApiFile))) { throw "Missing repository OpenSSL input: $rustApiFile" }
}
$env:OPENSSL_INCLUDE_DIR = $rustApiInclude
$env:OPENSSL_LIB_DIR = $rustApiLib
$env:OPENSSL_LIBS = 'libssl:libcrypto'
$env:OPENSSL_STATIC = '0'
$env:OPENSSL_NO_VENDOR = '1'
$env:PATH = "$rustApiLib;$env:PATH"
