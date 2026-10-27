#pragma once

#include <vector>

namespace azimuter
{
/**
    Finds the delay a *particular tape* should be aligned to.

    Azimuter's original assumption was that correct azimuth means zero
    inter-channel delay. That is false for any tape carrying a delay of its
    own — printed there by a duplicator with gap scatter, or by dubbing from a
    source that was itself played back off-azimuth. Two of the project's
    example tapes measure +54 µs and −99 µs: opposite signs, 153 µs apart, on
    the same deck. There is no universal target and no constant that could be
    shipped. It has to be measured per tape.

    What can be measured is where the *high-frequency loss* is smallest, and
    that is the criterion the tape-restoration literature has always used
    ("tune for MAXIMUM high end"). The obstacle is that programme material
    changes brightness constantly, so raw high-frequency level is useless on
    its own. The way through:

        Programme brightness is frequency-flat. Azimuth loss is
        frequency-scaled.

    When the music brightens, every band rises together. When the head tilts,
    each band loses a different amount, in the fixed ratio the gap-loss law
    dictates. So over a sweep of the azimuth control we fit

        tilt(t,b) = C_b + G(t) − loss( |τ(t) − τ₀| / ratio , f_b )

      C_b    the programme's average spectral shape        (one per band)
      G(t)   its overall brightness through the sweep       (smooth polynomial)
      τ₀     the tape's target delay                        ← what we want
      ratio  effective d/W of the head geometry             ← nuisance, but useful

    C_b and G(t) drop out by least squares; τ₀ and `ratio` come from a grid
    search. Validated against a sweep whose answer was independently known
    from a reference recording: this recovers it to 6 µs, a cost of under
    0.05 dB, and is stable for programme-polynomial degrees 2 through 10.

    **Threading:** observations arrive from, and `solve()` runs on, the message
    thread. Nothing here is real-time safe and nothing here should be called
    from `processBlock`. The audio thread publishes a snapshot; the editor's
    timer feeds it in.
*/
class AzimuthCalibrator
{
public:
    struct Result
    {
        bool valid{false};
        float targetDelayUs{0.0f}; ///< τ₀ — align the head to read this, not zero.
        float geometryRatio{0.0f}; ///< fitted d/W. ~1.8 on the reference deck.
        float residualDb{0.0f};    ///< rms fit residual; how well the model explains the sweep.
        float confidenceUs{0.0f};  ///< half-width of the near-minimum region in τ₀.
        int observations{0};
        float sweptRangeUs{0.0f};
        const char* rejectReason{nullptr}; ///< non-null when `valid` is false.
    };

    // --- gates on individual observations ---------------------------------
    /** Minimum `GccPhat::signalSignificance()` for an observation to count.
        1.0 is pure noise. The gaps between songs (each track's own hiss, no
        shared signal) sit at about 1.0, and their delay readings are random.
        On the example transfers 1.3 keeps 99.9 % of music and rejects ~99 % of
        gap samples, and it still accepts 88-100 % of samples at head
        positions more than 250 us off, which the fit needs for the sweep
        edges. Level-independent: a quiet deck scores the same as a loud one.

        (This replaced an absolute phase-coherence gate of 0.15, which sat
        below the noise floor and let every gap through.) */
    static constexpr float kMinSignificance = 1.3f;

    /** At solve time, observations more than this far below the sweep's own
        median reference-band level are dropped: fade-outs and near-silent
        passages, where hiss starts to colour the high bands. Relative to the
        sweep itself, so the deck's output level doesn't matter. */
    static constexpr float kMaxLevelDropDb = 25.0f;

    // --- gates on the sweep as a whole ------------------------------------
    static constexpr int kMinObservations = 40;
    /** Measured requirement: roughly ±100 µs either side of the optimum. A
        sweep that stops short on one side biases the answer — 0..+90 µs was
        14.6 µs out where −60..+160 µs was 0.6 µs out. */
    static constexpr float kMinSweptRangeUs = 150.0f;

    // --- model shape ------------------------------------------------------
    /** Bands below this carry too little gap loss to help separate azimuth
        from programme. */
    static constexpr float kMinBandCentreHz = 4000.0f;
    static constexpr int kProgrammePolyDegree = 4;
    /** Past the first sinc null the ideal law predicts infinite loss, which no
        real head produces. Clamping keeps those points from dominating. */
    static constexpr float kLossClampDb = 18.0f;

    static constexpr float kMinGeometryRatio = 1.3f;
    static constexpr float kMaxGeometryRatio = 2.9f;
    static constexpr float kGeometryRatioStep = 0.05f;
    static constexpr float kTargetStepUs = 2.0f;
    /** τ₀ is searched only where the sweep actually has data either side. */
    static constexpr float kTargetMarginUs = 20.0f;

    AzimuthCalibrator() = default;

    /** @param numBands  Number of tilt bands that will be supplied.
        @param centreHz  Their centre frequencies. */
    void prepare(int numBands, const float* centreHz);

    void reset();

    /** Add one sweep observation. Returns false if it was gated out.
        `tiltDb` must hold `numBands` entries, as supplied by SpectralTilt. */
    bool addObservation(double timeSeconds, float delayUs, const float* tiltDb, float referenceLevelDb,
                        float significance);

    int observationCount() const noexcept { return static_cast<int>(delays.size()); }

    /** Span of delay covered so far — the number the user is really trying to
        grow while sweeping, and what the progress cue should report. */
    float sweptRangeUs() const noexcept;

    bool hasEnoughData() const noexcept;

    Result solve() const;

    /** Gap loss in dB at a given alignment error, from the classical law
        `-20·log10|sinc(W·tanθ·f/v)|`, re-expressed in terms of the
        inter-channel delay the same tilt produces. Public so the editor can
        say what an error costs and tests can synthesise obedient sweeps. */
    static float gapLossDb(float errorUs, float geometryRatio, float frequencyHz) noexcept;

private:
    std::vector<float> centres; // band centre frequencies
    std::vector<int> usedBands; // indices with centre >= kMinBandCentreHz
    std::vector<double> times;  // per observation
    std::vector<float> delays;  // per observation
    std::vector<float> tilts;   // observation-major, numBands per observation
    std::vector<float> levels;  // reference-band level per observation, dB
    int bandCount{0};
};
} // namespace azimuter
