$ErrorActionPreference = "Stop"

# Locate WinLibs mingw64\bin if not already on PATH
$mingwBin = $null
$gppCmd = Get-Command "g++" -ErrorAction SilentlyContinue
if ($gppCmd) {
    $mingwBin = Split-Path -Parent $gppCmd.Source
} else {
    $mingwBin = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Filter "*WinLibs*" -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName "mingw64\bin" } |
        Where-Object { Test-Path (Join-Path $_ "g++.exe") } |
        Select-Object -First 1
    if ($mingwBin) {
        $env:PATH = "$mingwBin;$env:PATH"
    }
}

if (-not $mingwBin) {
    Write-Error "g++ not found. Please install MinGW-w64 / WinLibs and ensure g++ is on PATH."
    exit 1
}

$mingwRoot = Split-Path -Parent $mingwBin
$targetInc = Get-ChildItem (Join-Path $mingwRoot "include\c++") -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName "x86_64-w64-mingw32" } |
    Where-Object { Test-Path $_ } |
    Select-Object -First 1

$backendDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $backendDir "build"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$mysqlRoot = "C:\Program Files\MySQL\MySQL Server 8.0"
$mysqlInclude = Join-Path $mysqlRoot "include"
$mysqlDll = Join-Path $mysqlRoot "lib\libmysql.dll"

if (-not (Test-Path (Join-Path $mysqlInclude "mysql.h"))) {
    Write-Error "MySQL header mysql.h not found at $mysqlInclude"
    exit 1
}

$extraFlags = @()
if ($targetInc) {
    $extraFlags += "-isystem"
    $extraFlags += $targetInc
}

Write-Host "Compiling DSA & Auth unit tests (test_dsa.exe)..." -ForegroundColor Cyan
& (Join-Path $mingwBin "g++.exe") -std=c++17 -O2 -Wall -Wextra @extraFlags `
    -static -static-libgcc -static-libstdc++ `
    -I (Join-Path $backendDir "include") `
    (Join-Path $backendDir "tests\test_dsa.cpp") `
    -o (Join-Path $buildDir "test_dsa.exe")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Compiling Smart Parking C++ Crow Server (smart_parking_server.exe)..." -ForegroundColor Cyan
& (Join-Path $mingwBin "g++.exe") -std=c++17 -O2 -D_WIN32_WINNT=0x0A00 @extraFlags `
    -static-libgcc -static-libstdc++ `
    -I (Join-Path $backendDir "include") `
    -I (Join-Path $backendDir "third_party\crow") `
    -I (Join-Path $backendDir "third_party\asio") `
    -I $mysqlInclude `
    (Join-Path $backendDir "src\main.cpp") `
    $mysqlDll `
    "-Wl,-Bstatic" -lpthread "-Wl,-Bdynamic" -lws2_32 -lmswsock `
    -o (Join-Path $buildDir "smart_parking_server.exe")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Copy-Item $mysqlDll (Join-Path $buildDir "libmysql.dll") -Force
foreach ($extraDll in @(
    (Join-Path $mysqlRoot "bin\libssl-3-x64.dll"),
    (Join-Path $mysqlRoot "bin\libcrypto-3-x64.dll"),
    (Join-Path $mingwBin "libwinpthread-1.dll")
)) {
    if (Test-Path $extraDll) {
        Copy-Item $extraDll $buildDir -Force
    }
}

Write-Host "`nBuild succeeded! Executables created in: $buildDir" -ForegroundColor Green
try {
    & (Join-Path $buildDir "test_dsa.exe")
} catch {
    Write-Warning "Compiled successfully, but Windows Application Control blocked running unsigned test_dsa.exe directly: $($_.Exception.Message)"
}
