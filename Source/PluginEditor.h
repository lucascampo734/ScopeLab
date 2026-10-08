#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <map>
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
    void setData (std::vector<ScopeSeries> newSeries, std::vector<std::vector<int>> newLanes, float gain,
                  int divisions, const juce::String& divLabel, float sweep, const juce::String& status);
    void paint (juce::Graphics&) override;

private:
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
    void beginFrame (double sampleRate);
    void pushTrack (int key, const float* mono, juce::Colour colour);   // fftSize muestras
    void endFrame();
    void paint (juce::Graphics&) override;

private:
    struct Curve
    {
        std::vector<float> smoothed, peaks;
        juce::Colour colour;
        bool touched = false;
    };

    float valueAt (const std::vector<float>& bins, float f0, float f1) const;

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

    std::set<int> hiddenKeys;
    std::map<int, std::pair<float, float>> corrSmooth;
    std::vector<float> tmpL, tmpR, tmpMono, gL, gR, specL, specR, specMono, refMono, refLow, otherMono, otherLow;
    float holdL = 0.0f, holdR = 0.0f;
    int holdCounterL = 0, holdCounterR = 0;
    juce::Rectangle<int> headerArea, controlsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeLabAudioProcessorEditor)
};
