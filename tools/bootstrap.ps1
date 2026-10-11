param([string]$BuildDirectory = 'build')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot $BuildDirectory
$dependencyRoot = Join-Path $buildRoot '.deps'
New-Item -ItemType Directory -Force -Path $dependencyRoot | Out-Null
python -m pip install --target $dependencyRoot unicorn==2.1.4 capstone==5.0.6 cmake==4.4.4 ninja==1.13.2
if ($LASTEXITCODE -ne 0) { throw 'Dependency install failed' }
$cmakePath = Join-Path $dependencyRoot 'cmake/data/bin/cmake.exe'
$ninjaPath = Join-Path $dependencyRoot 'bin/ninja.exe'
$unicornRoot = Join-Path $dependencyRoot 'unicorn'
$importLibrary = Join-Path $unicornRoot 'lib/libunicorn.dll.a'
# The wheel's .lib is an MSVC static library. MinGW uses a DLL import library.
dlltool -d (Join-Path $PSScriptRoot 'unicorn.def') -D unicorn.dll -l $importLibrary
if ($LASTEXITCODE -ne 0) { throw 'dlltool failed; install MinGW-w64' }
& $cmakePath -S $projectRoot -B $buildRoot -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninjaPath" "-DUNICORN_ROOT=$unicornRoot" "-DUNICORN_LIBRARY=$importLibrary" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $cmakePath --build $buildRoot
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& (Join-Path $dependencyRoot 'cmake/data/bin/ctest.exe') --test-dir $buildRoot --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
