@echo off
:: install_fake_j2534.bat — Register the fake J2534 provider in the Windows Registry.
:: Must be run as Administrator.
::
:: Usage: install_fake_j2534.bat [path-to-dll]
::   Default DLL path: build32\fake_j2534.dll (relative to this script)

setlocal

set "REGKEY=HKLM\SOFTWARE\PassThruSupport.04.04\CANmatik Fake J2534"

:: Resolve DLL path
if "%~1"=="" (
    set "DLL_PATH=%~dp0build32\fake_j2534.dll"
) else (
    set "DLL_PATH=%~f1"
)

echo.
echo === CANmatik Fake J2534 — Registry Installer ===
echo.
echo Provider name : CANmatik Fake J2534
echo DLL path      : %DLL_PATH%
echo Registry key  : %REGKEY%
echo.

:: Check if DLL exists
if not exist "%DLL_PATH%" (
    echo ERROR: DLL not found at %DLL_PATH%
    echo Build the project first: cmake --build build32
    exit /b 1
)

:: Create registry entries
reg add "%REGKEY%" /v "Name"            /t REG_SZ    /d "CANmatik Fake J2534"   /f >nul
reg add "%REGKEY%" /v "Vendor"          /t REG_SZ    /d "CANmatik"              /f >nul
reg add "%REGKEY%" /v "FunctionLibrary" /t REG_SZ    /d "%DLL_PATH%"            /f >nul
reg add "%REGKEY%" /v "CAN"             /t REG_DWORD /d 1                       /f >nul
reg add "%REGKEY%" /v "ISO15765"        /t REG_DWORD /d 1                       /f >nul
reg add "%REGKEY%" /v "ConfigApplication" /t REG_SZ  /d ""                      /f >nul

if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to write registry. Run as Administrator.
    exit /b 1
)

echo.
echo SUCCESS: Fake J2534 provider registered.
echo.
echo Verify with:  canmatik scan
echo Uninstall:    uninstall_fake_j2534.bat
echo Logs:         %DLL_PATH:.dll=.log%
echo.
