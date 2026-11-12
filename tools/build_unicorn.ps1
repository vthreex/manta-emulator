param([Parameter(Mandatory=$true)][string]$DependencyRoot)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content (Join-Path $projectRoot 'third_party/source-manifest.json') -Raw | ConvertFrom-Json
$archive = Join-Path $projectRoot ('third_party/' + $manifest.engine_source_archive)
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.engine_source_sha256) { throw 'Engine source checksum mismatch' }
$sourceParent = Join-Path $DependencyRoot 'engine-source'
$engineBuild = Join-Path $DependencyRoot 'engine-build'
$enginePrefix = Join-Path $DependencyRoot 'engine'
if (Test-Path -LiteralPath (Join-Path $enginePrefix 'lib/libunicorn.dll.a')) { Write-Output $enginePrefix; return }   # already built (e.g. restored from the CI cache); delete the folder to rebuild
# This archive is checksum verified and provided alongside the modified binary.
python -m zipfile -e $archive $sourceParent
if ($LASTEXITCODE -ne 0) { throw 'Engine extraction failed' }
$sourceRoot = Join-Path $sourceParent 'unicorn-2.1.4-manta'
$cmakePath = Join-Path $DependencyRoot 'cmake/data/bin/cmake.exe'
$ninjaPath = Join-Path $DependencyRoot 'bin/ninja.exe'
$compilerC = (Get-Command gcc).Source
$compilerCxx = (Get-Command g++).Source
$previousPkgConfig = $env:PKG_CONFIG
try {
 $env:PKG_CONFIG = 'false' # Use the engine's bundled GLib compatibility library.
 & $cmakePath -S $sourceRoot -B $engineBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninjaPath" "-DCMAKE_C_COMPILER=$compilerC" "-DCMAKE_CXX_COMPILER=$compilerCxx" -DUNICORN_ARCH=mips -DUNICORN_BUILD_TESTS=OFF -DUNICORN_BUILD_SAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
 if ($LASTEXITCODE -ne 0) { throw 'Engine configuration failed' }
 # The shared target avoids an upstream Windows static-archive symlink step.
 & $cmakePath --build $engineBuild --target unicorn --parallel 4
 if ($LASTEXITCODE -ne 0) { throw 'Engine compilation failed' }
} finally { $env:PKG_CONFIG = $previousPkgConfig }
New-Item -ItemType Directory -Force (Join-Path $enginePrefix 'lib') | Out-Null
Copy-Item -Recurse -Force (Join-Path $sourceRoot 'include') $enginePrefix
Copy-Item (Join-Path $engineBuild 'libunicorn.dll') (Join-Path $enginePrefix 'lib/libunicorn.dll') -Force
Copy-Item (Join-Path $engineBuild 'libunicorn.dll.a') (Join-Path $enginePrefix 'lib/libunicorn.dll.a') -Force
foreach ($dllName in @('libwinpthread-1.dll', 'libgcc_s_seh-1.dll')) {
 $dllPath = (& $compilerCxx "-print-file-name=$dllName").Trim()
 if (!(Test-Path -LiteralPath $dllPath)) { throw "Missing compiler runtime: $dllName" }
 Copy-Item -LiteralPath $dllPath -Destination (Join-Path $enginePrefix 'lib') -Force
}
Write-Output $enginePrefix
