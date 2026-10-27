@echo off
REM ============================================================================
REM build_release.bat - Shippable Release build for Azimuter VST3.
REM
REM Builds the Release config (full optimisation, no debug symbols) and runs
REM the test suite against it as a final sanity check. The shippable VST3
REM bundle is at:
REM   build\Azimuter_artefacts\Release\VST3\Azimuter.vst3\
REM
REM Inside that bundle, only two files are needed to distribute:
REM   Contents\x86_64-win\Azimuter.vst3   (the actual VST3 DLL)
REM   Contents\Resources\moduleinfo.json
REM
REM Zip the whole Azimuter.vst3 folder and ship that.
REM
REM For day-to-day development, use build.bat instead - this is for cutting
REM a real release.
REM ============================================================================

setlocal
pushd "%~dp0"

set "BUILD_DIR=build"
set "CONFIG=Release"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [1/3] Configuring CMake ^(first run^)...
    cmake -S . -B "%BUILD_DIR%"
    if errorlevel 1 goto :fail
) else (
    echo [1/3] CMake already configured, skipping.
)

echo.
echo [2/3] Building %CONFIG%...
cmake --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 goto :fail

echo.
echo [3/3] Running tests against %CONFIG% binaries...
ctest --test-dir "%BUILD_DIR%" --output-on-failure -C %CONFIG%
if errorlevel 1 goto :fail

echo.
echo ============================================================================
echo RELEASE BUILD OK. Shippable VST3 bundle at:
echo   %CD%\%BUILD_DIR%\Azimuter_artefacts\%CONFIG%\VST3\Azimuter.vst3
echo.
echo Zip the entire Azimuter.vst3 folder ^(not just the inner DLL^) to ship.
echo ============================================================================
popd
endlocal
pause
exit /b 0

:fail
echo.
echo ============================================================================
echo RELEASE BUILD FAILED. See errors above.
echo ============================================================================
popd
endlocal
pause
exit /b 1
