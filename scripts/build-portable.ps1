param(
    [string]$BuildDir = "build-portable",
    [string]$Configuration = "Release",
    [string]$QtDeployTool = "windeployqt"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$build = Join-Path $root $BuildDir
$stage = Join-Path $build "stage"
if (-not $env:VCPKG_ROOT) { throw "Set VCPKG_ROOT to your vcpkg checkout." }
if (-not $env:Qt6_DIR) { throw "Set Qt6_DIR to the Qt 6 MSVC CMake package directory." }
$toolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $toolchain)) { throw "vcpkg toolchain not found: $toolchain" }
cmake -S $root -B $build -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DCMAKE_INSTALL_PREFIX=$stage" `
    "-DCMAKE_INSTALL_BINDIR=." `
    -DENABLE_QT=ON -DENABLE_GTK=OFF -DENABLE_MAC=OFF -DUSE_QT_VERSION=6 -DENABLE_TESTS=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
cmake --build $build --config $Configuration --target transmission-qt
if ($LASTEXITCODE -ne 0) { throw "Build failed." }
cmake --install $build --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Install failed." }
$exe = Join-Path $stage "transmission-qt.exe"
if (-not (Test-Path $exe)) { throw "Expected executable not found: $exe" }
& $QtDeployTool --release --compiler-runtime --dir $stage $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed." }
New-Item -ItemType File -Force (Join-Path $stage "portable.dat") | Out-Null
Write-Host "Portable stage ready: $stage"
