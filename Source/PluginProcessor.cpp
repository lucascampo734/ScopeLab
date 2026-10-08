#include "PluginProcessor.h"
#include "PluginEditor.h"

ScopeLabAudioProcessor::ScopeLabAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ScopeLabState", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout ScopeLabAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    NormalisableRange<float> timeRange (1.0f, 1000.0f, 0.01f);
    timeRange.setSkewForCentre (40.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "time", 1 }, "Ventana", timeRange, 20.0f,
        AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return v < 100.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gain", 1 }, "Zoom vertical", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "sync", 1 }, String::fromUTF8 ("Sincronía"), StringArray { "Libre", "Trigger", "Tempo" }, 1));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "beats", 1 }, "Tiempos", StringArray { "1/4 tiempo", "1/2 tiempo", "1 tiempo", "2 tiempos", String::fromUTF8 ("1 compás") }, 2));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "split", 1 }, "L/R separados", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Congelar", false));

    return layout;
}

void ScopeLabAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
}

bool ScopeLabAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void ScopeLabAudioProcessor::publishTransport (double bpm, double ppq, int64_t sample, bool playing) noexcept
{
    transportSeq.fetch_add (1, std::memory_order_acq_rel);   // impar = escribiendo
    tBpm.store (bpm, std::memory_order_relaxed);
    tPpq.store (ppq, std::memory_order_relaxed);
    tSample.store (sample, std::memory_order_relaxed);
    tPlaying.store (playing, std::memory_order_relaxed);
    transportSeq.fetch_add (1, std::memory_order_acq_rel);   // par = listo
}

ScopeLabAudioProcessor::Transport ScopeLabAudioProcessor::getTransport() const noexcept
{
    Transport t;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const auto s1 = transportSeq.load (std::memory_order_acquire);
        if (s1 & 1u) continue;
        t.bpm     = tBpm.load (std::memory_order_relaxed);
        t.ppq     = tPpq.load (std::memory_order_relaxed);
        t.sample  = tSample.load (std::memory_order_relaxed);
        t.playing = tPlaying.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (transportSeq.load (std::memory_order_relaxed) == s1)
            break;
    }
    return t;
}

void ScopeLabAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    if (numIn == 0 || n == 0)
        return;

    // El audio pasa sin modificar: solo lo copiamos para dibujarlo.
    const auto blockStart = scope.getWritePosition();

    bool gotTransport = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto ppq = pos->getPpqPosition())
            {
                publishTransport (pos->getBpm().orFallback (0.0), *ppq, blockStart, pos->getIsPlaying());
                gotTransport = true;
            }
        }
    }
    if (! gotTransport)
        publishTransport (0.0, 0.0, blockStart, false);

    const float* l = buffer.getReadPointer (0);
    const float* r = numIn > 1 ? buffer.getReadPointer (1) : l;
    scope.push (l, r, n);

    const float pl = buffer.getMagnitude (0, 0, n);
    const float pr = numIn > 1 ? buffer.getMagnitude (1, 0, n) : pl;
    if (pl > peakLeft.load (std::memory_order_relaxed))  peakLeft.store (pl, std::memory_order_relaxed);
    if (pr > peakRight.load (std::memory_order_relaxed)) peakRight.store (pr, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* ScopeLabAudioProcessor::createEditor()
{
    return new ScopeLabAudioProcessorEditor (*this);
}

void ScopeLabAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ScopeLabAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ScopeLabAudioProcessor();
}
