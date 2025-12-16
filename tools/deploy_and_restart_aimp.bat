@echo off
setlocal

rem Args:
rem   %1 - path to built plugin dll
rem   %2 - AIMP install root (defaults to "C:\Program Files\AIMP" or "C:\Program Files (x86)\AIMP")
rem   %3 - arch label ("x64" or "x86")

set "BUILD_DLL=%~1"
set "AIMP_ROOT=%~2"
set "ARCH=%~3"

rem Normalize slashes to backslashes so copy works with CMake-style paths
set "BUILD_DLL=%BUILD_DLL:/=\%"
set "AIMP_ROOT=%AIMP_ROOT:/=\%"

if "%ARCH%"=="" set "ARCH=x64"

if "%AIMP_ROOT%"=="" (
    if /I "%ARCH%"=="x64" (
        set "AIMP_ROOT=C:\Program Files\AIMP"
    ) else (
        set "AIMP_ROOT=C:\Program Files (x86)\AIMP"
    )
)

set "AIMP_EXE=%AIMP_ROOT%\AIMP.exe"
set "AIMP_PLUGIN_DIR=%AIMP_ROOT%\Plugins\aimp_rx2_plugin"
set "AIMP_PLUGIN_DLL=%AIMP_PLUGIN_DIR%\aimp_rx2_plugin.dll"

if not exist "%BUILD_DLL%" (
    echo [AIMP RX2] ERROR: built dll not found: %BUILD_DLL%
    exit /b 1
)

echo [AIMP RX2] Target AIMP root: "%AIMP_ROOT%" (arch=%ARCH%)

if not exist "%AIMP_PLUGIN_DIR%" (
    echo [AIMP RX2] Creating plugin folder "%AIMP_PLUGIN_DIR%" ...
    mkdir "%AIMP_PLUGIN_DIR%" >nul 2>&1
)

echo [AIMP RX2] Killing AIMP if running...
taskkill /IM AIMP.exe /F >nul 2>&1

echo [AIMP RX2] Copying "%BUILD_DLL%" to "%AIMP_PLUGIN_DLL%" ...
copy /Y "%BUILD_DLL%" "%AIMP_PLUGIN_DLL%" >nul

echo [AIMP RX2] Starting AIMP...
start "" "%AIMP_EXE%"

endlocal
