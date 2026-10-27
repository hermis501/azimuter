#include "Azimuth/AzimuthCalibrator.h"

#include <algorithm>
#include <cmath>

namespace azimuter
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

/** sin(pi x) / (pi x), the gap-loss amplitude response. */
float sincPi(float x) noexcept
{
    const float a = std::abs(x);
    if (a < 1.0e-6f)
        return 1.0f;
    const float px = kPi * a;
    return std::sin(px) / px;
}

/** Loss lookup so the grid search doesn't evaluate a sine per point per
    candidate. Indexed by x = errorUs / ratio * f, i.e. the sinc argument. */
class LossTable
{
public:
    static constexpr int kSize = 2048;
    static constexpr float kMaxX = 1.2f;

    LossTable() noexcept
    {
        for (int i = 0; i < kSize; ++i)
        {
            const float x = kMaxX * static_cast<float>(i) / static_cast<float>(kSize - 1);
            const float mag = std::abs(sincPi(x));
            const float db = mag > 1.0e-6f ? -20.0f * std::log10(mag) : AzimuthCalibrator::kLossClampDb;
            table[i] = std::min(db, AzimuthCalibrator::kLossClampDb);
        }
    }

    float operator()(float x) const noexcept
    {
        const float a = std::abs(x);
        if (a >= kMaxX)
            return AzimuthCalibrator::kLossClampDb;
        const float pos = a * (static_cast<float>(kSize - 1) / kMaxX);
        const int i = static_cast<int>(pos);
        const float frac = pos - static_cast<float>(i);
        return table[i] + frac * (table[i + 1] - table[i]);
    }

private:
    float table[kSize]{};
};

const LossTable& lossTable() noexcept
{
    static const LossTable t;
    return t;
}

/** Modified Gram-Schmidt. `a` is nrows x ncols in column-major order and is
    overwritten with an orthonormal basis. Columns that collapse are dropped,
    and the surviving count is returned. */
int orthonormalise(std::vector<double>& a, int nrows, int ncols)
{
    int kept = 0;
    for (int c = 0; c < ncols; ++c)
    {
        double* v = a.data() + static_cast<size_t>(c) * static_cast<size_t>(nrows);
        for (int p = 0; p < kept; ++p)
        {
            const double* q = a.data() + static_cast<size_t>(p) * static_cast<size_t>(nrows);
            double dot = 0.0;
            for (int r = 0; r < nrows; ++r)
                dot += q[r] * v[r];
            for (int r = 0; r < nrows; ++r)
                v[r] -= dot * q[r];
        }
        double norm = 0.0;
        for (int r = 0; r < nrows; ++r)
            norm += v[r] * v[r];
        norm = std::sqrt(norm);
        if (norm < 1.0e-9)
            continue;
        double* dst = a.data() + static_cast<size_t>(kept) * static_cast<size_t>(nrows);
        for (int r = 0; r < nrows; ++r)
            dst[r] = v[r] / norm;
        ++kept;
    }
    return kept;
}
} // namespace

float AzimuthCalibrator::gapLossDb(float errorUs, float geometryRatio, float frequencyHz) noexcept
{
    if (geometryRatio <= 0.0f)
        return 0.0f;
    const float x = std::abs(errorUs) * 1.0e-6f / geometryRatio * frequencyHz;
    return lossTable()(x);
}

void AzimuthCalibrator::prepare(int numBands, const float* centreHz)
{
    bandCount = std::max(0, numBands);
    centres.assign(centreHz, centreHz + bandCount);
    usedBands.clear();
    for (int b = 0; b < bandCount; ++b)
        if (centres[static_cast<size_t>(b)] >= kMinBandCentreHz)
            usedBands.push_back(b);
    reset();
}

void AzimuthCalibrator::reset()
{
    times.clear();
    delays.clear();
    tilts.clear();
    levels.clear();
}

bool AzimuthCalibrator::addObservation(double timeSeconds, float delayUs, const float* tiltDb, float referenceLevelDb,
                                       float significance)
{
    if (bandCount == 0 || tiltDb == nullptr)
        return false;
    if (!(significance >= kMinSignificance))
        return false;
    if (!std::isfinite(delayUs) || !std::isfinite(referenceLevelDb))
        return false;
    for (int b = 0; b < bandCount; ++b)
        if (!std::isfinite(tiltDb[b]))
            return false;

    times.push_back(timeSeconds);
    delays.push_back(delayUs);
    tilts.insert(tilts.end(), tiltDb, tiltDb + bandCount);
    levels.push_back(referenceLevelDb);
    return true;
}

float AzimuthCalibrator::sweptRangeUs() const noexcept
{
    if (delays.size() < 2)
        return 0.0f;
    const auto mm = std::minmax_element(delays.begin(), delays.end());
    return *mm.second - *mm.first;
}

bool AzimuthCalibrator::hasEnoughData() const noexcept
{
    return observationCount() >= kMinObservations && sweptRangeUs() >= kMinSweptRangeUs;
}

AzimuthCalibrator::Result AzimuthCalibrator::solve() const
{
    // Relative level gate: drop observations far below the sweep's own median
    // reference level (fade-outs, near-silence). Relative, so independent of
    // how loud the deck is.
    std::vector<double> obsTimes;
    std::vector<float> obsDelays;
    std::vector<float> obsTilts;
    {
        std::vector<float> sorted(levels);
        float floorDb = -1.0e30f;
        if (!sorted.empty())
        {
            std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(sorted.size() / 2), sorted.end());
            floorDb = sorted[sorted.size() / 2] - kMaxLevelDropDb;
        }
        for (size_t i = 0; i < this->delays.size(); ++i)
        {
            if (this->levels[i] < floorDb)
                continue;
            obsTimes.push_back(this->times[i]);
            obsDelays.push_back(this->delays[i]);
            const auto first = this->tilts.begin() + static_cast<long>(i * static_cast<size_t>(bandCount));
            obsTilts.insert(obsTilts.end(), first, first + bandCount);
        }
    }

    Result result;
    result.observations = static_cast<int>(obsDelays.size());
    result.sweptRangeUs = 0.0f;
    if (obsDelays.size() >= 2)
    {
        const auto range = std::minmax_element(obsDelays.begin(), obsDelays.end());
        result.sweptRangeUs = *range.second - *range.first;
    }

    if (usedBands.size() < 2)
    {
        result.rejectReason = "not enough analysis bands";
        return result;
    }
    if (result.observations < kMinObservations)
    {
        result.rejectReason = "not enough of the sweep was usable";
        return result;
    }
    if (result.sweptRangeUs < kMinSweptRangeUs)
    {
        result.rejectReason = "the azimuth control was not turned far enough";
        return result;
    }

    const int nObs = result.observations;
    const int nb = static_cast<int>(usedBands.size());
    const int nrows = nObs * nb;
    const int ncols = nb + kProgrammePolyDegree;

    // --- design matrix: one indicator column per band (the programme's average
    // spectral shape) plus a polynomial in normalised time (its brightness
    // drifting through the sweep). Neither depends on the parameters we are
    // searching for, so this is factorised once.
    const double t0 = obsTimes.front();
    const double t1 = obsTimes.back();
    const double halfSpan = std::max(1.0e-6, 0.5 * (t1 - t0));
    const double mid = 0.5 * (t0 + t1);

    std::vector<double> basis(static_cast<size_t>(nrows) * static_cast<size_t>(ncols), 0.0);
    for (int i = 0; i < nObs; ++i)
    {
        const double tn = (obsTimes[static_cast<size_t>(i)] - mid) / halfSpan;
        for (int j = 0; j < nb; ++j)
        {
            const int r = i * nb + j;
            basis[static_cast<size_t>(j) * static_cast<size_t>(nrows) + static_cast<size_t>(r)] = 1.0;
            double p = 1.0;
            for (int d = 0; d < kProgrammePolyDegree; ++d)
            {
                p *= tn;
                basis[static_cast<size_t>(nb + d) * static_cast<size_t>(nrows) + static_cast<size_t>(r)] = p;
            }
        }
    }
    const int nbasis = orthonormalise(basis, nrows, ncols);
    if (nbasis == 0)
    {
        result.rejectReason = "degenerate sweep";
        return result;
    }

    // Band centre per row, so the inner loop is a flat walk.
    std::vector<float> rowFreq(static_cast<size_t>(nrows));
    std::vector<float> rowTilt(static_cast<size_t>(nrows));
    for (int i = 0; i < nObs; ++i)
        for (int j = 0; j < nb; ++j)
        {
            const int b = usedBands[static_cast<size_t>(j)];
            rowFreq[static_cast<size_t>(i * nb + j)] = centres[static_cast<size_t>(b)];
            rowTilt[static_cast<size_t>(i * nb + j)] =
                obsTilts[static_cast<size_t>(i) * static_cast<size_t>(bandCount) + static_cast<size_t>(b)];
        }

    const auto mm = std::minmax_element(obsDelays.begin(), obsDelays.end());
    const float lo = *mm.first + kTargetMarginUs;
    const float hi = *mm.second - kTargetMarginUs;
    const int nTargets = std::max(1, static_cast<int>((hi - lo) / kTargetStepUs) + 1);
    const int nRatios = std::max(1, static_cast<int>((kMaxGeometryRatio - kMinGeometryRatio) / kGeometryRatioStep) + 1);

    const LossTable& loss = lossTable();
    std::vector<double> y(static_cast<size_t>(nrows));
    std::vector<double> profile(static_cast<size_t>(nTargets), 0.0);

    double bestErr = 0.0;
    float bestTarget = 0.0f;
    float bestRatio = 0.0f;
    bool haveBest = false;

    for (int ti = 0; ti < nTargets; ++ti)
    {
        const float target = lo + kTargetStepUs * static_cast<float>(ti);
        double bestForTarget = 0.0;
        bool haveForTarget = false;

        for (int ri = 0; ri < nRatios; ++ri)
        {
            const float ratio = kMinGeometryRatio + kGeometryRatioStep * static_cast<float>(ri);
            const float scale = 1.0e-6f / ratio;

            double yy = 0.0;
            for (int i = 0; i < nObs; ++i)
            {
                const float err = std::abs(obsDelays[static_cast<size_t>(i)] - target) * scale;
                for (int j = 0; j < nb; ++j)
                {
                    const size_t r = static_cast<size_t>(i * nb + j);
                    const double v = static_cast<double>(rowTilt[r]) + loss(err * rowFreq[r]);
                    y[r] = v;
                    yy += v * v;
                }
            }
            // Residual of the least-squares fit against an orthonormal basis is
            // just ||y||^2 minus the energy the basis captures.
            double captured = 0.0;
            for (int c = 0; c < nbasis; ++c)
            {
                const double* q = basis.data() + static_cast<size_t>(c) * static_cast<size_t>(nrows);
                double dot = 0.0;
                for (int r = 0; r < nrows; ++r)
                    dot += q[r] * y[static_cast<size_t>(r)];
                captured += dot * dot;
            }
            const double err2 = std::max(0.0, yy - captured) / static_cast<double>(nrows);

            if (!haveForTarget || err2 < bestForTarget)
            {
                bestForTarget = err2;
                haveForTarget = true;
            }
            if (!haveBest || err2 < bestErr)
            {
                bestErr = err2;
                bestTarget = target;
                bestRatio = ratio;
                haveBest = true;
            }
        }
        profile[static_cast<size_t>(ti)] = bestForTarget;
    }

    if (!haveBest)
    {
        result.rejectReason = "search found no solution";
        return result;
    }

    // Confidence: how wide is the contiguous run of targets whose residual sits
    // within 2 % of the best? A shallow minimum means the sweep did not
    // constrain τ₀, which happens when it never crossed the optimum.
    const int bestIndex = static_cast<int>(std::lround((bestTarget - lo) / kTargetStepUs));
    const double threshold = bestErr * 1.02;
    int first = bestIndex, last = bestIndex;
    while (first > 0 && profile[static_cast<size_t>(first - 1)] <= threshold)
        --first;
    while (last + 1 < nTargets && profile[static_cast<size_t>(last + 1)] <= threshold)
        ++last;

    result.valid = true;
    result.targetDelayUs = bestTarget;
    result.geometryRatio = bestRatio;
    result.residualDb = static_cast<float>(std::sqrt(bestErr));
    result.confidenceUs = 0.5f * static_cast<float>(last - first) * kTargetStepUs;
    return result;
}
} // namespace azimuter
