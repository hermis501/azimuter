cmake_minimum_required(VERSION 3.22)
project(Azimuter VERSION 1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # /utf-8 is needed because COMPANY_NAME contains a non-ASCII character
    # (č in "Hermis Kasperavičius"). Without it, MSVC reads UTF-8 source as the
    # active Windows code page and the JUCE-generated PluginDefines header
    # produces a mojibake'd manufacturer string in DAW browsers.
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
endif()

option(AZIMUTER_BUILD_TESTS "Build Catch2 unit tests" ON)
option(AZIMUTER_COPY_PLUGIN_AFTER_BUILD "Install built VST3 to the user VST3 folder" OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

# Resolve an SDK path in priority order:
#   1. environment variable (per-developer override),
#   2. shared C:\sdk\ install — first path that exists wins,
#   3. fall back to FetchContent (slow first time, fine for CI/fresh checkouts).
# `result_var` is set in PARENT_SCOPE to the resolved path, or empty string if none.
function(azimuter_resolve_sdk result_var env_var)
    if(DEFINED ENV{${env_var}} AND EXISTS "$ENV{${env_var}}/CMakeLists.txt")
        set(${result_var} "$ENV{${env_var}}" PARENT_SCOPE)
        return()
    endif()
    foreach(candidate IN LISTS ARGN)
        if(EXISTS "${candidate}/CMakeLists.txt")
            set(${result_var} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${result_var} "" PARENT_SCOPE)
endfunction()

# Current install is C:/_tools/JUCE (8.0.15). The C:/sdk/... entries are this
# machine's previous layout, kept as fallbacks so an older checkout still
# configures. First path that exists wins.
azimuter_resolve_sdk(AZIMUTER_JUCE_LOCAL_DIR JUCE_SDK_DIR
    "C:/_tools/JUCE" "C:/sdk/JUCE" "C:/sdk/JUCE-8.0.4")
if(AZIMUTER_JUCE_LOCAL_DIR)
    message(STATUS "Azimuter: using local JUCE at ${AZIMUTER_JUCE_LOCAL_DIR}")
    add_subdirectory("${AZIMUTER_JUCE_LOCAL_DIR}" juce_build EXCLUDE_FROM_ALL)
else()
    message(STATUS "Azimuter: no local JUCE found, fetching via FetchContent")
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# --- Core (headless DSP / analysis) ------------------------------------------
# Pure, plugin-agnostic code that the unit tests can link against without
# pulling in the full plugin / GUI stack.
add_library(azimuter_core STATIC
    Source/Azimuth/AlignmentReadout.cpp
    Source/Azimuth/AzimuthCalibrator.cpp
    Source/Azimuth/GccPhat.cpp
    Source/Azimuth/Sonifier.cpp
    Source/Azimuth/SpectralTilt.cpp
)
target_include_directories(azimuter_core PUBLIC Source)
target_compile_features(azimuter_core PUBLIC cxx_std_17)

# --- Plugin -------------------------------------------------------------------
# Single source of truth for the company name. The non-ASCII c-caron in it is
# the reason for /utf-8 above, for the JUCE patch in patches/, and for the
# moduleinfo.json repair below.
set(AZIMUTER_COMPANY_NAME "Hermis Kasperavičius")

juce_add_plugin(Azimuter
    COMPANY_NAME            "${AZIMUTER_COMPANY_NAME}"
    BUNDLE_ID               "com.azimuter.Azimuter"
    PLUGIN_MANUFACTURER_CODE Azmt
    PLUGIN_CODE             Azim
    FORMATS                 VST3
    PRODUCT_NAME            "Azimuter"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    # COPY_PLUGIN_AFTER_BUILD is deliberately NOT set here. It would register
    # the copy step inside juce_add_plugin, i.e. before the moduleinfo repair
    # below, so the installed bundle would carry the broken vendor string. The
    # copy is enabled after the repair instead.
)

target_sources(Azimuter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ScreenReaderSpeech.cpp
)

target_compile_definitions(Azimuter PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Azimuter
    PRIVATE
        azimuter_core
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Post-build: repair the moduleinfo.json vendor -----------------------------
# The DLL itself is correct, but the helper that writes moduleinfo.json loads it
# and re-encodes the vendor through a codepage conversion, turning the c-caron
# into a Windows-1257 byte in the factory info and into U+FFFD in the class
# entries. Hosts read that file during scanning instead of loading the DLL, and
# the installer ships it, so it has to be repaired or the mojibake surfaces
# anyway. Done here rather than as another JUCE patch so it survives SDK
# upgrades and moves. See patches/fix_moduleinfo_vendor.cmake.
if(TARGET Azimuter_VST3)
    # Hex-encode at configure time: the raw name on a command line would go
    # through the same MSBuild response-file encoding that causes the bug.
    string(HEX "${AZIMUTER_COMPANY_NAME}" AZIMUTER_COMPANY_NAME_HEX)

    add_custom_command(TARGET Azimuter_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -DMODULEINFO=$<TARGET_FILE_DIR:Azimuter_VST3>/../Resources/moduleinfo.json
                -DVENDOR_HEX=${AZIMUTER_COMPANY_NAME_HEX}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/patches/fix_moduleinfo_vendor.cmake"
        VERBATIM
        COMMENT "Repairing moduleinfo.json vendor encoding")
endif()

# Enabled here rather than via juce_add_plugin's COPY_PLUGIN_AFTER_BUILD so the
# copy runs *after* the repair above and the installed bundle gets the fixed
# moduleinfo.json. Post-build commands run in registration order.
if(AZIMUTER_COPY_PLUGIN_AFTER_BUILD)
    juce_enable_copy_plugin_step(Azimuter)
endif()

# --- Tests --------------------------------------------------------------------
if(AZIMUTER_BUILD_TESTS)
    azimuter_resolve_sdk(AZIMUTER_CATCH2_LOCAL_DIR CATCH2_SDK_DIR
        "C:/_tools/Catch2" "C:/sdk/Catch2" "C:/sdk/Catch2-3.5.4")
    if(AZIMUTER_CATCH2_LOCAL_DIR)
        message(STATUS "Azimuter: using local Catch2 at ${AZIMUTER_CATCH2_LOCAL_DIR}")
        add_subdirectory("${AZIMUTER_CATCH2_LOCAL_DIR}" catch2_build EXCLUDE_FROM_ALL)
        set(AZIMUTER_CATCH2_EXTRAS_DIR "${AZIMUTER_CATCH2_LOCAL_DIR}/extras")
    else()
        message(STATUS "Azimuter: no local Catch2 found, fetching via FetchContent")
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.5.4
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(Catch2)
        set(AZIMUTER_CATCH2_EXTRAS_DIR "${Catch2_SOURCE_DIR}/extras")
    endif()

    add_executable(AzimuterTests
        Tests/AlignmentReadoutTests.cpp
        Tests/AzimuthCalibratorTests.cpp
        Tests/DeflectionMapTests.cpp
        Tests/GccPhatTests.cpp
        Tests/SignalGateTests.cpp
        Tests/SonifierTests.cpp
        Tests/SpectralTiltTests.cpp
    )
    target_link_libraries(AzimuterTests PRIVATE
        azimuter_core
        Catch2::Catch2WithMain
    )

    # Real azimuth sweeps from a Nakamichi DR-1, used as calibration
    # regressions. They must be regenerated from the original transfers if the
    # analysis chain (band layout, coarse/fine lock) ever changes; the export
    # script and the audio are not in the repository.
    target_compile_definitions(AzimuterTests PRIVATE
        AZIMUTER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/data"
    )

    include(CTest)
    include(${AZIMUTER_CATCH2_EXTRAS_DIR}/Catch.cmake)
    catch_discover_tests(AzimuterTests)
endif()
