# Builds the slim Windows x64 feature set. This script does not perform a security audit.
param(
    [string]$SourceRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
Set-Location -LiteralPath $SourceRoot
$root = Join-Path $SourceRoot 'build/portable-ci'
if (Test-Path -LiteralPath $root) {
    throw 'Build destination already exists. Use a fresh checkout; nothing is deleted or overwritten.'
}
if (@(git status --porcelain).Count) { throw 'Source checkout must be clean before building.' }
$submodules = @(git submodule status --recursive)
if ($LASTEXITCODE -ne 0 -or ($submodules | Where-Object { $_ -match '^[-+U]' })) {
    throw 'Submodules must match the checked-in commits.'
}
$commit = (git rev-parse HEAD).Trim()
$version = (python scripts/derive-version.py --field artifact).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Version derivation failed.' }
$env:CI_VERSION = $version

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vsPath) { throw 'MSVC x64 Build Tools not found.' }
$vcvars = Join-Path $vsPath 'VC/Auxiliary/Build/vcvarsall.bat'
$vcEnvironment = & $env:ComSpec /d /s /c "`"$vcvars`" amd64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize MSVC.' }
foreach ($line in $vcEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
$qmake = (Get-Command qmake.exe -ErrorAction Stop).Source
$qtVersion = (& $qmake -query QT_VERSION).Trim()
if ($LASTEXITCODE -ne 0 -or $qtVersion -ne '6.12.0') { throw 'Expected Qt 6.12.0.' }
$qtBins = (& $qmake -query QT_INSTALL_BINS).Trim()
$windeployqt = Join-Path $qtBins 'windeployqt.exe'
$env:PATH = "$qtBins;$env:PATH"
$env:CL = '/MP2 /Zm500'
$env:LINK = '/INCREMENTAL:NO'
$env:CFLAGS = '/GL'
$env:CXXFLAGS = '/GL'
$env:LDFLAGS = '/LTCG'
$env:MAKEFLAGS = ''
$env:_CL_ = ''
$build = Join-Path $root 'compile'
$deploy = Join-Path $root 'portable'
$artifacts = Join-Path $root 'artifacts'
New-Item -ItemType Directory -Path $build, $deploy, $artifacts | Out-Null

Push-Location -LiteralPath $build
try {
    Invoke-Checked $qmake @((Join-Path $SourceRoot 'moonlight-qt.pro'))
    Invoke-Checked 'nmake.exe' @('/NOLOGO', 'release')
} finally { Pop-Location }

$deps = Join-Path $SourceRoot 'libs/windows/lib/x64'
Get-ChildItem -LiteralPath $deps -Filter '*.dll' |
    Where-Object Name -ne 'discord-rpc.dll' |
    Copy-Item -Destination $deploy
Copy-Item -LiteralPath (Join-Path $build 'app/release/Moonlight.exe') -Destination $deploy
Copy-Item -LiteralPath (Join-Path $build 'AntiHooking/release/AntiHooking.dll') -Destination $deploy
Copy-Item -LiteralPath (Join-Path $build 'clipboard-helper/release/moonlight-clipboard-helper.exe') -Destination $deploy
Copy-Item -LiteralPath (Join-Path $SourceRoot 'app/SDL_GameControllerDB/gamecontrollerdb.txt') -Destination $deploy
Copy-Item -LiteralPath (Join-Path $SourceRoot 'LICENSE') -Destination $deploy

# Match the existing Qt 6 deployment flags, retaining all remaining plugins.
Invoke-Checked $windeployqt @(
    '--dir', $deploy, '--release', '--qmldir', (Join-Path $SourceRoot 'app/gui'),
    '--no-opengl-sw', '--no-compiler-runtime', '--no-sql',
    '--no-system-d3d-compiler', '--no-system-dxc-compiler',
    '--skip-plugin-types', 'qmltooling,generic', '--no-ffmpeg',
    '--no-quickcontrols2imagine', '--no-quickcontrols2universal',
    '--no-quickcontrols2imaginestyleimpl', '--no-quickcontrols2universalstyleimpl',
    '--no-quickcontrols2windowsstyleimpl', (Join-Path $deploy 'Moonlight.exe')
)
$redistCandidates = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT')
if ($LASTEXITCODE -ne 0 -or !$redistCandidates.Count) { throw 'MSVC redistributable DLLs not found.' }
$redist = $redistCandidates | Sort-Object | Select-Object -Last 1
Copy-Item -Path (Join-Path $redist '*.dll') -Destination $deploy
New-Item -ItemType File -Path (Join-Path $deploy 'portable.dat') | Out-Null
& (Join-Path $PSScriptRoot 'verify-pe-machine.ps1') -Path $deploy -ExpectedMachine 0x8664

# Focused existing regressions; no streaming host or user credentials are used.
$env:PATH = "$deploy;$env:PATH"
$testResults = @()
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
Invoke-Checked (Join-Path $qtBins 'qmltestrunner.exe') @(
    '-input', (Join-Path $SourceRoot 'tests/lite_ui'), '-o', '-,txt'
)
$testResults += @{ name = 'lite-manual-connection-ui'; result = 'passed-offscreen' }
foreach ($test in @('keyboard_state', 'shared_helpers', 'clipboard_payload_routing', 'overlay_menu_navigation')) {
    $testBuild = Join-Path $root "test-$test"
    New-Item -ItemType Directory -Path $testBuild | Out-Null
    Push-Location -LiteralPath $testBuild
    try {
        Invoke-Checked $qmake @((Join-Path $SourceRoot "tests/$test/$test.pro"))
        Invoke-Checked 'nmake.exe' @('/NOLOGO', 'release')
        Invoke-Checked (Join-Path $testBuild "release/$test.exe") @()
        $testResults += @{ name = $test; result = 'passed' }
    } finally { Pop-Location }
}

& (Join-Path $PSScriptRoot 'smoke-portable-gui.ps1') -PortablePath $deploy `
    -TestRoot (Join-Path $root 'gui-smoke') -QtPlugins (& $qmake -query QT_INSTALL_PLUGINS)
$testResults += @{ name = 'gui-qml-startup'; result = 'passed-offscreen-no-host' }

$provenance = [ordered]@{
    schemaVersion = 1
    repository = $env:GITHUB_REPOSITORY
    commit = $commit
    baseTag = 'v6.5.0'
    baseCommit = 'ac1a11ea8396e56545c24e2d4203a17e76777a23'
    version = $version
    submodules = $submodules
    runUrl = "https://github.com/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID"
    runAttempt = $env:GITHUB_RUN_ATTEMPT
    runnerImage = "$env:ImageOS/$env:ImageVersion"
    qt = @{ version = $qtVersion; qmakeSha256 = (Get-FileHash $qmake -Algorithm SHA256).Hash; origin = 'Qt official prebuilt SDK, installer hash verification enabled' }
    qtInstaller = @{ repository = 'jdpurcell/naqt'; commit = $env:NAQT_COMMIT }
    msvc = (Get-Item (Get-Command cl.exe).Source).VersionInfo.FileVersion
    windowsSDK = $env:WindowsSDKVersion
    python = (python --version)
    dotnet = (dotnet --version)
    prebuiltDependencies = (Get-Content (Join-Path $SourceRoot 'build/portable-deps.json') -Raw | ConvertFrom-Json)
    tests = $testResults
    features = 'Lite: no updater, online wallpaper/promotion, Discord, STUN, USB/IP, or file mapping; bundled gamepad mappings and text/image clipboard retained; microphone disabled'
    assurance = 'Source build with prebuilt third-party dependencies; not a full security audit or a bit-reproducibility claim'
}
$provenance | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $deploy 'build-provenance.json')
$fileHashes = @(Get-ChildItem -LiteralPath $deploy -File -Recurse | Sort-Object FullName | ForEach-Object {
    @{ path = [IO.Path]::GetRelativePath($deploy, $_.FullName).Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
$fileHashes | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $deploy 'file-sha256.json')
$zipName = "Moonlight-VPlus-Lite-x64-$version.zip"
$package = Join-Path $artifacts $zipName
Compress-Archive -Path (Join-Path $deploy '*') -DestinationPath $package
$packageHash = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash
"$packageHash  $zipName" | Set-Content -LiteralPath (Join-Path $artifacts 'SHA256SUMS.txt')
Copy-Item -LiteralPath (Join-Path $deploy 'build-provenance.json') -Destination $artifacts
Copy-Item -LiteralPath (Join-Path $deploy 'file-sha256.json') -Destination $artifacts
Copy-Item -LiteralPath (Join-Path $SourceRoot '.github/workflows/portable-x64.yml') -Destination $artifacts
git diff --exit-code
if ($LASTEXITCODE -ne 0) { throw 'Build unexpectedly modified tracked source files.' }
Write-Host "Portable archive: $zipName SHA256=$packageHash"
