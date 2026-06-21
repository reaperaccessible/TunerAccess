#include "AccessibleSettingsPanel.h"

#ifdef _WIN32
 #include <Windows.h>
 #include <objbase.h>
 #include <UIAutomation.h>
#endif

#include "PluginProcessor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

//==============================================================================
void notifyToggleStateChanged (juce::Component& toggle, bool isOn)
{
#ifdef _WIN32
    auto* handler = toggle.getAccessibilityHandler();
    if (handler == nullptr) return;
    auto* native = handler->getNativeImplementation();
    if (native == nullptr) return;

    using RaisePropFunc = HRESULT (WINAPI*) (IRawElementProviderSimple*, PROPERTYID, VARIANT, VARIANT);
    static auto* uiaModule = LoadLibraryW (L"UIAutomationCore.dll");
    static auto raiseProp = uiaModule
        ? reinterpret_cast<RaisePropFunc> (GetProcAddress (uiaModule, "UiaRaiseAutomationPropertyChangedEvent"))
        : nullptr;

    if (raiseProp == nullptr) return;

    VARIANT oldVal, newVal;
    VariantInit (&oldVal);
    VariantInit (&newVal);
    oldVal.vt = VT_I4;
    oldVal.lVal = isOn ? ToggleState_Off : ToggleState_On;
    newVal.vt = VT_I4;
    newVal.lVal = isOn ? ToggleState_On : ToggleState_Off;

    raiseProp (reinterpret_cast<IRawElementProviderSimple*> (native),
               UIA_ToggleToggleStatePropertyId, oldVal, newVal);
#else
    juce::ignoreUnused (toggle, isOn);
#endif
}

//==============================================================================
AccessibleSettingsPanel::AccessibleSettingsPanel (juce::AudioDeviceManager& deviceManager,
                                                   TunerAccessAudioProcessor& processor)
    : dm (deviceManager), proc (processor)
{
    setName ("Audio Settings Panel");
    // Single space, not a real title: prevents NVDA prefixing
    // "Audio Settings Panel" on every Tab navigation inside this dialog.
    // See feedback_juce_setname_vs_settitle_uia_trap.md.
    setTitle (" ");
    setAccessible (true);
    setWantsKeyboardFocus (true);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);

    nextFocusOrder = 1;

    auto addLabelled = [this] (juce::Label& lbl, AccessibleComboDropdown& combo,
                               const juce::String& labelText,
                               const juce::String& comboName,
                               const juce::String& comboDesc,
                               std::function<void()> onApply)
    {
        lbl.setText (labelText, juce::dontSendNotification);
        lbl.setColour (juce::Label::textColourId, juce::Colours::white);
        makeAccessible (lbl, labelText + " Label", {}, false);
        addAndMakeVisible (lbl);

        combo.setName (comboName);
        combo.setTitle (comboName); // UIA Name path — NVDA reads componentTitle, not componentName.
        combo.setDescription (comboDesc);
        combo.setExplicitFocusOrder (nextFocusOrder++);
        combo.onConfirm = std::move (onApply);
        addAndMakeVisible (combo);
    };

    addLabelled (deviceTypeLabel,    deviceTypeCombo,
                 "Audio Driver:",    "Audio Driver",
                 "Select the audio backend (ASIO, DirectSound, WASAPI...). Press Alt+Down to open list.",
                 [this] { applyDeviceType(); });

   #if JUCE_WINDOWS
    addLabelled (audioDeviceLabel,   audioDeviceCombo,
                 "Audio Device:",    "Audio Device",
                 "Select the audio device. Press Alt+Down to open list.",
                 [this] { applyAudioDevice(); });
   #else
    // macOS: input and output are separate CoreAudio devices.
    addLabelled (audioDeviceLabel,   audioDeviceCombo,
                 "Input Device:",    "Input Device",
                 "Select the input device — your instrument or microphone.",
                 [this] { applyAudioDevice(); });

    addLabelled (outputDeviceLabel,  outputDeviceCombo,
                 "Output Device:",   "Output Device",
                 "Select the output device — where you hear yourself and the in-tune tone.",
                 [this] { applyOutputDevice(); });
   #endif

    addLabelled (outputPairLabel,    outputPairCombo,
                 "Output Pair:",     "Output Pair",
                 "Select which stereo output pair to use. Press Alt+Down to open list.",
                 [this] { applyOutputPair(); });

    addLabelled (sampleRateLabel,    sampleRateCombo,
                 "Sample Rate:",     "Sample Rate",
                 "Select the audio sample rate. Press Alt+Down to open list.",
                 [this] { applySampleRate(); });

    addLabelled (bufferSizeLabel,    bufferSizeCombo,
                 "Buffer Size:",     "Buffer Size",
                 "Select the audio buffer size in samples. Press Alt+Down to open list.",
                 [this] { applyBufferSize(); });

    populateDeviceTypes();
    populateAudioDevices();
   #if ! JUCE_WINDOWS
    populateOutputDevices();
   #endif
    populateOutputPairs();
    populateSampleRates();
    populateBufferSizes();

    makeAccessible (closeButton, "Close Settings", "Close this settings panel.");
    closeButton.setExplicitFocusOrder (nextFocusOrder++);
    closeButton.onClick = [this]
    {
        // Dialog is non-modal (launchAsync) — exitModalState() is a no-op there.
        // closeButtonPressed() is the canonical close path: same as the native X.
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->closeButtonPressed();
    };
    addAndMakeVisible (closeButton);

   #if JUCE_WINDOWS
    int rows = 5; // device type + audio device + output pair + SR + buffer
   #else
    int rows = 6; // device type + input device + output device + output pair + SR + buffer
   #endif
    int height = 12 + (30 + 6) * rows + 12 + 32 + 12;
    setSize (520, juce::jmax (height, 320));

    dm.addChangeListener (this);

    // Initial focus grab — explicit on the first combo, with a 200ms retry as a
    // belt-and-suspenders fallback. The KeyboardFocusTraverser fallback used to
    // silently fail when the dialog hadn't fully realised yet, leaving the user
    // unable to Tab/Shift+Tab at all. Always go through deviceTypeCombo (first
    // child in tab order) directly.
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<AccessibleSettingsPanel> (this)]
    {
        if (safeThis == nullptr) return;
        safeThis->deviceTypeCombo.grabKeyboardFocus();
    });

    juce::Timer::callAfterDelay (200, [safeThis = juce::Component::SafePointer<AccessibleSettingsPanel> (this)]
    {
        if (safeThis == nullptr) return;
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (focused == nullptr || ! safeThis->isParentOf (focused))
        {
            safeThis->deviceTypeCombo.grabKeyboardFocus();
            // Also re-fire UIA so NVDA picks up the focus event.
            if (auto* h = safeThis->deviceTypeCombo.getAccessibilityHandler())
                h->grabFocus();
        }
    });
}

AccessibleSettingsPanel::~AccessibleSettingsPanel()
{
    dm.removeChangeListener (this);
}

void AccessibleSettingsPanel::makeAccessible (juce::Component& comp,
                                               const juce::String& name,
                                               const juce::String& description,
                                               bool wantsKbFocus)
{
    comp.setName (name);
    comp.setAccessible (true);
    comp.setWantsKeyboardFocus (wantsKbFocus);
    if (description.isNotEmpty())
        comp.setDescription (description);
}

void AccessibleSettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF1A1A2E));
}

void AccessibleSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (12);
    int labelW = 130;
    int rowH = 30;
    int gap = 6;

    auto layoutRow = [&] (juce::Label& lbl, juce::Component& comp)
    {
        auto row = area.removeFromTop (rowH);
        lbl.setBounds (row.removeFromLeft (labelW));
        comp.setBounds (row);
        area.removeFromTop (gap);
    };

    layoutRow (deviceTypeLabel,    deviceTypeCombo);
    layoutRow (audioDeviceLabel,   audioDeviceCombo);
   #if ! JUCE_WINDOWS
    layoutRow (outputDeviceLabel,  outputDeviceCombo);
   #endif
    layoutRow (outputPairLabel,    outputPairCombo);
    layoutRow (sampleRateLabel,    sampleRateCombo);
    layoutRow (bufferSizeLabel,    bufferSizeCombo);

    area.removeFromTop (gap);
    closeButton.setBounds (area.removeFromTop (32).reduced (160, 0));
}

//==============================================================================
void AccessibleSettingsPanel::populateDeviceTypes()
{
    deviceTypeCombo.clear();

    auto& types = dm.getAvailableDeviceTypes();
    auto currentName = dm.getCurrentAudioDeviceType();

    int selectedId = 0;
    for (int i = 0; i < types.size(); ++i)
    {
        auto name = types[i]->getTypeName();
        deviceTypeCombo.addItem (name, i + 1);
        if (name == currentName)
            selectedId = i + 1;
    }

    if (selectedId > 0)
        deviceTypeCombo.setSelectedId (selectedId);
    else if (deviceTypeCombo.getNumItems() > 0)
        deviceTypeCombo.setSelectedId (1);
}

void AccessibleSettingsPanel::populateAudioDevices()
{
    audioDeviceCombo.clear();

    auto* currentType = dm.getCurrentDeviceTypeObject();
    if (currentType == nullptr)
        return;

   #if JUCE_WINDOWS
    // ASIO: a single device provides both input and output.
    auto devices = currentType->getDeviceNames (false);
    auto* currentDevice = dm.getCurrentAudioDevice();
    juce::String currentDeviceName = currentDevice ? currentDevice->getName() : "";
   #else
    // CoreAudio: input and output are SEPARATE devices. The tuner needs the input
    // (instrument/mic), so this combo selects the INPUT device.
    auto devices = currentType->getDeviceNames (true);
    juce::AudioDeviceManager::AudioDeviceSetup curSetup;
    dm.getAudioDeviceSetup (curSetup);
    juce::String currentDeviceName = curSetup.inputDeviceName;
   #endif

    int selectedId = 0;
    for (int i = 0; i < devices.size(); ++i)
    {
        audioDeviceCombo.addItem (devices[i], i + 1);
        if (devices[i] == currentDeviceName)
            selectedId = i + 1;
    }

    if (selectedId > 0)
        audioDeviceCombo.setSelectedId (selectedId);
    else if (audioDeviceCombo.getNumItems() > 0)
        audioDeviceCombo.setSelectedId (1);
}

void AccessibleSettingsPanel::populateOutputPairs()
{
    outputPairCombo.clear();

    auto* device = dm.getCurrentAudioDevice();
    if (device == nullptr) return;

    auto outputChannels = device->getOutputChannelNames();
    int numPairs = outputChannels.size() / 2;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    dm.getAudioDeviceSetup (setup);
    auto activeOutputs = setup.outputChannels;

    int selectedId = 0;
    for (int i = 0; i < numPairs; ++i)
    {
        int ch1 = i * 2;
        int ch2 = ch1 + 1;

        juce::String pairName = (ch2 < outputChannels.size())
            ? outputChannels[ch1] + " / " + outputChannels[ch2]
            : outputChannels[ch1] + " (mono)";

        pairName = juce::String (i + 1) + ": " + pairName;
        outputPairCombo.addItem (pairName, i + 1);

        if (activeOutputs[ch1])
            selectedId = i + 1;
    }

    if (outputChannels.size() % 2 == 1)
    {
        int lastCh = outputChannels.size() - 1;
        outputPairCombo.addItem (juce::String (numPairs + 1) + ": " + outputChannels[lastCh] + " (mono)",
                                 numPairs + 1);
    }

    if (selectedId > 0)
        outputPairCombo.setSelectedId (selectedId);
    else if (outputPairCombo.getNumItems() > 0)
        outputPairCombo.setSelectedId (1);
}

void AccessibleSettingsPanel::populateSampleRates()
{
    sampleRateCombo.clear();

    auto* device = dm.getCurrentAudioDevice();
    if (device == nullptr) return;

    auto rates = device->getAvailableSampleRates();
    double currentRate = device->getCurrentSampleRate();
    int selectedId = 0;

    for (int i = 0; i < rates.size(); ++i)
    {
        auto rateStr = juce::String (rates[i], 0) + " Hz";
        sampleRateCombo.addItem (rateStr, i + 1);
        if (std::abs (rates[i] - currentRate) < 1.0)
            selectedId = i + 1;
    }

    if (selectedId > 0)
        sampleRateCombo.setSelectedId (selectedId);
}

void AccessibleSettingsPanel::populateBufferSizes()
{
    bufferSizeCombo.clear();

    auto* device = dm.getCurrentAudioDevice();
    if (device == nullptr) return;

    auto sizes = device->getAvailableBufferSizes();
    int currentSize = device->getCurrentBufferSizeSamples();
    int selectedId = 0;

    for (int i = 0; i < sizes.size(); ++i)
    {
        auto sizeStr = juce::String (sizes[i]) + " samples";
        bufferSizeCombo.addItem (sizeStr, i + 1);
        if (sizes[i] == currentSize)
            selectedId = i + 1;
    }

    if (selectedId > 0)
        bufferSizeCombo.setSelectedId (selectedId);
}

//==============================================================================
void AccessibleSettingsPanel::applyDeviceType()
{
    auto& types = dm.getAvailableDeviceTypes();
    auto idx = deviceTypeCombo.getSelectedItemIndex();
    if (idx < 0 || idx >= types.size())
        return;

    auto name = types[idx]->getTypeName();
    if (dm.getCurrentAudioDeviceType() != name)
        dm.setCurrentAudioDeviceType (name, true);

    populateAudioDevices();
   #if ! JUCE_WINDOWS
    populateOutputDevices();
   #endif
    populateOutputPairs();
    populateSampleRates();
    populateBufferSizes();
}

void AccessibleSettingsPanel::applyAudioDevice()
{
    auto* currentType = dm.getCurrentDeviceTypeObject();
    if (currentType == nullptr) return;

   #if JUCE_WINDOWS
    auto devices = currentType->getDeviceNames (false);
   #else
    auto devices = currentType->getDeviceNames (true);   // CoreAudio: input devices
   #endif
    auto idx = audioDeviceCombo.getSelectedItemIndex();

    if (idx >= 0 && idx < devices.size())
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm.getAudioDeviceSetup (setup);
       #if JUCE_WINDOWS
        // ASIO: one device name for both directions.
        setup.outputDeviceName = devices[idx];
        setup.inputDeviceName  = devices[idx];
       #else
        // CoreAudio: set ONLY the input device. Assigning an input-only name to the
        // output (or vice-versa) makes setAudioDeviceSetup fail with "No such device".
        setup.inputDeviceName  = devices[idx];
       #endif
        auto result = dm.setAudioDeviceSetup (setup, true);

        // Re-apply the active input preset (re-routes channels for new device).
        proc.applyActiveInput (&dm);

        populateOutputPairs();
        populateSampleRates();
        populateBufferSizes();

        if (result.isNotEmpty())
        {
            AccessibleComboDropdown::screenReaderAnnounceNow ("Error: " + result);
            populateAudioDevices();
        }
    }
}

#if ! JUCE_WINDOWS
void AccessibleSettingsPanel::populateOutputDevices()
{
    outputDeviceCombo.clear();

    auto* currentType = dm.getCurrentDeviceTypeObject();
    if (currentType == nullptr)
        return;

    auto devices = currentType->getDeviceNames (false);   // CoreAudio: output devices
    juce::AudioDeviceManager::AudioDeviceSetup curSetup;
    dm.getAudioDeviceSetup (curSetup);
    juce::String currentName = curSetup.outputDeviceName;

    int selectedId = 0;
    for (int i = 0; i < devices.size(); ++i)
    {
        outputDeviceCombo.addItem (devices[i], i + 1);
        if (devices[i] == currentName)
            selectedId = i + 1;
    }

    if (selectedId > 0)
        outputDeviceCombo.setSelectedId (selectedId);
    else if (outputDeviceCombo.getNumItems() > 0)
        outputDeviceCombo.setSelectedId (1);
}

void AccessibleSettingsPanel::applyOutputDevice()
{
    auto* currentType = dm.getCurrentDeviceTypeObject();
    if (currentType == nullptr) return;

    auto devices = currentType->getDeviceNames (false);   // output devices
    auto idx = outputDeviceCombo.getSelectedItemIndex();

    if (idx >= 0 && idx < devices.size())
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm.getAudioDeviceSetup (setup);
        setup.outputDeviceName = devices[idx];     // set ONLY the output; leave input
        setup.useDefaultOutputChannels = true;
        auto result = dm.setAudioDeviceSetup (setup, true);

        proc.applyActiveInput (&dm);
        populateOutputPairs();
        populateSampleRates();
        populateBufferSizes();

        if (result.isNotEmpty())
        {
            AccessibleComboDropdown::screenReaderAnnounceNow ("Error: " + result);
            populateOutputDevices();
        }
    }
}
#endif


void AccessibleSettingsPanel::applyOutputPair()
{
    auto* device = dm.getCurrentAudioDevice();
    if (device == nullptr) return;

    int pairIdx = outputPairCombo.getSelectedItemIndex();
    if (pairIdx < 0) return;

    auto outputChannels = device->getOutputChannelNames();
    int numChannels = outputChannels.size();

    juce::BigInteger channels;
    channels.setRange (0, numChannels, false);

    int ch1 = pairIdx * 2;
    int ch2 = ch1 + 1;
    if (ch1 < numChannels) channels.setBit (ch1, true);
    if (ch2 < numChannels) channels.setBit (ch2, true);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    dm.getAudioDeviceSetup (setup);
    setup.outputChannels = channels;
    setup.useDefaultOutputChannels = false;
    dm.setAudioDeviceSetup (setup, true);
}

void AccessibleSettingsPanel::applySampleRate()
{
    auto* device = dm.getCurrentAudioDevice();
    if (device == nullptr) return;

    auto rates = device->getAvailableSampleRates();
    auto idx = sampleRateCombo.getSelectedItemIndex();

    if (idx >= 0 && idx < rates.size())
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm.getAudioDeviceSetup (setup);
        setup.sampleRate = rates[idx];
        dm.setAudioDeviceSetup (setup, true);
    }
}

void AccessibleSettingsPanel::applyBufferSize()
{
    auto* device = dm.getCurrentAudioDevice();
    if (device == nullptr) return;

    auto sizes = device->getAvailableBufferSizes();
    auto idx = bufferSizeCombo.getSelectedItemIndex();

    if (idx >= 0 && idx < sizes.size())
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm.getAudioDeviceSetup (setup);
        setup.bufferSize = sizes[idx];
        dm.setAudioDeviceSetup (setup, true);
    }
}

void AccessibleSettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
   #if ! JUCE_WINDOWS
    populateOutputDevices();
   #endif
    populateOutputPairs();
    populateSampleRates();
    populateBufferSizes();
}

bool AccessibleSettingsPanel::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool alt   = mods.isAltDown();
    const bool ctrl  = mods.isCtrlDown();
    const bool shift = mods.isShiftDown();
    const auto kc    = key.getKeyCode();

    // Alt+F4 or Escape: close ONLY this dialog (never quit the host app).
    // closeButtonPressed() is the canonical close path for non-modal launchAsync
    // dialogs — it triggers the same destruction logic as the native title-bar X.
    if ((alt && !ctrl && !shift && kc == juce::KeyPress::F4Key)
        || (!alt && !ctrl && !shift && kc == juce::KeyPress::escapeKey))
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        {
            dw->closeButtonPressed();
            return true;
        }
    }
    return false;
}
