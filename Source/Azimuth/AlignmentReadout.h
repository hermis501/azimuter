#pragma once

namespace azimuter
{
/** Coarse alignment-quality bucket. Deliberately coarse: a screen reader that
    fires on every coincidence of the reading is unusable. */
enum class AlignmentBucket
{
    excellent,
    good,
    fair,
    poor,
    severe,
    offScale
};

/** Upper edges of the quality buckets, in microseconds of alignment error.

    Chosen from what an error costs in high frequencies (measured on a real
    sweep, worst band ~14 kHz) and kept above the reading's own wobble, so the
    spoken quality only changes when the alignment does:

        excellent  < 5 us    ~0.02 dB   inaudible
        good       < 15 us   < 0.2 dB
        fair       < 30 us   < 0.7 dB
        poor       < 60 us   < 3 dB     audibly dull
        severe     < full scale
*/
inline constexpr float kExcellentUs = 5.0f;
inline constexpr float kGoodUs = 15.0f;
inline constexpr float kFairUs = 30.0f;
inline constexpr float kPoorUs = 60.0f;

/** Bucket for an alignment error. `severe` runs up to
    `fullScaleUs` (azimuter::DeflectionMap::fullScaleMicroseconds), past which
    the sonifier has saturated and the reading is reported as off-scale. */
AlignmentBucket bucketForError(float errorUs, float fullScaleUs) noexcept;

/** The spoken phrase for a bucket, e.g. "Good alignment, left side".
    `excellent` carries no side — there is no side to turn towards once you are
    there, and naming one would invite the user to keep moving. */
const char* bucketName(AlignmentBucket bucket) noexcept;

/**
    Decides *when* the alignment quality should be spoken.

    The reading wobbles by several microseconds on its own: about 6 µs at the
    default 0.25 s smoothing, about 2 µs at 2 s. The original bucket scale
    (0.5, 2, 5, 15 µs) put three boundaries inside that noise. A first attempt using a short
    median and a 100 ms hold still bounced between buckets every few hundred
    milliseconds, and in Reaper each bounce started a new announcement that cut
    the previous one off. "Excellent" began to be spoken and was immediately
    talked over, constantly. This version is built to stay quiet unless
    something real and lasting has happened. It does four things:

    1. **Median over about a second** of polls, not a single sample.
    2. **Hysteresis.** Getting *better* uses the nominal boundaries. Getting
       *worse* has to clear the boundary by a clear margin
       (`boundary * kWorseFactor + kWorseMarginUs`), so a reading sitting near
       a boundary stays in the better bucket instead of flipping back and
       forth.
    3. **Hold time.** An improvement must persist for `kDwellPolls` (about
       0.75 s) before it counts; a worsening for `kWorseDwellPolls` (about 2 s).
    4. **Minimum gap.** There are at least `kMinIntervalPolls` (3 s) between
       announcements, so each one has time to be spoken in full. A change that
       becomes due inside the gap is held back, then spoken once the gap is
       over, as long as it is still true.

    In addition, the left/right side is only announced from Fair upward. Below
    15 µs the error costs under 0.2 dB and its sign is close to the noise, so
    "Good, left side" followed by "Good, right side" is not an instruction
    anyone should act on.

    Pure logic, no JUCE: the editor owns the speaking, this owns the deciding.
    All timing is counted in polls; the editor polls at 20 Hz.
*/
class AlignmentAnnouncer
{
public:
    static constexpr int kPollHz = 20;

    /** About a second of history for the median. */
    static constexpr int kMedianWindow = 21;

    /** An *improvement* must hold this many consecutive polls (~0.75 s). */
    static constexpr int kDwellPolls = 15;

    /** A *worsening* must hold this long (~2 s). Turning the screw away from
        the optimum is sustained; a noise excursion past the hysteresis margin
        is not. The asymmetry is what stops "Excellent" and "Good" trading places
        while the user holds still. The tone, not the speech, is the instant
        cue for turning the wrong way. */
    static constexpr int kWorseDwellPolls = 40;

    /** Minimum gap between announcements (3 s). Long enough for a screen
        reader to say "Good alignment, left side." without being cut off. */
    static constexpr int kMinIntervalPolls = 60;

    /** Hysteresis on getting worse: leaving bucket `b` towards a worse one
        requires |error| >= upperBoundary(b) * kWorseFactor + kWorseMarginUs. */
    static constexpr float kWorseFactor = 1.5f;
    static constexpr float kWorseMarginUs = 0.5f;

    /** What gets spoken.

        `arrivalOnly` is what the plugin uses: while turning the screw, the
        tone is the guidance and speech only confirms the one thing the tone
        can't state absolutely: "you are there". It speaks when the settled
        bucket becomes Excellent, and only again after the reading has really
        left Excellent (past the hysteresis margin, held ~2 s) and come back.
        Users found the intermediate "Poor / Fair / Good" announcements talked
        over the tone they were steering by.

        `everyChange` speaks every settled change of quality or side. */
    enum class Mode
    {
        arrivalOnly,
        everyChange
    };

    void setMode(Mode m) noexcept { mode = m; }
    Mode getMode() const noexcept { return mode; }

    /** Clears history and spoken state. The mode is kept. */
    void reset() noexcept;

    /** Feed one poll of the alignment error. Returns true when the caller
        should speak `bucket()` now. In `arrivalOnly` mode that is only ever
        Excellent. */
    bool push(float errorUs, float fullScaleUs) noexcept;

    /** The hysteresis-stabilised bucket. This is what to show and speak. */
    AlignmentBucket bucket() const noexcept { return held; }

    /** Median error the bucket was derived from. Signed. */
    float settledErrorUs() const noexcept { return settledError; }

    /** Side to name: -1 left, +1 right, 0 when no side should be named
        (Excellent and Good). */
    int side() const noexcept { return heldSide; }

    /** True once the median window has filled. Until then `push` never
        announces. */
    bool ready() const noexcept { return filled; }

private:
    float history[kMedianWindow]{};
    int writePos{0};
    int count{0};
    bool filled{false};

    float settledError{0.0f};

    // Hysteresis-stabilised state.
    AlignmentBucket held{AlignmentBucket::offScale};
    int heldSide{0};
    bool haveHeld{false};

    // Candidate change waiting out its hold time.
    AlignmentBucket candidate{AlignmentBucket::offScale};
    int candidateSide{0};
    int dwell{0};

    // What was last spoken, and how long ago.
    bool haveAnnounced{false};
    AlignmentBucket announcedBucket{AlignmentBucket::offScale};
    int announcedSide{0};
    int pollsSinceAnnouncement{0};

    Mode mode{Mode::arrivalOnly};
    // arrivalOnly: true once the held bucket has been something other than
    // Excellent since the last arrival was spoken. Starts true, so starting
    // already aligned is announced once.
    bool awayFromExcellent{true};
};
} // namespace azimuter
