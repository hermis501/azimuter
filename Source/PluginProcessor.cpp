#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>

namespace
{
// Persisted alongside the parameters, because the correct target is a property
// of the tape rather than of the user's settings.
const juce::Identifier kTargetProperty{"targetDelayUs"};
const juce::Identifier kCalibratedProperty{"azimuthCalibrated"};

/** Minimum gap between accepted sweep observations. The analyser updates every
    ~23 ms, but neighbouring frames share most of their averaging window, so
    taking them all would inflate the observation count without adding
    information the fit can use. */
constexpr double kCalibrationSampleIntervalSeconds = 0.25;
} // namespace

AzimuterAudioProcessor::AzimuterAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("Azimuter"), createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AzimuterAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Lower band edge of the PHAT pass-band. Bins below this frequency
    // contribute nothing to the cross-correlation, so the delay estimate
    // is driven by the HF range where azimuth-induced phase shift is biggest.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"cutoff", 1}, "Analysis Cutoff",
        juce::NormalisableRange<float>(1000.0f, 16000.0f, 1.0f, 0.5f), 6000.0f));

    // Time constant of the running cross-spectrum average. Longer = steadier
    // reading, slower response when the user turns the screw.
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"smoothing", 1}, "Smoothing",
                                                           juce::NormalisableRange<float>(0.01f, 2.0f, 0.001f, 0.4f),
                                                           0.25f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"sonifyGain", 1}, "Tone Level",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.4f));

    return layout;
}

void AzimuterAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    const auto cutoff = parameters.getRawParameterValue("cutoff")->load();
    const auto smoothing = parameters.getRawParameterValue("smoothing")->load();
    // 4096-sample window (~85 ms at 48 kHz), hop 1024 (~21 ms between updates).
    // Upper PHAT band edge is fixed near the typical cassette HF roll-off so
    // we don't burn cycles on bins that are mostly aliasing or anti-alias
    // filter ringing.
    const float hfHigh = std::min(static_cast<float>(sampleRate) * 0.45f, 18000.0f);
    analyser.prepare(sampleRate,
                     /*fftOrder=*/12,
                     /*hopSize=*/1024,
                     /*hfBandLowHz=*/cutoff,
                     /*hfBandHighHz=*/hfHigh,
                     /*smoothingSeconds=*/smoothing);
    lastCutoffHz = cutoff;
    lastSmoothingSeconds = smoothing;
    currentSampleRate = sampleRate;
    signalGate.reset();
    sonifier.prepare(sampleRate);
}

bool AzimuterAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AzimuterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    // Analysis cutoff and smoothing are live: both only recompute scalars in
    // the analyser, so they can follow the sliders per block. They used to be
    // baked in at prepareToPlay, which left both controls inert until the host
    // happened to re-prepare the plugin.
    const float cutoffHz = parameters.getRawParameterValue("cutoff")->load();
    if (cutoffHz != lastCutoffHz)
    {
        analyser.setBandLowHz(cutoffHz);
        lastCutoffHz = cutoffHz;
    }

    const float smoothingSeconds = parameters.getRawParameterValue("smoothing")->load();
    if (smoothingSeconds != lastSmoothingSeconds)
    {
        analyser.setSmoothingSeconds(smoothingSeconds);
        lastSmoothingSeconds = smoothingSeconds;
    }

    // Analysis pass — reads input in place.
    {
        const auto* left = buffer.getReadPointer(0);
        const auto* right = buffer.getReadPointer(1);
        for (int n = 0; n < numSamples; ++n)
            analyser.process(left[n], right[n]);
    }

    const float delayUs = analyser.delayMicroseconds();
    delayUsSnapshot.store(delayUs, std::memory_order_relaxed);

    // The tape may carry an inter-channel delay of its own — printed by a
    // duplicator with gap scatter, or dubbed from a source played off-azimuth.
    // Correct azimuth is where the *error* against that target is zero, not
    // where the raw reading is zero. Uncalibrated, the target is 0 and this
    // reduces to the original behaviour.
    const float errorUs = delayUs - targetDelayUs.load(std::memory_order_relaxed);
    errorUsSnapshot.store(errorUs, std::memory_order_relaxed);

    sonifier.setSignedNormalisedDelay(azimuter::DeflectionMap::fromMicroseconds(errorUs));
    // Silence when there is nothing shared by both tracks to measure: deck
    // paused, gap between songs, hiss only. Decided from signal significance,
    // not level, so a quiet deck is not mistaken for a stopped one, and the
    // tone no longer wanders at random through the gaps. See SignalGate.h.
    sonifier.setSignalPresent(
        signalGate.update(analyser.signalSignificance(), static_cast<float>(numSamples / currentSampleRate)));
    sonifier.setGain(parameters.getRawParameterValue("sonifyGain")->load());

    // The plugin always replaces the input with the guidance tone. To silence
    // the tone, bypass the FX in the host — an on/off parameter here would
    // duplicate that and turn the plugin into a no-op when "off".
    sonifier.process(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);

    // Publish one analysis frame for the calibrator. Wait-free; the message
    // thread picks it up on its own schedule.
    {
        azimuter::AnalysisSnapshot::Frame frame;
        frame.delayUs = delayUs;
        frame.coarseDelayUs = analyser.coarseDelayMicroseconds();
        frame.phaseCoherence = analyser.phaseCoherence();
        frame.significance = analyser.signalSignificance();
        frame.signalLevel = analyser.signalLevel();

        const auto& tilt = analyser.tilt();
        frame.numBands = std::min(tilt.numBands(), azimuter::AnalysisSnapshot::kMaxBands);
        frame.referenceLevelDb = tilt.referenceLevelDb();
        for (int b = 0; b < frame.numBands; ++b)
            frame.tiltDb[b] = tilt.tiltDb(b);

        snapshot.publish(frame);
    }
}

// ---------------------------------------------------------------------------
// Calibration. Everything below runs on the message thread.
// ---------------------------------------------------------------------------

void AzimuterAudioProcessor::setTargetDelayMicroseconds(float microseconds)
{
    targetDelayUs.store(microseconds, std::memory_order_relaxed);
    calibrated.store(true, std::memory_order_relaxed);
    parameters.state.setProperty(kTargetProperty, microseconds, nullptr);
    parameters.state.setProperty(kCalibratedProperty, true, nullptr);
}

void AzimuterAudioProcessor::clearCalibration()
{
    targetDelayUs.store(0.0f, std::memory_order_relaxed);
    calibrated.store(false, std::memory_order_relaxed);
    parameters.state.setProperty(kTargetProperty, 0.0f, nullptr);
    parameters.state.setProperty(kCalibratedProperty, false, nullptr);
}

bool AzimuterAudioProcessor::beginCalibration()
{
    const auto& tilt = analyser.tilt();
    const int bands = tilt.numBands();
    if (bands <= 0)
        return false;

    float centres[azimuter::AnalysisSnapshot::kMaxBands]{};
    const int n = std::min(bands, azimuter::AnalysisSnapshot::kMaxBands);
    for (int b = 0; b < n; ++b)
        centres[b] = tilt.centreHz(b);

    calibrator.prepare(n, centres);
    lastCalibrationSampleTime = -1.0e9;
    calibrating = true;
    return true;
}

bool AzimuterAudioProcessor::pushCalibrationSample(double timeSeconds)
{
    if (!calibrating)
        return false;
    if (timeSeconds - lastCalibrationSampleTime < kCalibrationSampleIntervalSeconds)
        return false;

    azimuter::AnalysisSnapshot::Frame frame;
    if (!readAnalysis(frame) || frame.numBands <= 0)
        return false;

    if (!calibrator.addObservation(timeSeconds, frame.delayUs, frame.tiltDb, frame.referenceLevelDb,
                                   frame.significance))
        return false;

    lastCalibrationSampleTime = timeSeconds;
    return true;
}

azimuter::AzimuthCalibrator::Result AzimuterAudioProcessor::finishCalibration()
{
    calibrating = false;
    const auto result = calibrator.solve();
    if (result.valid)
        setTargetDelayMicroseconds(result.targetDelayUs);
    return result;
}

void AzimuterAudioProcessor::abortCalibration()
{
    calibrating = false;
    calibrator.reset();
}

juce::AudioProcessorEditor* AzimuterAudioProcessor::createEditor()
{
    return new AzimuterAudioProcessorEditor(*this);
}

void AzimuterAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = parameters.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
}

void AzimuterAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(parameters.state.getType()))
        {
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
            targetDelayUs.store(static_cast<float>(parameters.state.getProperty(kTargetProperty, 0.0)),
                                std::memory_order_relaxed);
            calibrated.store(static_cast<bool>(parameters.state.getProperty(kCalibratedProperty, false)),
                             std::memory_order_relaxed);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AzimuterAudioProcessor();
}
