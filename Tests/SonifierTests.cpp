#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Azimuth/Sonifier.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;

// Run the sonifier long enough that internal smoothers (~30 ms freq, ~10 ms gain) settle.
void settle(azimuter::Sonifier& s, int milliseconds = 200)
{
    const int n = static_cast<int>(kSampleRate * milliseconds / 1000.0);
    std::vector<float> l(static_cast<size_t>(n), 0.0f), r(static_cast<size_t>(n), 0.0f);
    s.process(l.data(), r.data(), n);
}
} // namespace

TEST_CASE("Sonifier pitch peaks at zero delay", "[sonifier]")
{
    azimuter::Sonifier s;
    s.prepare(kSampleRate);
    s.setPitchRange(200.0f, 2000.0f);
    s.setSignalPresent(true);
    s.setGain(1.0f);
    s.setSignedNormalisedDelay(0.0f);

    settle(s);
    REQUIRE_THAT(s.currentFrequencyHz(), Catch::Matchers::WithinRel(2000.0f, 0.01f));
}

TEST_CASE("Sonifier pitch bottoms out at full deflection", "[sonifier]")
{
    azimuter::Sonifier s;
    s.prepare(kSampleRate);
    s.setPitchRange(200.0f, 2000.0f);
    s.setSignalPresent(true);
    s.setGain(1.0f);
    s.setSignedNormalisedDelay(1.0f);

    settle(s);
    REQUIRE_THAT(s.currentFrequencyHz(), Catch::Matchers::WithinRel(200.0f, 0.01f));
}

TEST_CASE("Sonifier pitch is identical for symmetric deflections", "[sonifier]")
{
    // Pitch depends only on magnitude; sign drives panning, not pitch.
    azimuter::Sonifier sPos, sNeg;
    sPos.prepare(kSampleRate);
    sNeg.prepare(kSampleRate);
    sPos.setSignalPresent(true);
    sNeg.setSignalPresent(true);
    sPos.setGain(1.0f);
    sNeg.setGain(1.0f);
    sPos.setSignedNormalisedDelay(+0.3f);
    sNeg.setSignedNormalisedDelay(-0.3f);

    settle(sPos);
    settle(sNeg);
    REQUIRE_THAT(sPos.currentFrequencyHz(), Catch::Matchers::WithinRel(sNeg.currentFrequencyHz(), 0.001f));
}

TEST_CASE("Sonifier pulse engages only past half deflection", "[sonifier]")
{
    azimuter::Sonifier s;
    s.prepare(kSampleRate);
    s.setSignalPresent(true);
    s.setGain(1.0f);

    s.setSignedNormalisedDelay(0.2f);
    settle(s, 400);
    REQUIRE(s.currentPulseDepth() < 0.01f);

    s.setSignedNormalisedDelay(0.9f);
    settle(s, 400);
    REQUIRE(s.currentPulseDepth() > 0.7f);
}

TEST_CASE("Sonifier pans to the right for positive deflection", "[sonifier]")
{
    azimuter::Sonifier s;
    s.prepare(kSampleRate);
    s.setSignalPresent(true);
    s.setGain(1.0f);
    s.setSignedNormalisedDelay(1.0f);

    settle(s, 300);
    REQUIRE(s.currentRightPanGain() > 0.95f);
    REQUIRE(s.currentLeftPanGain() < 0.05f);
}

TEST_CASE("Sonifier pans to the left for negative deflection", "[sonifier]")
{
    azimuter::Sonifier s;
    s.prepare(kSampleRate);
    s.setSignalPresent(true);
    s.setGain(1.0f);
    s.setSignedNormalisedDelay(-1.0f);

    settle(s, 300);
    REQUIRE(s.currentLeftPanGain() > 0.95f);
    REQUIRE(s.currentRightPanGain() < 0.05f);
}

TEST_CASE("Sonifier centres pan at zero deflection", "[sonifier]")
{
    azimuter::Sonifier s;
    s.prepare(kSampleRate);
    s.setSignalPresent(true);
    s.setGain(1.0f);
    s.setSignedNormalisedDelay(0.0f);

    settle(s, 300);
    // Constant-power centre is cos(pi/4) = sin(pi/4) ~= 0.7071.
    REQUIRE_THAT(s.currentLeftPanGain(), Catch::Matchers::WithinAbs(0.70710678f, 0.005f));
    REQUIRE_THAT(s.currentRightPanGain(), Catch::Matchers::WithinAbs(0.70710678f, 0.005f));
}

TEST_CASE("Sonifier negative-deflection output is amplitude-modulated", "[sonifier]")
{
    azimuter::Sonifier s;
    s.prepare(kSampleRate);
    s.setSignalPresent(true);
    s.setGain(1.0f);
    s.setSignedNormalisedDelay(-1.0f); // fully panned left, pulse fully engaged
    settle(s, 400);

    // One full pulse cycle is 1/3 s = 16000 samples at 48 kHz. Capture 1.5
    // cycles and confirm the envelope clearly varies — and never fully silences.
    constexpr int captureSamples = 24000;
    std::vector<float> l(captureSamples, 0.0f), r(captureSamples, 0.0f);
    s.process(l.data(), r.data(), captureSamples);

    constexpr int windowSize = 200;
    float minWindowRms = std::numeric_limits<float>::infinity();
    float maxWindowRms = 0.0f;
    for (int start = 0; start + windowSize <= captureSamples; start += windowSize)
    {
        float sumSq = 0.0f;
        for (int i = 0; i < windowSize; ++i)
            sumSq += l[static_cast<size_t>(start + i)] * l[static_cast<size_t>(start + i)];
        const float rms = std::sqrt(sumSq / windowSize);
        minWindowRms = std::min(minWindowRms, rms);
        maxWindowRms = std::max(maxWindowRms, rms);
    }
    REQUIRE(maxWindowRms > 2.0f * minWindowRms);
    REQUIRE(minWindowRms > 0.0f); // distinct from "no signal" state
}

TEST_CASE("Sonifier mutes when signal is absent", "[sonifier]")
{
    azimuter::Sonifier s;
    s.prepare(kSampleRate);
    s.setSignalPresent(false);
    s.setGain(1.0f);
    s.setSignedNormalisedDelay(0.0f);

    settle(s);
    REQUIRE(s.currentGainLinear() < 0.001f);
}

TEST_CASE("Sonifier respects user gain and unmutes when signal returns", "[sonifier]")
{
    azimuter::Sonifier s;
    s.prepare(kSampleRate);
    s.setSignalPresent(false);
    s.setGain(0.7f);
    s.setSignedNormalisedDelay(0.0f);
    settle(s);
    REQUIRE(s.currentGainLinear() < 0.001f);

    s.setSignalPresent(true);
    settle(s);
    REQUIRE_THAT(s.currentGainLinear(), Catch::Matchers::WithinRel(0.7f, 0.02f));
}

TEST_CASE("Sonifier process overwrites the buffer", "[sonifier]")
{
    azimuter::Sonifier s;
    s.prepare(kSampleRate);
    s.setSignalPresent(true);
    s.setGain(0.5f);
    s.setSignedNormalisedDelay(0.0f); // centred, so L == R
    settle(s);

    constexpr int n = 256;
    std::vector<float> l(n, 1.0f), r(n, -1.0f); // sentinels
    s.process(l.data(), r.data(), n);

    REQUIRE(l[100] != 1.0f);
    REQUIRE(r[100] != -1.0f);
    // At zero deflection the pan is symmetric, so L and R samples coincide.
    REQUIRE_THAT(l[100], Catch::Matchers::WithinAbs(r[100], 1.0e-5f));
}
