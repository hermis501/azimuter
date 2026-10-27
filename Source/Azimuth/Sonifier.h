#pragma once

namespace azimuter
{
/** Continuous-tone sonification of an azimuth-misalignment reading.

    Driven by a signed, normalised inter-channel delay in [-1, +1]:
      - **0** is the alignment target (lock).
      - **Sign** distinguishes which side the user has rotated past: negative
        biases the tone to the left ear, positive to the right ear. The user
        turns the screw and the tone migrates toward centre as they approach
        alignment, crossing through perfect centre at lock.
      - **Magnitude** drives the same pitch + pulse mapping the previous
        correlation-based sonifier used, so the three audible states stay
        unambiguous:

           |delay|=0     → smooth pitched sine at `highHz`, centred.
           |delay|<0.5   → pitched glide; pitch falls toward `lowHz` as |delay| grows.
           |delay|>=0.5  → pitch saturates at `lowHz`, pulse modulator ramps in
                           (raised-cosine, ~3 Hz), the "you've turned past the
                           lock point — keep going or reverse" cue.

    Plus the original signal-absent state:

           signalPresent=false → silence (deck paused, no HF energy at all).

    All transitions are click-free: pitch, pulse depth, gain and pan are
    smoothed at audio rate.

    `process()` always overwrites the buffer. Only call it when sonification
    is enabled; when disabled, leave the host buffer untouched so the plugin
    is a clean passthrough. */
class Sonifier
{
public:
    Sonifier() = default;

    void prepare(double sampleRate);
    void reset();

    /** Set the signed, normalised inter-channel delay. The processor is
        responsible for clipping to [-1, +1] before calling. */
    void setSignedNormalisedDelay(float deflection) noexcept;
    void setSignalPresent(bool present) noexcept;
    void setGain(float gainLinear) noexcept; ///< 0..1, applied after the mute gate
    void setPitchRange(float lowHz, float highHz) noexcept;

    /** Render `numSamples` of stereo sonification, overwriting the buffer. */
    void process(float* left, float* right, int numSamples) noexcept;

    /** Smoothed values that the next output sample will actually use.
        Exposed for tests / accessibility-side introspection. */
    float currentFrequencyHz() const noexcept { return smoothedFreqHz; }
    float currentGainLinear() const noexcept { return smoothedGain; }
    float currentPulseDepth() const noexcept { return smoothedPulseDepth; }
    float currentLeftPanGain() const noexcept { return smoothedLeftPan; }
    float currentRightPanGain() const noexcept { return smoothedRightPan; }

private:
    void recomputeTargetGain() noexcept;
    void recomputeFromDelay() noexcept;

    double sampleRate{48000.0};

    float lowHz{200.0f};
    float highHz{2000.0f};

    float deflection{0.0f}; // signed, [-1, +1]
    bool signalPresent{false};
    float userGain{0.5f};

    float targetFreqHz{200.0f};
    float targetGain{0.0f};
    float targetPulseDepth{0.0f};
    float targetLeftPan{0.70710678f}; // constant-power centre
    float targetRightPan{0.70710678f};

    float smoothedFreqHz{200.0f};
    float smoothedGain{0.0f};
    float smoothedPulseDepth{0.0f};
    float smoothedLeftPan{0.70710678f};
    float smoothedRightPan{0.70710678f};

    float pulseRateHz{3.0f};

    float freqSmoothingAlpha{1.0f}; // per-sample one-pole coefficients
    float gainSmoothingAlpha{1.0f};
    float pulseSmoothingAlpha{1.0f};
    float panSmoothingAlpha{1.0f};

    double phase{0.0};      // carrier phase, 0..2π
    double pulsePhase{0.0}; // pulse phase, 0..1
};
} // namespace azimuter
