#include "Azimuth/SpectralTilt.h"

#include <algorithm>
#include <cmath>

namespace azimuter
{
namespace
{
constexpr float kPowerFloor = 1.0e-30f;

int binForHz(double hz, double sampleRate, int fftLength) noexcept
{
    return static_cast<int>(std::lround(hz * static_cast<double>(fftLength) / sampleRate));
}
} // namespace

void SpectralTilt::prepare(double sr, int fftLength, float topHz, float smoothingSeconds, double hop)
{
    sampleRate = sr;
    spectrumSize = fftLength / 2 + 1;
    hopSeconds = hop;

    const double nyquist = sampleRate * 0.5;
    const double top = std::min(static_cast<double>(topHz), nyquist * 0.9);

    refFirstBin = std::max(1, binForHz(kReferenceLowHz, sampleRate, fftLength));
    refLastBin = std::min(spectrumSize - 1, binForHz(kReferenceHighHz, sampleRate, fftLength));
    if (refLastBin <= refFirstBin)
        refLastBin = std::min(spectrumSize - 1, refFirstBin + 1);

    // Third-octave bands upward from kFirstBandLowHz. A band is kept only if it
    // fits entirely below `top` — a partially truncated top band would have a
    // centre frequency that lies about the loss it represents, and the fit in
    // AzimuthCalibrator weights bands by their centre frequency.
    bandCount = 0;
    for (int n = 0; n < kMaxBands; ++n)
    {
        const double lo = static_cast<double>(kFirstBandLowHz) * std::pow(2.0, n / 3.0);
        const double hi = lo * std::pow(2.0, 1.0 / 3.0);
        if (hi > top)
            break;

        Band b;
        b.firstBin = std::max(1, binForHz(lo, sampleRate, fftLength));
        b.lastBin = std::min(spectrumSize - 1, binForHz(hi, sampleRate, fftLength));
        if (b.lastBin <= b.firstBin)
            continue;
        b.centre = static_cast<float>(std::sqrt(lo * hi));
        bands[bandCount++] = b;
    }

    setSmoothingSeconds(smoothingSeconds);
    reset();
}

void SpectralTilt::setSmoothingSeconds(float seconds) noexcept
{
    if (seconds > 0.0f && hopSeconds > 0.0)
        smoothingAlpha = static_cast<float>(1.0 - std::exp(-hopSeconds / static_cast<double>(seconds)));
    else
        smoothingAlpha = 1.0f;
}

void SpectralTilt::reset()
{
    std::fill(std::begin(bandPower), std::end(bandPower), 0.0f);
    refPower = 0.0f;
    initialised = false;
}

void SpectralTilt::accumulate(const float* powerSpectrum) noexcept
{
    if (powerSpectrum == nullptr || bandCount == 0)
        return;

    auto meanOver = [powerSpectrum](int first, int last) noexcept
    {
        float sum = 0.0f;
        for (int k = first; k <= last; ++k)
            sum += powerSpectrum[static_cast<size_t>(k)];
        return sum / static_cast<float>(last - first + 1);
    };

    const float refNow = meanOver(refFirstBin, refLastBin);

    if (!initialised)
    {
        refPower = refNow;
        for (int b = 0; b < bandCount; ++b)
            bandPower[b] = meanOver(bands[b].firstBin, bands[b].lastBin);
        initialised = true;
        return;
    }

    const float a = smoothingAlpha;
    refPower += a * (refNow - refPower);
    for (int b = 0; b < bandCount; ++b)
        bandPower[b] += a * (meanOver(bands[b].firstBin, bands[b].lastBin) - bandPower[b]);
}

float SpectralTilt::centreHz(int band) const noexcept
{
    if (band < 0 || band >= bandCount)
        return 0.0f;
    return bands[band].centre;
}

float SpectralTilt::tiltDb(int band) const noexcept
{
    if (band < 0 || band >= bandCount || !initialised)
        return 0.0f;
    const float num = std::max(bandPower[band], kPowerFloor);
    const float den = std::max(refPower, kPowerFloor);
    return 10.0f * std::log10(num / den);
}

float SpectralTilt::referenceLevelDb() const noexcept
{
    return 10.0f * std::log10(std::max(refPower, kPowerFloor));
}
} // namespace azimuter
