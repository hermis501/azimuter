#include "Azimuth/AlignmentReadout.h"
#include "Azimuth/DeflectionMap.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace
{
constexpr float kFullScale = azimuter::DeflectionMap::fullScaleMicroseconds;
constexpr int kPollHz = azimuter::AlignmentAnnouncer::kPollHz;

using azimuter::AlignmentAnnouncer;
using azimuter::AlignmentBucket;

/** The reading's wobble, modelled the way it is produced: white noise through a
    one-pole smoother. Independent samples would let a median average the noise
    away far more effectively than it can in reality, and would hide exactly the
    flicker users heard. */
class ReadingNoise
{
public:
    /** @param sigmaUs          standard deviation of the wobble
        @param smoothingSeconds the analyser's smoothing setting */
    ReadingNoise(float sigmaUs, float smoothingSeconds, unsigned seed = 12345u)
        : sigma(sigmaUs), rho(std::exp(-1.0f / (static_cast<float>(kPollHz) * smoothingSeconds))), state(seed)
    {
    }

    float next() noexcept
    {
        value = rho * value + std::sqrt(1.0f - rho * rho) * sigma * gaussian();
        return value;
    }

private:
    float uniform() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (static_cast<float>(state >> 8) + 0.5f) / 16777216.0f;
    }

    float gaussian() noexcept
    {
        const float u1 = uniform();
        const float u2 = uniform();
        return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
    }

    float sigma;
    float rho;
    float value{0.0f};
    unsigned state;
};

struct Announcement
{
    int poll;
    AlignmentBucket bucket;
    int side;
};

/** Run a scenario: `trueError(poll)` plus reading noise, for `polls` polls. */
template <typename Fn>
std::vector<Announcement> run(AlignmentAnnouncer& a, Fn&& trueError, ReadingNoise& noise, int polls)
{
    std::vector<Announcement> out;
    for (int i = 0; i < polls; ++i)
        if (a.push(trueError(i) + noise.next(), kFullScale))
            out.push_back({i, a.bucket(), a.side()});
    return out;
}

int minimumGap(const std::vector<Announcement>& v)
{
    int gap = 1 << 30;
    for (size_t i = 1; i < v.size(); ++i)
        gap = std::min(gap, v[i].poll - v[i - 1].poll);
    return gap;
}
} // namespace

// --- bucket table ------------------------------------------------------------

TEST_CASE("Bucket boundaries are where the manual says", "[readout]")
{
    REQUIRE(azimuter::bucketForError(0.0f, kFullScale) == AlignmentBucket::excellent);
    REQUIRE(azimuter::bucketForError(4.9f, kFullScale) == AlignmentBucket::excellent);
    REQUIRE(azimuter::bucketForError(5.1f, kFullScale) == AlignmentBucket::good);
    REQUIRE(azimuter::bucketForError(14.9f, kFullScale) == AlignmentBucket::good);
    REQUIRE(azimuter::bucketForError(15.1f, kFullScale) == AlignmentBucket::fair);
    REQUIRE(azimuter::bucketForError(29.9f, kFullScale) == AlignmentBucket::fair);
    REQUIRE(azimuter::bucketForError(30.1f, kFullScale) == AlignmentBucket::poor);
    REQUIRE(azimuter::bucketForError(59.9f, kFullScale) == AlignmentBucket::poor);
    REQUIRE(azimuter::bucketForError(60.1f, kFullScale) == AlignmentBucket::severe);
    REQUIRE(azimuter::bucketForError(kFullScale - 1.0f, kFullScale) == AlignmentBucket::severe);
    REQUIRE(azimuter::bucketForError(kFullScale + 1.0f, kFullScale) == AlignmentBucket::offScale);
}

TEST_CASE("Excellent and Good are wider than the reading's own wobble", "[readout]")
{
    // The whole point of the rescale. The reading wobbles ~6 us at default
    // smoothing; a bucket narrower than that cannot be held, and its boundary
    // turns noise into speech.
    REQUIRE(azimuter::kExcellentUs >= 5.0f);
    REQUIRE(azimuter::kGoodUs - azimuter::kExcellentUs >= 6.0f);
}

TEST_CASE("Buckets are symmetric in the sign of the error", "[readout]")
{
    for (float us : {0.2f, 1.0f, 3.0f, 9.0f, 40.0f, 400.0f})
        REQUIRE(azimuter::bucketForError(us, kFullScale) == azimuter::bucketForError(-us, kFullScale));
}

TEST_CASE("Every bucket has a spoken name", "[readout]")
{
    for (auto b : {AlignmentBucket::excellent, AlignmentBucket::good, AlignmentBucket::fair, AlignmentBucket::poor,
                   AlignmentBucket::severe, AlignmentBucket::offScale})
        REQUIRE_FALSE(std::string(azimuter::bucketName(b)).empty());
    REQUIRE(std::string(azimuter::bucketName(AlignmentBucket::excellent)) == "Excellent alignment");
}

// --- the failure users reported ----------------------------------------------

TEST_CASE("Holding still at the optimum does not chatter at default smoothing", "[readout]")
{
    // Default 0.25 s smoothing: ~6 us of wobble. This is the condition under
    // which users heard announcements starting and being cut off constantly.
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    ReadingNoise noise(6.0f, 0.25f);
    const auto said = run(a, [](int) { return 0.0f; }, noise, 120 * kPollHz); // two minutes

    INFO("announcements in two minutes: " << said.size());
    REQUIRE(said.size() <= 8);
    if (said.size() > 1)
        REQUIRE(minimumGap(said) >= AlignmentAnnouncer::kMinIntervalPolls);
}

TEST_CASE("Holding still at the optimum is calm at long smoothing", "[readout]")
{
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    ReadingNoise noise(2.0f, 2.0f);
    const auto said = run(a, [](int) { return 0.0f; }, noise, 120 * kPollHz);

    INFO("announcements in two minutes: " << said.size());
    REQUIRE(said.size() <= 4);
    if (said.size() > 1)
        REQUIRE(minimumGap(said) >= AlignmentAnnouncer::kMinIntervalPolls);
}

TEST_CASE("Arriving at the optimum with long smoothing announces Excellent", "[readout]")
{
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    ReadingNoise noise(2.0f, 2.0f);
    // Turn in from 30 us over ten seconds, then hold still for twenty.
    const auto said = run(
        a, [](int i) { return i < 10 * kPollHz ? 30.0f * (1.0f - float(i) / float(10 * kPollHz)) : 0.0f; }, noise,
        30 * kPollHz);

    REQUIRE_FALSE(said.empty());
    REQUIRE(said.back().bucket == AlignmentBucket::excellent);
}

TEST_CASE("No announcement is ever cut off by the next one", "[readout]")
{
    // Sweep back and forth through every bucket. Whatever is said, there must
    // always be at least the minimum gap before the next thing is said.
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    ReadingNoise noise(4.0f, 0.5f);
    const auto said = run(a, [](int i) { return 60.0f * std::sin(float(i) / 120.0f); }, noise, 180 * kPollHz);

    REQUIRE(said.size() > 2);
    REQUIRE(minimumGap(said) >= AlignmentAnnouncer::kMinIntervalPolls);
}

// --- mechanics -----------------------------------------------------------------

TEST_CASE("Nothing is announced until the median window has filled", "[readout]")
{
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    for (int i = 0; i < AlignmentAnnouncer::kMedianWindow - 1; ++i)
        REQUIRE_FALSE(a.push(0.1f, kFullScale));
    REQUIRE_FALSE(a.ready());
}

TEST_CASE("A steady reading is announced once", "[readout]")
{
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    int n = 0;
    for (int i = 0; i < 600; ++i)
        n += a.push(40.0f, kFullScale) ? 1 : 0;
    REQUIRE(n == 1);
    REQUIRE(a.bucket() == AlignmentBucket::poor);
}

TEST_CASE("A brief spike does not trigger an announcement", "[readout]")
{
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    for (int i = 0; i < 200; ++i)
        a.push(0.1f, kFullScale);
    REQUIRE(a.bucket() == AlignmentBucket::excellent);

    bool fired = false;
    for (int i = 0; i < 400; ++i)
        fired = a.push((i % 40) < 4 ? 80.0f : 0.1f, kFullScale) || fired; // 200 ms spikes, every 2 s
    REQUIRE_FALSE(fired);
    REQUIRE(a.bucket() == AlignmentBucket::excellent);
}

TEST_CASE("Hysteresis: hovering just past a boundary stays in the better bucket", "[readout]")
{
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    for (int i = 0; i < 200; ++i)
        a.push(10.0f, kFullScale);
    REQUIRE(a.bucket() == AlignmentBucket::good);

    // 18 us is nominally Fair, but not far enough past 15 us to leave Good.
    bool fired = false;
    for (int i = 0; i < 400; ++i)
        fired = a.push(18.0f, kFullScale) || fired;
    REQUIRE_FALSE(fired);
    REQUIRE(a.bucket() == AlignmentBucket::good);

    // Clearly worse does move it.
    for (int i = 0; i < 400; ++i)
        fired = a.push(26.0f, kFullScale) || fired;
    REQUIRE(fired);
    REQUIRE(a.bucket() == AlignmentBucket::fair);
}

TEST_CASE("Getting better is recognised at the nominal boundary", "[readout]")
{
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    for (int i = 0; i < 200; ++i)
        a.push(20.0f, kFullScale);
    REQUIRE(a.bucket() == AlignmentBucket::fair);

    for (int i = 0; i < 400; ++i)
        a.push(3.0f, kFullScale);
    REQUIRE(a.bucket() == AlignmentBucket::excellent);
}

TEST_CASE("Side is named only from Fair upward", "[readout]")
{
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    for (int i = 0; i < 200; ++i)
        a.push(-10.0f, kFullScale);
    REQUIRE(a.bucket() == AlignmentBucket::good);
    REQUIRE(a.side() == 0);

    // Crossing zero inside Good is not an instruction worth acting on: silence.
    bool fired = false;
    for (int i = 0; i < 400; ++i)
        fired = a.push(10.0f, kFullScale) || fired;
    REQUIRE_FALSE(fired);

    // Out at Fair (past the hysteresis margin, 15 * 1.5 + 0.5 = 23 us), the
    // side is real and a crossing is announced.
    for (int i = 0; i < 400; ++i)
        a.push(-26.0f, kFullScale);
    REQUIRE(a.bucket() == AlignmentBucket::fair);
    REQUIRE(a.side() == -1);
    fired = false;
    for (int i = 0; i < 400; ++i)
        fired = a.push(26.0f, kFullScale) || fired;
    REQUIRE(fired);
    REQUIRE(a.side() == 1);
}

TEST_CASE("A change that arrives during the gap is spoken after it, if still true", "[readout]")
{
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    int first = -1, second = -1;
    for (int i = 0; i < 1000 && second < 0; ++i)
    {
        const float e = i < 100 ? 40.0f : 0.2f; // Poor, then quickly Excellent
        if (a.push(e, kFullScale))
            (first < 0 ? first : second) = i;
    }
    REQUIRE(first >= 0);
    REQUIRE(second >= 0);
    REQUIRE(second - first >= AlignmentAnnouncer::kMinIntervalPolls);
    REQUIRE(a.bucket() == AlignmentBucket::excellent);
}

TEST_CASE("reset() clears history and announced state", "[readout]")
{
    AlignmentAnnouncer a;
    a.setMode(AlignmentAnnouncer::Mode::everyChange);
    for (int i = 0; i < 200; ++i)
        a.push(9.0f, kFullScale);
    REQUIRE(a.ready());

    a.reset();
    REQUIRE_FALSE(a.ready());

    bool fired = false;
    for (int i = 0; i < 200; ++i)
        fired = a.push(9.0f, kFullScale) || fired;
    REQUIRE(fired);
}

// --- arrival-only: what the plugin actually speaks while turning ---------------

namespace
{
std::vector<Announcement> runArrival(float sigma, float smoothing, int seconds, float (*path)(float), unsigned seed)
{
    AlignmentAnnouncer a; // default mode
    REQUIRE(a.getMode() == AlignmentAnnouncer::Mode::arrivalOnly);
    ReadingNoise noise(sigma, smoothing, seed);
    return run(a, [path](int i) { return path(float(i) / kPollHz); }, noise, seconds * kPollHz);
}

float holdAtOptimum(float)
{
    return 0.0f;
}
float holdAtTwenty(float)
{
    return 20.0f;
}
float turnIn(float t)
{
    return t < 20.0f ? 60.0f * (1.0f - t / 20.0f) : 0.0f;
}
float inAwayBack(float t)
{
    if (t < 20.0f)
        return 60.0f * (1.0f - t / 20.0f); // turn in
    if (t < 35.0f)
        return 0.0f; // hold
    if (t < 45.0f)
        return 40.0f * (t - 35.0f) / 10.0f; // turn away
    if (t < 55.0f)
        return 40.0f; // hold away
    if (t < 65.0f)
        return 40.0f * (1.0f - (t - 55.0f) / 10.0f); // back in
    return 0.0f;
}
} // namespace

TEST_CASE("Arrival-only: only Excellent is ever spoken", "[readout][arrival]")
{
    for (unsigned seed = 1; seed <= 20; ++seed)
        for (const auto& x : runArrival(6.0f, 0.25f, 80, inAwayBack, seed))
            REQUIRE(x.bucket == AlignmentBucket::excellent);
}

TEST_CASE("Arrival-only: turning in says Excellent once, on arrival", "[readout][arrival]")
{
    for (float smoothing : {0.25f, 2.0f})
    {
        const float sigma = 6.0f * std::sqrt(0.25f / smoothing);
        const auto said = runArrival(sigma, smoothing, 60, turnIn, 3u);
        REQUIRE(said.size() == 1);
        // Excellent (< 5 us) is reached at ~18.3 s; spoken within ~2 s of that.
        REQUIRE(said[0].poll >= 17 * kPollHz);
        REQUIRE(said[0].poll <= 21 * kPollHz);
    }
}

TEST_CASE("Arrival-only: holding still at the optimum stays quiet after the arrival", "[readout][arrival]")
{
    int worst = 0;
    for (unsigned seed = 1; seed <= 50; ++seed)
        worst = std::max(worst, int(runArrival(6.0f, 0.25f, 120, holdAtOptimum, seed * 7919u).size()));
    INFO("worst announcements in two minutes across 50 seeds: " << worst);
    REQUIRE(worst <= 2);
}

TEST_CASE("Arrival-only: never at Excellent means never spoken", "[readout][arrival]")
{
    REQUIRE(runArrival(6.0f, 0.25f, 120, holdAtTwenty, 5u).empty());
}

TEST_CASE("Arrival-only: leaving and coming back is spoken again", "[readout][arrival]")
{
    const auto said = runArrival(2.0f, 2.0f, 80, inAwayBack, 9u);
    REQUIRE(said.size() == 2);
    REQUIRE(said[0].poll < 30 * kPollHz); // first arrival
    REQUIRE(said[1].poll > 60 * kPollHz); // the return
}

TEST_CASE("Arrival-only: starting already aligned is spoken once", "[readout][arrival]")
{
    AlignmentAnnouncer a;
    int n = 0;
    for (int i = 0; i < 600; ++i)
        n += a.push(0.5f, kFullScale) ? 1 : 0;
    REQUIRE(n == 1);
}

TEST_CASE("Arrival-only: reset keeps the mode", "[readout][arrival]")
{
    AlignmentAnnouncer a;
    a.reset();
    REQUIRE(a.getMode() == AlignmentAnnouncer::Mode::arrivalOnly);
}
