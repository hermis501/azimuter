#pragma once

#include <algorithm>
#include <cmath>

namespace azimuter
{
/** Mapping from a measured inter-channel delay (microseconds) to the signed
    normalised deflection the Sonifier consumes.

    The two cues the deflection drives have very different useful ranges, and
    a single linear map cannot serve both:

      - **Pitch** carries the fine information. It glides from `highHz` at
        lock down to `lowHz`, bottoming out at |deflection| = 0.5. This is
        the cue the user rides while the screwdriver is in their hand, so its
        resolution near zero must not be diluted.
      - **Pulse depth** carries the coarse "you are nowhere near lock"
        information, ramping in from |deflection| = 0.5 to 1.

    So the map is piecewise: linear to 0.5 across the fine span, then linear
    to 1 across the much wider coarse span. Widening full scale therefore
    extends the coarse range without touching fine resolution.

    Measured azimuth drift across one C-90 side spans 55-75 microseconds, so
    a full scale of 50 saturated both cues during ordinary use — two heads
    50 and 90 microseconds out produced an identical sound. */
struct DeflectionMap
{
    /** |delay| at which the pitch glide bottoms out and the pulse starts. */
    static constexpr float pitchFloorMicroseconds = 25.0f;

    /** |delay| that maps to full deflection: lowest pitch, hardest pan, full
        pulse depth, and the readout's "Off-scale" bucket. */
    static constexpr float fullScaleMicroseconds = 100.0f;

    static float fromMicroseconds(float delayUs) noexcept
    {
        const float magnitude = std::abs(delayUs);
        const float sign = delayUs < 0.0f ? -1.0f : 1.0f;

        const float d =
            magnitude <= pitchFloorMicroseconds
                ? 0.5f * magnitude / pitchFloorMicroseconds
                : 0.5f + 0.5f * (magnitude - pitchFloorMicroseconds) / (fullScaleMicroseconds - pitchFloorMicroseconds);

        return sign * std::clamp(d, 0.0f, 1.0f);
    }
};
} // namespace azimuter
