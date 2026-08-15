@echo off
REM Copyright 2026 Mario Vinciguerra
REM SPDX-License-Identifier: Apache-2.0

setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\release.ps1" %*
exit /b %ERRORLEVEL%
