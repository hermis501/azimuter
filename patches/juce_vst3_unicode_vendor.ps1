# ============================================================================
# juce_vst3_unicode_vendor.ps1
#
# Patches <JUCE>\modules\juce_audio_plugin_client\VST3\juce_VST3ModuleInfo.h
# so that the Azimuter plugin's company name "Hermis Kasperavicius" (with c-caron)
# round-trips correctly into the VST3 Unicode class info (PClassInfoW). The stock
# JUCE code calls PClassInfoW::fromAscii which widens each UTF-8 byte 1:1, so the
# c-caron's UTF-8 bytes (0xC4 0x8D) become two broken UTF-16 code units. Reaper
# and other hosts that read the Unicode path display mojibake until this is fixed.
#
# Idempotent: re-running on an already-patched file is a no-op.
# Errors out if the stock anchor text is missing (means JUCE source has changed
# upstream and the patch needs manual review).
#
# Invoked via apply_juce_patches.bat. Can also be run directly:
#   powershell -ExecutionPolicy Bypass -File juce_vst3_unicode_vendor.ps1 [target.h]
# ============================================================================

param(
    [string]$TargetFile = ""
)

$ErrorActionPreference = 'Stop'

# Resolve the JUCE tree the same way CMakeLists.txt does: JUCE_SDK_DIR env
# override first, then the known install locations, first hit wins. Keep this
# list in sync with azimuter_resolve_sdk() in CMakeLists.txt.
$relativePath = "modules\juce_audio_plugin_client\VST3\juce_VST3ModuleInfo.h"
$searchRoots = @()
if ($env:JUCE_SDK_DIR) { $searchRoots += $env:JUCE_SDK_DIR }
$searchRoots += "C:\_tools\JUCE", "C:\sdk\JUCE", "C:\sdk\JUCE-8.0.4"

if ([string]::IsNullOrWhiteSpace($TargetFile)) {
    foreach ($root in $searchRoots) {
        $candidate = Join-Path $root $relativePath
        if (Test-Path $candidate) { $TargetFile = $candidate; break }
    }
}

if ([string]::IsNullOrWhiteSpace($TargetFile) -or -not (Test-Path $TargetFile)) {
    Write-Host "ERROR: juce_VST3ModuleInfo.h not found." -ForegroundColor Red
    Write-Host "  Searched: $($searchRoots -join ', ')"
    Write-Host ""
    Write-Host "Either JUCE isn't installed in one of those places, or its layout changed."
    Write-Host "Pass the file explicitly:"
    Write-Host "  juce_vst3_unicode_vendor.ps1 <path-to-juce_VST3ModuleInfo.h>"
    exit 1
}

Write-Host "Target: $TargetFile"

$content = Get-Content $TargetFile -Raw -Encoding UTF8

# --- Idempotency check ------------------------------------------------------
if ($content -match 'juceAzimuterUtf8ToUtf16') {
    Write-Host "Already patched. No changes needed." -ForegroundColor Green
    exit 0
}

# --- Anchor 1: helper function insertion point ------------------------------
$anchor1 = @'
[[maybe_unused]] static std::vector<VST3Interface::Id> getAllVST3CompatibleClasses()
{
    return
    {
       #if JUCE_VST3_CAN_REPLACE_VST2
        getVST3InterfaceId (VST3Interface::Type::component),
       #endif

       #ifdef JUCE_VST3_COMPATIBLE_CLASSES
        JUCE_VST3_COMPATIBLE_CLASSES
       #endif
    };
}
'@

$replacement1 = @'
[[maybe_unused]] static std::vector<VST3Interface::Id> getAllVST3CompatibleClasses()
{
    return
    {
       #if JUCE_VST3_CAN_REPLACE_VST2
        getVST3InterfaceId (VST3Interface::Type::component),
       #endif

       #ifdef JUCE_VST3_COMPATIBLE_CLASSES
        JUCE_VST3_COMPATIBLE_CLASSES
       #endif
    };
}

//==============================================================================
// Azimuter patch: PClassInfoW::fromAscii widens each UTF-8 byte 1:1, so non-ASCII
// vendor names (e.g. "Hermis Kasperavicius") arrive mojibake'd in hosts that read
// the Unicode class info path (Reaper, etc.). This helper properly UTF-8 decodes
// into UTF-16 with BMP + surrogate-pair handling. Replays after fromAscii to keep
// the non-text fields (cid, cardinality, category, subCategories) unchanged.
[[maybe_unused]] static void juceAzimuterUtf8ToUtf16 (const char* src,
                                                      Steinberg::char16* dst,
                                                      size_t dstCapacity) noexcept
{
    if (dst == nullptr || dstCapacity == 0)
        return;

    size_t out = 0;
    while (out + 1 < dstCapacity && *src != 0)
    {
        const unsigned char b0 = (unsigned char) *src++;
        uint32_t cp = 0;
        int extra = 0;

        if      (b0 < 0x80)           { cp = b0;        extra = 0; }
        else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; extra = 1; }
        else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; extra = 2; }
        else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; extra = 3; }
        else                          { cp = 0xFFFD;    extra = 0; }

        for (int i = 0; i < extra; ++i)
        {
            const unsigned char b = (unsigned char) *src;
            if ((b & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            ++src;
            cp = (cp << 6) | (b & 0x3F);
        }

        if (cp < 0x10000)
        {
            dst[out++] = (Steinberg::char16) cp;
        }
        else if (out + 2 < dstCapacity)
        {
            cp -= 0x10000;
            dst[out++] = (Steinberg::char16) (0xD800 | (cp >> 10));
            dst[out++] = (Steinberg::char16) (0xDC00 | (cp & 0x3FF));
        }
        else
        {
            break;
        }
    }
    dst[out] = 0;
}
'@

# --- Anchor 2: call-site insertion point ------------------------------------
$anchor2 = @'
                     JucePlugin_Manufacturer,
                     JucePlugin_VersionString,
                     kVstVersionString)
        {
            infoW.fromAscii (info2);
        }
'@

$replacement2 = @'
                     JucePlugin_Manufacturer,
                     JucePlugin_VersionString,
                     kVstVersionString)
        {
            infoW.fromAscii (info2);
            // Azimuter patch: re-encode wide-string fields with proper UTF-8 decoding;
            // see juceAzimuterUtf8ToUtf16 above for rationale.
            juceAzimuterUtf8ToUtf16 (info2.name,       infoW.name,       std::size (infoW.name));
            juceAzimuterUtf8ToUtf16 (info2.vendor,     infoW.vendor,     std::size (infoW.vendor));
            juceAzimuterUtf8ToUtf16 (info2.version,    infoW.version,    std::size (infoW.version));
            juceAzimuterUtf8ToUtf16 (info2.sdkVersion, infoW.sdkVersion, std::size (infoW.sdkVersion));
        }
'@

# --- Apply ------------------------------------------------------------------
# Normalise line endings inside the haystack for matching; preserve in output.
# Get-Content -Raw on Windows preserves CRLF as-is; here-strings in PS are LF.
# So we have to compare in a line-ending-agnostic way and rewrite identically.
$anchor1Lf = $anchor1 -replace "`r`n", "`n"
$anchor2Lf = $anchor2 -replace "`r`n", "`n"
$replacement1Lf = $replacement1 -replace "`r`n", "`n"
$replacement2Lf = $replacement2 -replace "`r`n", "`n"

$contentLf = $content -replace "`r`n", "`n"
$hadCrlf = ($content -ne $contentLf)

if (-not $contentLf.Contains($anchor1Lf)) {
    Write-Host "ERROR: Anchor 1 (helper insertion point) not found." -ForegroundColor Red
    Write-Host "JUCE's getAllVST3CompatibleClasses() block has changed shape."
    Write-Host "Patch needs manual review against the current JUCE source."
    exit 1
}
if (-not $contentLf.Contains($anchor2Lf)) {
    Write-Host "ERROR: Anchor 2 (fromAscii call site) not found." -ForegroundColor Red
    Write-Host "JUCE's ClassEntry constructor has changed shape."
    Write-Host "Patch needs manual review against the current JUCE source."
    exit 1
}

$patched = $contentLf.Replace($anchor1Lf, $replacement1Lf).Replace($anchor2Lf, $replacement2Lf)
if ($hadCrlf) {
    $patched = $patched -replace "`n", "`r`n"
}

# Write as UTF-8 without BOM (matches stock JUCE files).
[System.IO.File]::WriteAllText($TargetFile, $patched, (New-Object System.Text.UTF8Encoding $false))

Write-Host "Patched successfully:" -ForegroundColor Green
Write-Host "  $TargetFile"
Write-Host ""
Write-Host "Now do a clean rebuild to pick up the change:"
Write-Host "  rmdir /s /q build"
Write-Host "  build_release.bat"
exit 0
