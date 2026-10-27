@echo off
REM ============================================================================
REM build_installer.bat - Compile the Windows installer for Azimuter.
REM
REM Wraps Inno Setup's ISCC.exe to turn the shipped VST3 bundle into a
REM single double-clickable Setup .exe that end users can run.
REM
REM Expected workflow:
REM   1. Run build_release.bat   (produces the VST3 bundle)
REM   2. Run build_installer.bat (produces dist\Azimuter-x.y.z-Setup.exe)
REM
REM This script does NOT rebuild the plugin. If you change source code,
REM rerun build_release.bat first.
REM
REM Prerequisite: Inno Setup 6+ must be installed (separate one-time download,
REM not part of the CMake/VS toolchain).
REM ============================================================================

setlocal
pushd "%~dp0"

set "BUNDLE_DIR=build\Azimuter_artefacts\Release\VST3\Azimuter.vst3"
set "ISS_FILE=Azimuter.iss"

REM --- Locate Inno Setup's ISCC.exe -------------------------------------------
set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" goto :no_iscc

REM --- Verify the Release VST3 bundle exists ----------------------------------
if not exist "%BUNDLE_DIR%\Contents\Resources\moduleinfo.json" goto :no_bundle

REM --- Compile the installer --------------------------------------------------
echo [1/1] Compiling installer with Inno Setup...
echo       ISCC: %ISCC%
echo       Project: %ISS_FILE%
echo.
"%ISCC%" "%ISS_FILE%"
if errorlevel 1 goto :fail

echo.
echo ============================================================================
echo INSTALLER OK. Output:
for %%F in ("dist\*.exe") do echo   %CD%\%%F
echo.
echo Give that .exe to users to install Azimuter.
echo ============================================================================
popd
endlocal
pause
exit /b 0

:no_iscc
echo.
echo ============================================================================
echo ERROR: Inno Setup compiler (ISCC.exe) not found.
echo.
echo Expected at one of:
echo   C:\Program Files (x86)\Inno Setup 6\ISCC.exe
echo   C:\Program Files\Inno Setup 6\ISCC.exe
echo.
echo Install Inno Setup 6+ first, then re-run this script.
echo ============================================================================
popd
endlocal
pause
exit /b 1

:no_bundle
echo.
echo ============================================================================
echo ERROR: Release VST3 bundle not found at:
echo   %CD%\%BUNDLE_DIR%
echo.
echo Run build_release.bat first to produce the bundle, then re-run this script.
echo ============================================================================
popd
endlocal
pause
exit /b 1

:fail
echo.
echo ============================================================================
echo INSTALLER BUILD FAILED. See errors above.
echo ============================================================================
popd
endlocal
pause
exit /b 1
