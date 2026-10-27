@echo off
REM ============================================================================
REM apply_juce_patches.bat - Re-apply proper vendor showing local patches to shared JUCE.
REM
REM Run this after upgrading OR MOVING the shared JUCE tree. It has no version
REM control, so overwriting it with a fresh download - or installing it somewhere
REM new - wipes any previously-applied patches. This script re-applies them.
REM
REM The JUCE tree is located the same way CMakeLists.txt locates it: the
REM JUCE_SDK_DIR environment variable first, then the known install locations
REM (currently C:\_tools\JUCE, then the older C:\sdk\JUCE paths), first hit
REM wins. Keep that list in sync with azimuter_resolve_sdk() in CMakeLists.txt.
REM
REM Currently applies:
REM   - juce_vst3_unicode_vendor.ps1
REM       Fixes PClassInfoW vendor mojibake for non-ASCII company names like
REM       "Hermis Kasperavicius" (c-caron). Without it, Reaper shows mojibake
REM       in the FX browser.
REM
REM Idempotent: re-running on an already-patched JUCE is a no-op.
REM ============================================================================

setlocal
pushd "%~dp0"

echo Applying JUCE patches...
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "juce_vst3_unicode_vendor.ps1"
if errorlevel 1 goto :fail

echo.
echo ============================================================================
echo ALL PATCHES APPLIED OK.
echo.
echo If you patched after a build, do a clean rebuild so the change takes effect:
echo   rmdir /s /q build
echo ============================================================================
popd
endlocal
pause
exit /b 0

:fail
echo.
echo ============================================================================
echo PATCH FAILED. See errors above.
echo ============================================================================
popd
endlocal
pause
exit /b 1
