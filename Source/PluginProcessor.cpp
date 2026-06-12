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

    // Mix the in-tune lock tone on top of the monitored signal.
    renderLockTone(buffer);
}

void TunerAccessAudioProcessor::renderLockTone(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;

    // ----- Decide whether the tone should sound (per block) -----
    bool gateOn = false;

    if (tunerEngine.active.load(std::memory_order_relaxed)
        && lockToneEnabled.load(std::memory_order_relaxed))
    {
        float freq = tunerEngine.detectedFrequency.load(std::memory_order_relaxed);

        if (freq > 0.0f)
        {
            // Cents to target: explicit note in guided mode, nearest note in chromatic.
            float cents = 1000.0f;
            if (lockGuided.load(std::memory_order_relaxed))
            {
                int target = lockTargetMidi.load(std::memory_order_relaxed);
                if (target >= 0)
                    cents = centsFromTarget(freq, target);
            }
            else
            {
                auto note = frequencyToNote(freq);
                if (note.valid)
                    cents = note.centsDeviation;
            }

            // Prime on the first valid frame of a note so the tone can latch
            // immediately on an in-tune note instead of crawling down from 1000
            // over ~2 s. Then smooth (tau ~0.25 s) to survive ~1 cent jitter.
            if (!lockPrimed)
            {
                lockSmoothedCents = std::abs(cents);
                lockPrimed = true;
            }
            else
            {
                float coeff = 1.0f - std::exp(-static_cast<float>(numSamples) / (static_cast<float>(sr) * 0.25f));
                lockSmoothedCents += (std::abs(cents) - lockSmoothedCents) * coeff;
            }

            // Hysteresis: latch ON within ±2.5 cents (the same window NVDA calls
            // "tuned"), release only at >=3.0 cents so it doesn't flicker at the edge.
            if (lockLatched)
                gateOn = (lockSmoothedCents <= 3.0f);
            else
                gateOn = (lockSmoothedCents <= 2.5f);
            lockLatched = gateOn;
        }
        else
        {
            // Silence — re-prime on the next note; don't hold a stale value.
            lockLatched = false;
            lockPrimed  = false;
        }
    }
    else
    {
        lockLatched = false;
    }

    // ----- Synthesize: persistent-phase 880 Hz sine with a click-free envelope -----
    // Skip entirely when off and already silent (no CPU, no clicks).
    if (!gateOn && lockEnv < 1.0e-4f)
    {
        lockEnv = 0.0f;
        return;
    }

    const float  targetEnv = gateOn ? 1.0f : 0.0f;
    const float  envCoeff  = 1.0f - std::exp(-1.0f / (0.008f * static_cast<float>(sr))); // ~8 ms ramp
    const float  amp       = juce::Decibels::decibelsToGain(-15.0f);
    const double phaseInc  = 2.0 * juce::MathConstants<double>::pi * 880.0 / sr;

    for (int i = 0; i < numSamples; ++i)
    {
        lockEnv += (targetEnv - lockEnv) * envCoeff;
        float s = static_cast<float>(std::sin(lockPhase)) * amp * lockEnv;
        lockPhase += phaseInc;
        if (lockPhase >= 2.0 * juce::MathConstants<double>::pi)
            lockPhase -= 2.0 * juce::MathConstants<double>::pi;

        for (int c = 0; c < numChannels; ++c)
            buffer.getWritePointer(c)[i] += s;
    }
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
    state.setProperty("activeInput",       activeInputIndex.load(std::memory_order_relaxed), nullptr);
    state.setProperty("instrumentIndex",   savedInstrumentIndex,   nullptr);
    state.setProperty("tuningPresetIndex", savedTuningPresetIndex, nullptr);
    state.setProperty("currentString",     savedCurrentString,     nullptr);
    state.setProperty("lockToneEnabled",   lockToneEnabled.load(std::memory_order_relaxed), nullptr);

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

    // Tuner UI state — default 0 if absent (old save files stay valid).
    savedInstrumentIndex   = (int) state.getProperty("instrumentIndex",   0);
    savedTuningPresetIndex = (int) state.getProperty("tuningPresetIndex", 0);
    savedCurrentString     = (int) state.getProperty("currentString",     0);
    lockToneEnabled.store((bool) state.getProperty("lockToneEnabled", false), std::memory_order_relaxed);

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
