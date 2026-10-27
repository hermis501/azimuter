#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Azimuth/AnalysisSnapshot.h"
#include "Azimuth/AzimuthCalibrator.h"
#include "Azimuth/DeflectionMap.h"
#include "Azimuth/GccPhat.h"
#include "Azimuth/SignalGate.h"
#include "Azimuth/Sonifier.h"
#include "ScreenReaderSpeech.h"

#include <atomic>

class AzimuterAudioProcessor : public juce::AudioProcessor, public juce::VST3ClientExtensions
{
public:
    AzimuterAudioProcessor();
    ~AzimuterAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    /** Thread-safe snapshot of the measured signed inter-channel delay in
        microseconds. Positive = R leads L. This is the raw reading, *not* the
        alignment error — on a tape carrying skew of its own they differ. */
    float getCurrentDelayMicroseconds() const noexcept { return delayUsSnapshot.load(std::memory_order_relaxed); }

    /** Distance from the delay this tape should be aligned to. Zero means
        correct azimuth. Everything user-facing — readout, spoken buckets,
        pitch, pan and pulse — is driven by this rather than by the raw delay,
        because on a tape carrying its own skew the two are not the same and
        only this one corresponds to maximum high frequency. */
    float getAlignmentErrorMicroseconds() const noexcept { return errorUsSnapshot.load(std::memory_order_relaxed); }

    /** The reading that corresponds to correct azimuth on the tape currently
        loaded. Zero until calibrated, which reproduces the old behaviour. */
    float getTargetDelayMicroseconds() const noexcept { return targetDelayUs.load(std::memory_order_relaxed); }

    /** Message thread. Persists with the session state. */
    void setTargetDelayMicroseconds(float microseconds);

    bool hasCalibration() const noexcept { return calibrated.load(std::memory_order_relaxed); }

    void clearCalibration();

    // --- calibration; all of this is message-thread only -------------------

    /** Begin collecting a sweep. False if the plugin has not been prepared, so
        there is no band layout to calibrate against yet. */
    bool beginCalibration();

    /** Feed the newest analysis frame into the sweep. Rate-limits internally,
        so it is safe to call on every timer tick. True if an observation was
        actually accepted. */
    bool pushCalibrationSample(double timeSeconds);

    /** Solve the sweep. On success the target is adopted and persisted. */
    azimuter::AzimuthCalibrator::Result finishCalibration();

    void abortCalibration();

    bool isCalibrating() const noexcept { return calibrating; }
    int calibrationObservations() const noexcept { return calibrator.observationCount(); }
    float calibrationSweptRangeUs() const noexcept { return calibrator.sweptRangeUs(); }

    /** Newest analysis frame, for the editor. False if none is available yet
        or the read was lapped — just try again on the next tick. */
    bool readAnalysis(azimuter::AnalysisSnapshot::Frame& out) const noexcept { return snapshot.read(out); }

    /** |delay| that maps to full deflection — lowest pitch, hardest pan, full
        pulse — and to the readout's "Off-scale" bucket. Shared with the
        sonifier's µs → deflection map so the spoken bucket and the audible
        saturation point can never drift apart. */
    static constexpr float kFullScaleDelayMicroseconds = azimuter::DeflectionMap::fullScaleMicroseconds;

    juce::AudioProcessorValueTreeState parameters;

    /** Speech to the user's screen reader (OSARA in REAPER, else UIA). Message
        thread only. */
    ScreenReaderSpeech& getSpeech() noexcept { return speech; }

    // --- VST3ClientExtensions ---------------------------------------------
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    void setIHostApplication(Steinberg::FUnknown* hostContext) override { speech.connectToHost(hostContext); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    azimuter::GccPhat analyser;
    azimuter::Sonifier sonifier;
    // Silence vs tone, from shared-signal significance rather than level.
    azimuter::SignalGate signalGate;
    double currentSampleRate{44100.0};
    azimuter::AnalysisSnapshot snapshot;

    // Message thread only. The audio thread never touches the calibrator — it
    // publishes frames, and the editor's timer feeds them in.
    azimuter::AzimuthCalibrator calibrator;
    ScreenReaderSpeech speech;
    bool calibrating{false};
    double lastCalibrationSampleTime{-1.0e9};

    std::atomic<float> delayUsSnapshot{0.0f};
    std::atomic<float> errorUsSnapshot{0.0f};
    std::atomic<float> targetDelayUs{0.0f};
    std::atomic<bool> calibrated{false};

    // Last values pushed into the analyser, so processBlock only recomputes
    // coefficients when a slider actually moved.
    float lastCutoffHz{-1.0f};
    float lastSmoothingSeconds{-1.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AzimuterAudioProcessor)
};
