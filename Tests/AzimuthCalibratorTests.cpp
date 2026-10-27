#include "Azimuth/AzimuthCalibrator.h"
#include "Azimuth/SpectralTilt.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using azimuter::AzimuthCalibrator;

const std::vector<float>& bandCentres()
{
    static const std::vector<float> c = {2806.0f, 3536.0f, 4454.0f, 5612.0f, 7071.0f, 8909.0f, 11225.0f, 14142.0f};
    return c;
}

/** Build a sweep that obeys the model: a programme with its own spectral shape
    and slowly drifting brightness, plus gap loss around a known target. */
struct SyntheticSweep
{
    float target{60.0f};
    float ratio{1.8f};
    float fromUs{-200.0f};
    float toUs{200.0f};
    int count{200};
    float noiseDb{0.0f};
    float levelDb{10.0f}; // reference-band level; absolute value must not matter

    void feed(AzimuthCalibrator& cal) const
    {
        const auto& fc = bandCentres();
        unsigned seed = 12345u;
        auto noise = [&seed]()
        {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>(static_cast<double>(seed % 10000u) / 10000.0 - 0.5);
        };

        for (int i = 0; i < count; ++i)
        {
            const double frac = static_cast<double>(i) / static_cast<double>(count - 1);
            const double t = frac * 120.0;
            const float delay = fromUs + static_cast<float>(frac) * (toUs - fromUs);

            std::vector<float> tilt(fc.size());
            // Programme: a falling spectral shape, plus brightness wandering
            // several dB over the sweep — the confound the model must reject.
            const float brightness = 3.0f * static_cast<float>(std::sin(frac * 5.0));
            for (size_t b = 0; b < fc.size(); ++b)
            {
                const float shape = -4.0f * static_cast<float>(b);
                tilt[b] =
                    shape + brightness - AzimuthCalibrator::gapLossDb(delay - target, ratio, fc[b]) + noiseDb * noise();
            }
            cal.addObservation(t, delay, tilt.data(), levelDb, /*significance=*/2.0f);
        }
    }
};

AzimuthCalibrator makeCalibrator()
{
    AzimuthCalibrator c;
    c.prepare(static_cast<int>(bandCentres().size()), bandCentres().data());
    return c;
}

struct Fixture
{
    std::vector<double> time;
    std::vector<float> delay, level, coherence;
    std::vector<std::vector<float>> tilt;
};

bool loadFixture(const std::string& name, Fixture& out)
{
#ifdef AZIMUTER_TEST_DATA_DIR
    std::ifstream in(std::string(AZIMUTER_TEST_DATA_DIR) + "/" + name);
#else
    std::ifstream in(name);
#endif
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::stringstream ss(line);
        std::string cell;
        std::vector<float> v;
        while (std::getline(ss, cell, ','))
            v.push_back(std::strtof(cell.c_str(), nullptr));
        if (v.size() < 5)
            continue;
        out.time.push_back(v[0]);
        out.delay.push_back(v[1]);
        out.level.push_back(v[2]);
        out.coherence.push_back(v[3]);
        out.tilt.emplace_back(v.begin() + 4, v.end());
    }
    return !out.delay.empty();
}
} // namespace

TEST_CASE("Gap loss follows the classical law", "[calibrator]")
{
    // Zero error is zero loss, and loss grows with both error and frequency —
    // the frequency scaling is what lets the fit separate azimuth from
    // programme brightness, so it is not an incidental property.
    REQUIRE_THAT(AzimuthCalibrator::gapLossDb(0.0f, 1.8f, 14000.0f), Catch::Matchers::WithinAbs(0.0f, 1.0e-4f));
    REQUIRE(AzimuthCalibrator::gapLossDb(40.0f, 1.8f, 14000.0f) > AzimuthCalibrator::gapLossDb(20.0f, 1.8f, 14000.0f));
    REQUIRE(AzimuthCalibrator::gapLossDb(40.0f, 1.8f, 14000.0f) > AzimuthCalibrator::gapLossDb(40.0f, 1.8f, 7000.0f));
    // Symmetric in the sign of the error.
    REQUIRE_THAT(AzimuthCalibrator::gapLossDb(-55.0f, 1.8f, 11000.0f),
                 Catch::Matchers::WithinAbs(AzimuthCalibrator::gapLossDb(55.0f, 1.8f, 11000.0f), 1.0e-5f));
    // Clamped past the first null rather than running to infinity.
    REQUIRE(AzimuthCalibrator::gapLossDb(5000.0f, 1.8f, 14000.0f) <= AzimuthCalibrator::kLossClampDb + 1.0e-3f);
}

TEST_CASE("Recovers a known target from a synthetic sweep", "[calibrator]")
{
    auto cal = makeCalibrator();
    SyntheticSweep{}.feed(cal);

    REQUIRE(cal.hasEnoughData());
    const auto r = cal.solve();
    REQUIRE(r.valid);
    REQUIRE_THAT(r.targetDelayUs, Catch::Matchers::WithinAbs(60.0f, 6.0f));
    REQUIRE_THAT(r.geometryRatio, Catch::Matchers::WithinAbs(1.8f, 0.2f));
}

TEST_CASE("Recovers a negative target too", "[calibrator]")
{
    // brunius measures about -100 us, so the sign is not a corner case.
    auto cal = makeCalibrator();
    SyntheticSweep s;
    s.target = -100.0f;
    s.fromUs = -290.0f;
    s.toUs = 220.0f;
    s.feed(cal);

    const auto r = cal.solve();
    REQUIRE(r.valid);
    REQUIRE_THAT(r.targetDelayUs, Catch::Matchers::WithinAbs(-100.0f, 8.0f));
}

TEST_CASE("Survives noise on the band measurements", "[calibrator]")
{
    auto cal = makeCalibrator();
    SyntheticSweep s;
    s.noiseDb = 3.0f;
    s.feed(cal);

    const auto r = cal.solve();
    REQUIRE(r.valid);
    REQUIRE_THAT(r.targetDelayUs, Catch::Matchers::WithinAbs(60.0f, 12.0f));
}

TEST_CASE("Observations with no shared signal are rejected", "[calibrator]")
{
    // Significance 1.0 is pure noise: the gaps between songs, where each track
    // has only its own hiss and the delay reading is random.
    auto cal = makeCalibrator();
    std::vector<float> tilt(bandCentres().size(), -6.0f);

    REQUIRE(cal.addObservation(0.0, 10.0f, tilt.data(), 0.0f, 2.0f));
    REQUIRE_FALSE(cal.addObservation(1.0, 10.0f, tilt.data(), 0.0f, 1.0f));
    REQUIRE_FALSE(cal.addObservation(2.0, 10.0f, tilt.data(), 0.0f, 1.2f));
    REQUIRE(cal.observationCount() == 1);
}

TEST_CASE("No absolute level threshold: a very quiet deck calibrates the same", "[calibrator]")
{
    // The old gate rejected anything below -55 dB. A deck 70 dB quieter, with
    // perfectly good music on it, must give the same answer.
    auto loud = makeCalibrator();
    auto quiet = makeCalibrator();
    SyntheticSweep s;
    s.feed(loud);
    s.levelDb = -60.0f;
    s.feed(quiet);

    const auto a = loud.solve();
    const auto b = quiet.solve();
    REQUIRE(a.valid);
    REQUIRE(b.valid);
    REQUIRE_THAT(b.targetDelayUs, Catch::Matchers::WithinAbs(a.targetDelayUs, 1.0e-3f));
}

TEST_CASE("A sweep that crosses a gap between songs is not pulled off", "[calibrator]")
{
    // Gap samples: random delays, flat hiss spectrum, significance ~1.0. They
    // are rejected at the door, so the answer matches the clean sweep.
    auto clean = makeCalibrator();
    auto withGap = makeCalibrator();
    SyntheticSweep s;
    s.feed(clean);
    s.feed(withGap);

    std::vector<float> hiss(bandCentres().size(), -3.0f);
    unsigned seed = 99u;
    int accepted = 0;
    for (int i = 0; i < 60; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float randomDelay = static_cast<float>(seed % 600u) - 300.0f;
        if (withGap.addObservation(200.0 + i, randomDelay, hiss.data(), -30.0f, 1.0f))
            ++accepted;
    }
    REQUIRE(accepted == 0);
    REQUIRE_THAT(withGap.solve().targetDelayUs, Catch::Matchers::WithinAbs(clean.solve().targetDelayUs, 1.0e-3f));
}

TEST_CASE("Observations far below the sweep's own level are dropped at solve time", "[calibrator]")
{
    // A fade-out: still significant (shared signal), but 40 dB below the rest
    // of the sweep, where hiss colours the high bands. Relative to the sweep,
    // not absolute, so deck output level is irrelevant.
    auto clean = makeCalibrator();
    auto withFade = makeCalibrator();
    SyntheticSweep s;
    s.feed(clean);
    s.feed(withFade);

    std::vector<float> washedOut(bandCentres().size(), 0.0f); // treble-heavy: would drag the fit
    for (int i = 0; i < 30; ++i)
        REQUIRE(withFade.addObservation(130.0 + i, 150.0f, washedOut.data(), 10.0f - 40.0f, 2.0f));

    REQUIRE_THAT(withFade.solve().targetDelayUs, Catch::Matchers::WithinAbs(clean.solve().targetDelayUs, 1.0e-3f));
}

TEST_CASE("A sweep that did not turn the control far enough is refused", "[calibrator]")
{
    // Measured: a range that stops short of the optimum on one side biases the
    // answer badly. Refusing is much better than answering confidently.
    auto cal = makeCalibrator();
    SyntheticSweep s;
    s.fromUs = 0.0f;
    s.toUs = 40.0f;
    s.feed(cal);

    REQUIRE_FALSE(cal.hasEnoughData());
    const auto r = cal.solve();
    REQUIRE_FALSE(r.valid);
    REQUIRE(r.rejectReason != nullptr);
    REQUIRE_THAT(r.sweptRangeUs, Catch::Matchers::WithinAbs(40.0f, 1.0f));
}

TEST_CASE("Too few observations is refused", "[calibrator]")
{
    auto cal = makeCalibrator();
    SyntheticSweep s;
    s.count = 10;
    s.feed(cal);

    const auto r = cal.solve();
    REQUIRE_FALSE(r.valid);
    REQUIRE(r.rejectReason != nullptr);
}

TEST_CASE("reset() clears the sweep", "[calibrator]")
{
    auto cal = makeCalibrator();
    SyntheticSweep{}.feed(cal);
    REQUIRE(cal.observationCount() > 0);
    cal.reset();
    REQUIRE(cal.observationCount() == 0);
    REQUIRE_THAT(cal.sweptRangeUs(), Catch::Matchers::WithinAbs(0.0f, 1.0e-6f));
}

// --- regression against two real tapes ------------------------------------
//
// These traces come from actual azimuth sweeps on a Nakamichi DR-1, analysed
// through the same chain the plugin runs. The expected answers were
// established independently: for jungenu by using a second transfer of the
// same music as a programme reference (a crutch the plugin never has), which
// put the optimum at +54 us; brunius has no reference, and its value is the
// one eleven analysis variants agreed on.
//
// The point of the test is not the exact microsecond. It is that on real
// tapes the answer is nowhere near zero — which is precisely what the old
// algorithm assumed.

TEST_CASE("Finds the real optimum on the jungenu sweep", "[calibrator][fixture]")
{
    Fixture f;
    if (!loadFixture("sweep_jungenu.csv", f))
        SUCCEED("fixture not available");
    else
    {
        auto cal = makeCalibrator();
        for (size_t i = 0; i < f.delay.size(); ++i)
            cal.addObservation(f.time[i], f.delay[i], f.tilt[i].data(), f.level[i], f.coherence[i]);

        REQUIRE(cal.hasEnoughData());
        const auto r = cal.solve();
        REQUIRE(r.valid);
        REQUIRE_THAT(r.targetDelayUs, Catch::Matchers::WithinAbs(50.0f, 10.0f));
        REQUIRE(r.targetDelayUs > 25.0f); // aiming at zero would be the old bug
        REQUIRE(r.confidenceUs < 40.0f);
    }
}

TEST_CASE("Finds the real optimum on the brunius sweep", "[calibrator][fixture]")
{
    Fixture f;
    if (!loadFixture("sweep_brunius.csv", f))
        SUCCEED("fixture not available");
    else
    {
        auto cal = makeCalibrator();
        for (size_t i = 0; i < f.delay.size(); ++i)
            cal.addObservation(f.time[i], f.delay[i], f.tilt[i].data(), f.level[i], f.coherence[i]);

        REQUIRE(cal.hasEnoughData());
        const auto r = cal.solve();
        REQUIRE(r.valid);
        REQUIRE_THAT(r.targetDelayUs, Catch::Matchers::WithinAbs(-102.0f, 12.0f));
        REQUIRE(r.targetDelayUs < -60.0f);
    }
}

TEST_CASE("The two tapes disagree, which is why calibration exists", "[calibrator][fixture]")
{
    Fixture a, b;
    if (!loadFixture("sweep_jungenu.csv", a) || !loadFixture("sweep_brunius.csv", b))
        SUCCEED("fixtures not available");
    else
    {
        auto ca = makeCalibrator(), cb = makeCalibrator();
        for (size_t i = 0; i < a.delay.size(); ++i)
            ca.addObservation(a.time[i], a.delay[i], a.tilt[i].data(), a.level[i], a.coherence[i]);
        for (size_t i = 0; i < b.delay.size(); ++i)
            cb.addObservation(b.time[i], b.delay[i], b.tilt[i].data(), b.level[i], b.coherence[i]);

        const auto ra = ca.solve();
        const auto rb = cb.solve();
        REQUIRE(ra.valid);
        REQUIRE(rb.valid);
        // Opposite signs, ~150 us apart, same deck. No shippable constant.
        REQUIRE(ra.targetDelayUs > 0.0f);
        REQUIRE(rb.targetDelayUs < 0.0f);
        REQUIRE(std::abs(ra.targetDelayUs - rb.targetDelayUs) > 100.0f);
        // The head geometry, unlike the tape, is a property of the deck.
        REQUIRE_THAT(ra.geometryRatio, Catch::Matchers::WithinAbs(rb.geometryRatio, 0.35f));
    }
}
