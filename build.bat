@echo off
REM Copyright 2026 Mario Vinciguerra
REM SPDX-License-Identifier: Apache-2.0

setlocal
set "ROOT=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\build-project.ps1" -Project "%ROOT%compiler" -Output "%ROOT%target\compiler\razc.exe" %*
exit /b %ERRORLEVEL%
