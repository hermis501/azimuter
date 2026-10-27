#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "Azimuth/AlignmentReadout.h"
#include "PluginProcessor.h"

class AzimuterAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AzimuterAudioProcessorEditor(AzimuterAudioProcessor&);
    ~AzimuterAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateAlignmentReadout();
    void updateCalibrationReadout();
    void calibrateButtonClicked();
    void setCalibrationStatus(const juce::String& text, bool announce);
    juce::String delayAsSpokenText(azimuter::AlignmentBucket bucket, int side) const;

    AzimuterAudioProcessor& processorRef;

    juce::Label delayReadout;
    juce::Label calibrationReadout;
    juce::TextButton calibrateButton;
    juce::Slider cutoffSlider;
    juce::Slider smoothingSlider;
    juce::Slider sonifyGainSlider;
    juce::Label cutoffLabel;
    juce::Label smoothingLabel;
    juce::Label sonifyGainLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> cutoffAttachment;
    std::unique_ptr<SliderAttachment> smoothingAttachment;
    std::unique_ptr<SliderAttachment> sonifyGainAttachment;

    // Decides when the alignment quality is worth speaking. The raw reading
    // wobbles wider than the Excellent bucket, so bucketing it directly both
    // missed real arrivals and fired on noise that cut the speech short.
    azimuter::AlignmentAnnouncer announcer;

    juce::String lastCalibrationText;

    // Sweep progress. The user is told how far they have turned, because
    // covering enough range is the one thing that decides whether the
    // calibration can succeed.
    double calibrationStartMs{0.0};
    float lastAnnouncedRangeUs{-1.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AzimuterAudioProcessorEditor)
};
