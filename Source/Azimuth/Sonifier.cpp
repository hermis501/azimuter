#include "Azimuth/Sonifier.h"

#include <algorithm>
#include <cmath>

namespace azimuter
{
namespace
{
constexpr double kTwoPi = 6.28318530717958647692;
constexpr float kHalfPiF = 1.57079632679489661923f;

float onePoleAlphaForTimeConstant(double sampleRate, double seconds) noexcept
{
    if (seconds <= 0.0)
        return 1.0f;
    return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate)));
}
} // namespace

void Sonifier::prepare(double sr)
{
    sampleRate = sr;
    // 30 ms freq slew — fast enough to feel responsive when sweeping the screw,
    // slow enough that frame-to-frame analyser noise doesn't sound jerky.
    freqSmoothingAlpha = onePoleAlphaForTimeConstant(sr, 0.03);
    gainSmoothingAlpha = onePoleAlphaForTimeConstant(sr, 0.01);
    pulseSmoothingAlpha = onePoleAlphaForTimeConstant(sr, 0.05);
    // Pan slew matches pitch slew so the side-cue and pitch-cue move in lockstep.
    panSmoothingAlpha = onePoleAlphaForTimeConstant(sr, 0.03);
    reset();
}

void Sonifier::reset()
{
    smoothedFreqHz = targetFreqHz = lowHz;
    smoothedGain = targetGain = 0.0f;
    smoothedPulseDepth = targetPulseDepth = 0.0f;
    smoothedLeftPan = targetLeftPan = 0.70710678f;
    smoothedRightPan = targetRightPan = 0.70710678f;
    phase = 0.0;
    pulsePhase = 0.0;
}

void Sonifier::setPitchRange(float low, float high) noexcept
{
    lowHz = std::max(1.0f, low);
    highHz = std::max(lowHz + 1.0f, high);
    recomputeFromDelay();
}

void Sonifier::setSignedNormalisedDelay(float d) noexcept
{
    deflection = std::clamp(d, -1.0f, 1.0f);
    recomputeFromDelay();
}

void Sonifier::recomputeFromDelay() noexcept
{
    // Map |delay| → "alignment score" in [-1, +1], mirroring the previous
    // correlation semantics so the pitch + pulse mapping is identical:
    //   |delay|=0   → score = +1  → high pitch, no pulse.
    //   |delay|=0.5 → score = 0   → low pitch, pulse just starting.
    //   |delay|=1   → score = -1  → low pitch, full pulse.
    const float absDef = std::abs(deflection);
    const float score = std::clamp(1.0f - 2.0f * absDef, -1.0f, 1.0f);

    const float positive = std::max(0.0f, score);
    targetFreqHz = lowHz * std::pow(highHz / lowHz, positive);
    targetPulseDepth = std::max(0.0f, -score);

    // Constant-power panning. angle=0 → all left, 0.5 → centred, 1 → all right.
    const float panAngle = 0.5f + 0.5f * deflection;
    targetLeftPan = std::cos(panAngle * kHalfPiF);
    targetRightPan = std::sin(panAngle * kHalfPiF);

    recomputeTargetGain();
}

void Sonifier::setSignalPresent(bool present) noexcept
{
    signalPresent = present;
    recomputeTargetGain();
}

void Sonifier::setGain(float g) noexcept
{
    userGain = std::clamp(g, 0.0f, 1.0f);
    recomputeTargetGain();
}

void Sonifier::recomputeTargetGain() noexcept
{
    // The pulse modulator and the pan cue handle "which way / how far"; only
    // signal absence mutes the carrier itself.
    targetGain = signalPresent ? userGain : 0.0f;
}

void Sonifier::process(float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const double phaseInc = kTwoPi / sampleRate;
    const double pulsePhaseInc = pulseRateHz / sampleRate;

    // Pulse shape: a brief raised-cosine peak occupying the first 20 % of
    // each pulse cycle, on a 0.3 baseline. Envelope ranges from 0.3 most
    // of the cycle up to 1.0 briefly — unambiguously rhythmic without the
    // carrier ever fully silencing.
    constexpr float pulseDuty = 0.2f;
    constexpr float pulseBaseline = 0.3f;
    constexpr float pulsePeakSpan = 1.0f - pulseBaseline;

    for (int n = 0; n < numSamples; ++n)
    {
        smoothedFreqHz += freqSmoothingAlpha * (targetFreqHz - smoothedFreqHz);
        smoothedGain += gainSmoothingAlpha * (targetGain - smoothedGain);
        smoothedPulseDepth += pulseSmoothingAlpha * (targetPulseDepth - smoothedPulseDepth);
        smoothedLeftPan += panSmoothingAlpha * (targetLeftPan - smoothedLeftPan);
        smoothedRightPan += panSmoothingAlpha * (targetRightPan - smoothedRightPan);

        phase += phaseInc * smoothedFreqHz;
        if (phase >= kTwoPi)
            phase -= kTwoPi;

        pulsePhase += pulsePhaseInc;
        if (pulsePhase >= 1.0)
            pulsePhase -= 1.0;

        float pulseShape;
        if (pulsePhase < pulseDuty)
        {
            const float p = static_cast<float>(pulsePhase) / pulseDuty;
            pulseShape = pulseBaseline + pulsePeakSpan * 0.5f * (1.0f - std::cos(static_cast<float>(kTwoPi) * p));
        }
        else
        {
            pulseShape = pulseBaseline;
        }

        // depth=0 → envelope=1 (steady tone). depth=1 → envelope=pulseShape.
        const float envelope = (1.0f - smoothedPulseDepth) + smoothedPulseDepth * pulseShape;

        const float carrier = static_cast<float>(std::sin(phase)) * envelope * smoothedGain;
        left[n] = carrier * smoothedLeftPan;
        right[n] = carrier * smoothedRightPan;
    }
}
} // namespace azimuter
