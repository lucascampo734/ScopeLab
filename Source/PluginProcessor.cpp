#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
void LoudnessMeter::prepare (double fs)
{
    using juce::MathConstants;

    // Filtro K: estante de agudos + pasa-altos RLB (coeficientes válidos para cualquier frecuencia de muestreo)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (MathConstants<double>::pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& f : pre)
        {
            f = {};
            f.b0 = (Vh + Vb * K / Q + K * K) / a0;
            f.b1 = 2.0 * (K * K - Vh) / a0;
            f.b2 = (Vh - Vb * K / Q + K * K) / a0;
            f.a1 = 2.0 * (K * K - 1.0) / a0;
            f.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (MathConstants<double>::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& f : rlb)
        {
            f = {};
            f.b0 = 1.0; f.b1 = -2.0; f.b2 = 1.0;
            f.a1 = 2.0 * (K * K - 1.0) / a0;
            f.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    // True peak: interpolación x4 con un filtro sinc de 48 coeficientes (ventana Blackman)
    constexpr int N = tpPhases * tpTaps;
    for (int p = 0; p < tpPhases; ++p)
    {
        double sum = 0.0;
        for (int j = 0; j < tpTaps; ++j)
        {
            const int n = p + tpPhases * j;
            const double x = ((double) n - (N - 1) * 0.5) / tpPhases;
            const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (MathConstants<double>::pi * x) / (MathConstants<double>::pi * x);
            const double w = 0.42 - 0.5 * std::cos (MathConstants<double>::twoPi * n / (N - 1))
                                  + 0.08 * std::cos (2.0 * MathConstants<double>::twoPi * n / (N - 1));
            tpCoef[(size_t) p][(size_t) j] = (float) (sinc * w);
            sum += sinc * w;
        }
        for (auto& c : tpCoef[(size_t) p])
            c = (float) (c / sum);
    }
    for (auto& h : tpHist) h.fill (0.0f);
    tpPos.fill (0);

    subLen = juce::jmax (1, juce::roundToInt (fs * 0.1));
    subCount = 0; subFilled = 0; subIndex = 0; acc = 0.0;
    sub.fill (0.0);
}

float LoudnessMeter::truePeakSample (int ch, float x) noexcept
{
    auto& hist = tpHist[(size_t) ch];
    auto& pos = tpPos[(size_t) ch];
    pos = (pos + 1) % tpTaps;
    hist[(size_t) pos] = x;

    float peak = std::abs (x);
    for (int p = 0; p < tpPhases; ++p)
    {
        float y = 0.0f;
        int idx = pos;
        for (int j = 0; j < tpTaps; ++j)
        {
            y += tpCoef[(size_t) p][(size_t) j] * hist[(size_t) idx];
            idx = idx == 0 ? tpTaps - 1 : idx - 1;
        }
        peak = juce::jmax (peak, std::abs (y));
    }
    return peak;
}

void LoudnessMeter::process (const float* l, const float* r, int n, bool stereo) noexcept
{
    if (resetRequested.exchange (false))
    {
        numBlocks.store (0, std::memory_order_release);
        truePeak.store (0.0f, std::memory_order_relaxed);
    }

    float tp = truePeak.load (std::memory_order_relaxed);
    auto avgLast = [this] (int count)
    {
        double s = 0.0;
        for (int k = 1; k <= count; ++k)
            s += sub[(size_t) ((subIndex - k + 30) % 30)];
        return s / count;
    };

    for (int i = 0; i < n; ++i)
    {
        const double yl = rlb[0].process (pre[0].process (l[i]));
        double e = yl * yl;
        tp = juce::jmax (tp, truePeakSample (0, l[i]));
        if (stereo)
        {
            const double yr = rlb[1].process (pre[1].process (r[i]));
            e += yr * yr;
            tp = juce::jmax (tp, truePeakSample (1, r[i]));
        }
        acc += e;

        if (++subCount >= subLen)
        {
            sub[(size_t) subIndex] = acc / subLen;
            subIndex = (subIndex + 1) % 30;
            subFilled = juce::jmin (30, subFilled + 1);
            acc = 0.0;
            subCount = 0;

            if (subFilled >= 4)
            {
                const double em = avgLast (4);
                momentary.store (energyToLufs (em), std::memory_order_relaxed);
                const int nb = numBlocks.load (std::memory_order_relaxed);
                if (nb < maxBlocks)
                {
                    blocks[(size_t) nb] = (float) em;
                    numBlocks.store (nb + 1, std::memory_order_release);
                }
            }
            shortTerm.store (energyToLufs (avgLast (subFilled)), std::memory_order_relaxed);
        }
    }
    truePeak.store (tp, std::memory_order_relaxed);
}

float LoudnessMeter::computeIntegrated() const
{
    // Doble compuerta de EBU R128: absoluta a -70 LUFS y relativa a -10 LU
    const int n = numBlocks.load (std::memory_order_acquire);
    const double absGate = std::pow (10.0, (-70.0 + 0.691) / 10.0);
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < n; ++i)
        if (blocks[(size_t) i] > absGate) { sum += blocks[(size_t) i]; ++count; }
    if (count == 0)
        return -200.0f;

    const double relGate = (sum / count) * 0.1;
    sum = 0.0; count = 0;
    for (int i = 0; i < n; ++i)
        if (blocks[(size_t) i] > absGate && blocks[(size_t) i] > relGate) { sum += blocks[(size_t) i]; ++count; }
    return count > 0 ? energyToLufs (sum / count) : -200.0f;
}

//==============================================================================
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
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "ms", 1 }, "Espectro Mid/Side", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "spectro", 1 }, "Cascada", false));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "specmode", 1 }, "Modo del espectro", StringArray { "Instante", "Promedio", "Golpes" }, 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Congelar", false));

    return layout;
}

void ScopeLabAudioProcessor::prepareToPlay (double sampleRate, int)
{
    getSlot().sampleRate.store (sampleRate);
    loudness.prepare (sampleRate);
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
    loudness.process (l, r, n, numIn > 1);
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
