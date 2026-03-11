@echo off
REM ===========================================================================
REM  CANmatik — 32-bit build script (MSYS2 MinGW32 toolchain)
REM ===========================================================================
REM  Requires MSYS2 with the i686 toolchain installed:
REM    pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
REM
REM  Usage:
REM    build32.bat              — Release build (default)
REM    build32.bat Debug        — Debug build
REM    build32.bat Release      — Release build (explicit)
REM    build32.bat clean        — Delete build directory
REM ===========================================================================

setlocal enabledelayedexpansion

REM --- Locate MSYS2 installation ---
set "MSYS2_ROOT="
if exist "C:\msys64" set "MSYS2_ROOT=C:\msys64"
if exist "D:\msys64" set "MSYS2_ROOT=D:\msys64"
if defined MSYS2_HOME set "MSYS2_ROOT=%MSYS2_HOME%"

if not defined MSYS2_ROOT (
    echo ERROR: MSYS2 installation not found.
    echo   Expected at C:\msys64 or D:\msys64.
    echo   Set MSYS2_HOME to your MSYS2 root directory.
    exit /b 1
)

set "MINGW32=%MSYS2_ROOT%\mingw32"
set "BUILD_DIR=build32"

REM --- Verify i686 toolchain is installed ---
if not exist "%MINGW32%\bin\gcc.exe" (
    echo ERROR: MinGW 32-bit toolchain not found at %MINGW32%\bin\gcc.exe
    echo   Install it with: pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
    exit /b 1
)

REM --- Handle arguments ---
set "BUILD_TYPE=Release"

if /i "%~1"=="clean" (
    echo Cleaning %BUILD_DIR%...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo Done.
    exit /b 0
)

if /i "%~1"=="Debug"   set "BUILD_TYPE=Debug"
if /i "%~1"=="Release" set "BUILD_TYPE=Release"

echo.
echo  CANmatik 32-bit build
echo  Build type : %BUILD_TYPE%
echo  Toolchain  : %MINGW32%
echo  Output dir : %BUILD_DIR%
echo.

REM --- Put MinGW32 at the front of PATH ---
set "PATH=%MINGW32%\bin;%MSYS2_ROOT%\usr\bin;%PATH%"

REM --- Configure ---
if not exist "%BUILD_DIR%\build.ninja" (
    echo --- Configuring ---
    cmake -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
        -DCMAKE_C_COMPILER="%MINGW32%\bin\gcc.exe" ^
        -DCMAKE_CXX_COMPILER="%MINGW32%\bin\g++.exe" ^
        -DCMAKE_MAKE_PROGRAM="%MINGW32%\bin\ninja.exe"
    if errorlevel 1 (
        echo ERROR: CMake configure failed.
        exit /b 1
    )
)

REM --- Build ---
echo --- Building ---
cmake --build "%BUILD_DIR%" -- -j%NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo  Build succeeded.
echo.

endlocal
