# Build only the Windows Lite native libraries. The streaming app/helper still
# use Qt and are deliberately not part of this entry point.
param(
    [string]$SourceRoot = (Split-Path $PSScriptRoot -Parent),
    [string]$BuildDirectory,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
if (!$BuildDirectory) { $BuildDirectory = Join-Path $SourceRoot 'build/lite-native' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$deps = Join-Path $SourceRoot 'libs/windows/lib/x64'

# Prefer PATH; otherwise use an already-installed Visual Studio CMake. Nothing
# is installed and the machine's persistent environment is never changed.
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmake = $cmakeCommand.Source
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'CMake or Visual Studio Build Tools with CMake is required.' }
    $candidates = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find '**/cmake.exe')
    if ($LASTEXITCODE -ne 0 -or $candidates.Count -eq 0) { throw 'No installed CMake found.' }
    $cmake = $candidates[0]
}
$ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
if (!(Test-Path -LiteralPath $ctest)) { throw 'ctest.exe must be beside cmake.exe.' }

Invoke-Checked $cmake @('-S', (Join-Path $SourceRoot 'windows-lite'), '-B', $BuildDirectory,
    '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DBUILD_TESTING=ON')
Invoke-Checked $cmake @('--build', $BuildDirectory, '--config', $Configuration, '--parallel', '2')

# Tests may load only the native dependencies and Windows runtime from PATH.
# In particular, the developer's Qt bin/plugins paths are not available here.
$savedPath = $env:PATH
try {
    $env:PATH = "$deps;$env:SystemRoot/System32;$env:SystemRoot"
    Invoke-Checked $ctest @('--test-dir', $BuildDirectory, '-C', $Configuration,
        '--output-on-failure', '--no-tests=error', '--timeout', '60')
} finally {
    $env:PATH = $savedPath
}
Write-Host "Native-library stage passed ($Configuration). This does not build the Qt-free streaming app."
