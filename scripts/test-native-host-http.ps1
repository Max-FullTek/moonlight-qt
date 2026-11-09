# Explicit capability probe, not a production transport or part of portable packaging.
param([string]$BuildDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build/native-host-http'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path $PSScriptRoot -Parent
$archive = Join-Path $repoRoot 'build/curl-8.23.0.zip'
$source = Join-Path $repoRoot 'build/curl-capability-source/curl-8.23.0'
$expected = '85477FE11408471285F38A601E7C6F53470D13BE9A5A316919B071AD02E68213'
if (!(Test-Path -LiteralPath $archive)) {
    New-Item -ItemType Directory -Force (Join-Path $repoRoot 'build') | Out-Null
    Invoke-WebRequest -Uri 'https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.zip' -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
    throw 'Official curl 8.23.0 release SHA256 mismatch; nothing will be extracted.'
}
if (!(Test-Path -LiteralPath $source)) {
    $destination = Split-Path $source -Parent
    if (Test-Path -LiteralPath $destination) { throw 'Extraction destination already exists; no files were overwritten.' }
    Expand-Archive -LiteralPath $archive -DestinationPath $destination
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$cmakePaths = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find '**/cmake.exe')
if ($LASTEXITCODE -ne 0 -or !$cmakePaths.Count) { throw 'Installed VS CMake is required.' }
$cmake = $cmakePaths[0]
& $cmake -S (Join-Path $repoRoot 'tests/native_host_http') -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DCURL_SOURCE_DIRECTORY=$source"
if ($LASTEXITCODE -ne 0) { throw 'HTTP probe configure failed' }
& $cmake --build $BuildDirectory --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'HTTP probe build failed' }
$python = (Get-Command python.exe -ErrorAction Stop).Source
$savedPath = $env:PATH
try {
    $env:PATH = "$(Join-Path $repoRoot 'libs/windows/lib/x64');$env:SystemRoot/System32;$env:SystemRoot"
    $work = Join-Path $BuildDirectory ('scenarios-' + [guid]::NewGuid().ToString('N'))
    & $python (Join-Path $repoRoot 'tests/native_host_http/check.py') (Join-Path $BuildDirectory 'Release/native_host_http.exe') (Join-Path $repoRoot 'tests/clipboard_payload_routing/fixtures') $work
    if ($LASTEXITCODE -ne 0) { throw 'HTTP capability checks failed' }
} finally { $env:PATH = $savedPath }
