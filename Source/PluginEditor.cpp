#include "PluginEditor.h"
#include "AccessibleSettingsPanel.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

// UIA constants for screen reader notifications
#if JUCE_WINDOWS
 #ifndef NotificationKind_ActionCompleted
  #define NotificationKind_ActionCompleted 4
 #endif
 #ifndef NotificationProcessing_ImportantAll
  #define NotificationProcessing_ImportantAll 2
 #endif
 #include <Shlobj.h>
#endif

//==============================================================================
// Read-only text value interface for braille display support.
// Components with getCurrentText() use this so that braille displays
// persistently show the current value (not just a 4-second flash).
//==============================================================================
class ReadOnlyTextValue : public juce::AccessibilityTextValueInterface
{
public:
    explicit ReadOnlyTextValue (std::function<juce::String()> getter)
        : getTextFn (std::move (getter)) {}

    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override
    {
        return getTextFn ? getTextFn() : juce::String{};
    }
    void setValueAsString (const juce::String&) override {}

private:
    std::function<juce::String()> getTextFn;
};

#if JUCE_WINDOWS
//==============================================================================
// NVDA Controller Client — static singleton
//==============================================================================
static NvdaApi initNvda()
{
    NvdaApi api;

    wchar_t appData[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData);

    std::wstring dllPath = std::wstring(appData) + L"\\TunerAccess\\nvdaControllerClient.dll";
    api.dll = LoadLibraryW(dllPath.c_str());

    // Standard Windows DLL search (next to the host EXE / VST3) — keeps autonomy.
    if (!api.dll)
        api.dll = LoadLibraryA("nvdaControllerClient64.dll");

    if (!api.dll)
        api.dll = LoadLibraryA("nvdaControllerClient.dll");

    if (api.dll)
    {
        api.speakText      = (NvdaApi::SpeakFunc)     GetProcAddress(api.dll, "nvdaController_speakText");
        api.testIfRunning  = (NvdaApi::TestFunc)      GetProcAddress(api.dll, "nvdaController_testIfRunning");
        api.cancelSpeech   = (NvdaApi::CancelFunc)    GetProcAddress(api.dll, "nvdaController_cancelSpeech");
        api.speakSsml      = (NvdaApi::SpeakSsmlFunc) GetProcAddress(api.dll, "nvdaController_speakSsml");
    }

    return api;
}

const NvdaApi& getNvda()
{
    static NvdaApi instance = initNvda();
    return instance;
}

//==============================================================================
// UIA notification fallback (for non-NVDA screen readers)
//==============================================================================
static void uiaRaiseNotification(const juce::String& message)
{
    using RaiseNotifFunc = HRESULT(WINAPI*)(void*, int, int, BSTR, BSTR);

    static auto* uiaModule = LoadLibraryW(L"UIAutomationCore.dll");
    static auto raiseNotif = uiaModule
        ? reinterpret_cast<RaiseNotifFunc>(GetProcAddress(uiaModule, "UiaRaiseNotificationEvent"))
        : nullptr;

    if (!raiseNotif)
        return;

    auto& desktop = juce::Desktop::getInstance();
    if (desktop.getNumComponents() == 0) return;

    auto* topComp = desktop.getComponent(0);
    if (!topComp) return;

    auto* handler = topComp->getAccessibilityHandler();
    if (!handler) return;

    auto* native = handler->getNativeImplementation();
    if (!native) return;

    auto bstr = SysAllocString(message.toWideCharPointer());
    raiseNotif(reinterpret_cast<void*>(native),
               NotificationKind_ActionCompleted,
               NotificationProcessing_ImportantAll,
               bstr, bstr);
    SysFreeString(bstr);
}

//==============================================================================
// Global screen reader announce functions
//==============================================================================
static juce::String wrapSsml(const juce::String& plainText)
{
    auto escaped = plainText
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;");
    return "<speak>" + escaped + "</speak>";
}

static void nvdaSpeakWithPriority(const NvdaApi& nvda, const juce::String& message, int priority)
{
    if (nvda.speakSsml)
    {
        auto ssml = wrapSsml(message);
        nvda.speakSsml(ssml.toWideCharPointer(), kSymbolUnchanged, priority, 1);
    }
    else
    {
        if (priority == kSpeechNow && nvda.cancelSpeech)
            nvda.cancelSpeech();
        if (nvda.speakText)
            nvda.speakText(message.toWideCharPointer());
    }
}

#endif // JUCE_WINDOWS — end of NVDA / UIA Windows-specific block

//==============================================================================
// Cross-platform screen reader announce wrappers.
//
// Windows: route through NVDA Controller Client (or UIA fallback).
// macOS  : route through juce::AccessibilityHandler::postAnnouncement, which
//          uses NSAccessibility / VoiceOver under the hood.
//==============================================================================
void screenReaderAnnounce(const juce::String& message)
{
   #if JUCE_WINDOWS
    auto& nvda = getNvda();
    if (nvda.isNvdaRunning())
        nvdaSpeakWithPriority(nvda, message, kSpeechNormal);
    else
        uiaRaiseNotification(message);
   #else
    juce::AccessibilityHandler::postAnnouncement(message,
        juce::AccessibilityHandler::AnnouncementPriority::medium);
   #endif
}

void screenReaderAnnounceNow(const juce::String& message)
{
   #if JUCE_WINDOWS
    auto& nvda = getNvda();
    if (nvda.isNvdaRunning())
        nvdaSpeakWithPriority(nvda, message, kSpeechNow);
    else
        uiaRaiseNotification(message);
   #else
    juce::AccessibilityHandler::postAnnouncement(message,
        juce::AccessibilityHandler::AnnouncementPriority::high);
   #endif
}

void screenReaderAnnounceInterrupt(const juce::String& message)
{
   #if JUCE_WINDOWS
    auto& nvda = getNvda();
    if (nvda.isNvdaRunning())
        nvdaSpeakWithPriority(nvda, message, kSpeechNow);
    else
        uiaRaiseNotification(message);
   #else
    juce::AccessibilityHandler::postAnnouncement(message,
        juce::AccessibilityHandler::AnnouncementPriority::high);
   #endif
}

void screenReaderAnnounceNext(const juce::String& message)
{
   #if JUCE_WINDOWS
    auto& nvda = getNvda();
    if (nvda.isNvdaRunning())
        nvdaSpeakWithPriority(nvda, message, kSpeechNext);
    else
        uiaRaiseNotification(message);
   #else
    juce::AccessibilityHandler::postAnnouncement(message,
        juce::AccessibilityHandler::AnnouncementPriority::medium);
   #endif
}

void announceValue()
{
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (auto* handler = focused->getAccessibilityHandler())
            if (handler->getValueInterface() != nullptr)
                handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

//==============================================================================
// TunerComponent implementation (ported verbatim from GuitarAccess)
//==============================================================================
juce::String TunerAccessAudioProcessorEditor::TunerComponent::getCurrentText() const
{
    if (navField == 0)
    {
        if (tuningPresetIndex == kFreeChromatic)
            return "Tuning, Free Chromatic";
        return juce::String("Tuning, ") + getTuningPresets()[static_cast<size_t>(tuningPresetIndex)].name;
    }
    return getStringText();
}

juce::String TunerAccessAudioProcessorEditor::TunerComponent::getStringText() const
{
    if (tuningPresetIndex == kFreeChromatic)
        return "No string target in chromatic mode";

    auto& preset = getTuningPresets()[static_cast<size_t>(tuningPresetIndex)];
    int midi = preset.midiNotes[static_cast<size_t>(currentString)];
    return "String " + juce::String(6 - currentString) + ", target " + midiNoteToString(midi);
}

void TunerAccessAudioProcessorEditor::TunerComponent::focusGained(FocusChangeType)
{
    auto text = "Guitar Tuner. " + getCurrentText() + ". Press H for help";
    screenReaderAnnounceNow(text);
}

std::unique_ptr<juce::AccessibilityHandler>
TunerAccessAudioProcessorEditor::TunerComponent::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::unspecified,
        juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces {
            std::make_unique<ReadOnlyTextValue> ([this]() { return getCurrentText(); }) });
}

void TunerAccessAudioProcessorEditor::TunerComponent::timerCallback()
{
    float freq = editor.processorRef.tunerEngine.detectedFrequency.load(std::memory_order_relaxed);

    if (freq < 0.0f)
    {
        if (lastAnnouncedFreq >= 0.0f)
        {
            lastAnnouncedFreq = -1.0f;
            smoothedCents = 0.0f;
        }
        return;
    }

    buildAnnouncement(freq);
}

void TunerAccessAudioProcessorEditor::TunerComponent::buildAnnouncement(float freq)
{
    auto note = frequencyToNote(freq);
    if (!note.valid)
        return;

    bool isGuided = (tuningPresetIndex != kFreeChromatic);

    if (isGuided)
    {
        // Guided mode: compare to target string
        auto& preset = getTuningPresets()[static_cast<size_t>(tuningPresetIndex)];
        int targetMidi = preset.midiNotes[static_cast<size_t>(currentString)];
        float cents = centsFromTarget(freq, targetMidi);

        // Smooth
        smoothedCents = 0.6f * smoothedCents + 0.4f * cents;
        int centsRounded = static_cast<int>(std::round(smoothedCents));

        // Only announce on significant change or first detection
        bool firstDetection = (lastAnnouncedFreq < 0.0f);
        bool significantChange = std::abs(freq - lastAnnouncedFreq) > 3.0f;
        if (!firstDetection && !significantChange)
            return;
        lastAnnouncedFreq = freq;

        juce::String targetName = midiNoteToString(targetMidi);
        juce::String detectedName = juce::String(note.noteName) + juce::String(note.octave);

        // How far from target in semitones?
        int semitoneDiff = note.midiNote - targetMidi;

        juce::String msg;
        if (std::abs(semitoneDiff) > 1)
        {
            // Far from target — announce target, detected, direction
            msg = "Target " + targetName + ". Hearing " + detectedName;
            msg += (semitoneDiff > 0) ? ", tune down" : ", tune up";
        }
        else if (std::abs(centsRounded) <= 2)
        {
            msg = targetName + ", tuned";
        }
        else if (std::abs(centsRounded) <= 5)
        {
            // Very close — just cents, no direction
            msg = targetName;
            if (centsRounded > 0)
                msg += ", sharp " + juce::String(centsRounded) + " cents";
            else
                msg += ", flat " + juce::String(-centsRounded) + " cents";
        }
        else
        {
            // Close — cents + direction
            msg = targetName;
            if (centsRounded > 0)
                msg += ", sharp " + juce::String(centsRounded) + " cents, tune down";
            else
                msg += ", flat " + juce::String(-centsRounded) + " cents, tune up";
        }

        screenReaderAnnounceInterrupt(msg);
    }
    else
    {
        // Free chromatic mode
        smoothedCents = 0.6f * smoothedCents + 0.4f * note.centsDeviation;

        bool firstDetection = (lastAnnouncedFreq < 0.0f);
        bool significantChange = std::abs(freq - lastAnnouncedFreq) > 3.0f;
        if (!firstDetection && !significantChange)
            return;
        lastAnnouncedFreq = freq;

        int centsRounded = static_cast<int>(std::round(smoothedCents));
        juce::String msg = juce::String(note.noteName) + juce::String(note.octave);

        if (std::abs(centsRounded) <= 2)
            msg += ", tuned";
        else if (centsRounded > 0)
            msg += ", sharp " + juce::String(centsRounded) + " cents";
        else
            msg += ", flat " + juce::String(-centsRounded) + " cents";

        screenReaderAnnounceInterrupt(msg);
    }
}

//==============================================================================
// InputComponent — 2 named input slots, each with device channel + gain.
//==============================================================================
static juce::AudioDeviceManager* getStandaloneDeviceManager()
{
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        return &holder->deviceManager;
    return nullptr;
}

static juce::String getDeviceChannelName(int chIdx)
{
    auto* dm = getStandaloneDeviceManager();
    if (dm == nullptr) return "Channel " + juce::String(chIdx + 1);
    auto* dev = dm->getCurrentAudioDevice();
    if (dev == nullptr) return "Channel " + juce::String(chIdx + 1);
    auto names = dev->getInputChannelNames();
    if (chIdx >= 0 && chIdx < names.size())
        return "Channel " + juce::String(chIdx + 1) + ", " + names[chIdx];
    return "Channel " + juce::String(chIdx + 1);
}

static int getDeviceNumInputChannels()
{
    auto* dm = getStandaloneDeviceManager();
    if (dm == nullptr) return 2;
    auto* dev = dm->getCurrentAudioDevice();
    if (dev == nullptr) return 2;
    return dev->getInputChannelNames().size();
}

juce::String TunerAccessAudioProcessorEditor::InputComponent::getCurrentText() const
{
    auto& proc = editor.processorRef;
    int idx = juce::jlimit(0, 1, proc.activeInputIndex.load(std::memory_order_relaxed));
    const auto& p = proc.inputPresets[static_cast<size_t>(idx)];

    juce::String header = juce::String("Input ") + juce::String(idx + 1)
                        + ", " + p.name + ", ";

    if (subParam == 0)
    {
        // Gain field
        juce::String gainStr = (p.gainDb >= 0.0f ? "plus " : "minus ")
                             + juce::String(std::abs(p.gainDb), 1) + " dB";
        return header + "Gain, " + gainStr;
    }
    // Device field
    return header + "Device, " + getDeviceChannelName(p.deviceChannel);
}

void TunerAccessAudioProcessorEditor::InputComponent::focusGained(FocusChangeType)
{
    setName(getCurrentText());
    setTitle(getCurrentText());
    auto text = "Input. " + getCurrentText()
              + ". Up Down to switch input, Left Right to switch parameter, "
                "Alt Up Down to adjust, F2 to rename.";
    screenReaderAnnounceNow(text);
}

std::unique_ptr<juce::AccessibilityHandler>
TunerAccessAudioProcessorEditor::InputComponent::createAccessibilityHandler()
{
    class InputValue : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit InputValue(InputComponent& c) : comp(c) {}
        bool isReadOnly() const override { return true; }
        juce::String getCurrentValueAsString() const override { return comp.getCurrentText(); }
        void setValueAsString(const juce::String&) override {}
    private:
        InputComponent& comp;
    };

    return std::make_unique<juce::AccessibilityHandler>(
        *this,
        juce::AccessibilityRole::unspecified,
        juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces {
            std::make_unique<InputValue>(*this) });
}

void TunerAccessAudioProcessorEditor::InputComponent::refreshAccessibility()
{
    setName(getCurrentText());
    setTitle(getCurrentText());
    announceValue();
}

bool TunerAccessAudioProcessorEditor::InputComponent::keyPressed(const juce::KeyPress& key)
{
    auto kc = key.getKeyCode();
    bool alt = key.getModifiers().isAltDown();
    bool shift = key.getModifiers().isShiftDown();
    bool ctrl = key.getModifiers().isCtrlDown();
    bool noMods = !key.getModifiers().isAnyModifierKeyDown();

    auto& proc = editor.processorRef;

    // Up/Down (no mods): switch active input slot (Input 1 <-> Input 2).
    if (noMods && (kc == juce::KeyPress::upKey || kc == juce::KeyPress::downKey))
    {
        int dir = (kc == juce::KeyPress::upKey) ? -1 : 1;
        int idx = juce::jlimit(0, 1,
                  proc.activeInputIndex.load(std::memory_order_relaxed) + dir);
        proc.activeInputIndex.store(idx, std::memory_order_relaxed);
        proc.applyActiveInput(getStandaloneDeviceManager());
        refreshAccessibility();
        return true;
    }

    // Left/Right (no mods): toggle sub-param (Gain <-> Device).
    if (noMods && (kc == juce::KeyPress::leftKey || kc == juce::KeyPress::rightKey))
    {
        subParam = (kc == juce::KeyPress::rightKey) ? 1 : 0;
        refreshAccessibility();
        return true;
    }

    // Alt+Up/Down: adjust current sub-param.
    if (alt && !ctrl && (kc == juce::KeyPress::upKey || kc == juce::KeyPress::downKey))
    {
        int dir = (kc == juce::KeyPress::upKey) ? +1 : -1;
        int idx = juce::jlimit(0, 1, proc.activeInputIndex.load(std::memory_order_relaxed));

        if (subParam == 0)
        {
            // Gain
            float step = shift ? 3.0f : 0.5f;
            float newGain = proc.inputPresets[static_cast<size_t>(idx)].gainDb + dir * step;
            proc.setPresetGainDb(idx, newGain);
        }
        else
        {
            // Device channel — cycle within device's range
            int numCh = getDeviceNumInputChannels();
            if (numCh <= 0) return true;
            int cur = proc.inputPresets[static_cast<size_t>(idx)].deviceChannel;
            int next = juce::jlimit(0, numCh - 1, cur + dir);
            proc.setPresetDeviceChannel(idx, next, getStandaloneDeviceManager());
        }
        refreshAccessibility();
        return true;
    }

    // F2: rename active input
    if (noMods && kc == juce::KeyPress::F2Key)
    {
        startRename();
        return true;
    }

    // F10: passes through (handled at editor level via TunerComponent, but here too)
    if (noMods && kc == juce::KeyPress::F10Key)
    {
        if (editor.isStandalone())
            editor.openAudioSettings();
        return true;
    }

    return false;
}

void TunerAccessAudioProcessorEditor::InputComponent::startRename()
{
    if (renameEditor != nullptr) return;

    auto& proc = editor.processorRef;
    int idx = juce::jlimit(0, 1, proc.activeInputIndex.load(std::memory_order_relaxed));

    renameEditor = std::make_unique<juce::TextEditor>();
    renameEditor->setText(proc.inputPresets[static_cast<size_t>(idx)].name, juce::dontSendNotification);
    renameEditor->setSelectAllWhenFocused(true);
    renameEditor->selectAll();
    renameEditor->setBounds(getLocalBounds().reduced(8, 8));
    renameEditor->setName("Rename Input " + juce::String(idx + 1));
    renameEditor->setTitle("Rename Input " + juce::String(idx + 1));
    renameEditor->setDescription("Type new name. Enter to confirm, Escape to cancel.");
    renameEditor->setEscapeAndReturnKeysConsumed(true);
    renameEditor->setWantsKeyboardFocus(true);

    auto self = juce::Component::SafePointer<InputComponent>(this);
    renameEditor->onReturnKey = [self]{ if (self) self->commitRename(); };
    renameEditor->onEscapeKey = [self]{ if (self) self->cancelRename(); };
    renameEditor->onFocusLost = [self]{ if (self) self->cancelRename(); };

    addAndMakeVisible(*renameEditor);
    renameEditor->grabKeyboardFocus();

    screenReaderAnnounceNow("Rename Input " + juce::String(idx + 1)
                            + ". Current name: " + proc.inputPresets[static_cast<size_t>(idx)].name
                            + ". Type new name, Enter to confirm, Escape to cancel.");
}

void TunerAccessAudioProcessorEditor::InputComponent::commitRename()
{
    if (renameEditor == nullptr) return;

    auto& proc = editor.processorRef;
    int idx = juce::jlimit(0, 1, proc.activeInputIndex.load(std::memory_order_relaxed));
    auto newName = renameEditor->getText().trim();
    proc.setPresetName(idx, newName);

    auto self = juce::Component::SafePointer<InputComponent>(this);
    juce::MessageManager::callAsync([self]
    {
        if (self == nullptr) return;
        self->renameEditor.reset();
        self->refreshAccessibility();
        self->grabKeyboardFocus();
        auto& p = self->editor.processorRef.inputPresets[
                    static_cast<size_t>(juce::jlimit(0, 1,
                        self->editor.processorRef.activeInputIndex.load(std::memory_order_relaxed)))];
        screenReaderAnnounceNow("Renamed to " + p.name);
    });
}

void TunerAccessAudioProcessorEditor::InputComponent::cancelRename()
{
    if (renameEditor == nullptr) return;
    auto self = juce::Component::SafePointer<InputComponent>(this);
    juce::MessageManager::callAsync([self]
    {
        if (self == nullptr) return;
        self->renameEditor.reset();
        self->refreshAccessibility();
        self->grabKeyboardFocus();
        screenReaderAnnounceNow("Rename cancelled.");
    });
}

void TunerAccessAudioProcessorEditor::TunerComponent::announceHelp()
{
    // Same content as GuitarAccess contextHelpData["tuner"]
    juce::String help =
        "Guitar Tuner with tuning presets. "
        "Left or Right: Switch field between Tuning and String. "
        "Up or Down: Change tuning preset or string. "
        "Enter: Start or stop tuner. "
        "Control plus Up or Down: Quick string change. "
        "Home or End: First or last item. "
        "Tab moves to the Input panel where you can switch between 2 named inputs, "
        "adjust gain with Alt Up Down, change device channel with Left Right then Alt Up Down, "
        "and rename with F2. "
        "F10 opens audio settings.";
    screenReaderAnnounceNow(help);
}

bool TunerAccessAudioProcessorEditor::TunerComponent::keyPressed(const juce::KeyPress& key)
{
    auto kc = key.getKeyCode();
    bool alt = key.getModifiers().isAltDown();
    bool shift = key.getModifiers().isShiftDown();
    bool ctrl = key.getModifiers().isCtrlDown();
    bool noMods = !key.getModifiers().isAnyModifierKeyDown();

    auto& presets = getTuningPresets();
    int numPresets = static_cast<int>(presets.size());

    // Left/Right: navigate between 2 fields (Tuning <-> String).
    // In chromatic mode, only Tuning is shown.
    if (noMods && (kc == juce::KeyPress::leftKey || kc == juce::KeyPress::rightKey))
    {
        if (tuningPresetIndex == kFreeChromatic)
            navField = 0;
        else
            navField = (navField == 0) ? 1 : 0;
        setName(getCurrentText());
        setTitle(getCurrentText());
        announceValue();
        return true;
    }

    // Up/Down: change value in current field
    if (noMods && (kc == juce::KeyPress::upKey || kc == juce::KeyPress::downKey))
    {
        int dir = (kc == juce::KeyPress::upKey) ? -1 : 1;

        if (navField == 0)
        {
            // Browse tuning presets: Free Chromatic (-1), then 0..numPresets-1
            if (dir > 0)
            {
                if (tuningPresetIndex == kFreeChromatic)
                    tuningPresetIndex = 0;
                else if (tuningPresetIndex < numPresets - 1)
                    tuningPresetIndex++;
            }
            else
            {
                if (tuningPresetIndex == 0)
                    tuningPresetIndex = kFreeChromatic;
                else if (tuningPresetIndex > 0)
                    tuningPresetIndex--;
            }
            currentString = 0;
        }
        else if (navField == 1 && tuningPresetIndex != kFreeChromatic)
        {
            currentString = juce::jlimit(0, 5, currentString + dir);
        }

        setName(getCurrentText());
        setTitle(getCurrentText());
        announceValue();
        return true;
    }

    // Home/End in tuning preset field
    if (noMods && navField == 0 && kc == juce::KeyPress::homeKey)
    {
        tuningPresetIndex = kFreeChromatic;
        currentString = 0;
        setName(getCurrentText());
        setTitle(getCurrentText());
        announceValue();
        return true;
    }
    if (noMods && navField == 0 && kc == juce::KeyPress::endKey)
    {
        tuningPresetIndex = numPresets - 1;
        currentString = 0;
        setName(getCurrentText());
        setTitle(getCurrentText());
        announceValue();
        return true;
    }

    // Home/End in string field
    if (noMods && navField == 1 && kc == juce::KeyPress::homeKey)
    {
        currentString = 0;
        setName(getCurrentText());
        setTitle(getCurrentText());
        announceValue();
        return true;
    }
    if (noMods && navField == 1 && kc == juce::KeyPress::endKey)
    {
        currentString = 5;
        setName(getCurrentText());
        setTitle(getCurrentText());
        announceValue();
        return true;
    }

    // Ctrl+Up/Down: change string quickly (any field).
    // Update UIA Name so braille reflects the new string immediately.
    if (ctrl && !alt && !shift && (kc == juce::KeyPress::upKey || kc == juce::KeyPress::downKey))
    {
        if (tuningPresetIndex != kFreeChromatic)
        {
            int dir = (kc == juce::KeyPress::upKey) ? -1 : 1;
            currentString = juce::jlimit(0, 5, currentString + dir);
            lastAnnouncedFreq = -1.0f;
            smoothedCents = 0.0f;
            setName(getCurrentText());
            setTitle(getCurrentText());
            screenReaderAnnounce(getStringText());
        }
        return true;
    }

    // Enter: toggle tuner (any field).
    // Set UIA Name (componentTitle) BEFORE the speech announcement so any async
    // NVDA UIA query during the announcement sees the new state. See
    // braille_display_implementation.md.
    if (noMods && kc == juce::KeyPress::returnKey)
    {
        tunerActive = !tunerActive;
        setName(getCurrentText());
        setTitle(getCurrentText());
        editor.processorRef.tunerEngine.active.store(tunerActive, std::memory_order_relaxed);

        if (tunerActive)
        {
            lastAnnouncedFreq = -1.0f;
            smoothedCents = 0.0f;
            startTimerHz(15);

            if (tuningPresetIndex == kFreeChromatic)
                screenReaderAnnounceNow("Tuner started, chromatic mode. Play a string.");
            else
                screenReaderAnnounceNow("Tuner started. " + getStringText() + ". Play the string.");
        }
        else
        {
            stopTimer();
            screenReaderAnnounceNow("Tuner stopped.");
        }
        return true;
    }

    // H or Ctrl+H: announce help
    if ((noMods || (ctrl && !alt && !shift)) && kc == 'H')
    {
        announceHelp();
        return true;
    }

    // F10  or  Ctrl+,  : open accessible Audio Settings (standalone only).
    // F10 is layout-independent — Ctrl+, doesn't fire ',' on French AZERTY
    // keyboards (the physical comma key sends ';' there).
    if ((noMods && kc == juce::KeyPress::F10Key)
        || (ctrl && !alt && !shift && kc == ','))
    {
        if (editor.isStandalone())
            editor.openAudioSettings();
        return true;
    }

    // Alt+F4: quit the standalone (defensive — Windows normally routes this,
    // but the JUCE first-launch audio popup can swallow it).
    if (alt && !ctrl && !shift && kc == juce::KeyPress::F4Key)
    {
        if (juce::StandalonePluginHolder::getInstance() != nullptr)
        {
            if (auto* app = juce::JUCEApplicationBase::getInstance())
            {
                app->systemRequestedQuit();
                return true;
            }
        }
        return false; // let plugin host handle it
    }

    return false;
}

void TunerAccessAudioProcessorEditor::openAudioSettings()
{
    if (! isStandalone())
    {
        screenReaderAnnounceNow("Audio settings are only available in standalone mode.");
        return;
    }

    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr)
        return;

    // Remember who had focus so we can restore it after the dialog closes
    // (handled by componentBroughtToFront when the editor regains foreground).
    savedFocusComponent = juce::Component::getCurrentlyFocusedComponent();

    auto content = std::make_unique<AccessibleSettingsPanel>(holder->deviceManager, processorRef);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content.release());
    options.dialogTitle                  = "Audio Settings";
    options.dialogBackgroundColour       = juce::Colour(0xFF1A1A2E);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar            = true;
    options.resizable                    = false;
    auto* dw = options.launchAsync();
    if (dw != nullptr)
        dw->setTitle(" "); // Suppress NVDA "Audio Settings fenêtre" prefix on combo Tab

    // NEXT priority queues after the native focus announcement instead of racing it.
    screenReaderAnnounceNext("Audio settings opened.");
}

//==============================================================================
// Editor
//==============================================================================
TunerAccessAudioProcessorEditor::TunerAccessAudioProcessorEditor(TunerAccessAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    setResizable(false, false);
    setSize(700, 260);

    setName("TunerAccess");
    setTitle(" "); // Defensive: prevent UIA Name fallback to JucePlugin_Name on the editor itself
    setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);

    tunerComp.setName("Guitar Tuner");
    tunerComp.setTitle("Guitar Tuner");
    tunerComp.setDescription("Up or Down to change tuning, Enter to start or stop. Press H for help.");
    tunerComp.setWantsKeyboardFocus(true);
    tunerComp.setExplicitFocusOrder(1);
    addAndMakeVisible(tunerComp);

    inputComp.setName("Input");
    inputComp.setTitle("Input");
    inputComp.setDescription("Up Down switches input slot. Left Right switches parameter. Alt Up Down adjusts. F2 renames.");
    inputComp.setWantsKeyboardFocus(true);
    inputComp.setExplicitFocusOrder(2);
    addAndMakeVisible(inputComp);

    setWantsKeyboardFocus(true);

    // Stage A (300ms): once the standalone window has fully wrapped the editor,
    // clear the parent chain's UIA Name so NVDA stops prefixing "TunerAccess fenêtre"
    // on every Tab. setName("") alone does NOT work — UIA reads componentTitle.
    // See feedback_juce_setname_vs_settitle_uia_trap.md (one-line fix after 6 failed
    // attempts in DrumAccess). Also register as ComponentListener on the top-level
    // window so we recover focus after Alt+Tab / minimize-restore.
    juce::Timer::callAfterDelay(300, [safeThis = juce::Component::SafePointer<TunerAccessAudioProcessorEditor>(this)]()
    {
        if (safeThis == nullptr) return;

        if (safeThis->isStandalone())
        {
            for (auto* p = safeThis->getParentComponent(); p != nullptr; p = p->getParentComponent())
            {
                p->setName("");
                p->setTitle(" "); // THE actual UIA Name suppression (single space, NOT empty)
            }
        }

        if (auto* topLevel = safeThis->getTopLevelComponent())
        {
            if (topLevel != safeThis.getComponent())
            {
                safeThis->trackedTopLevel = topLevel;
                topLevel->addComponentListener(safeThis.getComponent());
            }
        }
    });

    // Stage B (1000ms, standalone only): native Win32 focus kickstart.
    // JUCE-only grabKeyboardFocus() is invisible to NVDA on first launch — we must
    // simulate a Tab keypress in the native message queue so the OS focus chain
    // wakes up. Then redirect focus to tunerComp + re-fire UIA focus for NVDA.
    juce::Timer::callAfterDelay(1000, [safeThis = juce::Component::SafePointer<TunerAccessAudioProcessorEditor>(this)]()
    {
        if (safeThis == nullptr || !safeThis->isShowing() || !safeThis->isStandalone())
            return;

      #if JUCE_WINDOWS
        if (auto* topLevel = safeThis->getTopLevelComponent())
        {
            if (auto* peer = topLevel->getPeer())
            {
                auto hwnd = (HWND) peer->getNativeHandle();
                if (hwnd != nullptr)
                {
                    ::SetFocus(hwnd);
                    ::PostMessage(hwnd, WM_KEYDOWN, VK_TAB, 0);
                    ::PostMessage(hwnd, WM_KEYUP,   VK_TAB, 0);
                }
            }
        }
      #endif

        // Stage C (+300ms): redirect focus to tunerComp.
        juce::Timer::callAfterDelay(300, [safeThis2 = juce::Component::SafePointer<TunerAccessAudioProcessorEditor>(safeThis.getComponent())]()
        {
            if (safeThis2 == nullptr) return;
            safeThis2->tunerComp.grabKeyboardFocus();
            screenReaderAnnounceNow("Guitar Tuner. " + safeThis2->tunerComp.getCurrentText() + ". Press H for help.");

            // Stage D (+150ms): re-fire UIA focus so NVDA actually speaks.
            juce::Timer::callAfterDelay(150, [safeComp = juce::Component::SafePointer<juce::Component>(&safeThis2->tunerComp)]()
            {
                if (safeComp == nullptr) return;
                if (safeComp->hasKeyboardFocus(false))
                    if (auto* handler = safeComp->getAccessibilityHandler())
                        handler->grabFocus();
            });
        });
    });
}

TunerAccessAudioProcessorEditor::~TunerAccessAudioProcessorEditor()
{
    if (trackedTopLevel != nullptr)
        trackedTopLevel->removeComponentListener(this);

    tunerComp.deactivate();
    processorRef.tunerEngine.active.store(false, std::memory_order_relaxed);
}

std::unique_ptr<juce::AccessibilityHandler>
TunerAccessAudioProcessorEditor::createAccessibilityHandler()
{
    // Explicit handler with role=unspecified guarantees UIA Name is sourced from
    // componentTitle (which we set to " "). Without this override, JUCE may fall
    // back to AudioProcessorEditor defaults that don't expose the title path.
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::unspecified);
}

void TunerAccessAudioProcessorEditor::focusGained(FocusChangeType)
{
    // The editor itself should never hold focus; bounce immediately to tunerComp
    // (or to a saved focus target if we just returned from the Settings dialog).
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<TunerAccessAudioProcessorEditor>(this)]()
    {
        if (safe == nullptr || !safe->isShowing()) return;

        if (safe->savedFocusComponent != nullptr && safe->savedFocusComponent->isShowing())
        {
            safe->savedFocusComponent->grabKeyboardFocus();
            safe->savedFocusComponent = nullptr;
        }
        else
        {
            safe->tunerComp.grabKeyboardFocus();
        }
    });
}

void TunerAccessAudioProcessorEditor::componentBroughtToFront(juce::Component&)
{
  #if JUCE_WINDOWS
    if (isStandalone())
    {
        if (auto* topLevel = getTopLevelComponent())
            if (auto* peer = topLevel->getPeer())
            {
                auto hwnd = (HWND) peer->getNativeHandle();
                if (hwnd != nullptr && hwnd != ::GetFocus())
                    ::SetFocus(hwnd);
            }
    }
  #endif

    if (!isStandalone())
        return;

    // Stage 1 (150ms): make sure a real child has JUCE focus after Alt+Tab return.
    juce::Timer::callAfterDelay(150, [safeThis = juce::Component::SafePointer<TunerAccessAudioProcessorEditor>(this)]()
    {
        if (safeThis == nullptr) return;

        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (focused == nullptr || !safeThis->isParentOf(focused))
        {
            if (safeThis->savedFocusComponent != nullptr && safeThis->savedFocusComponent->isShowing())
            {
                safeThis->savedFocusComponent->grabKeyboardFocus();
                focused = safeThis->savedFocusComponent.getComponent();
                safeThis->savedFocusComponent = nullptr;
            }
            else
            {
                safeThis->tunerComp.grabKeyboardFocus();
                focused = &safeThis->tunerComp;
            }
        }

        // Stage 2 (+300ms): re-fire UIA focus so NVDA actually speaks the focused control.
        if (focused != nullptr)
        {
            juce::Timer::callAfterDelay(300, [safeComp = juce::Component::SafePointer<juce::Component>(focused)]()
            {
                if (safeComp == nullptr) return;
                if (safeComp->hasKeyboardFocus(false))
                    if (auto* handler = safeComp->getAccessibilityHandler())
                        handler->grabFocus();
            });
        }
    });
}

void TunerAccessAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(20.0f));
    g.drawFittedText("TunerAccess", getLocalBounds().removeFromTop(40), juce::Justification::centred, 1);

    g.setFont(juce::FontOptions(14.0f));
    g.drawFittedText("Tab: move between Tuner and Input  -  H: help  -  F10: audio settings",
                     getLocalBounds().removeFromBottom(30),
                     juce::Justification::centred, 1);
}

void TunerAccessAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20, 50);
    int h = area.getHeight();
    tunerComp.setBounds(area.removeFromTop(h / 2 - 4));
    area.removeFromTop(8);
    inputComp.setBounds(area);
}
