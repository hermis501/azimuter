#include "ScreenReaderSpeech.h"

JUCE_BEGIN_IGNORE_WARNINGS_MSVC(4100)
#include <pluginterfaces/base/funknown.h>
JUCE_END_IGNORE_WARNINGS_MSVC

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// UIAutomation.h needs windows.h first; the blank line stops clang-format
// re-sorting them.

#include <UIAutomation.h>
#endif

namespace
{
// From Cockos' reaper_vst3_interfaces.h: REAPER (5.02+) exposes this on the
// IHostApplication it hands every VST3 plug-in. Declared here rather than
// pulled from JUCE's examples folder, which is not a stable include path.
class IReaperHostApplication : public Steinberg::FUnknown
{
public:
    virtual void* PLUGIN_API getReaperApi(Steinberg::CStringA funcname) = 0;
    virtual void* PLUGIN_API getReaperParent(Steinberg::uint32 w) = 0;
    virtual void* PLUGIN_API reaperExtended(Steinberg::uint32 call, void* parm1, void* parm2, void* parm3) = 0;

    static const Steinberg::FUID iid;
};

DECLARE_CLASS_IID(IReaperHostApplication, 0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C)
DEF_CLASS_IID(IReaperHostApplication)

#if JUCE_WINDOWS
/** Raises a UIA notification on the component's accessibility element.
    False if the API is missing (pre-1709 Windows) or there is no element. */
bool raiseUiaNotification(const juce::String& text, juce::Component& source)
{
    using RaiseFn = HRESULT(WINAPI*)(IRawElementProviderSimple*, NotificationKind, NotificationProcessing, BSTR, BSTR);

    // Looked up at run time so the plugin still loads on Windows builds that
    // predate the call. uiautomationcore.dll is already loaded by JUCE.
    static const auto raise = []() -> RaiseFn
    {
        if (auto* module = GetModuleHandleW(L"uiautomationcore.dll"))
            return reinterpret_cast<RaiseFn>(GetProcAddress(module, "UiaRaiseNotificationEvent"));
        if (auto* module = LoadLibraryW(L"uiautomationcore.dll"))
            return reinterpret_cast<RaiseFn>(GetProcAddress(module, "UiaRaiseNotificationEvent"));
        return nullptr;
    }();

    if (raise == nullptr)
        return false;

    auto* handler = source.getAccessibilityHandler();
    if (handler == nullptr || handler->getNativeImplementation() == nullptr)
        return false;

    // JUCE's AccessibilityNativeHandle is declared opaque outside JUCE, but on
    // Windows it is a ComBaseClassHelper whose *first* base is
    // IRawElementProviderSimple, so the two pointers coincide. If a JUCE
    // upgrade ever reorders those bases this cast breaks — check
    // juce_AccessibilityElement_windows.h.
    auto* provider = reinterpret_cast<IRawElementProviderSimple*>(handler->getNativeImplementation());

    BSTR message = SysAllocString(text.toWideCharPointer());
    BSTR activity = SysAllocString(L"Azimuter");
    const HRESULT hr =
        raise(provider, NotificationKind_ActionCompleted, NotificationProcessing_ImportantAll, message, activity);
    SysFreeString(activity);
    SysFreeString(message);
    return SUCCEEDED(hr);
}
#endif
} // namespace

void ScreenReaderSpeech::connectToHost(Steinberg::FUnknown* hostContext)
{
    osaraOutputMessage = nullptr;
    if (hostContext == nullptr)
        return;

    void* reaper = nullptr;
    if (hostContext->queryInterface(IReaperHostApplication::iid, &reaper) != Steinberg::kResultOk || reaper == nullptr)
        return;

    auto* app = static_cast<IReaperHostApplication*>(reaper);
    // Registered by OSARA as "API_osara_outputMessage"; null without OSARA.
    osaraOutputMessage = reinterpret_cast<OsaraOutputMessage>(app->getReaperApi("osara_outputMessage"));
    app->release();
}

void ScreenReaderSpeech::speak(const juce::String& text, juce::Component* source)
{
    if (text.isEmpty())
        return;

    if (osaraOutputMessage != nullptr)
    {
        osaraOutputMessage(text.toRawUTF8());
        return;
    }

#if JUCE_WINDOWS
    if (source != nullptr && raiseUiaNotification(text, *source))
        return;
#else
    juce::ignoreUnused(source);
#endif

    juce::AccessibilityHandler::postAnnouncement(text, juce::AccessibilityHandler::AnnouncementPriority::medium);
}
