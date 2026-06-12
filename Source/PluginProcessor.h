#pragma once

#include <JuceHeader.h>
#include <array>
#include "GuitarTuner.h"

class TunerAccessAudioProcessor : public juce::AudioProcessor
{
public:
    TunerAccessAudioProcessor();
    ~TunerAccessAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
   #endif

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    TunerEngine tunerEngine;

    //==========================================================================
    // Input Presets — 2 independent named inputs with device channel + gain.
    //==========================================================================
    struct InputPreset
    {
        juce::String name = "Input 1";
        int deviceChannel = 0;     // 0-indexed device channel (any number)
        float gainDb = 0.0f;       // -12 .. +24 dB
    };

    static constexpr float kMinGainDb = -12.0f;
    static constexpr float kMaxGainDb = +24.0f;

    std::array<InputPreset, 2> inputPresets;

    //==========================================================================
    // Persisted UI state for the Tuner widget. UI thread only — the editor reads
    // these on construction and writes them on every change, so they survive
    // session restart via getStateInformation / setStateInformation.
    //==========================================================================
    int savedInstrumentIndex   = 0;
    int savedTuningPresetIndex = 0;
    int savedCurrentString     = 0;

    // Active input index (0 or 1). Audio thread reads activeChannelInBus and
    // activeGainLinear, which the UI updates atomically via applyActiveInput().
    std::atomic<int>   activeInputIndex   { 0 };
    std::atomic<int>   activeChannelInBus { 0 };   // 0 or 1, derived from preset's deviceChannel parity
    std::atomic<float> activeGainLinear   { 1.0f };

    // Push the current preset to the audio thread (computes channelInBus + gain).
    // Optionally reconfigures the AudioDeviceManager so the device pair containing
    // deviceChannel is active (called when user switches preset or changes channel).
    void applyActiveInput(juce::AudioDeviceManager* dm = nullptr);

    // Convenience for UI: change a preset's gain (clamped) and refresh audio state
    // if it's the active one.
    void setPresetGainDb(int presetIdx, float gainDb);
    void setPresetName(int presetIdx, const juce::String& name);
    void setPresetDeviceChannel(int presetIdx, int channel, juce::AudioDeviceManager* dm = nullptr);

    //==========================================================================
    // In-tune lock tone — a continuous 880 Hz sine that sounds while the detected
    // pitch is within ~0.5 cent of the target (audio equivalent of a strobe lock).
    // The UI publishes the target; the audio thread computes the gate + tone.
    //==========================================================================
    std::atomic<bool> lockToneEnabled { false };  // master toggle (T key)
    std::atomic<bool> lockGuided      { false };  // true = use lockTargetMidi; false = nearest note
    std::atomic<int>  lockTargetMidi  { -1 };     // target MIDI note in guided mode

    void setLockTarget(int midiNote, bool guided)
    {
        lockTargetMidi.store(midiNote, std::memory_order_relaxed);
        lockGuided.store(guided, std::memory_order_relaxed);
    }

private:
    // Lock-tone synthesis state — audio thread only (no atomics needed).
    void renderLockTone(juce::AudioBuffer<float>& buffer);
    double lockPhase        = 0.0;
    float  lockEnv          = 0.0f;       // 0..1 click-free envelope
    float  lockSmoothedCents = 1000.0f;   // smoothed |cents| to target (large = out)
    bool   lockLatched      = false;      // hysteresis state
    bool   lockPrimed       = false;      // first valid frame of the current note seen

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerAccessAudioProcessor)
};
