#include "Azimuth/GccPhat.h"

#include <algorithm>
#include <cmath>

namespace azimuter
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

int nextPow2(int n) noexcept
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}
} // namespace

void GccPhat::prepare(double sr, int order, int hop, float hfBandLowHz, float hfBandHighHz, float smoothingSeconds)
{
    sampleRate = sr;
    fftOrder = std::clamp(order, 8, 14); // 256..16384
    fftLength = 1 << fftOrder;
    hopSamples = hop > 0 ? hop : (fftLength / 4);

    // Frequency-bin range for the PHAT pass-band. The upper edge is fixed at
    // prepare() time; the lower edge is live (setBandLowHz), so set it last.
    const float binWidth = static_cast<float>(sampleRate) / static_cast<float>(fftLength);
    const float topHz = hfBandHighHz > 0.0f ? hfBandHighHz : static_cast<float>(sampleRate * 0.5);
    binHigh = std::min(fftLength / 2, static_cast<int>(std::round(topHz / binWidth)));
    binHigh = std::max(binHigh, 2);
    coarseBinLow = std::max(1, static_cast<int>(std::round(kCoarseBandLowHz / binWidth)));
    coarseBinLow = std::min(coarseBinLow, binHigh - 1);
    setBandLowHz(hfBandLowHz);

    // Expected cassette-azimuth delays sit well within ±100 µs. At 192 kHz
    // that's still under 20 samples; cap the peak search at a generous ±64
    // samples so a worst-case mis-aligned deck still produces a finite reading
    // rather than rolling off the search window.
    searchRadiusSamples = std::min(fftLength / 4, std::max(8, static_cast<int>(std::round(sampleRate * 0.0005))));

    // Hann window.
    hannWindow.assign(static_cast<size_t>(fftLength), 0.0f);
    for (int n = 0; n < fftLength; ++n)
        hannWindow[static_cast<size_t>(n)] =
            0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(n) / static_cast<float>(fftLength - 1)));

    // Bit-reversal table.
    bitReverse.assign(static_cast<size_t>(fftLength), 0);
    for (int i = 0; i < fftLength; ++i)
    {
        int x = i, rev = 0;
        for (int b = 0; b < fftOrder; ++b)
        {
            rev = (rev << 1) | (x & 1);
            x >>= 1;
        }
        bitReverse[static_cast<size_t>(i)] = rev;
    }

    const size_t N = static_cast<size_t>(fftLength);
    ringL.assign(N, 0.0f);
    ringR.assign(N, 0.0f);
    reL.assign(N, 0.0f);
    imL.assign(N, 0.0f);
    reR.assign(N, 0.0f);
    imR.assign(N, 0.0f);
    crossRe.assign(N, 0.0f);
    crossIm.assign(N, 0.0f);
    avgCrossRe.assign(N, 0.0f);
    avgCrossIm.assign(N, 0.0f);
    corrRe.assign(N, 0.0f);
    corrIm.assign(N, 0.0f);
    powerScratch.assign(N / 2 + 1, 0.0f);
    sigAvgRe.assign(N, 0.0f);
    sigAvgIm.assign(N, 0.0f);
    sigAlpha = static_cast<float>(
        1.0 - std::exp(-(static_cast<double>(hopSamples) / sampleRate) / static_cast<double>(kSignificanceSeconds)));

    // The azimuth-loss analyser shares these FFTs; it only needs the power
    // spectrum, which analyse() hands it before reusing the scratch buffers.
    tiltAnalyser.prepare(sampleRate, fftLength, topHz, smoothingSeconds, static_cast<double>(hopSamples) / sampleRate);

    setSmoothingSeconds(smoothingSeconds);

    // RMS-level smoothing: ~50 ms.
    rmsAlpha = static_cast<float>(1.0 - std::exp(-1.0 / (0.05 * sampleRate)));

    reset();
}

// Time-averaging coefficient: one-pole at the analysis-frame rate.
// alpha = 1 - exp(-hopSeconds / smoothingSeconds).
void GccPhat::setSmoothingSeconds(float seconds) noexcept
{
    if (seconds > 0.0f)
    {
        const double hopSeconds = static_cast<double>(hopSamples) / sampleRate;
        avgSmoothingAlpha = static_cast<float>(1.0 - std::exp(-hopSeconds / static_cast<double>(seconds)));
    }
    else
    {
        avgSmoothingAlpha = 1.0f;
    }
    tiltAnalyser.setSmoothingSeconds(seconds);
}

// Moving the lower band edge only re-indexes the existing buffers. Bins that
// drop out of the band stop being refreshed, so the running average bleeds
// them away over the smoothing time constant rather than snapping.
void GccPhat::setBandLowHz(float hz) noexcept
{
    const float binWidth = static_cast<float>(sampleRate) / static_cast<float>(fftLength);
    const int requested = std::max(1, static_cast<int>(std::round(hz / binWidth)));
    binLow = std::clamp(requested, coarseBinLow, binHigh - 1);
    updateFineSearchRadius();
}

// The fine band's correlation oscillates at its geometric-mean centre
// frequency, so neighbouring lobes sit one period apart. Searching no further
// than half a period from the coarse peak makes a lobe skip impossible.
void GccPhat::updateFineSearchRadius() noexcept
{
    const float binWidth = static_cast<float>(sampleRate) / static_cast<float>(fftLength);
    const float centreHz = std::sqrt(static_cast<float>(binLow) * static_cast<float>(binHigh)) * binWidth;
    const int half = centreHz > 0.0f ? static_cast<int>(std::round(0.5 * sampleRate / static_cast<double>(centreHz)))
                                     : searchRadiusSamples;
    fineSearchRadiusSamples = std::clamp(half, 1, searchRadiusSamples);
}

void GccPhat::reset()
{
    std::fill(ringL.begin(), ringL.end(), 0.0f);
    std::fill(ringR.begin(), ringR.end(), 0.0f);
    std::fill(avgCrossRe.begin(), avgCrossRe.end(), 0.0f);
    std::fill(avgCrossIm.begin(), avgCrossIm.end(), 0.0f);
    averageInitialised = false;
    writePos = 0;
    // Don't run the first analyse until the ring has been filled with real
    // samples; otherwise the first cross-spectrum is windowed zeros and the
    // averaging buffer would be seeded with garbage.
    samplesUntilNextAnalysis = fftLength;
    currentDelaySamples = 0.0f;
    currentCoarseDelaySamples = 0.0f;
    currentPhaseCoherence = 0.0f;
    std::fill(sigAvgRe.begin(), sigAvgRe.end(), 0.0f);
    std::fill(sigAvgIm.begin(), sigAvgIm.end(), 0.0f);
    sigWeightSq = 1.0f;
    sigInitialised = false;
    currentSignificance = 0.0f;
    rmsLevel = 0.0f;
    tiltAnalyser.reset();
}

void GccPhat::process(float left, float right) noexcept
{
    ringL[static_cast<size_t>(writePos)] = left;
    ringR[static_cast<size_t>(writePos)] = right;
    writePos = (writePos + 1) % fftLength;

    // Track broadband RMS (mono sum) for downstream gating.
    const float instMS = 0.5f * (left * left + right * right);
    rmsLevel += rmsAlpha * (std::sqrt(instMS) - rmsLevel);

    if (--samplesUntilNextAnalysis <= 0)
    {
        analyse();
        samplesUntilNextAnalysis = hopSamples;
    }
}

void GccPhat::analyse() noexcept
{
    const int N = fftLength;
    const size_t Nu = static_cast<size_t>(N);

    // Unwrap the ring into the FFT input buffers, windowed.
    // The oldest sample is at writePos; the newest is just before.
    for (int n = 0; n < N; ++n)
    {
        const int idx = (writePos + n) % N;
        const float w = hannWindow[static_cast<size_t>(n)];
        reL[static_cast<size_t>(n)] = ringL[static_cast<size_t>(idx)] * w;
        reR[static_cast<size_t>(n)] = ringR[static_cast<size_t>(idx)] * w;
    }
    std::fill(imL.begin(), imL.end(), 0.0f);
    std::fill(imR.begin(), imR.end(), 0.0f);

    fftRadix2(reL, imL, /*inverse=*/false);
    fftRadix2(reR, imR, /*inverse=*/false);

    // Power spectrum for the azimuth-loss analyser. Taken now, because reL/imL
    // and reR/imR are about to be consumed. Summing the pair costs nothing and
    // buys signal-to-noise: gap loss hits both channels equally, so nothing
    // that distinguishes them is thrown away.
    for (int k = 0; k <= N / 2; ++k)
    {
        const size_t ku = static_cast<size_t>(k);
        powerScratch[ku] = reL[ku] * reL[ku] + imL[ku] * imL[ku] + reR[ku] * reR[ku] + imR[ku] * imR[ku];
    }
    tiltAnalyser.accumulate(powerScratch.data());

    // Cross-spectrum L * conj(R), PHAT-normalised across the *coarse* band and
    // zeroed elsewhere. The fine band is a subset of it, masked at correlation
    // time, so this one accumulator serves both stages of the lock and moving
    // the cutoff slider no longer strands stale bins in the average.
    std::fill(crossRe.begin(), crossRe.end(), 0.0f);
    std::fill(crossIm.begin(), crossIm.end(), 0.0f);

    constexpr float kMagEps = 1.0e-12f;
    for (int k = coarseBinLow; k <= binHigh && k < N / 2; ++k)
    {
        const size_t ku = static_cast<size_t>(k);
        const float lr = reL[ku], li = imL[ku];
        const float rr = reR[ku], ri = imR[ku];
        // (lr + j*li) * (rr - j*ri) = (lr*rr + li*ri) + j*(li*rr - lr*ri)
        const float xRe = lr * rr + li * ri;
        const float xIm = li * rr - lr * ri;
        const float mag = std::sqrt(xRe * xRe + xIm * xIm);
        if (mag > kMagEps)
        {
            crossRe[ku] = xRe / mag;
            crossIm[ku] = xIm / mag;
        }
    }

    // Welch-style time averaging across frames.
    if (!averageInitialised)
    {
        std::copy(crossRe.begin(), crossRe.end(), avgCrossRe.begin());
        std::copy(crossIm.begin(), crossIm.end(), avgCrossIm.begin());
        averageInitialised = true;
    }
    else
    {
        const float a = avgSmoothingAlpha;
        for (size_t i = 0; i < Nu; ++i)
        {
            avgCrossRe[i] += a * (crossRe[i] - avgCrossRe[i]);
            avgCrossIm[i] += a * (crossIm[i] - avgCrossIm[i]);
        }
    }

    // Each frame contributed a unit phasor, so the magnitude that survives the
    // average measures how consistently the frames agreed on one delay.
    {
        float sum = 0.0f;
        int count = 0;
        for (int k = binLow; k <= binHigh && k < N / 2; ++k)
        {
            const size_t ku = static_cast<size_t>(k);
            sum += std::sqrt(avgCrossRe[ku] * avgCrossRe[ku] + avgCrossIm[ku] * avgCrossIm[ku]);
            ++count;
        }
        currentPhaseCoherence = count > 0 ? sum / static_cast<float>(count) : 0.0f;
    }

    // Signal significance: the same unit phasors, averaged at a fixed speed
    // over the coarse band, compared with the noise floor of that averaging.
    // S is the sum of squared averaging weights; for unrelated channels the
    // mean phasor magnitude is kNoiseFloorConstant * sqrt(S). Seeding with the
    // first frame gives S = 1 there, so a single frame correctly scores as no
    // evidence at all rather than as perfect agreement.
    {
        const int lo = coarseBinLow;
        const int hi = std::min(binHigh, N / 2 - 1);
        if (!sigInitialised)
        {
            for (int k = lo; k <= hi; ++k)
            {
                sigAvgRe[static_cast<size_t>(k)] = crossRe[static_cast<size_t>(k)];
                sigAvgIm[static_cast<size_t>(k)] = crossIm[static_cast<size_t>(k)];
            }
            sigWeightSq = 1.0f;
            sigInitialised = true;
        }
        else
        {
            const float a = sigAlpha;
            for (int k = lo; k <= hi; ++k)
            {
                const size_t ku = static_cast<size_t>(k);
                sigAvgRe[ku] += a * (crossRe[ku] - sigAvgRe[ku]);
                sigAvgIm[ku] += a * (crossIm[ku] - sigAvgIm[ku]);
            }
            sigWeightSq = (1.0f - a) * (1.0f - a) * sigWeightSq + a * a;
        }

        float sum = 0.0f;
        for (int k = lo; k <= hi; ++k)
        {
            const size_t ku = static_cast<size_t>(k);
            sum += std::sqrt(sigAvgRe[ku] * sigAvgRe[ku] + sigAvgIm[ku] * sigAvgIm[ku]);
        }
        const int count = hi - lo + 1;
        const float coherence = count > 0 ? sum / static_cast<float>(count) : 0.0f;
        currentSignificance = coherence / (kNoiseFloorConstant * std::sqrt(sigWeightSq));
    }

    // Stage 1 - coarse. The wide band has no carrier ambiguity, so this picks
    // the right lobe even when the head is badly out.
    currentCoarseDelaySamples = peakDelay(coarseBinLow, binHigh, 0, searchRadiusSamples);

    // Stage 2 - fine. The HF sub-band gives the resolution, but its correlation
    // oscillates at the band centre and neighbouring lobes are equally tall.
    // Restricting the search to within half a period of the coarse answer is
    // the classical "set a low tone first, then work up" procedure, in one pass.
    const int centre = static_cast<int>(std::lround(currentCoarseDelaySamples));
    currentDelaySamples = peakDelay(binLow, binHigh, centre, fineSearchRadiusSamples);
}

float GccPhat::peakDelay(int lowBin, int highBin, int centreTau, int radius) noexcept
{
    const int N = fftLength;

    std::fill(corrRe.begin(), corrRe.end(), 0.0f);
    std::fill(corrIm.begin(), corrIm.end(), 0.0f);
    for (int k = lowBin; k <= highBin && k < N / 2; ++k)
    {
        const size_t ku = static_cast<size_t>(k);
        corrRe[ku] = avgCrossRe[ku];
        corrIm[ku] = avgCrossIm[ku];
        // X[N-k] = conj(X[k]) keeps the transform real.
        corrRe[static_cast<size_t>(N - k)] = avgCrossRe[ku];
        corrIm[static_cast<size_t>(N - k)] = -avgCrossIm[ku];
    }

    fftRadix2(corrRe, corrIm, /*inverse=*/true);

    // Index 0 is tau=0; index 1 is tau=+1 (R leads L); index N-1 is tau=-1.
    auto sampleAt = [this, N](int tau) -> float
    {
        int idx = tau % N;
        if (idx < 0)
            idx += N;
        return corrRe[static_cast<size_t>(idx)];
    };

    const int limit = std::min(radius, searchRadiusSamples);
    int peakTau = centreTau;
    float peakVal = sampleAt(centreTau);
    for (int tau = centreTau - limit; tau <= centreTau + limit; ++tau)
    {
        const float v = sampleAt(tau);
        if (v > peakVal)
        {
            peakVal = v;
            peakTau = tau;
        }
    }

    // Parabolic 3-point sub-sample refinement around the integer peak.
    const float yL = sampleAt(peakTau - 1);
    const float yC = peakVal;
    const float yR = sampleAt(peakTau + 1);
    const float denom = yL - 2.0f * yC + yR;
    float frac = 0.0f;
    if (std::abs(denom) > 1.0e-12f)
        frac = 0.5f * (yL - yR) / denom;
    frac = std::clamp(frac, -1.0f, 1.0f);

    return static_cast<float>(peakTau) + frac;
}

float GccPhat::coarseDelayMicroseconds() const noexcept
{
    return currentCoarseDelaySamples * static_cast<float>(1.0e6 / sampleRate);
}

float GccPhat::delayMicroseconds() const noexcept
{
    return currentDelaySamples * static_cast<float>(1.0e6 / sampleRate);
}

// Iterative radix-2 decimation-in-time FFT. In-place; bit-reversal permutation
// done up front. Hand-rolled to keep azimuter_core JUCE-free.
void GccPhat::fftRadix2(std::vector<float>& re, std::vector<float>& im, bool inverse) noexcept
{
    const int N = fftLength;

    // Bit-reverse permutation.
    for (int i = 0; i < N; ++i)
    {
        const int j = bitReverse[static_cast<size_t>(i)];
        if (j > i)
        {
            std::swap(re[static_cast<size_t>(i)], re[static_cast<size_t>(j)]);
            std::swap(im[static_cast<size_t>(i)], im[static_cast<size_t>(j)]);
        }
    }

    // Butterflies.
    const float dir = inverse ? +1.0f : -1.0f;
    for (int len = 2; len <= N; len <<= 1)
    {
        const int half = len >> 1;
        const float ang = dir * kTwoPi / static_cast<float>(len);
        const float wRe0 = std::cos(ang);
        const float wIm0 = std::sin(ang);
        for (int i = 0; i < N; i += len)
        {
            float wRe = 1.0f, wIm = 0.0f;
            for (int k = 0; k < half; ++k)
            {
                const size_t a = static_cast<size_t>(i + k);
                const size_t b = static_cast<size_t>(i + k + half);
                const float xRe = re[b] * wRe - im[b] * wIm;
                const float xIm = re[b] * wIm + im[b] * wRe;
                re[b] = re[a] - xRe;
                im[b] = im[a] - xIm;
                re[a] = re[a] + xRe;
                im[a] = im[a] + xIm;
                const float nwRe = wRe * wRe0 - wIm * wIm0;
                const float nwIm = wRe * wIm0 + wIm * wRe0;
                wRe = nwRe;
                wIm = nwIm;
            }
        }
    }

    if (inverse)
    {
        const float scale = 1.0f / static_cast<float>(N);
        for (int i = 0; i < N; ++i)
        {
            re[static_cast<size_t>(i)] *= scale;
            im[static_cast<size_t>(i)] *= scale;
        }
    }
}
} // namespace azimuter
