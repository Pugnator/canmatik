@echo off
:: uninstall_fake_j2534.bat — Remove the fake J2534 provider from the Windows Registry.
:: Must be run as Administrator.

setlocal

set "REGKEY=HKLM\SOFTWARE\PassThruSupport.04.04\CANmatik Fake J2534"

echo.
echo === CANmatik Fake J2534 — Registry Uninstaller ===
echo.

reg delete "%REGKEY%" /f >nul 2>&1

if %ERRORLEVEL% neq 0 (
    echo Key not found or access denied. Run as Administrator.
    exit /b 1
)

echo SUCCESS: Fake J2534 provider removed from registry.
echo.
