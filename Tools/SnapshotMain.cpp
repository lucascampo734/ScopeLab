// Alimenta el plugin con una mezcla de prueba (bombo, bajo, acorde estéreo, hi-hats)
// y guarda capturas PNG de la interfaz. Uso: ScopeSnapshot <carpeta_salida>
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

struct DemoMix
{
    double sr = 48000.0, bpm = 124.0;
    juce::Random rng { 42 };
    float hatPrev = 0.0f;

    void render (float* L, float* R, int n, int64_t startSample)
    {
        const double twoPi = juce::MathConstants<double>::twoPi;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (startSample + i) / sr;
            const double beat = t * bpm / 60.0;
            const double tk = (beat - std::floor (beat)) * 60.0 / bpm;            // seg desde el tiempo
            const double hb = beat + 0.5;
            const double th = (hb - std::floor (hb)) * 60.0 / bpm;                // seg desde el contratiempo

            // Bombo: barrido 135 -> 45 Hz
            const double kPhase = twoPi * (45.0 * tk + 90.0 / 28.0 * (1.0 - std::exp (-28.0 * tk)));
            const double kick = 0.85 * std::sin (kPhase) * std::exp (-tk * 6.5);

            // Bajo diente de sierra suave (A1 = 55 Hz) con sidechain
            double bass = 0.0;
            for (int h = 1; h <= 10; ++h)
                bass += std::sin (twoPi * 55.0 * h * t) / h;
            bass *= 0.16 * (1.0 - 0.85 * std::exp (-tk * 9.0));

            // Acorde (A, C, E) con armónicos, desafinado entre L y R para dar ancho estéreo
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
            const double padAmp = 0.06 * (0.6 + 0.4 * std::sin (twoPi * 0.25 * t));
            padL *= padAmp; padR *= padAmp;

            // Hi-hat: ruido pasa-altos en el contratiempo
            const float noise = rng.nextFloat() * 2.0f - 1.0f;
            const float hp = noise - hatPrev; hatPrev = noise;
            const double hat = 0.22 * hp * std::exp (-th * 55.0);

            L[i] = (float) (kick + bass + padL + hat * 0.7);
            R[i] = (float) (kick + bass + padR + hat * 1.0);
        }
    }
};

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                      : juce::File::getCurrentWorkingDirectory());
    outDir.createDirectory();

    constexpr double sr = 48000.0;
    constexpr int block = 400;

    ScopeLabAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);

    FakePlayHead playHead;
    proc.setPlayHead (&playHead);

    DemoMix mix;
    mix.sr = sr; mix.bpm = playHead.bpm;
    int64_t pos = 0;
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    auto feed = [&] (int samples)
    {
        while (samples > 0)
        {
            const int n = juce::jmin (block, samples);
            buffer.setSize (2, n, false, false, true);
            mix.render (buffer.getWritePointer (0), buffer.getWritePointer (1), n, pos);
            playHead.ppq = (double) pos / sr * playHead.bpm / 60.0;
            proc.processBlock (buffer, midi);
            pos += n;
            samples -= n;
        }
    };

    auto setParam = [&] (const juce::String& id, float plainValue)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    };

    feed ((int) sr * 2);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditorIfNeeded());
    auto* editor = dynamic_cast<ScopeLabAudioProcessorEditor*> (ed.get());
    editor->setSize (980, 620);

    auto save = [&] (const juce::String& name, float scale = 1.0f)
    {
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        auto f = outDir.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    };

    auto settle = [&] (int frames)
    {
        for (int i = 0; i < frames; ++i) { feed ((int) sr / 60); editor->refresh(); }
    };

    // 1) Trigger, 30 ms
    setParam ("sync", 1); setParam ("time", 30.0f); setParam ("gain", 0.0f);
    settle (40);
    save ("01_trigger.png");

    // 2) Tempo del DAW, 1 compás (captura a mitad del compás para ver el barrido)
    setParam ("sync", 2); setParam ("beats", 4);
    settle (50);
    {
        const double spb = sr * 60.0 / playHead.bpm;
        const double barPos = std::fmod ((double) pos, spb * 4.0);
        const int toMid = (int) std::fmod (spb * 2.6 - barPos + spb * 4.0, spb * 4.0);
        feed (toMid);
        editor->refresh();
    }
    save ("02_tempo_compas.png");

    // 3) Tempo, 1 tiempo, L/R separados
    setParam ("beats", 2); setParam ("split", 1.0f);
    settle (30);
    save ("03_tempo_separados.png");
    setParam ("split", 0.0f);

    // 4) Frames para animación (tempo, 1 compás, 30 fps)
    setParam ("beats", 4);
    settle (10);
    auto framesDir = outDir.getChildFile ("frames");
    framesDir.createDirectory();
    for (int i = 0; i < 120; ++i)
    {
        feed ((int) sr / 30);
        editor->refresh();
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 0.7f);
        auto f = framesDir.getChildFile (juce::String::formatted ("f%03d.png", i));
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    }

    ed.reset();
    proc.setPlayHead (nullptr);
    std::puts ("ok");
    return 0;
}
