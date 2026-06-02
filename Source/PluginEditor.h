#pragma once

// _WIN32 is defined by the compiler before any JUCE header, so we can safely
// gate windows.h on it. JUCE_WINDOWS (from juce_core) is only safe AFTER
// including JuceHeader.h.
#ifdef _WIN32
 #include <windows.h>
#endif
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Screen reader integration.
//
// On Windows: NVDA Controller Client DLL provides direct speech + braille.
// On macOS:   TODO — wire up NSAccessibility / VoiceOver (see README-MAC.md).
//==============================================================================
#ifdef _WIN32
struct NvdaApi
{
    using SpeakFunc     = long(__stdcall*)(const wchar_t*);
    using TestFunc      = long(__stdcall*)();
    using CancelFunc    = long(__stdcall*)();
    using SpeakSsmlFunc = long(__stdcall*)(const wchar_t*, int, int, unsigned char);

    SpeakFunc     speakText      = nullptr;
    TestFunc      testIfRunning  = nullptr;
    CancelFunc    cancelSpeech   = nullptr;
    SpeakSsmlFunc speakSsml      = nullptr;
    HMODULE       dll            = nullptr;

    bool isNvdaRunning() const
    {
        return testIfRunning && testIfRunning() == 0;
    }
};

// Speech priority constants (match NVDA's SPEECH_PRIORITY enum)
static constexpr int kSpeechNormal    = 0;
static constexpr int kSpeechNext      = 1;
static constexpr int kSpeechNow       = 2;
static constexpr int kSymbolUnchanged = -1;

const NvdaApi& getNvda();
#endif // _WIN32

void screenReaderAnnounce(const juce::String& message);          // NORMAL priority
void screenReaderAnnounceNow(const juce::String& message);       // NOW priority
void screenReaderAnnounceNext(const juce::String& message);      // NEXT priority
void screenReaderAnnounceInterrupt(const juce::String& message); // speakSsml(NOW) — no manual cancel
void announceValue();  // fire valueChanged on focused component (speech + braille)

//==============================================================================
class TunerAccessAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::ComponentListener
{
public:
    TunerAccessAudioProcessorEditor(TunerAccessAudioProcessor&);
    ~TunerAccessAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Opens the accessible Audio Settings panel (Ctrl+, in the Tuner).
    // Has no effect outside of Standalone mode.
    void openAudioSettings();

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void focusGained(FocusChangeType) override;

    TunerAccessAudioProcessor& processorRef;

    bool isStandalone() const
    {
        return processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    }

    //==========================================================================
    // Guitar Tuner
    //==========================================================================
    class TunerComponent : public juce::Component, private juce::Timer
    {
    public:
        TunerComponent(TunerAccessAudioProcessorEditor& e) : editor(e) {}
        ~TunerComponent() override { stopTimer(); }
        bool keyPressed(const juce::KeyPress& key) override;
        void focusGained(FocusChangeType) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        int tuningPresetIndex = 0;   // 0-7 = presets, -1 = Free Chromatic
        int currentString = 0;       // 0-5 (string 6 to string 1)
        bool tunerActive = false;

        juce::String getCurrentText() const;
        juce::String getStringText() const;
        void deactivate() { if (tunerActive) { tunerActive = false; stopTimer(); } }

    private:
        void timerCallback() override;
        void buildAnnouncement(float freq);
        void announceHelp();
        TunerAccessAudioProcessorEditor& editor;
        float lastAnnouncedFreq = -1.0f;
        float smoothedCents = 0.0f;
        int announceThrottle = 0;

        // Navigation state: 0 = tuning preset combo, 1 = string selector
        int navField = 0;
    };

    TunerComponent tunerComp{*this};

    //==========================================================================
    // Input Component — selects active input slot (1/2), edits gain + device channel.
    //==========================================================================
    class InputComponent : public juce::Component
    {
    public:
        InputComponent(TunerAccessAudioProcessorEditor& e) : editor(e) {}
        ~InputComponent() override = default;

        bool keyPressed(const juce::KeyPress& key) override;
        void focusGained(FocusChangeType) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        juce::String getCurrentText() const;

    private:
        void startRename();
        void commitRename();
        void cancelRename();
        void announceCurrentSubParam();
        void refreshAccessibility();

        TunerAccessAudioProcessorEditor& editor;

        // 0 = Gain (default), 1 = Device channel
        int subParam = 0;

        std::unique_ptr<juce::TextEditor> renameEditor;
    };

    InputComponent inputComp{*this};

private:
    void componentBroughtToFront(juce::Component&) override;

    juce::Component::SafePointer<juce::Component> savedFocusComponent;
    juce::Component* trackedTopLevel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerAccessAudioProcessorEditor)
};
