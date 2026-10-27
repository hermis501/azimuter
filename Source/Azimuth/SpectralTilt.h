#pragma once

#include <cstddef>
#include <vector>

namespace azimuter
{
/**
    Third-octave spectral tilt — the observable that actually identifies
    correct head azimuth.

    Azimuth misalignment produces *two* effects, rigidly linked by the head
    geometry: an inter-channel time delay, and a within-channel loss of high
    frequencies. `GccPhat` measures the first. This class measures the second.

    Only the second one identifies true azimuth. A tape can carry a fixed
    inter-channel delay of its own — recorded that way by a duplicator with
    gap scatter, or dubbed from a source played back with an azimuth error —
    and no playback head angle can remove it. Nulling the delay on such a tape
    forces the head off the recorded transitions and destroys high frequencies
    permanently. Measured on two of the project's example tapes, aiming at zero
    delay cost 2–3.5 dB and 9–15 dB of the top octave respectively.

    The classical gap-loss law (verified here against Nakamichi's published
    Dragon figures to about 1 dB) is

        loss(f) = -20 * log10 | sinc( W * tan(theta) * f / v ) |

    with W the track width, v the tape speed and theta the angle between the
    playback gap and the recorded transitions. The loss is therefore strongly
    frequency-scaled: at a given tilt, 14 kHz loses roughly three times the dB
    that 7 kHz does.

    That frequency scaling is the whole trick. Programme material also changes
    brightness constantly, but when the music gets brighter *every band rises
    together*. Azimuth loss does not — it rises in the fixed ratio the sinc law
    dictates. Reporting energy per third-octave band, relative to a 1–2.5 kHz
    reference band where azimuth loss is negligible, hands `AzimuthCalibrator`
    exactly the signal it needs to separate the two without any reference
    recording.

    Real-time safe: `prepare()` sizes everything, `accumulate()` allocates
    nothing.
*/
class SpectralTilt
{
public:
    static constexpr int kMaxBands = 12;

    /** Lower edge of the lowest analysis band. Below this, gap loss is too
        small to measure against programme variation. */
    static constexpr float kFirstBandLowHz = 2500.0f;

    /** Reference band. Wide enough to be statistically stable, low enough that
        gap loss inside it is negligible at any tilt worth aligning out. */
    static constexpr float kReferenceLowHz = 1000.0f;
    static constexpr float kReferenceHighHz = 2500.0f;

    SpectralTilt() = default;

    /** @param topHz             Upper edge of the highest band (clamped to Nyquist).
        @param smoothingSeconds  Time constant of the per-band power average.
        @param hopSeconds        Interval between `accumulate()` calls. */
    void prepare(double sampleRate, int fftLength, float topHz, float smoothingSeconds, double hopSeconds);

    void reset();

    /** Time constant of the running per-band average. Recomputes one scalar,
        so it is safe to call from the audio thread. */
    void setSmoothingSeconds(float seconds) noexcept;

    /** Feed one analysis frame's power spectrum — |L|^2 + |R|^2 summed across
        the pair, `fftLength / 2 + 1` entries. Both channels are summed because
        gap loss applies to each equally, and summing buys signal-to-noise
        without touching the inter-channel information `GccPhat` needs. */
    void accumulate(const float* powerSpectrum) noexcept;

    int numBands() const noexcept { return bandCount; }

    /** Geometric-mean centre frequency of a band, in Hz. */
    float centreHz(int band) const noexcept;

    /** Smoothed band power in dB relative to the reference band. This is the
        quantity `AzimuthCalibrator` consumes. */
    float tiltDb(int band) const noexcept;

    /** Smoothed reference-band power in dB. Used to gate out passages too
        quiet to carry a usable spectrum. */
    float referenceLevelDb() const noexcept;

    /** False until the first frame has been accumulated. */
    bool ready() const noexcept { return initialised; }

private:
    struct Band
    {
        int firstBin{0};
        int lastBin{0};
        float centre{0.0f};
    };

    double sampleRate{48000.0};
    int spectrumSize{2049};
    int bandCount{0};
    Band bands[kMaxBands]{};
    int refFirstBin{0};
    int refLastBin{0};

    double hopSeconds{0.0};
    float smoothingAlpha{1.0f};
    bool initialised{false};

    float bandPower[kMaxBands]{};
    float refPower{0.0f};
};
} // namespace azimuter
