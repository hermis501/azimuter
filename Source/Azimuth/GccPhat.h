#pragma once

#include "Azimuth/SpectralTilt.h"

#include <cstddef>
#include <vector>

namespace azimuter
{
/**
    Inter-channel time-delay estimator based on GCC-PHAT
    (Generalised Cross-Correlation with Phase Transform).

    Cassette azimuth misalignment manifests as a small inter-channel time
    delay τ between the left and right gap halves. The audio-level effect
    is a frequency-dependent phase shift, classically tracked by HF
    correlation — but that reading is heavily modulated by program-material
    spectral content (loud snare → jump, soft bass passage → drop), making
    it nearly unusable for fine alignment.

    GCC-PHAT sidesteps that by whitening the cross-spectrum: each
    frequency bin's contribution is normalised to unit magnitude before
    the inverse transform, so loud transients no longer dominate. The
    output is a direct, physical quantity — signed inter-channel delay
    in samples — that depends almost entirely on the gap geometry and
    barely on the program material. As a bonus, tape hiss alone provides
    enough broadband reference for the estimate to lock on during the
    quietest musical passages.

    Pipeline per analysis frame (every `hopSize` samples):
      1. Windowed (Hann) FFT of both channels.
      2. Cross-spectrum  C(f) = L(f) * conj(R(f)).
      3. PHAT normalisation: each bin in the configured HF pass-band gets
         divided by its magnitude; bins outside the band are zeroed
         (band-limited PHAT, focused where azimuth phase shift is biggest).
      4. Welch-style time averaging into a running cross-spectrum estimate
         (controllable via `smoothingSeconds`).
      5. Inverse FFT → smoothed cross-correlation r(τ).
      6. Peak search inside a small ±τ_max window with parabolic
         sub-sample interpolation → signed delay.

    Convention: positive delay means the **right channel** leads the
    **left channel** (R arrived first). The plugin's sonifier uses the
    sign to drive the stereo direction cue, so users hear "tone on the
    right side" → turn the screw to retard the right gap, and vice
    versa.

    Real-time safe: no allocation, locks, or system calls in `process()`
    or `analyse()`. All buffers are sized once in `prepare()`.
*/
class GccPhat
{
public:
    GccPhat() = default;

    /** Configure the analyser.
        @param sampleRate        Audio sample rate (Hz).
        @param fftOrder          log2 of the FFT size. 12 → 4096 samples,
                                 a good default at 44.1/48 kHz (≈85 ms window).
        @param hopSize           Samples between successive analyses. 0 → fftSize/4.
        @param hfBandLowHz       Lower edge of the PHAT pass-band. Bins below
                                 this contribute nothing to the cross-correlation.
        @param hfBandHighHz      Upper edge of the PHAT pass-band. 0 → Nyquist.
        @param smoothingSeconds  Time constant of the running cross-spectrum
                                 average. Longer = steadier reading, slower
                                 response when the user turns the screw.
    */
    void prepare(double sampleRate, int fftOrder = 12, int hopSize = 0, float hfBandLowHz = 2000.0f,
                 float hfBandHighHz = 0.0f, float smoothingSeconds = 0.20f);

    void reset();

    /** Live analysis controls. Both only recompute scalars — no buffer
        resizing and no allocation — so the processor can apply parameter
        changes per block instead of waiting for the host to re-prepare
        the plugin. Safe to call from the audio thread. */
    void setSmoothingSeconds(float seconds) noexcept;
    void setBandLowHz(float hz) noexcept;

    /** Lower edge of the coarse pass-band. Wide enough that its correlation
        envelope carries no carrier ambiguity, so the coarse peak identifies
        which lobe the fine estimate belongs in. */
    static constexpr float kCoarseBandLowHz = 1000.0f;

    /** Push one stereo sample. The estimate updates internally every `hopSize`
        samples; in between, `delaySamples()` continues to return the most
        recent value. */
    void process(float left, float right) noexcept;

    /** Latest signed inter-channel delay in samples (positive = R leads L). */
    float delaySamples() const noexcept { return currentDelaySamples; }

    /** Same, expressed in microseconds — friendlier unit for the user-facing
        readout. */
    float delayMicroseconds() const noexcept;

    /** Smoothed broadband RMS of the input (averaged across channels).
        Below ~1e-3 the analyser has nothing to lock onto — tape paused, deck
        muted, etc. — and the consumer should mute downstream UX. */
    float signalLevel() const noexcept { return rmsLevel; }

    /** Unambiguous wide-band estimate, in microseconds. The fine estimate is
        searched within half a carrier period of this, which is the classical
        "start low, work up" azimuth procedure collapsed into one pass. */
    float coarseDelayMicroseconds() const noexcept;

    /** Mean magnitude of the time-averaged unit phasors across the fine band:
        1 means every frame agreed on the same delay, 0 means noise. Cheaper
        than classical coherence and more to the point — it answers "is there a
        stable delay here at all", which is exactly what gates calibration. */
    float phaseCoherence() const noexcept { return currentPhaseCoherence; }

    /** How far the inter-channel agreement over the coarse 1-18 kHz band sits
        above what pure noise would produce. 1.0 means "indistinguishable from
        noise": nothing shared by both tracks to measure. Music sits around
        1.5-3; the gaps between songs, where only each track's own hiss
        remains, sit at about 1.0.

        This is the gate for "is the reading meaningful", and it is deliberately
        independent of signal level. It is built from unit-magnitude phasors,
        so a quiet deck and a loud one score the same; and it is normalised by
        the noise floor of the averaging itself (`kNoiseFloorConstant` times
        the root of the summed squared averaging weights), so the threshold
        does not move when the averaging changes.

        It is computed with its own fixed `kSignificanceSeconds` averaging, not
        the user's Smoothing setting. The question "is there a shared signal?"
        doesn't need long averaging, and tying it to a 2 s Smoothing made the
        tone take seconds to start and keep sounding through gaps.

        The coarse band is used, not the fine one, because a badly tilted head
        wipes out the treble. A fine-band gate would go quiet exactly when the
        user is far off and most needs guidance; the 1-6 kHz part survives the
        tilt and keeps the coarse band above threshold. */
    float signalSignificance() const noexcept { return currentSignificance; }

    /** Averaging time constant of `signalSignificance`, fixed. */
    static constexpr float kSignificanceSeconds = 0.25f;

    /** Ratio of noise-only phasor coherence to the root of the summed squared
        averaging weights, for this window and overlap (4096-point Hann, hop a
        quarter of the window). Measured on uncorrelated noise: 1.10-1.14
        across averaging times from 0.05 s to 2 s. */
    static constexpr float kNoiseFloorConstant = 1.13f;

    /** Third-octave azimuth-loss analyser, fed from the same FFTs. */
    const SpectralTilt& tilt() const noexcept { return tiltAnalyser; }

    /** Window size in samples, for tests/introspection. */
    int fftSize() const noexcept { return fftLength; }

private:
    void analyse() noexcept;
    void updateFineSearchRadius() noexcept;
    /** Correlate the averaged cross-spectrum restricted to [lowBin, highBin]
        and return the sub-sample peak within +/-radius of centreTau. */
    float peakDelay(int lowBin, int highBin, int centreTau, int radius) noexcept;
    void fftRadix2(std::vector<float>& re, std::vector<float>& im, bool inverse) noexcept;

    double sampleRate{48000.0};
    int fftOrder{12};
    int fftLength{4096};
    int hopSamples{1024};
    int binLow{0};
    int binHigh{0};
    int coarseBinLow{1};
    int searchRadiusSamples{32};
    int fineSearchRadiusSamples{4};

    // Stereo ring buffer of the most recent fftLength samples.
    std::vector<float> ringL, ringR;
    int writePos{0};
    int samplesUntilNextAnalysis{0};

    // Pre-baked Hann window.
    std::vector<float> hannWindow;

    // FFT scratch buffers (reused across frames).
    std::vector<float> reL, imL;
    std::vector<float> reR, imR;
    std::vector<float> crossRe, crossIm;       // per-frame cross-spectrum
    std::vector<float> avgCrossRe, avgCrossIm; // time-averaged cross-spectrum
    std::vector<float> corrRe, corrIm;         // IFFT scratch for peakDelay()
    std::vector<float> powerScratch;           // |L|^2 + |R|^2, feeds tiltAnalyser
    float avgSmoothingAlpha{0.0f};
    bool averageInitialised{false};

    // Bit-reversal permutation table for the FFT (size = fftLength).
    std::vector<int> bitReverse;

    SpectralTilt tiltAnalyser;

    float currentDelaySamples{0.0f};
    float currentCoarseDelaySamples{0.0f};
    float currentPhaseCoherence{0.0f};

    // Fixed-speed accumulator behind signalSignificance().
    std::vector<float> sigAvgRe, sigAvgIm;
    float sigAlpha{0.0f};
    float sigWeightSq{1.0f}; // sum of squared averaging weights
    bool sigInitialised{false};
    float currentSignificance{0.0f};
    float rmsLevel{0.0f};
    float rmsAlpha{0.0f};
};
} // namespace azimuter
