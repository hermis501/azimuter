# =============================================================================
# fix_moduleinfo_vendor.cmake  -  post-build repair of the VST3 moduleinfo.json
#
# The built DLL carries the company name correctly: the /utf-8 compile option
# keeps the narrow literal as UTF-8, and juce_vst3_unicode_vendor.ps1 makes the
# PClassInfoW path decode it properly. But moduleinfo.json is written afterwards
# by a helper that loads the finished DLL, reads the vendor back and re-encodes
# it — and that round trip is where the č in "Hermis Kasperavičius" dies:
#
#     "Factory Info" -> "Vendor" :  Kasperavi<E8>ius          (Windows-1257 byte)
#     "Classes"      -> "Vendor" :  Kasperavi<EF BF BD>ius    (U+FFFD)
#
# The second form has already lost the character, so nothing can recover it from
# the file itself — the correct bytes have to be substituted back in. Hosts read
# moduleinfo.json during scanning precisely so they don't have to load the DLL,
# and Azimuter.iss ships it inside the bundle, so leaving it broken means the
# mojibake the patch exists to prevent shows up anyway.
#
# Fixing it here rather than patching JUCE means this survives every JUCE
# upgrade and every move of the shared SDK tree.
#
# Invoked from CMakeLists.txt as a POST_BUILD step:
#
#   cmake -DMODULEINFO=<path> -DVENDOR_HEX=<hex> -P fix_moduleinfo_vendor.cmake
#
# VENDOR_HEX is the UTF-8 vendor string hex-encoded by string(HEX) at configure
# time. It is deliberately hex: passing the raw name on a command line would run
# it through the same MSBuild response-file encoding that mangles it in the first
# place. Hex is pure ASCII, so nothing can touch it in transit.
# =============================================================================

if(NOT DEFINED MODULEINFO OR NOT DEFINED VENDOR_HEX)
    message(FATAL_ERROR "fix_moduleinfo_vendor: MODULEINFO and VENDOR_HEX are both required")
endif()

if(NOT EXISTS "${MODULEINFO}")
    # Not every JUCE version or format emits one; that is not an error.
    message(STATUS "Azimuter: no moduleinfo.json at ${MODULEINFO}, nothing to repair")
    return()
endif()

# --- hex -> bytes ------------------------------------------------------------
string(LENGTH "${VENDOR_HEX}" _hexLength)
if(_hexLength LESS 2)
    message(FATAL_ERROR "fix_moduleinfo_vendor: VENDOR_HEX is empty")
endif()
math(EXPR _lastPair "${_hexLength} - 2")

set(_codes)
foreach(_i RANGE 0 ${_lastPair} 2)
    string(SUBSTRING "${VENDOR_HEX}" ${_i} 2 _pair)
    math(EXPR _code "0x${_pair}")
    list(APPEND _codes ${_code})
endforeach()
string(ASCII ${_codes} _vendor)

# --- rewrite every Vendor value ---------------------------------------------
# All three (the factory's and one per exported class) are the company name, so
# they all get the same value. The pattern keeps the match inside a single JSON
# string so it cannot run past the closing quote.
file(READ "${MODULEINFO}" _content)

string(REGEX MATCHALL "\"Vendor\"[ \t]*:[ \t]*\"[^\"]*\"" _before "${_content}")
list(LENGTH _before _count)
if(_count EQUAL 0)
    message(WARNING "Azimuter: no Vendor field found in ${MODULEINFO} - left untouched")
    return()
endif()

string(REGEX REPLACE "\"Vendor\"[ \t]*:[ \t]*\"[^\"]*\"" "\"Vendor\": \"${_vendor}\"" _fixed "${_content}")

if(_fixed STREQUAL _content)
    message(STATUS "Azimuter: moduleinfo.json vendor already correct (${_count} fields)")
    return()
endif()

file(WRITE "${MODULEINFO}" "${_fixed}")
message(STATUS "Azimuter: repaired ${_count} vendor field(s) in moduleinfo.json")
