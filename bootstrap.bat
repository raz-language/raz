@echo off
REM Copyright 2026 Mario Vinciguerra
REM SPDX-License-Identifier: Apache-2.0

setlocal
where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    py -3 "%~dp0tools\bootstrap.py" %*
) else (
    python "%~dp0tools\bootstrap.py" %*
)
exit /b %ERRORLEVEL%
