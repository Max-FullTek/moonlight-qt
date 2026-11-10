# SA-approved parser capability comparison; does not replace production parsers.
param(
    [string]$BuildDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build/native-parsers'),
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$QtDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (!$QtDirectory) { throw 'Supply -QtDirectory for the reference comparison; native_parser_probe itself requires no Qt.' }
$repoRoot = Split-Path $PSScriptRoot -Parent
$python = (Get-Command python.exe -ErrorAction Stop).Source
$prepared = & $python (Join-Path $repoRoot 'tests/native_parsers/prepare.py') (Join-Path $repoRoot 'build/native-parsers-source')
if ($LASTEXITCODE -ne 0) { throw 'Verified parser source preparation failed.' }
$sources = $prepared | ConvertFrom-Json
$QtDirectory = (Resolve-Path -LiteralPath $QtDirectory).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$cmakePaths = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find '**/cmake.exe')
if ($LASTEXITCODE -ne 0 -or !$cmakePaths.Count) { throw 'Installed VS CMake is required.' }
$cmake = $cmakePaths[0]
& $cmake -S (Join-Path $repoRoot 'tests/native_parsers') -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 `
    "-DEXPAT_SOURCE_DIRECTORY=$($sources.expat)" "-DJSON_SOURCE_DIRECTORY=$($sources.json)" `
    -DVERIFY_QT_PARSER=ON "-DCMAKE_PREFIX_PATH=$QtDirectory"
if ($LASTEXITCODE -ne 0) { throw 'Parser probe configure failed.' }
& $cmake --build $BuildDirectory --config $Configuration --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Parser probe build failed.' }
& $python (Join-Path $repoRoot 'tests/native_parsers/check.py') `
    (Join-Path $BuildDirectory "$Configuration/native_parser_probe.exe") `
    (Join-Path $BuildDirectory "$Configuration/qt_parser_reference.exe") `
    (Join-Path $BuildDirectory "comparison-$Configuration.json") (Join-Path $QtDirectory 'bin')
if ($LASTEXITCODE -ne 0) { throw 'Parser comparison could not execute.' }
Write-Host 'Review comparison differences before any production switch; a completed probe is not compatibility acceptance.'
