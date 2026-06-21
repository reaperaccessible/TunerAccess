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

    // Opens the HTML user manual in the default browser (F1). Works in both
    // standalone and plugin — searches several known locations for the file.
    void openManual();

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
        TunerComponent(TunerAccessAudioProcessorEditor& e) : editor(e)
        {
            // Pull persisted tuner UI state from the processor.
            auto& proc = e.processorRef;
            auto& instruments = getInstruments();
            instrumentIndex = juce::jlimit(0, static_cast<int>(instruments.size()) - 1,
                                            proc.savedInstrumentIndex);
            int numTunings = static_cast<int>(instruments[static_cast<size_t>(instrumentIndex)].tunings.size());
            tuningPresetIndex = (proc.savedTuningPresetIndex == kFreeChromatic)
                ? kFreeChromatic
                : juce::jlimit(0, numTunings - 1, proc.savedTuningPresetIndex);
            int n = (tuningPresetIndex == kFreeChromatic) ? 1
                  : static_cast<int>(instruments[static_cast<size_t>(instrumentIndex)]
                                     .tunings[static_cast<size_t>(tuningPresetIndex)].midiNotes.size());
            currentString = juce::jlimit(0, juce::jmax(0, n - 1), proc.savedCurrentString);
        }
        ~TunerComponent() override { stopTimer(); }
        bool keyPressed(const juce::KeyPress& key) override;
        void focusGained(FocusChangeType) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        int instrumentIndex = 0;     // 0 = 6-string, 1 = 7-string, ...
        int tuningPresetIndex = 0;   // index in the active instrument's tunings list, or -1 for Free Chromatic
        int currentString = 0;       // 0 = lowest (string N), N-1 = highest (string 1)
        bool tunerActive = false;

        juce::String getCurrentText() const;
        juce::String getStringText() const;
        void deactivate() { if (tunerActive) { tunerActive = false; stopTimer(); } }
        void pushStateToProcessor()
        {
            auto& p = editor.processorRef;
            p.savedInstrumentIndex   = instrumentIndex;
            p.savedTuningPresetIndex = tuningPresetIndex;
            p.savedCurrentString     = currentString;

            // Publish the lock-tone target for the audio thread.
            if (tuningPresetIndex == kFreeChromatic)
            {
                p.setLockTarget(-1, false);   // chromatic: audio thread uses nearest note
            }
            else
            {
                auto& instruments = getInstruments();
                int ii = juce::jlimit(0, static_cast<int>(instruments.size()) - 1, instrumentIndex);
                auto& tunings = instruments[static_cast<size_t>(ii)].tunings;
                int ti = juce::jlimit(0, static_cast<int>(tunings.size()) - 1, tuningPresetIndex);
                auto& preset = tunings[static_cast<size_t>(ti)];
                int numStrings = static_cast<int>(preset.midiNotes.size());
                int cs = juce::jlimit(0, numStrings - 1, currentString);
                p.setLockTarget(preset.midiNotes[static_cast<size_t>(cs)], true);
            }
        }

    private:
        void timerCallback() override;
        void buildAnnouncement(float freq, bool doSpeak);
        void announceHelp();

        // Pluck-to-hear model: stay silent while a note rings. After >=50 ms of
        // silence the engine reports -1 and we re-arm (noteArmed = true). On the
        // next pluck we wait ~120 ms for the attack transient to settle, then
        // announce the pitch exactly once and go silent until the next silence.
        void resetForNextNote()
        {
            smoothedCents = 0.0f;
            inAttack      = false;
            attackPrimed  = false;
            noteArmed     = true;
        }

        TunerAccessAudioProcessorEditor& editor;
        float  smoothedCents = 0.0f;
        bool   noteArmed    = true;     // ready to announce on the next pluck
        bool   inAttack     = false;    // inside the post-attack stabilization window
        bool   attackPrimed = false;    // first valid frame of this attack seen
        double attackStartMs = 0.0;     // timestamp (ms) when the current attack began

        // Navigation state: 0 = instrument, 1 = tuning preset, 2 = string selector
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

    // Tab-reachable button to open the audio settings (standalone only). The
    // JUCE title-bar "Audio Settings" button is window chrome and not part of
    // the editor's focus container, so a real in-editor button is needed for
    // keyboard / screen-reader users.
    juce::TextButton audioSettingsButton { "Audio Settings" };

private:
    void componentBroughtToFront(juce::Component&) override;

    juce::Component::SafePointer<juce::Component> savedFocusComponent;
    juce::Component* trackedTopLevel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerAccessAudioProcessorEditor)
};
