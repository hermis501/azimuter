#include "Azimuth/AlignmentReadout.h"

#include <algorithm>
#include <cmath>

namespace azimuter
{

AlignmentBucket bucketForError(float errorUs, float fullScaleUs) noexcept
{
    // Boundaries are set by what an error costs in high frequencies, measured
    // on a real sweep (worst band, ~14 kHz), not by what the meter can display.
    // They must also sit above the reading's own wobble — ~6 us at default
    // smoothing, ~2 us at 2 s — or the spoken quality flips with nothing
    // actually changing. The original 0.5 / 2 / 5 / 15 us scale failed that
    // test and made the announcements unusable.
    const float a = std::abs(errorUs);
    if (a < kExcellentUs) // ~0.02 dB: inaudible
        return AlignmentBucket::excellent;
    if (a < kGoodUs) // under ~0.2 dB
        return AlignmentBucket::good;
    if (a < kFairUs) // under ~0.7 dB
        return AlignmentBucket::fair;
    if (a < kPoorUs) // under ~3 dB: audibly dull
        return AlignmentBucket::poor;
    if (a < fullScaleUs)
        return AlignmentBucket::severe;
    return AlignmentBucket::offScale;
}

const char* bucketName(AlignmentBucket bucket) noexcept
{
    switch (bucket)
    {
    case AlignmentBucket::excellent:
        return "Excellent alignment";
    case AlignmentBucket::good:
        return "Good alignment";
    case AlignmentBucket::fair:
        return "Fair alignment";
    case AlignmentBucket::poor:
        return "Poor alignment";
    case AlignmentBucket::severe:
        return "Severe alignment error";
    case AlignmentBucket::offScale:
    default:
        return "Off-scale";
    }
}

namespace
{
int indexOf(AlignmentBucket b) noexcept
{
    return static_cast<int>(b);
}

/** Upper edge of a bucket in microseconds; off-scale has none. */
float upperBoundary(AlignmentBucket b, float fullScaleUs) noexcept
{
    switch (b)
    {
    case AlignmentBucket::excellent:
        return kExcellentUs;
    case AlignmentBucket::good:
        return kGoodUs;
    case AlignmentBucket::fair:
        return kFairUs;
    case AlignmentBucket::poor:
        return kPoorUs;
    case AlignmentBucket::severe:
        return fullScaleUs;
    case AlignmentBucket::offScale:
    default:
        return 1.0e30f;
    }
}

/** Side is only named once the error is clearly larger than the wobble, and
    large enough to be worth acting on: from Fair upward. */
int namedSide(AlignmentBucket b, float errorUs) noexcept
{
    if (indexOf(b) < indexOf(AlignmentBucket::fair))
        return 0;
    return errorUs < 0.0f ? -1 : 1;
}
} // namespace

void AlignmentAnnouncer::reset() noexcept
{
    writePos = 0;
    count = 0;
    filled = false;
    settledError = 0.0f;
    held = AlignmentBucket::offScale;
    heldSide = 0;
    haveHeld = false;
    candidate = AlignmentBucket::offScale;
    candidateSide = 0;
    dwell = 0;
    haveAnnounced = false;
    announcedBucket = AlignmentBucket::offScale;
    announcedSide = 0;
    pollsSinceAnnouncement = 0;
    awayFromExcellent = true;
}

bool AlignmentAnnouncer::push(float errorUs, float fullScaleUs) noexcept
{
    if (!std::isfinite(errorUs))
        return false;

    history[writePos] = errorUs;
    writePos = (writePos + 1) % kMedianWindow;
    if (count < kMedianWindow)
        ++count;
    filled = count >= kMedianWindow;

    float scratch[kMedianWindow];
    std::copy(history, history + count, scratch);
    std::sort(scratch, scratch + count);
    settledError = scratch[count / 2];

    if (pollsSinceAnnouncement < kMinIntervalPolls)
        ++pollsSinceAnnouncement;

    if (!filled)
        return false;

    // --- hysteresis -----------------------------------------------------------
    // Better: nominal boundaries, so an improvement is recognised promptly.
    // Worse: must clear the current bucket's upper edge by a clear margin.
    const float magnitude = std::abs(settledError);
    const AlignmentBucket nominal = bucketForError(settledError, fullScaleUs);
    AlignmentBucket proposed = nominal;
    if (haveHeld && indexOf(nominal) > indexOf(held))
    {
        const float threshold = upperBoundary(held, fullScaleUs) * kWorseFactor + kWorseMarginUs;
        if (magnitude < threshold)
            proposed = held;
    }
    const int proposedSide = namedSide(proposed, settledError);

    // --- hold time ------------------------------------------------------------
    if (haveHeld && proposed == held && proposedSide == heldSide)
    {
        dwell = 0;
    }
    else
    {
        if (dwell > 0 && proposed == candidate && proposedSide == candidateSide)
            ++dwell;
        else
        {
            candidate = proposed;
            candidateSide = proposedSide;
            dwell = 1;
        }

        const bool worse = haveHeld && indexOf(candidate) > indexOf(held);
        if (dwell >= (worse ? kWorseDwellPolls : kDwellPolls))
        {
            held = candidate;
            heldSide = candidateSide;
            haveHeld = true;
            dwell = 0;
        }
    }

    if (!haveHeld)
        return false;

    // --- arrival-only ---------------------------------------------------------
    // Speak Excellent on arrival; stay silent about everything else. Leaving
    // Excellent is governed by the same hysteresis and 2 s hold as above, so
    // the wobble at the optimum cannot make it repeat.
    if (mode == Mode::arrivalOnly)
    {
        if (held != AlignmentBucket::excellent)
        {
            awayFromExcellent = true;
            return false;
        }
        if (!awayFromExcellent)
            return false; // this arrival has already been spoken
        if (haveAnnounced && pollsSinceAnnouncement < kMinIntervalPolls)
            return false; // held back; spoken once the gap is over if still true

        awayFromExcellent = false;
        haveAnnounced = true;
        announcedBucket = held;
        announcedSide = heldSide;
        pollsSinceAnnouncement = 0;
        return true;
    }

    // --- minimum gap ----------------------------------------------------------
    const bool differs = !haveAnnounced || held != announcedBucket || heldSide != announcedSide;
    if (!differs)
        return false;
    if (haveAnnounced && pollsSinceAnnouncement < kMinIntervalPolls)
        return false; // held back; spoken once the gap is over if still true

    haveAnnounced = true;
    announcedBucket = held;
    announcedSide = heldSide;
    pollsSinceAnnouncement = 0;
    return true;
}
} // namespace azimuter
