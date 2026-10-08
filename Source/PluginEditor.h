#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <map>
#include <optional>
#include <set>
#include "PluginProcessor.h"

namespace ScopeColours
{
    const juce::Colour bg         { 0xff090d12 };
    const juce::Colour panel      { 0xff0f151c };
    const juce::Colour panelEdge  { 0xff1a2430 };
    const juce::Colour grid       { 0xff17212b };
    const juce::Colour gridStrong { 0xff26394a };
    const juce::Colour text       { 0xff7f93a6 };
    const juce::Colour textBright { 0xffe4edf5 };
    const juce::Colour left       { 0xff35e0ff };
    const juce::Colour right      { 0xffff7a45 };
    const juce::Colour mid        { 0xffb6ff5c };
    const juce::Colour warn       { 0xffff4f6a };
    const juce::Colour caution    { 0xffffc84a };
}

//==============================================================================
class ScopeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ScopeLookAndFeel();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
};

//==============================================================================
struct ScopeSeries
{
    std::vector<float> data;
    juce::Colour colour;
    juce::String label;
    bool isSum = false;
};

class WaveformView : public juce::Component
{
public:
    static constexpr float dbAxisWidth = 30.0f;

    void setData (std::vector<ScopeSeries> newSeries, std::vector<std::vector<int>> newLanes, float gain,
                  int divisions, const juce::String& divLabel, float sweep, const juce::String& status, bool showLRLegend);
    void setExpanded (bool e) { expanded = e; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void()> onExpandToggle;

private:
    juce::Rectangle<float> expandIcon;
    bool expanded = false, showLegend = true;
    void drawLane (juce::Graphics&, juce::Rectangle<float> lane, const std::vector<int>& indices);
    void drawSeries (juce::Graphics&, juce::Rectangle<float> lane, const ScopeSeries&);

    std::vector<ScopeSeries> series;
    std::vector<std::vector<int>> lanes;
    float gain = 1.0f, sweep = -1.0f;
    int divisions = 10;
    juce::String divLabel, status;
};

//==============================================================================
class GoniometerView : public juce::Component
{
public:
    void setData (const std::vector<float>& l, const std::vector<float>& r, float gain);
    void paint (juce::Graphics&) override;

private:
    std::vector<float> left, right;
    float gain = 1.0f, correlation = 0.0f;
};

//==============================================================================
class PhaseView : public juce::Component
{
public:
    struct Row
    {
        juce::String name;
        juce::Colour colour;
        float lowCorr = 0.0f, fullCorr = 0.0f;
        bool hasSignal = false;
        bool alignValid = false;
        float lagMs = 0.0f, alignCorr = 0.0f;   // lag > 0: esta pista llega tarde
    };

    void setRows (std::vector<Row> newRows, const juce::String& refName, juce::Colour refColour, const juce::String& message);
    void paint (juce::Graphics&) override;

private:
    std::vector<Row> rows;
    juce::String refName, message;
    juce::Colour refColour;
};

//==============================================================================
class TrackBar : public juce::Component
{
public:
    struct Chip
    {
        int key = 0;
        juce::String name;
        juce::Colour colour;
        bool visible = true, isThis = false;
    };

    void setChips (std::vector<Chip> newChips, const juce::String& hint);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void (int key)> onToggle;

private:
    std::vector<Chip> chips;
    std::vector<juce::Rectangle<float>> chipBounds;
    juce::String hint;
};

//==============================================================================
class SpectrumView : public juce::Component
{
public:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;

    SpectrumView();
    void beginFrame (double sampleRate, bool showCollisions);
    void pushTrack (int key, const float* mono, juce::Colour colour, const juce::String& name, bool isSum = false);   // fftSize muestras
    void endFrame();
    void paint (juce::Graphics&) override;

    void setOptions (bool msAvailable, bool msEnabled, bool spectrogramEnabled);
    void setReference (std::vector<float>* curve, juce::String* name) { reference = curve; referenceName = name; }
    void toggleReference();

    void setExpanded (bool e) { expanded = e; repaint(); }
    void setHover (std::optional<juce::Point<float>> p) { hoverPos = p; repaint(); }
    void mouseMove (const juce::MouseEvent& e) override  { setHover (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override  { setHover (e.position); }
    void mouseExit (const juce::MouseEvent&) override    { setHover (std::nullopt); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void()> onExpandToggle, onToggleMs, onToggleSpectrogram;

private:
    struct Curve
    {
        std::vector<float> smoothed, peaks, average;
        juce::Colour colour;
        juce::String name;
        bool touched = false, isSum = false;
    };

    float valueAt (const std::vector<float>& bins, float f0, float f1) const;
    int mainCurveKey() const;
    void pushSpectrogramColumn();
    void paintSpectrogram (juce::Graphics&, juce::Rectangle<float> area, juce::Rectangle<float> leftLabels, juce::Rectangle<float> bottomLabels);
    bool hitsHeaderControl (juce::Point<float> p) const;

    std::optional<juce::Point<float>> hoverPos;
    juce::Rectangle<float> expandIcon, msChip, refChip, spectroChip, lastPlot;
    bool expanded = false, collisions = false, msAvailable = false, msOn = false, spectroOn = false;

    std::vector<float>* reference = nullptr;
    juce::String* referenceName = nullptr;

    juce::Image spectroImage;
    std::array<juce::Colour, 256> lut;
    int spectroKey = 0;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> fftData;
    std::map<int, Curve> curves;
    std::vector<int> order;
    double sampleRate = 48000.0;
    float windowGain = 1.0f;
};

//==============================================================================
class ScopeLabAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ScopeLabAudioProcessorEditor (ScopeLabAudioProcessor&);
    ~ScopeLabAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Lee el audio de todas las pistas y actualiza las vistas (lo llama el timer a 60 Hz).
    void refresh();

    // 0 = normal, 1 = osciloscopio ampliado, 2 = espectro ampliado
    void setFocusPanel (int panel);
    SpectrumView& getSpectrumView() noexcept { return spectrum; }

private:
    struct Track
    {
        int key = 0;
        ScopeSlot* slot = nullptr;
        juce::String name;
        juce::Colour colour;
        bool isThis = false, visible = true;
        ScopeTransport transport;
        double sampleRate = 48000.0;
        int64_t end = 0, alignedEnd = 0;
        std::vector<float> l, r;   // ventana a dibujar
    };

    void timerCallback() override { refresh(); }
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float level, juce::Colour, const juce::String& name);
    void drawLoudness (juce::Graphics&, juce::Rectangle<float>);
    void mouseDown (const juce::MouseEvent&) override;
    void updatePhase (std::vector<Track*>& tracks, Track& me, bool aligned);

    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ScopeLabAudioProcessor& proc;
    ScopeLookAndFeel lnf;

    TrackBar       trackBar;
    WaveformView   wave;
    GoniometerView gonio;
    PhaseView      phase;
    SpectrumView   spectrum;

    juce::Label viewLabel, syncLabel, timeLabel, beatsLabel, gainLabel;
    juce::ComboBox viewBox, syncBox, beatsBox;
    juce::Slider timeSlider, gainSlider;
    juce::TextButton splitButton { "Separados" }, freezeButton { "Congelar" };

    std::unique_ptr<ComboBoxAttachment> viewAtt, syncAtt, beatsAtt;
    std::unique_ptr<SliderAttachment>   timeAtt, gainAtt;
    std::unique_ptr<ButtonAttachment>   splitAtt, freezeAtt;

    std::atomic<float>* timeParam   = nullptr;
    std::atomic<float>* gainParam   = nullptr;
    std::atomic<float>* syncParam   = nullptr;
    std::atomic<float>* beatsParam  = nullptr;
    std::atomic<float>* viewParam   = nullptr;
    std::atomic<float>* splitParam  = nullptr;
    std::atomic<float>* freezeParam = nullptr;
    std::atomic<float>* msParam     = nullptr;
    std::atomic<float>* spectroParam = nullptr;

    struct AlignInfo { float lagMs = 0.0f, corr = 0.0f; bool valid = false; };
    std::map<int, AlignInfo> alignInfo;
    std::vector<float> alignL, alignR, alignMono, alignLow, alignRef, alignOther;
    int frameCounter = 0;
    float integratedLufs = -200.0f;
    juce::Rectangle<int> loudnessArea;

    std::set<int> hiddenKeys;
    std::map<int, std::pair<float, float>> corrSmooth;
    std::vector<float> tmpL, tmpR, tmpMono, gL, gR, specL, specR, specMono, refMono, refLow, otherMono, otherLow;
    int focusPanel = 0, currentHz = 60;
    float holdL = 0.0f, holdR = 0.0f;
    int holdCounterL = 0, holdCounterR = 0;
    juce::Rectangle<int> headerArea, controlsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeLabAudioProcessorEditor)
};
