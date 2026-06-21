#pragma once
#include <JuceHeader.h>
#include "AccessibleComboDropdown.h"

class TunerAccessAudioProcessor;

// Notify NVDA of toggle/check state change via UIA TogglePattern.
void notifyToggleStateChanged (juce::Component& toggle, bool isOn);

//==============================================================================
// Accessible Audio settings panel for TunerAccess standalone mode.
// Lets the user pick: audio device type, device, input channel, output pair,
// sample rate and buffer size — all via the AccessibleComboDropdown.
//==============================================================================
class AccessibleSettingsPanel : public juce::Component,
                                 private juce::ChangeListener
{
public:
    AccessibleSettingsPanel (juce::AudioDeviceManager& deviceManager,
                             TunerAccessAudioProcessor& processor);
    ~AccessibleSettingsPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    juce::AudioDeviceManager& dm;
    TunerAccessAudioProcessor& proc;

    juce::Label deviceTypeLabel;
    AccessibleComboDropdown deviceTypeCombo;

    juce::Label audioDeviceLabel;
    AccessibleComboDropdown audioDeviceCombo;

   #if ! JUCE_WINDOWS
    // macOS / CoreAudio: input and output are SEPARATE devices, so the standalone
    // tuner needs an explicit output-device chooser (to hear yourself + lock tone).
    juce::Label outputDeviceLabel;
    AccessibleComboDropdown outputDeviceCombo;
   #endif

    juce::Label outputPairLabel;
    AccessibleComboDropdown outputPairCombo;

    juce::Label sampleRateLabel;
    AccessibleComboDropdown sampleRateCombo;

    juce::Label bufferSizeLabel;
    AccessibleComboDropdown bufferSizeCombo;

    juce::TextButton closeButton { "Close" };

    void makeAccessible (juce::Component& comp, const juce::String& name,
                         const juce::String& description = {},
                         bool wantsKeyboardFocus = true);

    void populateDeviceTypes();
    void populateAudioDevices();
    void populateOutputPairs();
    void populateSampleRates();
    void populateBufferSizes();

    void applyDeviceType();
    void applyAudioDevice();
    void applyOutputPair();
    void applySampleRate();
    void applyBufferSize();

   #if ! JUCE_WINDOWS
    void populateOutputDevices();
    void applyOutputDevice();
   #endif

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    int nextFocusOrder = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AccessibleSettingsPanel)
};
