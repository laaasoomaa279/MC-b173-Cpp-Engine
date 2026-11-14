$ErrorActionPreference = "Stop"
Write-Host "Building Minecraft Beta 1.7.3 (C++20 Bare-Metal Engine)..." -ForegroundColor Cyan

if (!(Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()

if (!(Test-Path "build/miniz.o")) {
    Write-Host "  [1/3] Compiling vendor/miniz.c..." -ForegroundColor DarkGray
    clang -O3 -c vendor/miniz.c -o build/miniz.o
}
if (!(Test-Path "build/stb.o")) {
    Write-Host "  [2/3] Compiling vendor/stb.cpp..." -ForegroundColor DarkGray
    clang++ -O3 -c vendor/stb.cpp -Ivendor -o build/stb.o
}

Write-Host "  [3/3] Compiling src/main.cpp..." -ForegroundColor Gray
clang++ -std=c++20 -O3 -pthread `
    -Isrc -Ivendor `
    src/main.cpp build/miniz.o build/stb.o `
    -lopengl32 -lgdi32 -luser32 -lwinmm -lws2_32 `
    "-Wl,--stack,16777216" `
    -o build/minecraft.exe

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed (Exit Code: $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

$sw.Stop()
Write-Host ("Build Succeeded in {0:N2}s! Run ./build/minecraft.exe" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green