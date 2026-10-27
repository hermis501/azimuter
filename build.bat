@echo off
REM ============================================================================
REM build.bat - Development build for Azimuter VST3.
REM
REM Builds the RelWithDebInfo config (optimised, with debug symbols) and runs
REM the test suite. Use this while iterating on the source. The resulting VST3
REM bundle is at:
REM   build\Azimuter_artefacts\RelWithDebInfo\VST3\Azimuter.vst3\
REM
REM For a shippable Release build, use build_release.bat instead.
REM ============================================================================

setlocal
pushd "%~dp0"

set "BUILD_DIR=build"
set "CONFIG=RelWithDebInfo"

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
echo [3/3] Running tests...
ctest --test-dir "%BUILD_DIR%" --output-on-failure -C %CONFIG%
if errorlevel 1 goto :fail

echo.
echo ============================================================================
echo BUILD OK. VST3 bundle at:
echo   %CD%\%BUILD_DIR%\Azimuter_artefacts\%CONFIG%\VST3\Azimuter.vst3
echo ============================================================================
popd
endlocal
pause
exit /b 0

:fail
echo.
echo ============================================================================
echo BUILD FAILED. See errors above.
echo ============================================================================
popd
endlocal
pause
exit /b 1
