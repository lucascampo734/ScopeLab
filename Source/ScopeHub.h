#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
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
        const auto oldest = getWritePosition() - capacity;
        for (int i = 0; i < n; ++i)
        {
            const auto pos = start + i;
            if (pos < 0 || pos < oldest) { l[i] = r[i] = 0.0f; continue; }
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
struct ScopeTransport
{
    double  bpm     = 0.0;
    double  ppq     = 0.0;   // posición en negras al inicio del último bloque
    int64_t sample  = 0;     // posición del buffer circular al inicio de ese bloque
    bool    playing = false;
};

//==============================================================================
// Una pista: la escribe una instancia del plugin, la pueden leer todas.
class ScopeSlot
{
public:
    std::atomic<bool>     inUse       { false };
    std::atomic<uint32_t> generation  { 0 };
    std::atomic<uint32_t> lastBlockMs { 0 };
    std::atomic<double>   sampleRate  { 44100.0 };

    // Se crea una sola vez y no se libera mientras exista el hub.
    std::unique_ptr<ScopeRingBuffer> buffer;

    void publishTransport (double bpm, double ppq, int64_t sample, bool playing) noexcept
    {
        seq.fetch_add (1, std::memory_order_acq_rel);   // impar = escribiendo
        tBpm.store (bpm, std::memory_order_relaxed);
        tPpq.store (ppq, std::memory_order_relaxed);
        tSample.store (sample, std::memory_order_relaxed);
        tPlaying.store (playing, std::memory_order_relaxed);
        seq.fetch_add (1, std::memory_order_acq_rel);   // par = listo
    }

    ScopeTransport getTransport() const noexcept
    {
        ScopeTransport t;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto s1 = seq.load (std::memory_order_acquire);
            if (s1 & 1u) continue;
            t.bpm     = tBpm.load (std::memory_order_relaxed);
            t.ppq     = tPpq.load (std::memory_order_relaxed);
            t.sample  = tSample.load (std::memory_order_relaxed);
            t.playing = tPlaying.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq.load (std::memory_order_relaxed) == s1)
                break;
        }
        return t;
    }

    void setInfo (const juce::String& newName, juce::Colour newColour)
    {
        const juce::SpinLock::ScopedLockType sl (infoLock);
        name = newName;
        colour = newColour;
    }

    void setHostInfo (const juce::String& hostName, juce::Colour hostColour)
    {
        const juce::SpinLock::ScopedLockType sl (infoLock);
        if (hostName.isNotEmpty())              name = hostName;
        if (! hostColour.isTransparent())       colour = hostColour.withAlpha (1.0f);
    }

    juce::String getName() const    { const juce::SpinLock::ScopedLockType sl (infoLock); return name; }
    juce::Colour getColour() const  { const juce::SpinLock::ScopedLockType sl (infoLock); return colour; }

    // ¿Está procesando audio ahora? (una pista borrada o apagada deja de aparecer)
    bool isAlive (uint32_t nowMs) const noexcept
    {
        const auto last = lastBlockMs.load (std::memory_order_relaxed);
        return inUse.load() && last != 0 && nowMs - last < 1500;
    }

private:
    std::atomic<uint32_t> seq { 0 };
    std::atomic<double>   tBpm { 0.0 }, tPpq { 0.0 };
    std::atomic<int64_t>  tSample { 0 };
    std::atomic<bool>     tPlaying { false };

    mutable juce::SpinLock infoLock;
    juce::String name;
    juce::Colour colour;
};

//==============================================================================
// Compartido por todas las instancias de ScopeLab cargadas en el DAW
// (mismo formato: todas VST3 o todas AU).
class ScopeHub
{
public:
    static constexpr int maxSlots = 16;

    static juce::Colour defaultColour (int index)
    {
        static const juce::uint32 palette[] = { 0xff35e0ff, 0xffff7a45, 0xffb6ff5c, 0xffff4fd8,
                                                0xffffd84a, 0xff8f7bff, 0xff4affb4, 0xffff5a5a };
        return juce::Colour (palette[(size_t) index % 8]);
    }

    int acquire()
    {
        const juce::ScopedLock sl (lock);
        for (int i = 0; i < maxSlots; ++i)
        {
            auto& s = slots[(size_t) i];
            if (s.inUse.load())
                continue;

            if (s.buffer == nullptr)
                s.buffer = std::make_unique<ScopeRingBuffer>();

            s.lastBlockMs.store (0);
            s.setInfo ("Pista " + juce::String (i + 1), defaultColour (i));
            s.generation.fetch_add (1);
            s.inUse.store (true);
            return i;
        }
        return -1;
    }

    void release (int index)
    {
        if (! juce::isPositiveAndBelow (index, maxSlots))
            return;
        const juce::ScopedLock sl (lock);
        slots[(size_t) index].inUse.store (false);
        slots[(size_t) index].lastBlockMs.store (0);
    }

    ScopeSlot& slot (int index) noexcept { return slots[(size_t) index]; }

private:
    juce::CriticalSection lock;
    std::array<ScopeSlot, maxSlots> slots;
};
