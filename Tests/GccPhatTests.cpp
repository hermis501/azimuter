#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Azimuth/GccPhat.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kTwoPi = 6.28318530717958647692;

// A deterministic, broadband HF source: a few additive sinusoids that cover
// the analyser's pass-band so the cross-spectrum has reliable energy in
// every bin GccPhat looks at. Defined as a continuous function of sample
// index so we can fractionally shift it for sub-sample-delay tests.
float hfSource(double sampleIndex) noexcept
{
    const double t = sampleIndex / kSampleRate;
    return 0.4f * static_cast<float>(std::sin(kTwoPi * 3000.0 * t)) +
           0.4f * static_cast<float>(std::sin(kTwoPi * 7000.0 * t + 1.3)) +
           0.4f * static_cast<float>(std::sin(kTwoPi * 11000.0 * t + 0.7)) +
           0.2f * static_cast<float>(std::sin(kTwoPi * 14000.0 * t + 2.1));
}

// Run for `seconds` seconds of audio at the given inter-channel delay (in
// samples; positive = right channel leads, mirroring the GccPhat convention).
// `delaySamples` may be fractional.
float settleAndMeasure(azimuter::GccPhat& g, double delaySamples, double seconds = 1.5)
{
    const int n = static_cast<int>(kSampleRate * seconds);
    for (int i = 0; i < n; ++i)
    {
        const float l = hfSource(static_cast<double>(i));
        const float r = hfSource(static_cast<double>(i) + delaySamples);
        g.process(l, r);
    }
    return g.delaySamples();
}
} // namespace

TEST_CASE("Identical channels => delay near zero", "[gcc-phat]")
{
    azimuter::GccPhat g;
    g.prepare(kSampleRate);

    const float d = settleAndMeasure(g, 0.0);
    REQUIRE_THAT(d, Catch::Matchers::WithinAbs(0.0f, 0.05f));
}

TEST_CASE("Right-channel-leads delay is positive", "[gcc-phat]")
{
    azimuter::GccPhat g;
    g.prepare(kSampleRate);

    const float d = settleAndMeasure(g, 3.0);
    REQUIRE_THAT(d, Catch::Matchers::WithinAbs(3.0f, 0.1f));
}

TEST_CASE("Left-channel-leads delay is negative", "[gcc-phat]")
{
    azimuter::GccPhat g;
    g.prepare(kSampleRate);

    const float d = settleAndMeasure(g, -4.0);
    REQUIRE_THAT(d, Catch::Matchers::WithinAbs(-4.0f, 0.1f));
}

TEST_CASE("Sub-sample delay resolved via parabolic interpolation", "[gcc-phat]")
{
    azimuter::GccPhat g;
    g.prepare(kSampleRate);

    const float d = settleAndMeasure(g, 2.5);
    // Sub-sample accuracy should be tight: ±0.15 samples is well under the
    // 5 µs "Fair" threshold of the spoken-text bucket logic.
    REQUIRE_THAT(d, Catch::Matchers::WithinAbs(2.5f, 0.15f));
}

TEST_CASE("Estimator is robust to amplitude modulation", "[gcc-phat]")
{
    // Mimic program-material spectral modulation: scale the same shifted
    // source by a low-rate amplitude envelope. The PHAT pipeline should
    // produce essentially the same delay reading as for the un-modulated
    // signal, because the per-bin magnitude is whitened.
    azimuter::GccPhat g;
    g.prepare(kSampleRate);

    constexpr double delay = 2.0;
    const int n = static_cast<int>(kSampleRate * 1.5);
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / kSampleRate;
        const float envelope = 0.1f + 0.9f * static_cast<float>(0.5 * (1.0 + std::sin(kTwoPi * 0.7 * t)));
        const float l = envelope * hfSource(static_cast<double>(i));
        const float r = envelope * hfSource(static_cast<double>(i) + delay);
        g.process(l, r);
    }

    REQUIRE_THAT(g.delaySamples(), Catch::Matchers::WithinAbs(2.0f, 0.15f));
}

TEST_CASE("Estimator locks on shifted broadband noise", "[gcc-phat]")
{
    // Tape hiss is broadband stationary noise. Confirm GccPhat resolves an
    // inter-channel delay from noise alone — this is what makes the plugin
    // usable during quiet musical passages.
    azimuter::GccPhat g;
    g.prepare(kSampleRate);

    const int total = static_cast<int>(kSampleRate * 2.0);
    constexpr int delay = 2;
    std::vector<float> noise(static_cast<size_t>(total + delay + 16));
    uint32_t state = 0x13579bdfu;
    for (auto& s : noise)
    {
        state = state * 1664525u + 1013904223u;
        s = (static_cast<int32_t>(state) / 2147483648.0f) * 0.5f;
    }

    for (int i = 0; i < total; ++i)
        g.process(noise[static_cast<size_t>(i)], noise[static_cast<size_t>(i + delay)]);

    REQUIRE_THAT(g.delaySamples(), Catch::Matchers::WithinAbs(static_cast<float>(delay), 0.3f));
}

TEST_CASE("Reset clears the estimator", "[gcc-phat]")
{
    azimuter::GccPhat g;
    g.prepare(kSampleRate);
    settleAndMeasure(g, 4.0);
    REQUIRE(std::abs(g.delaySamples()) > 1.0f);

    g.reset();
    REQUIRE(g.delaySamples() == 0.0f);
    REQUIRE(g.signalLevel() == 0.0f);
}

TEST_CASE("Microseconds matches samples * sample-period", "[gcc-phat]")
{
    azimuter::GccPhat g;
    g.prepare(kSampleRate);
    settleAndMeasure(g, 2.0);

    const float expectedUs = g.delaySamples() * static_cast<float>(1.0e6 / kSampleRate);
    REQUIRE_THAT(g.delayMicroseconds(), Catch::Matchers::WithinAbs(expectedUs, 1.0e-3f));
}

TEST_CASE("Analysis cutoff can be moved live without disturbing the estimate", "[gcc-phat]")
{
    // The cutoff and smoothing sliders used to be inert once the host had
    // prepared the plugin. Both now apply per block, so re-indexing the band
    // mid-stream has to leave a settled reading intact.
    azimuter::GccPhat g;
    g.prepare(kSampleRate, 12, 1024, 6000.0f, 18000.0f, 0.20f);

    const float before = settleAndMeasure(g, 2.0);
    REQUIRE_THAT(before, Catch::Matchers::WithinAbs(2.0f, 0.1f));

    g.setBandLowHz(2000.0f);
    REQUIRE_THAT(settleAndMeasure(g, 2.0), Catch::Matchers::WithinAbs(2.0f, 0.1f));

    g.setBandLowHz(12000.0f);
    REQUIRE_THAT(settleAndMeasure(g, 2.0), Catch::Matchers::WithinAbs(2.0f, 0.1f));
}

TEST_CASE("Smoothing can be changed live and lengthens the average", "[gcc-phat]")
{
    azimuter::GccPhat g;
    g.prepare(kSampleRate, 12, 1024, 6000.0f, 18000.0f, 0.05f);
    REQUIRE_THAT(settleAndMeasure(g, 1.0), Catch::Matchers::WithinAbs(1.0f, 0.1f));

    // A long time constant must make the estimate lag a step change rather
    // than follow it immediately — that lag is the whole point of the slider.
    g.setSmoothingSeconds(2.0f);
    const float afterShortBurst = settleAndMeasure(g, -3.0, 0.15);
    REQUIRE(std::abs(afterShortBurst - (-3.0f)) > 0.5f);

    // Given enough time it still converges on the new delay.
    REQUIRE_THAT(settleAndMeasure(g, -3.0, 12.0), Catch::Matchers::WithinAbs(-3.0f, 0.2f));
}

TEST_CASE("Cutoff above the band top is clamped rather than inverting the band", "[gcc-phat]")
{
    azimuter::GccPhat g;
    g.prepare(kSampleRate, 12, 1024, 6000.0f, 18000.0f, 0.20f);

    g.setBandLowHz(40000.0f); // past both the band top and Nyquist
    const float d = settleAndMeasure(g, 0.0);
    REQUIRE(std::isfinite(d));
}

// --- signal significance -----------------------------------------------------

namespace
{
/** Deterministic Gaussian-ish noise (sum of uniforms), independent per seed. */
struct Noise
{
    unsigned state;
    explicit Noise(unsigned seed) : state(seed) {}
    float next() noexcept
    {
        float acc = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            state = state * 1664525u + 1013904223u;
            acc += static_cast<float>(state >> 8) / 16777216.0f - 0.5f;
        }
        return acc;
    }
};

/** Feed `seconds` of audio; L and R come from the callback. Returns the last
    significance reading. */
template <typename Fn>
float significanceAfter(azimuter::GccPhat& g, double seconds, Fn&& sample)
{
    const int n = static_cast<int>(kSampleRate * seconds);
    for (int i = 0; i < n; ++i)
    {
        float l, r;
        sample(i, l, r);
        g.process(l, r);
    }
    return g.signalSignificance();
}
} // namespace

TEST_CASE("Significance reads ~1.0 when the channels share nothing", "[gcc-phat][significance]")
{
    // The gaps between songs: each track's own hiss, unrelated to the other.
    // The noise-floor normalisation has to put that at 1.0 regardless of the
    // user's smoothing, or the gate threshold would drift with the slider.
    for (float smoothing : {0.05f, 0.25f, 2.0f})
    {
        azimuter::GccPhat g;
        g.prepare(kSampleRate, 12, 1024, 6000.0f, 18000.0f, smoothing);
        Noise a(11u), b(29u);
        const float s = significanceAfter(g, 4.0,
                                          [&](int, float& l, float& r)
                                          {
                                              l = 0.1f * a.next();
                                              r = 0.1f * b.next();
                                          });
        INFO("smoothing " << smoothing << " significance " << s);
        REQUIRE(s > 0.85f);
        REQUIRE(s < 1.15f);
    }
}

TEST_CASE("Significance is high when both channels carry the same signal", "[gcc-phat][significance]")
{
    azimuter::GccPhat g;
    g.prepare(kSampleRate, 12, 1024, 6000.0f, 18000.0f, 0.25f);
    Noise src(5u), hissL(17u), hissR(23u);
    // Shared programme plus independent hiss on each track, 3 samples apart.
    std::vector<float> history(16, 0.0f);
    const float s =
        significanceAfter(g, 4.0,
                          [&](int i, float& l, float& r)
                          {
                              const float v = src.next();
                              history[static_cast<size_t>(i % 16)] = v;
                              l = 0.1f * history[static_cast<size_t>((i + 16 - 3) % 16)] + 0.02f * hissL.next();
                              r = 0.1f * v + 0.02f * hissR.next();
                          });
    INFO("significance " << s);
    REQUIRE(s > 1.5f);
}

TEST_CASE("Significance does not depend on level", "[gcc-phat][significance]")
{
    // The whole point of replacing the -60 dBFS gate: a quiet deck must read
    // the same as a loud one.
    auto run = [](float gain)
    {
        azimuter::GccPhat g;
        g.prepare(kSampleRate, 12, 1024, 6000.0f, 18000.0f, 0.25f);
        Noise src(5u), hissL(17u), hissR(23u);
        return significanceAfter(g, 3.0,
                                 [&](int, float& l, float& r)
                                 {
                                     const float v = src.next();
                                     l = gain * (v + 0.3f * hissL.next());
                                     r = gain * (v + 0.3f * hissR.next());
                                 });
    };
    const float loud = run(0.3f);
    const float quiet = run(0.0003f); // 60 dB down
    REQUIRE(loud > 1.3f);
    REQUIRE_THAT(quiet, Catch::Matchers::WithinRel(loud, 0.01f));
}

TEST_CASE("Significance survives a badly tilted head", "[gcc-phat][significance]")
{
    // Far off azimuth the treble collapses. The gate must stay open so the
    // tone keeps guiding and the sweep edges reach the calibrator. Model the
    // collapse by low-passing both channels hard, at a large delay.
    azimuter::GccPhat g;
    g.prepare(kSampleRate, 12, 1024, 6000.0f, 18000.0f, 0.25f);
    Noise src(5u), hissL(17u), hissR(23u);
    std::vector<float> history(64, 0.0f);
    float lpL = 0.0f, lpR = 0.0f;
    const float s = significanceAfter(g, 4.0,
                                      [&](int i, float& l, float& r)
                                      {
                                          const float v = src.next();
                                          history[static_cast<size_t>(i % 64)] = v;
                                          const float delayed =
                                              history[static_cast<size_t>((i + 64 - 13) % 64)]; // ~270 us
                                          lpL += 0.15f * (delayed - lpL);                       // treble gone
                                          lpR += 0.15f * (v - lpR);
                                          l = lpL + 0.002f * hissL.next();
                                          r = lpR + 0.002f * hissR.next();
                                      });
    INFO("significance " << s);
    REQUIRE(s > 1.3f);
}

TEST_CASE("Digital silence reads as no signal", "[gcc-phat][significance]")
{
    azimuter::GccPhat g;
    g.prepare(kSampleRate, 12, 1024, 6000.0f, 18000.0f, 0.25f);
    const float s = significanceAfter(g, 2.0, [](int, float& l, float& r) { l = r = 0.0f; });
    REQUIRE(s < 1.0f);
}
