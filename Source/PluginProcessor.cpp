#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

TunerAccessAudioProcessor::TunerAccessAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input",   juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    inputPresets[0].name = "Input 1";
    inputPresets[0].deviceChannel = 0;
    inputPresets[1].name = "Input 2";
    inputPresets[1].deviceChannel = 1;
    applyActiveInput(nullptr); // initialise atomics
}

void TunerAccessAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    tunerEngine.prepare(sampleRate);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TunerAccessAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}
#endif

void TunerAccessAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0)
        return;

    const int ch    = juce::jlimit(0, numChannels - 1,
                                   activeChannelInBus.load(std::memory_order_relaxed));
    const float gain = activeGainLinear.load(std::memory_order_relaxed);

    // Build the mono-with-gain signal once, then:
    //  - send it to the tuner (if active) for analysis
    //  - copy it to ALL output channels (mono -> stereo passthrough)
    juce::HeapBlock<float> mono(static_cast<size_t>(numSamples));
    {
        const float* src = buffer.getReadPointer(ch);
        if (gain == 1.0f)
            std::memcpy(mono.getData(), src, sizeof(float) * static_cast<size_t>(numSamples));
        else
            for (int i = 0; i < numSamples; ++i)
                mono[i] = src[i] * gain;
    }

    // Analyse.
    if (tunerEngine.active.load(std::memory_order_relaxed))
        tunerEngine.process(mono.getData(), numSamples);

    // Passthrough to every output channel (mono -> L + R + ...).
    for (int c = 0; c < numChannels; ++c)
        std::memcpy(buffer.getWritePointer(c), mono.getData(),
                    sizeof(float) * static_cast<size_t>(numSamples));
}

juce::AudioProcessorEditor* TunerAccessAudioProcessor::createEditor()
{
    return new TunerAccessAudioProcessorEditor(*this);
}

//==============================================================================
// Input Preset helpers
//==============================================================================
void TunerAccessAudioProcessor::applyActiveInput(juce::AudioDeviceManager* dm)
{
    const int idx = juce::jlimit(0, 1, activeInputIndex.load(std::memory_order_relaxed));
    const auto& preset = inputPresets[static_cast<size_t>(idx)];

    // Push to audio thread.
    activeChannelInBus.store(preset.deviceChannel & 1, std::memory_order_relaxed);
    activeGainLinear.store(std::pow(10.0f, preset.gainDb / 20.0f), std::memory_order_relaxed);

    // Reconfigure device input pair so the desired device channel reaches the bus.
    if (dm == nullptr)
        return;

    auto* device = dm->getCurrentAudioDevice();
    if (device == nullptr)
        return;

    const int numChannels = device->getInputChannelNames().size();
    if (numChannels <= 0)
        return;

    const int wanted = juce::jlimit(0, numChannels - 1, preset.deviceChannel);
    const int pairStart = (wanted / 2) * 2;

    juce::BigInteger active;
    active.setBit(pairStart, true);
    if (pairStart + 1 < numChannels)
        active.setBit(pairStart + 1, true);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    dm->getAudioDeviceSetup(setup);
    if (setup.inputChannels != active || setup.useDefaultInputChannels)
    {
        setup.inputChannels = active;
        setup.useDefaultInputChannels = false;
        dm->setAudioDeviceSetup(setup, true);
    }
}

void TunerAccessAudioProcessor::setPresetGainDb(int presetIdx, float gainDb)
{
    if (presetIdx < 0 || presetIdx > 1) return;
    inputPresets[static_cast<size_t>(presetIdx)].gainDb =
        juce::jlimit(kMinGainDb, kMaxGainDb, gainDb);

    if (presetIdx == activeInputIndex.load(std::memory_order_relaxed))
        applyActiveInput(nullptr);
}

void TunerAccessAudioProcessor::setPresetName(int presetIdx, const juce::String& name)
{
    if (presetIdx < 0 || presetIdx > 1) return;
    inputPresets[static_cast<size_t>(presetIdx)].name = name.isEmpty()
        ? juce::String("Input ") + juce::String(presetIdx + 1)
        : name;
}

void TunerAccessAudioProcessor::setPresetDeviceChannel(int presetIdx, int channel,
                                                        juce::AudioDeviceManager* dm)
{
    if (presetIdx < 0 || presetIdx > 1) return;
    inputPresets[static_cast<size_t>(presetIdx)].deviceChannel = juce::jmax(0, channel);

    if (presetIdx == activeInputIndex.load(std::memory_order_relaxed))
        applyActiveInput(dm);
}

//==============================================================================
// State save/restore — preset names, channels, gains, active index.
//==============================================================================
void TunerAccessAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree state("TunerAccessState");
    state.setProperty("activeInput", activeInputIndex.load(std::memory_order_relaxed), nullptr);

    for (int i = 0; i < 2; ++i)
    {
        juce::ValueTree p("InputPreset");
        p.setProperty("idx",  i, nullptr);
        p.setProperty("name", inputPresets[static_cast<size_t>(i)].name, nullptr);
        p.setProperty("ch",   inputPresets[static_cast<size_t>(i)].deviceChannel, nullptr);
        p.setProperty("gain", inputPresets[static_cast<size_t>(i)].gainDb, nullptr);
        state.appendChild(p, nullptr);
    }

    auto xml = state.createXml();
    if (xml != nullptr)
        copyXmlToBinary(*xml, dest);
}

void TunerAccessAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml(*xml);
    if (! state.isValid() || state.getType().toString() != "TunerAccessState") return;

    activeInputIndex.store(juce::jlimit(0, 1, (int) state.getProperty("activeInput", 0)),
                           std::memory_order_relaxed);

    for (auto child : state)
    {
        if (child.getType().toString() != "InputPreset") continue;
        int i = juce::jlimit(0, 1, (int) child.getProperty("idx", 0));
        auto& p = inputPresets[static_cast<size_t>(i)];
        p.name          = child.getProperty("name", juce::String("Input ") + juce::String(i + 1));
        p.deviceChannel = juce::jmax(0, (int) child.getProperty("ch",  i));
        p.gainDb        = juce::jlimit(kMinGainDb, kMaxGainDb, (float) child.getProperty("gain", 0.0));
    }

    applyActiveInput(nullptr);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TunerAccessAudioProcessor();
}
