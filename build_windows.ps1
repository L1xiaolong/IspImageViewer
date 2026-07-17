param(
    [ValidateSet("debug", "release")]
    [string]$Mode = "debug",

    [switch]$Test,
    [switch]$Rhi,
    [switch]$Clean,
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

function Show-Usage {
    Write-Host @"
Usage:
  .\build_windows.ps1 [-Mode debug|release] [-Test] [-Rhi] [-Clean] [-Jobs N]

Examples:
  .\build_windows.ps1
  .\build_windows.ps1 -Mode debug -Test
  .\build_windows.ps1 -Mode release -Jobs 8

Options:
  -Mode debug|release  Select the windows-debug or windows-release CMake preset. Default: debug.
  -Test                Run the matching CTest preset when available.
  -Rhi                 Run the native Direct3D 11 RHI acceptance test. Intended for Debug builds.
  -Clean               Remove the selected preset build directory before configuring.
  -Jobs N              Parallel build jobs. Default: CPU core count.
"@
}

if ($Help) {
    Show-Usage
    exit 0
}

if (-not $IsWindows -and $PSVersionTable.PSEdition -eq "Core") {
    throw "build_windows.ps1 must be run on Windows."
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

$Preset = "windows-$Mode"
$BuildDir = Join-Path $ScriptDir "build\windows-$Mode"

if ($Clean) {
    Write-Host "Removing $BuildDir"
    Remove-Item -LiteralPath $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Configuring preset: $Preset"
cmake --preset $Preset

Write-Host "Building preset: $Preset (-j $Jobs)"
cmake --build --preset $Preset --parallel $Jobs

if ($Test) {
    if ($Mode -eq "debug") {
        Write-Host "Running tests: windows-debug"
        ctest --preset windows-debug --output-on-failure
    } else {
        Write-Host "Release preset does not define a CTest preset; skipping -Test for release."
    }
}

if ($Rhi) {
    Write-Host "Running native Direct3D 11 RHI acceptance tests"
    ctest --preset windows-rhi-acceptance --output-on-failure
}

if ($Mode -eq "debug") {
    Write-Host "Executable directory: $ScriptDir\build\windows-debug\src\Debug"
} else {
    Write-Host "Executable directory: $ScriptDir\build\windows-release\src\Release"
}
