param(
    [ValidateSet("debug", "release")]
    [string]$Mode = "debug",

    [ValidateSet("msys2", "msvc")]
    [string]$Toolchain = "msys2",

    [string]$Msys2Ucrt64 = $env:MSYS2_UCRT64,

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
  .\build_windows.ps1 [-Toolchain msys2|msvc] [-Mode debug|release] [-Test] [-Rhi] [-Clean] [-Jobs N]

Examples:
  .\build_windows.ps1 -Toolchain msys2 -Mode debug -Test -Rhi
  .\build_windows.ps1 -Toolchain msys2 -Mode release -Jobs 8
  .\build_windows.ps1 -Toolchain msvc -Mode debug -Test

Options:
  -Toolchain msys2|msvc  Use MSYS2/UCRT64 Ninja (default) or the legacy Visual Studio preset.
  -Mode debug|release  Select the build configuration. Default: debug.
  -Msys2Ucrt64 PATH     MSYS2 UCRT64 prefix; defaults to MSYS2_UCRT64 or qmake discovery.
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

if ($Toolchain -eq "msys2") {
    if ([string]::IsNullOrWhiteSpace($Msys2Ucrt64)) {
        $qmake = Get-Command qmake -ErrorAction SilentlyContinue
        if ($null -ne $qmake) {
            $Msys2Ucrt64 = (& $qmake.Source -query QT_INSTALL_PREFIX).Trim()
        }
    }
    if ([string]::IsNullOrWhiteSpace($Msys2Ucrt64) -or
        -not (Test-Path -LiteralPath (Join-Path $Msys2Ucrt64 "bin\qmake.exe"))) {
        throw "MSYS2 UCRT64 Qt was not found. Set MSYS2_UCRT64 to the UCRT64 prefix or put qmake on PATH."
    }

    $Msys2Ucrt64 = (Resolve-Path -LiteralPath $Msys2Ucrt64).Path
    $env:Path = "$(Join-Path $Msys2Ucrt64 'bin');$env:Path"
    $BuildDir = Join-Path $ScriptDir "build\windows-msys2-$Mode"
    if ($Clean) {
        Write-Host "Removing $BuildDir"
        Remove-Item -LiteralPath $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
    }

    $configureArgs = @(
        "-S", $ScriptDir,
        "-B", $BuildDir,
        "-G", "Ninja",
        "-DCMAKE_PREFIX_PATH=$Msys2Ucrt64",
        "-DCMAKE_BUILD_TYPE=$Mode"
    )
    if ($Mode -eq "debug") {
        $configureArgs += "-DBUILD_TESTING=ON"
    } else {
        $configureArgs += "-DBUILD_TESTING=OFF"
        $configureArgs += "-DISPVIEW_BUILD_BENCHMARKS=ON"
    }

    Write-Host "Configuring MSYS2/UCRT64 Ninja build: $BuildDir"
    cmake @configureArgs
    Write-Host "Building MSYS2/UCRT64 Ninja build (-j $Jobs)"
    cmake --build $BuildDir --parallel $Jobs

    if ($Test) {
        if ($Mode -eq "debug") {
            Write-Host "Running CTest: $BuildDir"
            ctest --test-dir $BuildDir --output-on-failure
        } else {
            Write-Host "Release build does not enable CTest; skipping -Test."
        }
    }

    if ($Rhi) {
        if ($Mode -ne "debug") {
            throw "-Rhi requires -Mode debug."
        }
        $previousRhiRequirement = $env:ISPVIEW_REQUIRE_NATIVE_RHI_TESTS
        $env:ISPVIEW_REQUIRE_NATIVE_RHI_TESTS = "1"
        try {
            Write-Host "Running native Direct3D 11 RHI acceptance test"
            ctest --test-dir $BuildDir -R "^ispview_render_tests$" --output-on-failure
        } finally {
            $env:ISPVIEW_REQUIRE_NATIVE_RHI_TESTS = $previousRhiRequirement
        }
    }

    Write-Host "Executable: $BuildDir\ISPImageViewer.exe"
    exit 0
}

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
