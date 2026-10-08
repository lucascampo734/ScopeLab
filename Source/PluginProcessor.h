#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>
#include <cstdint>

//==============================================================================
// Buffer circular sin locks: el hilo de audio escribe, la interfaz lee.
// Una lectura "rota" solo produce un glitch visual, nunca un problema de audio.
class ScopeRingBuffer
{
public:
    static constexpr int capacity = 1 << 18;   // 262144 muestras (~5.4 s a 48 kHz)
    static constexpr int mask     = capacity - 1;

    ScopeRingBuffer() : left ((size_t) capacity, 0.0f), right ((size_t) capacity, 0.0f) {}

    void push (const float* l, const float* r, int n) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const auto idx = (size_t) ((w + i) & mask);
            left[idx]  = l[i];
            right[idx] = r[i];
        }
        writePos.store (w + n, std::memory_order_release);
    }

    int64_t getWritePosition() const noexcept { return writePos.load (std::memory_order_acquire); }

    // Copia las n muestras que terminan en endPos (exclusivo).
    void read (int64_t endPos, int n, float* l, float* r) const noexcept
    {
        n = juce::jmin (n, capacity);
        const auto start = endPos - n;
        for (int i = 0; i < n; ++i)
        {
            const auto pos = start + i;
            if (pos < 0) { l[i] = r[i] = 0.0f; continue; }
            const auto idx = (size_t) (pos & mask);
            l[i] = left[idx];
            r[i] = right[idx];
        }
    }

private:
    std::vector<float> left, right;
    std::atomic<int64_t> writePos { 0 };
};

//==============================================================================
class ScopeLabAudioProcessor : public juce::AudioProcessor
{
public:
    ScopeLabAudioProcessor();
    ~ScopeLabAudioProcessor() override = default;

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

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct Transport
    {
        double  bpm     = 0.0;
        double  ppq     = 0.0;   // posición en negras al inicio del bloque
        int64_t sample  = 0;     // posición del buffer circular al inicio del bloque
        bool    playing = false;
    };

    Transport getTransport() const noexcept;
    double getScopeSampleRate() const noexcept { return currentSampleRate.load(); }

    juce::AudioProcessorValueTreeState apvts;
    ScopeRingBuffer scope;
    std::atomic<float> peakLeft { 0.0f }, peakRight { 0.0f };

private:
    void publishTransport (double bpm, double ppq, int64_t sample, bool playing) noexcept;

    std::atomic<double> currentSampleRate { 44100.0 };

    // seqlock para leer bpm/ppq/sample de forma consistente desde la interfaz
    std::atomic<uint32_t> transportSeq { 0 };
    std::atomic<double>   tBpm { 0.0 }, tPpq { 0.0 };
    std::atomic<int64_t>  tSample { 0 };
    std::atomic<bool>     tPlaying { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeLabAudioProcessor)
};
