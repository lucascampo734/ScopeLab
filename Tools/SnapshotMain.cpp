// Simula tres pistas de Ableton (bombo, bajo, pad), cada una con su propia instancia
// de ScopeLab, y guarda capturas PNG de la interfaz. Uso: ScopeSnapshot <carpeta_salida>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cmath>

struct FakePlayHead : juce::AudioPlayHead
{
    double bpm = 124.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        return p;
    }
};

enum class Part { kick, kickTop, bass, pad, full };

struct DemoSignal
{
    double sr = 48000.0, bpm = 124.0;
    Part part = Part::full;
    juce::Random rng { 42 };
    float hatPrev = 0.0f;

    void render (float* L, float* R, int n, int64_t startSample)
    {
        const double twoPi = juce::MathConstants<double>::twoPi;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (startSample + i) / sr - (part == Part::kickTop ? 0.0035 : 0.0);   // capa 3.5 ms tarde
            const double beat = t * bpm / 60.0;
            const double tk = (beat - std::floor (beat)) * 60.0 / bpm;
            const double hb = beat + 0.5;
            const double th = (hb - std::floor (hb)) * 60.0 / bpm;

            double l = 0.0, r = 0.0;

            if (part == Part::kickTop)
            {
                // Capa de bombo (otro sample): mismo cuerpo, más "click"
                const double kPhase = twoPi * (48.0 * tk + 90.0 / 28.0 * (1.0 - std::exp (-28.0 * tk)));
                const double body = 0.55 * std::sin (kPhase) * std::exp (-tk * 9.0);
                const double click = 0.25 * std::sin (twoPi * 2800.0 * tk) * std::exp (-tk * 120.0);
                l += body + click; r += body + click;
            }

            if (part == Part::kick || part == Part::full)
            {
                const double kPhase = twoPi * (48.0 * tk + 90.0 / 28.0 * (1.0 - std::exp (-28.0 * tk)));
                const double kick = 0.8 * std::sin (kPhase) * std::exp (-tk * 6.0);
                l += kick; r += kick;
            }

            if (part == Part::bass || part == Part::full)
            {
                // Bajo en La (55 Hz) que entra apenas sale el bombo y le pisa la cola
                double bass = 0.0;
                for (int h = 1; h <= 10; ++h)
                    bass += std::sin (twoPi * 55.0 * h * t + h * 0.3) / (h * (1.0 + h * 0.15));
                bass *= 0.32 * (1.0 - 0.55 * std::exp (-tk * 7.0));
                l += bass; r += bass;
            }

            if (part == Part::pad || part == Part::full)
            {
                double padL = 0.0, padR = 0.0;
                for (double f : { 220.0, 261.63, 329.63, 440.0 })
                    for (int h = 1; h <= 14; ++h)
                    {
                        const double hf = f * h;
                        if (hf > 16000.0) break;
                        const double amp = 1.0 / (h * (1.0 + hf / 2500.0));
                        padL += amp * std::sin (twoPi * hf * t + h);
                        padR += amp * std::sin (twoPi * hf * 1.006 * t + 0.7 * h);
                    }
                const double padAmp = 0.09 * (0.6 + 0.4 * std::sin (twoPi * 0.25 * t));
                const float noise = rng.nextFloat() * 2.0f - 1.0f;
                const float hp = noise - hatPrev; hatPrev = noise;
                const double hat = 0.22 * hp * std::exp (-th * 55.0);
                l += padL * padAmp + hat * 0.7;
                r += padR * padAmp + hat;
            }

            L[i] = (float) l;
            R[i] = (float) r;
        }
    }
};

struct SimTrack
{
    SimTrack (Part p, const juce::String& name, juce::Colour colour, double sr, int block)
    {
        signal.part = p;
        signal.sr = sr;
        proc.setPlayConfigDetails (2, 2, sr, block);
        proc.prepareToPlay (sr, block);
        proc.setPlayHead (&playHead);
        proc.updateTrackProperties ({ name, colour });
    }
    ~SimTrack() { proc.setPlayHead (nullptr); }

    ScopeLabAudioProcessor proc;
    FakePlayHead playHead;
    DemoSignal signal;
};

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                      : juce::File::getCurrentWorkingDirectory());
    outDir.createDirectory();

    constexpr double sr = 48000.0;
    constexpr int block = 400;

    // Colores parecidos a los de Ableton
    std::vector<std::unique_ptr<SimTrack>> tracks;
    tracks.push_back (std::make_unique<SimTrack> (Part::kick, "Kick", juce::Colour (0xffff6f3c), sr, block));
    tracks.push_back (std::make_unique<SimTrack> (Part::bass, "Bass", juce::Colour (0xff3ec7ff), sr, block));
    tracks.push_back (std::make_unique<SimTrack> (Part::kickTop, "Kick Top", juce::Colour (0xffffd84a), sr, block));
    tracks.push_back (std::make_unique<SimTrack> (Part::pad,  "Pad",  juce::Colour (0xffc46cff), sr, block));

    int64_t pos = 0;
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    auto feed = [&] (int samples)
    {
        while (samples > 0)
        {
            const int n = juce::jmin (block, samples);
            buffer.setSize (2, n, false, false, true);
            for (auto& t : tracks)
            {
                t->signal.render (buffer.getWritePointer (0), buffer.getWritePointer (1), n, pos);
                t->playHead.ppq = (double) pos / sr * t->playHead.bpm / 60.0;
                t->proc.processBlock (buffer, midi);
            }
            pos += n;
            samples -= n;
        }
    };

    auto& main = tracks.front()->proc;   // abrimos la ventana del plugin del bombo
    auto setParam = [&] (const juce::String& id, float plainValue)
    {
        auto* p = main.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    };

    feed ((int) sr * 10);
    std::unique_ptr<juce::AudioProcessorEditor> ed (main.createEditorIfNeeded());
    auto* editor = dynamic_cast<ScopeLabAudioProcessorEditor*> (ed.get());
    editor->setSize (1040, 660);

    auto writePng = [] (const juce::Image& img, const juce::File& f)
    {
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    };
    auto save = [&] (const juce::String& name)
    {
        writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), outDir.getChildFile (name));
    };
    auto settle = [&] (int frames)
    {
        for (int i = 0; i < frames; ++i) { feed ((int) sr / 60); editor->refresh(); }
    };
    auto feedToBeatFraction = [&] (double beats, double fraction)
    {
        const double spb = sr * 60.0 / 124.0;
        const double win = spb * beats;
        const double cur = std::fmod ((double) pos, win);
        feed ((int) std::fmod (win * fraction - cur + win, win));
        editor->refresh();
    };

    auto hoverAtHz = [&] (float hz)
    {
        auto& sv = editor->getSpectrumView();
        const float left = 10.0f + 30.0f, right = (float) sv.getWidth() - 10.0f;
        const float t = std::log (hz / 20.0f) / std::log (1000.0f);
        sv.setHover (juce::Point<float> (left + (right - left) * t, (float) sv.getHeight() * 0.5f));
    };

    // 1) Multipista, 2 compases, espectro en PROMEDIO (ocultamos el Pad)
    setParam ("view", 1); setParam ("sync", 2); setParam ("beats", 5); setParam ("split", 1.0f); setParam ("specmode", 1);
    settle (90);
    feedToBeatFraction (8.0, 0.55);
    save ("13_promedio.png");

    // 2) Espectro en GOLPES del Kick (esta pista): qué suena debajo de cada kick
    setParam ("specmode", 2);
    settle (30);
    hoverAtHz (62.0f);
    editor->refresh();
    save ("14_golpes_kick.png");
    editor->getSpectrumView().setHover (std::nullopt);

    // 3) Referencia de fase = Bass (clic en la fila del Bass)
    editor->selectPhaseReferenceByName ("Bass");
    setParam ("specmode", 1);
    settle (60);
    save ("15_referencia_bass.png");
    editor->selectPhaseReferenceByName ("Kick");

    ed.reset();
    tracks.clear();
    std::puts ("ok");
    return 0;
}
