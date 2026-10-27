#include "Azimuth/SpectralTilt.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

namespace
{
constexpr double kSampleRate = 44100.0;
constexpr int kFftLength = 4096;
constexpr float kTopHz = 18000.0f;

azimuter::SpectralTilt makeTilt()
{
    azimuter::SpectralTilt t;
    t.prepare(kSampleRate, kFftLength, kTopHz, /*smoothingSeconds=*/0.0f,
              /*hopSeconds=*/1024.0 / kSampleRate);
    return t;
}

float binHz(int k)
{
    return static_cast<float>(k) * static_cast<float>(kSampleRate) / static_cast<float>(kFftLength);
}

// Feed a spectrum described by a function of frequency.
template <typename Fn>
void feed(azimuter::SpectralTilt& t, Fn&& shape, int frames = 1)
{
    std::vector<float> power(static_cast<size_t>(kFftLength / 2 + 1));
    for (size_t k = 0; k < power.size(); ++k)
        power[k] = shape(binHz(static_cast<int>(k)));
    for (int i = 0; i < frames; ++i)
        t.accumulate(power.data());
}
} // namespace

TEST_CASE("Third-octave bands land where the gap-loss model expects", "[tilt]")
{
    auto t = makeTilt();

    // Third-octaves from 2.5 kHz, keeping only bands that fit whole under
    // 18 kHz. AzimuthCalibrator weights each band by this centre frequency,
    // so a wrong layout would silently bias every calibration.
    REQUIRE(t.numBands() == 8);
    const float expected[] = {2806.0f, 3536.0f, 4454.0f, 5612.0f, 7071.0f, 8909.0f, 11225.0f, 14142.0f};
    for (int b = 0; b < t.numBands(); ++b)
        REQUIRE_THAT(t.centreHz(b), Catch::Matchers::WithinRel(expected[b], 0.01f));
}

TEST_CASE("A flat spectrum has zero tilt in every band", "[tilt]")
{
    auto t = makeTilt();
    feed(t, [](float) { return 1.0f; });

    REQUIRE(t.ready());
    for (int b = 0; b < t.numBands(); ++b)
        REQUIRE_THAT(t.tiltDb(b), Catch::Matchers::WithinAbs(0.0f, 0.01f));
}

TEST_CASE("Tilt reports band energy relative to the reference band", "[tilt]")
{
    auto t = makeTilt();
    // Quarter power above 10 kHz — the shape azimuth loss produces.
    feed(t, [](float hz) { return hz >= 10000.0f ? 0.25f : 1.0f; });

    for (int b = 0; b < t.numBands(); ++b)
    {
        if (t.centreHz(b) > 11000.0f)
            REQUIRE_THAT(t.tiltDb(b), Catch::Matchers::WithinAbs(-6.02f, 0.1f));
        else if (t.centreHz(b) < 9000.0f)
            REQUIRE_THAT(t.tiltDb(b), Catch::Matchers::WithinAbs(0.0f, 0.1f));
    }
}

TEST_CASE("Tilt is independent of overall level", "[tilt]")
{
    // Programme gets louder; the tilt must not move, because the calibrator
    // relies on level cancelling against the reference band.
    auto quiet = makeTilt();
    auto loud = makeTilt();
    auto shape = [](float hz) { return hz >= 8000.0f ? 0.1f : 1.0f; };
    feed(quiet, [&](float hz) { return 1.0e-4f * shape(hz); });
    feed(loud, [&](float hz) { return 25.0f * shape(hz); });

    for (int b = 0; b < quiet.numBands(); ++b)
        REQUIRE_THAT(quiet.tiltDb(b), Catch::Matchers::WithinAbs(loud.tiltDb(b), 0.01f));

    REQUIRE(loud.referenceLevelDb() > quiet.referenceLevelDb() + 30.0f);
}

TEST_CASE("Reference level tracks absolute power for the quiet-passage gate", "[tilt]")
{
    auto t = makeTilt();
    feed(t, [](float) { return 4.0f; });
    REQUIRE_THAT(t.referenceLevelDb(), Catch::Matchers::WithinAbs(10.0f * std::log10(4.0f), 0.05f));
}

TEST_CASE("Averaging converges on a changed spectrum", "[tilt]")
{
    azimuter::SpectralTilt t;
    t.prepare(kSampleRate, kFftLength, kTopHz, /*smoothingSeconds=*/0.05f, /*hopSeconds=*/0.02);
    feed(t, [](float) { return 1.0f; });
    feed(t, [](float hz) { return hz >= 10000.0f ? 0.01f : 1.0f; }, 400);

    for (int b = 0; b < t.numBands(); ++b)
        if (t.centreHz(b) > 11000.0f)
            REQUIRE_THAT(t.tiltDb(b), Catch::Matchers::WithinAbs(-20.0f, 0.5f));
}

TEST_CASE("Bands that would be truncated by the band edge are dropped", "[tilt]")
{
    azimuter::SpectralTilt t;
    t.prepare(kSampleRate, kFftLength, /*topHz=*/9000.0f, 0.0f, 1024.0 / kSampleRate);
    REQUIRE(t.numBands() > 0);
    for (int b = 0; b < t.numBands(); ++b)
        REQUIRE(t.centreHz(b) < 9000.0f);
}
