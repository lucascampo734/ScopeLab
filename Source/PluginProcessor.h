#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ScopeHub.h"
#include <array>

//==============================================================================
// Loudness según ITU-R BS.1770 / EBU R128 (momentáneo, corto, integrado) y true peak.
class LoudnessMeter
{
public:
    static constexpr int maxBlocks = 36000;   // 1 hora de bloques de 100 ms

    LoudnessMeter() : blocks ((size_t) maxBlocks, 0.0f) {}

    void prepare (double sampleRate);
    void process (const float* l, const float* r, int n, bool stereo) noexcept;
    void requestReset() noexcept { resetRequested.store (true); }

    float getMomentary() const noexcept  { return momentary.load (std::memory_order_relaxed); }
    float getShortTerm() const noexcept  { return shortTerm.load (std::memory_order_relaxed); }
    float getTruePeak() const noexcept   { return truePeak.load (std::memory_order_relaxed); }   // lineal
    float computeIntegrated() const;

    static float energyToLufs (double e) { return e > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (e)) : -200.0f; }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static constexpr int tpPhases = 4, tpTaps = 12;
    float truePeakSample (int ch, float x) noexcept;

    std::array<Biquad, 2> pre, rlb;
    std::array<std::array<float, tpTaps>, 2> tpHist {};
    std::array<int, 2> tpPos {};
    std::array<std::array<float, tpTaps>, tpPhases> tpCoef {};

    std::array<double, 30> sub {};   // energía de los últimos 30 bloques de 100 ms
    int subFilled = 0, subIndex = 0, subLen = 4800, subCount = 0;
    double acc = 0.0;

    std::vector<float> blocks;       // energía de cada ventana de 400 ms, cada 100 ms
    std::atomic<int> numBlocks { 0 };
    std::atomic<float> momentary { -200.0f }, shortTerm { -200.0f }, truePeak { 0.0f };
    std::atomic<bool> resetRequested { false };
};

//==============================================================================
// Pasa-banda de 4.º orden (dos biquads) para escuchar una franja del espectro.
class AuditionFilter
{
public:
    void setBandpass (double fs, double freq, double q) noexcept
    {
        const double w0 = juce::MathConstants<double>::twoPi * freq / fs;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0; b1 = 0.0; b2 = -alpha / a0;
        a1 = -2.0 * std::cos (w0) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    float process (int stage, int ch, float x) noexcept
    {
        auto& s = state[(size_t) stage][(size_t) ch];
        const double y = b0 * x + s[0];
        s[0] = b1 * x - a1 * y + s[1];
        s[1] = b2 * x - a2 * y;
        return (float) y;
    }

    void reset() noexcept
    {
        for (auto& stage : state)
            for (auto& ch : stage)
                ch = { 0.0, 0.0 };
    }

private:
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<std::array<std::array<double, 2>, 2>, 2> state {};   // [etapa][canal][z1,z2]
};

//==============================================================================
class ScopeLabAudioProcessor : public juce::AudioProcessor
{
public:
    ScopeLabAudioProcessor();
    ~ScopeLabAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ScopeLab"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Ableton (VST3) informa acá el nombre y color de la pista
    void updateTrackProperties (const TrackProperties& properties) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    ScopeHub&  getHub() noexcept      { return *hub; }
    ScopeSlot& getSlot() noexcept     { return slotIndex >= 0 ? hub->slot (slotIndex) : *privateSlot; }
    int        getSlotIndex() const noexcept { return slotIndex; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> peakLeft { 0.0f }, peakRight { 0.0f };
    LoudnessMeter loudness;

    // Espectro de referencia guardado (solo lo usa la interfaz)
    std::vector<float> referenceSpectrum;
    juce::String referenceName;

    // Pista elegida como referencia en el panel de fase (0 = esta pista)
    int phaseReferenceKey = 0;

private:
    void applyAudition (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept;

    AuditionFilter auditionFilter;
    float auditionGain = 0.0f, auditionFreq = 1000.0f;

    juce::SharedResourcePointer<ScopeHub> hub;
    int slotIndex = -1;
    std::unique_ptr<ScopeSlot> privateSlot;   // solo si se llenan los 16 lugares del hub

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeLabAudioProcessor)
};
