#include "PluginProcessor.h"
#include "PluginEditor.h"

ScopeLabAudioProcessor::ScopeLabAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ScopeLabState", createParameterLayout())
{
    slotIndex = hub->acquire();
    if (slotIndex < 0)
    {
        privateSlot = std::make_unique<ScopeSlot>();
        privateSlot->buffer = std::make_unique<ScopeRingBuffer>();
        privateSlot->setInfo ("Esta pista", ScopeHub::defaultColour (0));
        privateSlot->inUse.store (true);
    }
}

ScopeLabAudioProcessor::~ScopeLabAudioProcessor()
{
    hub->release (slotIndex);
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
        ParameterID { "sync", 1 }, String::fromUTF8 ("Sincronía"), StringArray { "Libre", "Trigger", "Tempo" }, 2));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "beats", 1 }, "Tiempos",
        StringArray { "1/4 tiempo", "1/2 tiempo", "1 tiempo", "2 tiempos", String::fromUTF8 ("1 compás"),
                      "2 compases", "4 compases" }, 2));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "view", 1 }, "Vista", StringArray { "Esta pista (L/R)", "Multipista" }, 1));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "split", 1 }, "Separados", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Congelar", false));

    return layout;
}

void ScopeLabAudioProcessor::prepareToPlay (double sampleRate, int)
{
    getSlot().sampleRate.store (sampleRate);
}

bool ScopeLabAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void ScopeLabAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    // Ableton a veces manda "ScopeLab/BASS": nos quedamos con el nombre de la pista
    auto name = properties.name;
    if (name.startsWithIgnoreCase (getName() + "/"))
        name = name.fromFirstOccurrenceOf ("/", false, false);
    getSlot().setHostInfo (name.trim(), properties.colour);
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
    auto& slot = getSlot();
    auto& ring = *slot.buffer;
    const auto blockStart = ring.getWritePosition();

    bool gotTransport = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto ppq = pos->getPpqPosition())
            {
                slot.publishTransport (pos->getBpm().orFallback (0.0), *ppq, blockStart, pos->getIsPlaying());
                gotTransport = true;
            }
        }
    }
    if (! gotTransport)
        slot.publishTransport (0.0, 0.0, blockStart, false);

    const float* l = buffer.getReadPointer (0);
    const float* r = numIn > 1 ? buffer.getReadPointer (1) : l;
    ring.push (l, r, n);
    slot.lastBlockMs.store (juce::jmax ((juce::uint32) 1, juce::Time::getMillisecondCounter()), std::memory_order_relaxed);

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
