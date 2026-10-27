#pragma once

namespace azimuter
{
/**
    Decides whether there is anything worth guiding on: the sonifier's
    "silence" state.

    It used to be an absolute level test (input RMS above about -60 dBFS). That
    was wrong in both directions. A quiet deck could fall below it in the middle
    of perfectly measurable music. And the gaps between songs, where only each
    track's own hiss remains, sat above it, so the tone kept playing with its
    pitch and pan wandering at random, because hiss on two separate tracks
    carries no shared delay to measure.

    It now runs on `GccPhat::signalSignificance()`, which asks the question that
    actually matters ("is there something shared by both tracks?") and is
    independent of level. Thresholds and hold times were chosen by running the
    real analysis chain over the full example transfers and both sweeps:

      - on: significance >= kOnThreshold, held kOnHoldSeconds
      - off: significance <  kOffThreshold, held kOffHoldSeconds

    Measured at the default smoothing: tone silent for 0.00-0.04 % of music
    time (one or two momentary dropouts per 20-28 minute side), and on in only
    2-8 % of gap time on the full transfers. It stays on at far-off head
    positions, where the treble has collapsed, because the significance comes
    from the coarse band.

    The cost is about a second of latency at each edge: the tone starts roughly
    0.9 s after music begins and stops about a second after it ends. Telling
    shared signal from noise takes a few frames of evidence; an instant level
    check was fast, but it was answering the wrong question.
*/
class SignalGate
{
public:
    static constexpr float kOnThreshold = 1.30f;
    static constexpr float kOffThreshold = 1.15f;
    static constexpr float kOnHoldSeconds = 0.2f;
    static constexpr float kOffHoldSeconds = 1.0f;

    void reset() noexcept
    {
        state = false;
        timer = 0.0f;
    }

    /** Feed the current significance covering `seconds` of audio. Returns
        whether a signal is present after this update. */
    bool update(float significance, float seconds) noexcept
    {
        if (!state)
        {
            timer = significance >= kOnThreshold ? timer + seconds : 0.0f;
            if (timer >= kOnHoldSeconds)
            {
                state = true;
                timer = 0.0f;
            }
        }
        else
        {
            timer = significance < kOffThreshold ? timer + seconds : 0.0f;
            if (timer >= kOffHoldSeconds)
            {
                state = false;
                timer = 0.0f;
            }
        }
        return state;
    }

    bool present() const noexcept { return state; }

private:
    bool state{false};
    float timer{0.0f};
};
} // namespace azimuter
