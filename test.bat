@echo off
REM Copyright 2026 Mario Vinciguerra
REM SPDX-License-Identifier: Apache-2.0

setlocal
set "ROOT=%~dp0"
python "%ROOT%tests\check-source.py"
if errorlevel 1 exit /b %ERRORLEVEL%
if exist "%ROOT%target\bootstrap\host\release\CTestTestfile.cmake" (
  ctest --test-dir "%ROOT%target\bootstrap\host\release" --output-on-failure %*
  exit /b %ERRORLEVEL%
)
echo Static repository checks passed.
echo No configured native test tree was found; run your chosen bootstrap/build before CTest.
exit /b 0
