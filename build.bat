@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 Davidian
rem Builds build\duo57.lv2 (Windows, MSVC). Run from any prompt; sets up the
rem Visual Studio environment automatically if cl.exe is not on PATH.
rem   build.bat        plugin + GUI
rem   build.bat test   also runs the DSP test, the GUI host test and renders build\panel.bmp
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if not errorlevel 1 goto :have_cl
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:have_cl

if not exist build\duo57.lv2 mkdir build\duo57.lv2

rem /MT links the C runtime statically, so hosts don't need a redistributable installed.
cl /nologo /O2 /fp:fast /MT /W3 /LD src\duo57.c /Fobuild\ /Fe:build\duo57.lv2\duo57.dll /link /NOIMPLIB /NOEXP || exit /b 1

rem The GUI: a Win32 window drawn by its own software renderer. /MT so hosts need no redistributable.
cl /nologo /std:c11 /O2 /MT /W4 /LD src\ui\ui_lv2.c /Fobuild\ /Fe:build\duo57.lv2\duo57_ui.dll /link /NOIMPLIB /NOEXP user32.lib gdi32.lib || exit /b 1

copy /y duo57.lv2\duo57.ttl build\duo57.lv2\ >nul
copy /y LICENSE build\duo57.lv2\ >nul
copy /y LICENSE-LV2 build\duo57.lv2\ >nul
powershell -NoProfile -Command "(Get-Content duo57.lv2\manifest.ttl.in) -replace '@LIB_EXT@','.dll' -replace '@UI_CLASS@','WindowsUI' | Set-Content -Encoding ascii build\duo57.lv2\manifest.ttl"

if "%1"=="test" (
  cl /nologo /O2 /fp:fast /W3 test\render.c /Fobuild\ /Fe:build\render.exe || exit /b 1
  cl /nologo /std:c11 /O2 /W3 test\ui_render.c /Fobuild\ /Fe:build\ui_render.exe || exit /b 1
  cl /nologo /std:c11 /O2 /W3 test\ui_check.c /Fobuild\ /Fe:build\ui_check.exe /link user32.lib gdi32.lib || exit /b 1
  pushd build
  "%~dp0build\render.exe" || (popd & exit /b 1)
  "%~dp0build\ui_render.exe" panel.bmp || (popd & exit /b 1)
  "%~dp0build\ui_check.exe" duo57.lv2 window.bmp || (popd & exit /b 1)
  popd
)
echo Built build\duo57.lv2
