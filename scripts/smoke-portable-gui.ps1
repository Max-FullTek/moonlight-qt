# Checks actual QML startup using a fresh portable profile and the SDK's
# offscreen plugin. This is a load check, not a real streaming/UI acceptance test.
param(
    [Parameter(Mandatory = $true)][string]$PortablePath,
    [Parameter(Mandatory = $true)][string]$TestRoot,
    [Parameter(Mandatory = $true)][string]$QtPlugins
)

$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $TestRoot) { throw 'GUI smoke destination must be new.' }
New-Item -ItemType Directory -Path $TestRoot | Out-Null
$appRoot = Join-Path $TestRoot 'app'
Copy-Item -LiteralPath $PortablePath -Destination $appRoot -Recurse
$stdout = Join-Path $TestRoot 'stdout.txt'
$stderr = Join-Path $TestRoot 'stderr.txt'
$previousPlatform = $env:QT_QPA_PLATFORM
$previousPlugins = $env:QT_PLUGIN_PATH
$previousBackend = $env:QT_QUICK_BACKEND
$process = $null
$earlyExit = $null
try {
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_PLUGIN_PATH = $QtPlugins
    $env:QT_QUICK_BACKEND = 'software'
    $process = Start-Process -FilePath (Join-Path $appRoot 'Moonlight.exe') `
        -WorkingDirectory $appRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if ($process.WaitForExit(10000)) {
        $earlyExit = $process.ExitCode
    }
} finally {
    if ($process -and !$process.HasExited) {
        # Only this known test process is terminated. The test profile is not shipped.
        Stop-Process -Id $process.Id
        $process.WaitForExit()
    }
    $env:QT_QPA_PLATFORM = $previousPlatform
    $env:QT_PLUGIN_PATH = $previousPlugins
    $env:QT_QUICK_BACKEND = $previousBackend
}
$output = (Get-Content -LiteralPath $stdout, $stderr -Raw) -join "`n"
if ($null -ne $earlyExit) {
    Write-Output $output
    throw "GUI exited before the startup observation ended: $earlyExit"
}
if ($output -match 'ReferenceError|TypeError|QQmlApplicationEngine failed|is not a type|is not installed|Cannot assign to|Cannot load library') {
    Write-Output $output
    throw 'GUI startup reported a QML or plugin error.'
}
Write-Host 'Fresh-profile GUI QML startup passed (offscreen, no host configured).'
