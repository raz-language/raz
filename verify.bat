@echo off
REM Copyright 2026 Mario Vinciguerra
REM SPDX-License-Identifier: Apache-2.0

setlocal
set "ROOT=%~dp0"

where pwsh.exe >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set "POWERSHELL=pwsh.exe"
) else (
    set "POWERSHELL=powershell.exe"
)

"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\verify.ps1" %*
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" echo Verification failed with exit code %RC%.
if not "%RAZ_NO_PAUSE%"=="1" pause
exit /b %RC%
