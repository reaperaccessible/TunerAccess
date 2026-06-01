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

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerAccessAudioProcessor)
};
