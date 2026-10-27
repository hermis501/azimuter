#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Steinberg
{
class FUnknown;
}

/**
    Sends a message to the user's screen reader, in its own voice.

    juce::AccessibilityHandler::postAnnouncement is *not* that on Windows: JUCE
    implements it with a private SAPI voice, so the user hears a second,
    unrelated synthesiser that ignores their screen reader's voice, rate,
    volume and speech-interrupt key. This class tries, in order:

    1. **OSARA**, when the plugin runs inside REAPER with OSARA installed. Its
       exported `osara_outputMessage` is how REAPER's own accessibility layer
       speaks, so Azimuter's messages behave exactly like OSARA's.
    2. **A UIA notification event** (Windows 10 1709+) raised on the editor's
       accessibility element: what NVDA, JAWS and Narrator treat as "speak this
       now" from an application. Also what OSARA itself uses.
    3. JUCE's postAnnouncement, only if neither is available (old Windows, or
       non-Windows hosts, where it does map to the platform screen reader).

    Message thread only.
*/
class ScreenReaderSpeech
{
public:
    /** Called with the VST3 host context. Looks up OSARA if the host is REAPER. */
    void connectToHost(Steinberg::FUnknown* hostContext);

    /** `source` is the component the notification is raised on when UIA is
        used; normally the editor. May be null, which skips the UIA route. */
    void speak(const juce::String& text, juce::Component* source);

    bool usingOsara() const noexcept { return osaraOutputMessage != nullptr; }

private:
    using OsaraOutputMessage = void (*)(const char*);
    OsaraOutputMessage osaraOutputMessage{nullptr};
};
