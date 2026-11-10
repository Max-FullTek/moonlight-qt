# Explicit native transport regression; not part of portable packaging yet.
param(
    [string]$BuildDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build/native-host-http'),
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$QtDirectory # Optional: verify the transitional owner, not the native dependency.
)
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
$ownerOptions = @('-DVERIFY_QT_HTTP_OWNER=OFF')
if ($QtDirectory) {
    $QtDirectory = (Resolve-Path -LiteralPath $QtDirectory).Path
    $ownerOptions = @('-DVERIFY_QT_HTTP_OWNER=ON', "-DCMAKE_PREFIX_PATH=$QtDirectory")
}
& $cmake -S (Join-Path $repoRoot 'tests/native_host_http') -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DCURL_SOURCE_DIRECTORY=$source" @ownerOptions
if ($LASTEXITCODE -ne 0) { throw 'HTTP probe configure failed' }
& $cmake --build $BuildDirectory --config $Configuration --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'HTTP probe build failed' }
$python = (Get-Command python.exe -ErrorAction Stop).Source
$savedPath = $env:PATH
$savedWorker = [Environment]::GetEnvironmentVariable('NATIVE_HTTP_QT_WORKER', 'Process')
try {
    $env:PATH = "$(Join-Path $repoRoot 'libs/windows/lib/x64');$env:SystemRoot/System32;$env:SystemRoot"
    $work = Join-Path $BuildDirectory ('scenarios-' + [guid]::NewGuid().ToString('N'))
    & $python (Join-Path $repoRoot 'tests/native_host_http/check.py') (Join-Path $BuildDirectory "$Configuration/native_host_http.exe") (Join-Path $repoRoot 'tests/clipboard_payload_routing/fixtures') $work
    if ($LASTEXITCODE -ne 0) { throw 'HTTP capability checks failed' }
    $work = Join-Path $BuildDirectory ('event-scenarios-' + [guid]::NewGuid().ToString('N'))
    & $python (Join-Path $repoRoot 'tests/native_host_http/check.py') (Join-Path $BuildDirectory "$Configuration/native_host_http.exe") (Join-Path $repoRoot 'tests/clipboard_payload_routing/fixtures') $work event-owner
    if ($LASTEXITCODE -ne 0) { throw 'Native HTTP socket-owner checks failed' }
    if ($QtDirectory) {
        $env:PATH = "$(Join-Path $QtDirectory 'bin');$env:PATH"
        foreach ($owner in @('main', 'worker')) {
            $workerFlag = if ($owner -eq 'worker') { '1' } else { $null }
            [Environment]::SetEnvironmentVariable('NATIVE_HTTP_QT_WORKER', $workerFlag, 'Process')
            Write-Host "Verifying transitional Qt HTTP owner on $owner thread"
            $work = Join-Path $BuildDirectory ('qt-' + $owner + '-scenarios-' + [guid]::NewGuid().ToString('N'))
            & $python (Join-Path $repoRoot 'tests/native_host_http/check.py') (Join-Path $BuildDirectory "$Configuration/native_host_http_qt_owner.exe") (Join-Path $repoRoot 'tests/clipboard_payload_routing/fixtures') $work event-owner
            if ($LASTEXITCODE -ne 0) { throw "Transitional Qt HTTP $owner owner checks failed" }
        }
    }
} finally {
    $env:PATH = $savedPath
    [Environment]::SetEnvironmentVariable('NATIVE_HTTP_QT_WORKER', $savedWorker, 'Process')
}
