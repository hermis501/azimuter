#include "PluginEditor.h"

#include <cmath>

namespace
{
// NVDA on Windows reads only the accessibility *name* (JUCE's setTitle),
// not setDescription or setHelpText. So the title has to carry everything
// the user needs to hear when focusing the control — there is no second
// channel. JAWS will sometimes read description; we mirror title into both
// description and helpText for that, but the title alone must be self-sufficient.
void configureAccessibleLabel(juce::Label& label, const juce::String& title)
{
    label.setTitle(title);
    label.setDescription(title);
    label.setHelpText(title);
    label.setAccessible(true);
    label.setJustificationType(juce::Justification::centredLeft);
}

void configureAccessibleSlider(juce::Slider& slider, const juce::String& title)
{
    slider.setTitle(title);
    slider.setDescription(title);
    slider.setHelpText(title);
    slider.setAccessible(true);
    slider.setWantsKeyboardFocus(true);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 22);
}
} // namespace

AzimuterAudioProcessorEditor::AzimuterAudioProcessorEditor(AzimuterAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setWantsKeyboardFocus(true);
    setTitle("Azimuter. Cassette head azimuth alignment plugin. "
             "The plugin replaces the input with a guidance tone whose pitch and "
             "stereo position track the inter-channel delay between the deck's left "
             "and right channels.");
    setDescription(getTitle());

    configureAccessibleLabel(delayReadout,
                             "Azimuth alignment error in microseconds. Zero means the head matches the angle "
                             "this tape was recorded at. Positive means the right channel leads; negative "
                             "means the left channel leads. Smaller magnitudes are better.");
    delayReadout.setText("Error: --", juce::dontSendNotification);
    addAndMakeVisible(delayReadout);

    configureAccessibleLabel(calibrationReadout,
                             "Azimuth calibration status. Says which delay reading corresponds to correct "
                             "azimuth on the tape currently loaded.");
    calibrationReadout.setText("Not calibrated - aiming at zero delay", juce::dontSendNotification);
    addAndMakeVisible(calibrationReadout);

    // Calibration is the accessible route to correct azimuth: the user sweeps
    // the control and the plugin finds the peak, rather than the user having to
    // hear where the treble is loudest.
    calibrateButton.setTitle("Calibrate azimuth for this tape. "
                             "Press, then turn the azimuth control slowly across its whole range while the "
                             "tape plays, then press again to finish.");
    calibrateButton.setDescription(calibrateButton.getTitle());
    calibrateButton.setHelpText(calibrateButton.getTitle());
    calibrateButton.setAccessible(true);
    calibrateButton.setWantsKeyboardFocus(true);
    calibrateButton.setButtonText("Calibrate this tape");
    calibrateButton.onClick = [this] { calibrateButtonClicked(); };
    addAndMakeVisible(calibrateButton);

    configureAccessibleLabel(cutoffLabel, "Analysis cutoff in Hertz");
    cutoffLabel.setText("Cutoff (Hz)", juce::dontSendNotification);
    addAndMakeVisible(cutoffLabel);

    configureAccessibleSlider(cutoffSlider, "Analysis cutoff in Hertz. "
                                            "Frequencies below this contribute nothing to the reading.");
    cutoffSlider.setTextValueSuffix(" Hz");
    addAndMakeVisible(cutoffSlider);
    cutoffAttachment = std::make_unique<SliderAttachment>(processorRef.parameters, "cutoff", cutoffSlider);

    configureAccessibleLabel(smoothingLabel, "Smoothing time in seconds");
    smoothingLabel.setText("Smoothing (s)", juce::dontSendNotification);
    addAndMakeVisible(smoothingLabel);

    configureAccessibleSlider(smoothingSlider,
                              "Smoothing time in seconds. Time of the delay estimate. Longer values "
                              "give a steadier reading but respond more slowly when you turn the screw.");
    smoothingSlider.setTextValueSuffix(" s");
    addAndMakeVisible(smoothingSlider);
    smoothingAttachment = std::make_unique<SliderAttachment>(processorRef.parameters, "smoothing", smoothingSlider);

    configureAccessibleLabel(sonifyGainLabel, "Guidance tone level");
    sonifyGainLabel.setText("Tone level", juce::dontSendNotification);
    addAndMakeVisible(sonifyGainLabel);

    configureAccessibleSlider(sonifyGainSlider, "Guidance tone level.");
    addAndMakeVisible(sonifyGainSlider);
    sonifyGainAttachment = std::make_unique<SliderAttachment>(processorRef.parameters, "sonifyGain", sonifyGainSlider);

    setSize(460, 330);
    startTimerHz(20);
}

AzimuterAudioProcessorEditor::~AzimuterAudioProcessorEditor()
{
    stopTimer();
}

void AzimuterAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void AzimuterAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(12);

    delayReadout.setBounds(area.removeFromTop(48));
    area.removeFromTop(6);
    calibrationReadout.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    calibrateButton.setBounds(area.removeFromTop(30).removeFromLeft(220));
    area.removeFromTop(10);

    auto laidOutRow = [&area](juce::Component& label, juce::Component& control)
    {
        auto row = area.removeFromTop(28);
        label.setBounds(row.removeFromLeft(140));
        control.setBounds(row);
        area.removeFromTop(8);
    };

    laidOutRow(cutoffLabel, cutoffSlider);
    laidOutRow(smoothingLabel, smoothingSlider);
    laidOutRow(sonifyGainLabel, sonifyGainSlider);
}

juce::String AzimuterAudioProcessorEditor::delayAsSpokenText(azimuter::AlignmentBucket bucket, int side) const
{
    const juce::String magnitude(azimuter::bucketName(bucket));

    // The announcer only names a side from Poor upward. Inside 5 us the sign
    // of the error is noise, and naming it would make the readout flip between
    // "left" and "right" with nothing actually changing.
    if (side == 0)
        return magnitude;
    return magnitude + (side < 0 ? ", left side" : ", right side");
}

void AzimuterAudioProcessorEditor::setCalibrationStatus(const juce::String& text, bool announce)
{
    calibrationReadout.setText(text, juce::dontSendNotification);
    if (announce && text != lastCalibrationText)
    {
        lastCalibrationText = text;
        processorRef.getSpeech().speak(text, this);
    }
}

void AzimuterAudioProcessorEditor::calibrateButtonClicked()
{
    if (processorRef.isCalibrating())
    {
        const auto result = processorRef.finishCalibration();
        calibrateButton.setButtonText("Calibrate this tape");
        announcer.reset(); // the target just moved under the user; re-announce from scratch

        if (result.valid)
        {
            setCalibrationStatus("Calibrated. Correct azimuth is at " + juce::String(result.targetDelayUs, 1) +
                                     " us, plus or minus " + juce::String(result.confidenceUs, 0) +
                                     ". Now align until the tone peaks.",
                                 true);
        }
        else
        {
            const juce::String why = result.rejectReason != nullptr ? juce::String(result.rejectReason)
                                                                    : juce::String("the sweep could not be solved");
            setCalibrationStatus("Calibration failed: " + why + ". Swept " + juce::String(result.sweptRangeUs, 0) +
                                     " microseconds over " + juce::String(result.observations) +
                                     " samples. Nothing changed.",
                                 true);
        }
        return;
    }

    if (!processorRef.beginCalibration())
    {
        setCalibrationStatus("Cannot calibrate yet - start playback first.", true);
        return;
    }

    calibrationStartMs = juce::Time::getMillisecondCounterHiRes();
    lastAnnouncedRangeUs = -1.0f;
    calibrateButton.setButtonText("Finish calibration");
    setCalibrationStatus("Calibrating. Turn the azimuth control slowly across its whole range, "
                         "then press finish.",
                         true);
}

void AzimuterAudioProcessorEditor::updateCalibrationReadout()
{
    if (processorRef.isCalibrating())
    {
        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - calibrationStartMs) * 0.001;
        processorRef.pushCalibrationSample(elapsed);

        const float range = processorRef.calibrationSweptRangeUs();
        const int taken = processorRef.calibrationObservations();
        const bool enough = range >= azimuter::AzimuthCalibrator::kMinSweptRangeUs &&
                            taken >= azimuter::AzimuthCalibrator::kMinObservations;

        calibrationReadout.setText("Calibrating: swept " + juce::String(range, 0) + " us, " + juce::String(taken) +
                                       " samples" + (enough ? " - enough, finish when ready" : ""),
                                   juce::dontSendNotification);

        // Announce only as the swept range passes each 50 us milestone. Range
        // is the thing that decides whether the fit can succeed, so it is the
        // one number worth interrupting for.
        const float milestone = std::floor(range / 50.0f) * 50.0f;
        if (milestone > lastAnnouncedRangeUs && milestone >= 50.0f)
        {
            lastAnnouncedRangeUs = milestone;
            const juce::String msg =
                enough ? "Swept " + juce::String(milestone, 0) + " microseconds. Enough range - keep going or finish."
                       : "Swept " + juce::String(milestone, 0) + " microseconds. Keep turning.";
            processorRef.getSpeech().speak(msg, this);
        }
        return;
    }

    if (processorRef.hasCalibration())
        setCalibrationStatus("Calibrated: correct azimuth reads " +
                                 juce::String(processorRef.getTargetDelayMicroseconds(), 1) + " us",
                             false);
    else
        setCalibrationStatus("Not calibrated - aiming at zero delay", false);
}

void AzimuterAudioProcessorEditor::updateAlignmentReadout()
{
    const float errorUs = processorRef.getAlignmentErrorMicroseconds();

    // A sweep drags the error through every bucket, and the target is about to
    // move underneath it, so the quality cue is meaningless until it settles.
    if (processorRef.isCalibrating())
    {
        announcer.reset();
        delayReadout.setText("Error: " + juce::String(errorUs, 1) + " us  (calibrating)", juce::dontSendNotification);
        return;
    }

    const bool announce = announcer.push(errorUs, AzimuterAudioProcessor::kFullScaleDelayMicroseconds);
    const juce::String spoken = delayAsSpokenText(announcer.bucket(), announcer.side());

    // The number stays live so it tracks the screwdriver; the bucket is the
    // settled judgement, which is what gets spoken.
    delayReadout.setText("Error: " + juce::String(errorUs, 1) + " us  (" + spoken + ")", juce::dontSendNotification);

    if (!announce)
        return;

    // NVDA on Windows does NOT announce notifyAccessibilityEvent(valueChanged)
    // on a juce::Label — static-text elements don't surface value changes the
    // way sliders do. So it is spoken explicitly, through ScreenReaderSpeech:
    // OSARA inside REAPER, else a UIA notification. Not JUCE's
    // postAnnouncement, which on Windows is a private SAPI voice.
    //
    // While turning, the only thing spoken is the arrival at Excellent (the
    // announcer runs in arrivalOnly mode). The tone is the guidance; speech
    // confirms the one thing the tone can't state absolutely. No number: inside
    // 5 us it is mostly the meter's own wobble and there is nothing to act on.
    // The full reading stays in the label for anyone who focuses it.
    //
    // The announcer guarantees at least three seconds between announcements,
    // so each one is heard in full.
    processorRef.getSpeech().speak(spoken + ".", this);
}

void AzimuterAudioProcessorEditor::timerCallback()
{
    updateCalibrationReadout();
    updateAlignmentReadout();
}
