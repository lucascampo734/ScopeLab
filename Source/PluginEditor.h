#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
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
class WaveformView : public juce::Component
{
public:
    void setData (const std::vector<float>& l, const std::vector<float>& r, float gain, bool split,
                  int divisions, const juce::String& divLabel, float sweep, const juce::String& status);
    void paint (juce::Graphics&) override;

private:
    void drawLane (juce::Graphics&, juce::Rectangle<float> lane, bool drawL, bool drawR);
    void drawChannel (juce::Graphics&, juce::Rectangle<float> lane, const std::vector<float>& data, juce::Colour);

    std::vector<float> left, right;
    float gain = 1.0f, sweep = -1.0f;
    bool split = false;
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
class SpectrumView : public juce::Component
{
public:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;

    SpectrumView();
    void pushSamples (const float* mono, double sampleRate);
    void paint (juce::Graphics&) override;

private:
    float valueAt (const std::vector<float>& bins, float f0, float f1) const;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> fftData, smoothed, peaks;
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

    // Lee el audio del procesador y actualiza las vistas (lo llama el timer a 60 Hz).
    void refresh();

private:
    void timerCallback() override { refresh(); }
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float level, juce::Colour, const juce::String& name);

    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ScopeLabAudioProcessor& proc;
    ScopeLookAndFeel lnf;

    WaveformView   wave;
    GoniometerView gonio;
    SpectrumView   spectrum;

    juce::Label syncLabel, timeLabel, beatsLabel, gainLabel;
    juce::ComboBox syncBox, beatsBox;
    juce::Slider timeSlider, gainSlider;
    juce::TextButton splitButton { "L/R separados" }, freezeButton { "Congelar" };

    std::unique_ptr<ComboBoxAttachment> syncAtt, beatsAtt;
    std::unique_ptr<SliderAttachment>   timeAtt, gainAtt;
    std::unique_ptr<ButtonAttachment>   splitAtt, freezeAtt;

    std::atomic<float>* timeParam   = nullptr;
    std::atomic<float>* gainParam   = nullptr;
    std::atomic<float>* syncParam   = nullptr;
    std::atomic<float>* beatsParam  = nullptr;
    std::atomic<float>* splitParam  = nullptr;
    std::atomic<float>* freezeParam = nullptr;

    std::vector<float> bufL, bufR, dispL, dispR, mono, gL, gR, specMono, specR;
    float meterL = 0.0f, meterR = 0.0f, holdL = 0.0f, holdR = 0.0f;
    int holdCounterL = 0, holdCounterR = 0;
    juce::Rectangle<int> headerArea, controlsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeLabAudioProcessorEditor)
};
