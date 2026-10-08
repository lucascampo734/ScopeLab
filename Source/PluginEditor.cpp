#include "PluginEditor.h"

using namespace juce;

namespace
{
    Font uiFont (float size, bool bold = false)
    {
        return Font (FontOptions (size, bold ? Font::bold : Font::plain));
    }

    void drawPanel (Graphics& g, Rectangle<float> b)
    {
        g.setColour (ScopeColours::panel);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (ScopeColours::panelEdge);
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
    }

    String u8 (const char* text) { return String::fromUTF8 (text); }

    String formatSeconds (double s)
    {
        if (s < 0.001)  return String (s * 1.0e6, 0) + " us";
        if (s < 0.01)   return String (s * 1000.0, 2) + " ms";
        if (s < 1.0)    return String (s * 1000.0, 1) + " ms";
        return String (s, 2) + " s";
    }
}

//==============================================================================
ScopeLookAndFeel::ScopeLookAndFeel()
{
    using namespace ScopeColours;
    setColour (ResizableWindow::backgroundColourId, bg);
    setColour (Label::textColourId, text);
    setColour (Slider::textBoxTextColourId, textBright);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (ComboBox::textColourId, textBright);
    setColour (ComboBox::arrowColourId, text);
    setColour (PopupMenu::backgroundColourId, panel);
    setColour (PopupMenu::textColourId, textBright);
    setColour (PopupMenu::highlightedBackgroundColourId, left.withAlpha (0.25f));
    setColour (PopupMenu::highlightedTextColourId, textBright);
    setColour (TextButton::textColourOffId, text);
    setColour (TextButton::textColourOnId, bg);
}

void ScopeLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                                         Slider::SliderStyle, Slider& s)
{
    const auto alpha = s.isEnabled() ? 1.0f : 0.35f;
    const auto cy = (float) y + (float) h * 0.5f;
    const Rectangle<float> track ((float) x, cy - 2.0f, (float) w, 4.0f);

    g.setColour (ScopeColours::gridStrong.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track, 2.0f);

    auto filled = track.withRight (sliderPos);
    g.setColour (ScopeColours::left.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (filled, 2.0f);

    g.setColour (ScopeColours::textBright.withMultipliedAlpha (alpha));
    g.fillEllipse (Rectangle<float> (12.0f, 12.0f).withCentre ({ sliderPos, cy }));
}

void ScopeLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? ScopeColours::left : ScopeColours::panel;
    if (highlighted && ! on) fill = fill.brighter (0.15f);
    if (down) fill = fill.darker (0.2f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? ScopeColours::left : ScopeColours::gridStrong);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void ScopeLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (ScopeColours::panel);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.isEnabled() ? ScopeColours::gridStrong : ScopeColours::grid);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (ScopeColours::text.withAlpha (box.isEnabled() ? 1.0f : 0.35f));
    g.fillPath (arrow);
}

//==============================================================================
void WaveformView::setData (const std::vector<float>& l, const std::vector<float>& r, float g, bool s,
                            int divs, const String& label, float sw, const String& st)
{
    left = l; right = r; gain = g; split = s; divisions = jmax (1, divs);
    divLabel = label; sweep = sw; status = st;
    repaint();
}

void WaveformView::drawChannel (Graphics& g, Rectangle<float> lane, const std::vector<float>& data, Colour colour)
{
    const int n = (int) data.size();
    if (n < 2) return;

    const float w = lane.getWidth();
    const float cy = lane.getCentreY();
    const float halfH = lane.getHeight() * 0.5f * 0.92f;
    auto yOf = [&] (float v) { return cy - jlimit (-1.0f, 1.0f, v * gain) * halfH; };

    if ((float) n <= w * 1.5f)
    {
        // Pocas muestras: línea continua
        Path p;
        for (int i = 0; i < n; ++i)
        {
            const float x = lane.getX() + w * (float) i / (float) (n - 1);
            if (i == 0) p.startNewSubPath (x, yOf (data[0]));
            else        p.lineTo (x, yOf (data[(size_t) i]));
        }
        g.setColour (colour.withAlpha (0.16f));
        g.strokePath (p, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        return;
    }

    // Muchas muestras: envolvente min/max por columna de píxeles
    const int cols = jmax (2, (int) w);
    std::vector<float> mins ((size_t) cols), maxs ((size_t) cols);
    for (int c = 0; c < cols; ++c)
    {
        const int s0 = (int) ((int64) c * n / cols);
        const int s1 = jmax (s0 + 1, (int) ((int64) (c + 1) * n / cols));
        float mn = data[(size_t) s0], mx = mn;
        for (int s = s0; s < s1 && s < n; ++s)
        {
            mn = jmin (mn, data[(size_t) s]);
            mx = jmax (mx, data[(size_t) s]);
        }
        mins[(size_t) c] = yOf (mn);
        maxs[(size_t) c] = yOf (mx);
    }

    Path env, top, bottom;
    for (int c = 0; c < cols; ++c)
    {
        const float x = lane.getX() + w * (float) c / (float) (cols - 1);
        if (c == 0) { env.startNewSubPath (x, maxs[0]); top.startNewSubPath (x, maxs[0]); bottom.startNewSubPath (x, mins[0]); }
        else        { env.lineTo (x, maxs[(size_t) c]); top.lineTo (x, maxs[(size_t) c]); bottom.lineTo (x, mins[(size_t) c]); }
    }
    for (int c = cols - 1; c >= 0; --c)
        env.lineTo (lane.getX() + w * (float) c / (float) (cols - 1), mins[(size_t) c] + 0.6f);
    env.closeSubPath();

    g.setColour (colour.withAlpha (0.30f));
    g.fillPath (env);
    g.setColour (colour.withAlpha (0.14f));
    g.strokePath (top, PathStrokeType (4.0f));
    g.strokePath (bottom, PathStrokeType (4.0f));
    g.setColour (colour.withAlpha (0.95f));
    g.strokePath (top, PathStrokeType (1.3f));
    g.strokePath (bottom, PathStrokeType (1.3f));
}

void WaveformView::drawLane (Graphics& g, Rectangle<float> lane, bool drawL, bool drawR)
{
    // Grilla
    g.setColour (ScopeColours::grid);
    for (int i = 1; i < divisions; ++i)
    {
        const float x = lane.getX() + lane.getWidth() * (float) i / (float) divisions;
        g.drawVerticalLine (roundToInt (x), lane.getY(), lane.getBottom());
    }
    for (float v : { -0.5f, 0.5f })
        g.drawHorizontalLine (roundToInt (lane.getCentreY() - v * lane.getHeight() * 0.46f), lane.getX(), lane.getRight());

    g.setColour (ScopeColours::gridStrong);
    g.drawHorizontalLine (roundToInt (lane.getCentreY()), lane.getX(), lane.getRight());

    g.saveState();
    g.reduceClipRegion (lane.toNearestInt());
    if (drawR) drawChannel (g, lane, right, ScopeColours::right);
    if (drawL) drawChannel (g, lane, left,  ScopeColours::left);
    g.restoreState();
}

void WaveformView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b);
    const auto fullArea = b.reduced (10.0f, 8.0f).withTrimmedTop (18.0f);
    auto area = fullArea;

    if (split)
    {
        auto top = area.removeFromTop (area.getHeight() * 0.5f);
        drawLane (g, top.withTrimmedBottom (3.0f), true, false);
        drawLane (g, area.withTrimmedTop (3.0f), false, true);
    }
    else
    {
        drawLane (g, area, true, true);
    }

    if (sweep >= 0.0f)
    {
        const float x = fullArea.getX() + fullArea.getWidth() * sweep;
        g.setColour (Colours::black.withAlpha (0.28f));
        g.fillRect (Rectangle<float>::leftTopRightBottom (x, fullArea.getY(), fullArea.getRight(), fullArea.getBottom()));
        g.setColour (ScopeColours::textBright.withAlpha (0.55f));
        g.drawVerticalLine (roundToInt (x), fullArea.getY(), fullArea.getBottom());
    }

    // Etiquetas
    auto header = b.reduced (12.0f, 6.0f).removeFromTop (16.0f);
    g.setFont (uiFont (12.0f, true));
    g.setColour (ScopeColours::textBright);
    g.drawText ("OSCILOSCOPIO", header, Justification::centredLeft);
    auto legend = header.withTrimmedLeft (110.0f);
    g.setColour (ScopeColours::left);
    g.drawText ("L", legend.removeFromLeft (16.0f), Justification::centredLeft);
    g.setColour (ScopeColours::right);
    g.drawText ("R", legend.removeFromLeft (16.0f), Justification::centredLeft);

    g.setFont (uiFont (12.0f));
    g.setColour (ScopeColours::text);
    g.drawText (divLabel, header, Justification::centredRight);
    if (status.isNotEmpty())
        g.drawText (status, header.withTrimmedLeft (160.0f).withTrimmedRight (130.0f), Justification::centred);
}

//==============================================================================
void GoniometerView::setData (const std::vector<float>& l, const std::vector<float>& r, float g)
{
    left = l; right = r; gain = g;

    double slr = 0, sll = 0, srr = 0;
    for (size_t i = 0; i < left.size(); ++i)
    {
        slr += (double) left[i] * right[i];
        sll += (double) left[i] * left[i];
        srr += (double) right[i] * right[i];
    }
    const double denom = std::sqrt (sll * srr);
    const float c = denom > 1.0e-9 ? (float) (slr / denom) : 0.0f;
    correlation = correlation * 0.8f + c * 0.2f;
    repaint();
}

void GoniometerView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b);

    auto area = b.reduced (10.0f, 8.0f);
    auto header = area.removeFromTop (16.0f);
    auto corrArea = area.removeFromBottom (28.0f);

    g.setFont (uiFont (12.0f, true));
    g.setColour (ScopeColours::textBright);
    g.drawText (u8 ("ESTÉREO"), header, Justification::centredLeft);

    const float size = jmin (area.getWidth(), area.getHeight());
    auto sq = Rectangle<float> (size, size).withCentre (area.getCentre());
    const auto c = sq.getCentre();
    const float rad = size * 0.5f - 6.0f;

    // Grilla: círculo + ejes L, R, M, S
    g.setColour (ScopeColours::grid);
    g.drawEllipse (Rectangle<float> (rad * 2, rad * 2).withCentre (c), 1.0f);
    g.drawEllipse (Rectangle<float> (rad, rad).withCentre (c), 1.0f);
    const float d = rad * 0.7071f;
    g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d);
    g.drawLine (c.x + d, c.y - d, c.x - d, c.y + d);
    g.setColour (ScopeColours::gridStrong);
    g.drawLine (c.x, c.y - rad, c.x, c.y + rad);
    g.drawLine (c.x - rad, c.y, c.x + rad, c.y);

    g.setFont (uiFont (11.0f, true));
    g.setColour (ScopeColours::text);
    g.drawText ("M", Rectangle<float> (20, 14).withCentre ({ c.x + 10.0f, c.y - rad + 7.0f }), Justification::centred);
    g.drawText ("L", Rectangle<float> (14, 14).withCentre ({ c.x - d - 2.0f, c.y - d - 2.0f }), Justification::centred);
    g.drawText ("R", Rectangle<float> (14, 14).withCentre ({ c.x + d + 2.0f, c.y - d - 2.0f }), Justification::centred);
    g.drawText ("S", Rectangle<float> (14, 14).withCentre ({ c.x + rad - 6.0f, c.y - 9.0f }), Justification::centred);

    // Puntos (los más nuevos más brillantes)
    const int n = (int) left.size();
    const int groups = 6;
    for (int grp = 0; grp < groups; ++grp)
    {
        const float a = 0.08f + 0.55f * (float) (grp + 1) / (float) groups;
        g.setColour (ScopeColours::mid.withAlpha (a));
        const int s0 = grp * n / groups, s1 = (grp + 1) * n / groups;
        RectangleList<float> dots;
        for (int i = s0; i < s1; ++i)
        {
            const float l = left[(size_t) i] * gain, r = right[(size_t) i] * gain;
            float x = (r - l) * 0.7071f, y = (l + r) * 0.7071f;
            const float len = std::sqrt (x * x + y * y);
            if (len > 1.0f) { x /= len; y /= len; }
            dots.addWithoutMerging ({ c.x + x * rad - 0.8f, c.y - y * rad - 0.8f, 1.6f, 1.6f });
        }
        g.fillRectList (dots);
    }

    // Medidor de correlación
    auto bar = corrArea.withTrimmedTop (8.0f).withTrimmedBottom (10.0f).reduced (14.0f, 0.0f);
    g.setColour (ScopeColours::grid);
    g.fillRoundedRectangle (bar, 2.0f);
    const float cx = bar.getCentreX();
    const float vx = cx + correlation * bar.getWidth() * 0.5f;
    g.setColour (correlation >= 0.0f ? ScopeColours::mid : ScopeColours::warn);
    g.fillRect (Rectangle<float>::leftTopRightBottom (jmin (cx, vx), bar.getY(), jmax (cx, vx), bar.getBottom()));
    g.setColour (ScopeColours::gridStrong);
    g.drawVerticalLine (roundToInt (cx), bar.getY() - 2.0f, bar.getBottom() + 2.0f);

    g.setFont (uiFont (10.0f));
    g.setColour (ScopeColours::text);
    auto labels = corrArea.withTrimmedTop (corrArea.getHeight() - 11.0f).reduced (8.0f, 0.0f);
    g.drawText ("-1", labels, Justification::centredLeft);
    g.drawText (u8 ("correlación ") + String (correlation, 2), labels, Justification::centred);
    g.drawText ("+1", labels, Justification::centredRight);
}

//==============================================================================
SpectrumView::SpectrumView()
    : fftData ((size_t) fftSize * 2, 0.0f),
      smoothed ((size_t) fftSize / 2 + 1, -120.0f),
      peaks ((size_t) fftSize / 2 + 1, -120.0f)
{
    // Ganancia coherente de la ventana Hann, para que un seno a 0 dBFS marque ~0 dB
    std::vector<float> ones ((size_t) fftSize, 1.0f);
    window.multiplyWithWindowingTable (ones.data(), (size_t) fftSize);
    float sum = 0.0f;
    for (auto v : ones) sum += v;
    windowGain = 2.0f / sum;
}

void SpectrumView::pushSamples (const float* mono, double sr)
{
    sampleRate = sr;
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (mono, mono + fftSize, fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    for (size_t i = 0; i < smoothed.size(); ++i)
    {
        const float db = Decibels::gainToDecibels (fftData[i] * windowGain, -120.0f);
        auto& s = smoothed[i];
        s += (db - s) * (db > s ? 0.6f : 0.12f);
        peaks[i] = jmax (db, peaks[i] - 0.35f);
    }
    repaint();
}

float SpectrumView::valueAt (const std::vector<float>& bins, float f0, float f1) const
{
    const float binHz = (float) sampleRate / (float) fftSize;
    const float b0 = f0 / binHz, b1 = f1 / binHz;
    const int last = (int) bins.size() - 1;

    if (b1 - b0 < 1.0f)
    {
        const float b = jlimit (0.0f, (float) last, (b0 + b1) * 0.5f);
        const int i = jmin ((int) b, last - 1);
        const float frac = b - (float) i;
        return bins[(size_t) i] + (bins[(size_t) i + 1] - bins[(size_t) i]) * frac;
    }

    float mx = -120.0f;
    for (int i = jmax (0, (int) b0); i <= jmin (last, (int) std::ceil (b1)); ++i)
        mx = jmax (mx, bins[(size_t) i]);
    return mx;
}

void SpectrumView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b);

    auto area = b.reduced (10.0f, 8.0f);
    auto header = area.removeFromTop (16.0f);
    auto freqLabels = area.removeFromBottom (14.0f);
    auto dbLabels = area.removeFromLeft (30.0f);
    freqLabels.removeFromLeft (30.0f);

    g.setFont (uiFont (12.0f, true));
    g.setColour (ScopeColours::textBright);
    g.drawText ("ESPECTRO", header, Justification::centredLeft);
    g.setFont (uiFont (11.0f));
    g.setColour (ScopeColours::text);
    g.drawText (u8 ("FFT 4096  ·  pendiente 4.5 dB/oct"), header, Justification::centredRight);

    constexpr float minF = 20.0f, maxF = 20000.0f, topDb = 6.0f, bottomDb = -90.0f;
    auto xOf = [&] (float f) { return area.getX() + area.getWidth() * std::log (f / minF) / std::log (maxF / minF); };
    auto yOf = [&] (float db) { return jmap (jlimit (bottomDb, topDb, db), topDb, bottomDb, area.getY(), area.getBottom()); };
    auto tilt = [] (float f) { return 4.5f * std::log2 (f / 1000.0f); };

    // Grilla
    g.setFont (uiFont (10.0f));
    for (float f : { 30.f, 40.f, 60.f, 70.f, 80.f, 90.f, 300.f, 400.f, 600.f, 700.f, 800.f, 900.f, 3000.f, 4000.f, 6000.f, 7000.f, 8000.f, 9000.f })
    {
        g.setColour (ScopeColours::grid.withAlpha (0.6f));
        g.drawVerticalLine (roundToInt (xOf (f)), area.getY(), area.getBottom());
    }
    for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
    {
        const float x = xOf (f);
        g.setColour (ScopeColours::grid);
        g.drawVerticalLine (roundToInt (x), area.getY(), area.getBottom());
        g.setColour (ScopeColours::text);
        const String label = f >= 1000.0f ? String (f / 1000.0f, 0) + "k" : String ((int) f);
        auto just = f <= 20.0f ? Justification::centredLeft : (f >= 20000.0f ? Justification::centredRight : Justification::centred);
        auto lr = Rectangle<float> (36.0f, freqLabels.getHeight()).withCentre ({ x, freqLabels.getCentreY() });
        if (f <= 20.0f) lr.setX (x);
        if (f >= 20000.0f) lr.setRight (x);
        g.drawText (label, lr, just);
    }
    for (float db = 0.0f; db >= bottomDb + 1.0f; db -= 12.0f)
    {
        const float y = yOf (db);
        g.setColour (std::abs (db) < 0.5f ? ScopeColours::gridStrong : ScopeColours::grid);
        g.drawHorizontalLine (roundToInt (y), area.getX(), area.getRight());
        g.setColour (ScopeColours::text);
        g.drawText (String ((int) db), Rectangle<float> (dbLabels.getX(), y - 6.0f, dbLabels.getWidth() - 4.0f, 12.0f), Justification::centredRight);
    }

    // Curvas
    const int cols = jmax (2, (int) area.getWidth());
    Path fill, line, peakLine;
    for (int c = 0; c < cols; ++c)
    {
        const float t0 = (float) c / (float) cols, t1 = (float) (c + 1) / (float) cols;
        const float f0 = minF * std::pow (maxF / minF, t0), f1 = minF * std::pow (maxF / minF, t1);
        const float fc = std::sqrt (f0 * f1);
        const float x = area.getX() + area.getWidth() * (float) c / (float) (cols - 1);
        const float y = yOf (valueAt (smoothed, f0, f1) + tilt (fc));
        const float yp = yOf (valueAt (peaks, f0, f1) + tilt (fc));
        if (c == 0)
        {
            fill.startNewSubPath (x, area.getBottom());
            fill.lineTo (x, y);
            line.startNewSubPath (x, y);
            peakLine.startNewSubPath (x, yp);
        }
        else
        {
            fill.lineTo (x, y);
            line.lineTo (x, y);
            peakLine.lineTo (x, yp);
        }
    }
    fill.lineTo (area.getRight(), area.getBottom());
    fill.closeSubPath();

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    g.setGradientFill (ColourGradient (ScopeColours::left.withAlpha (0.42f), 0.0f, area.getY(),
                                       ScopeColours::left.withAlpha (0.02f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);
    g.setColour (ScopeColours::textBright.withAlpha (0.28f));
    g.strokePath (peakLine, PathStrokeType (1.0f));
    g.setColour (ScopeColours::left.withAlpha (0.18f));
    g.strokePath (line, PathStrokeType (4.0f));
    g.setColour (ScopeColours::left);
    g.strokePath (line, PathStrokeType (1.5f));
    g.restoreState();
}

//==============================================================================
ScopeLabAudioProcessorEditor::ScopeLabAudioProcessorEditor (ScopeLabAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    timeParam   = proc.apvts.getRawParameterValue ("time");
    gainParam   = proc.apvts.getRawParameterValue ("gain");
    syncParam   = proc.apvts.getRawParameterValue ("sync");
    beatsParam  = proc.apvts.getRawParameterValue ("beats");
    splitParam  = proc.apvts.getRawParameterValue ("split");
    freezeParam = proc.apvts.getRawParameterValue ("freeze");

    addAndMakeVisible (wave);
    addAndMakeVisible (gonio);
    addAndMakeVisible (spectrum);

    auto setupLabel = [this] (Label& l, const String& t)
    {
        l.setText (t, dontSendNotification);
        l.setFont (uiFont (11.0f, true));
        l.setJustificationType (Justification::bottomLeft);
        addAndMakeVisible (l);
    };
    setupLabel (syncLabel,  u8 ("SINCRONÍA"));
    setupLabel (timeLabel,  "VENTANA");
    setupLabel (beatsLabel, u8 ("DURACIÓN (TEMPO)"));
    setupLabel (gainLabel,  "ZOOM VERTICAL");

    syncBox.addItemList ({ "Libre", "Trigger", "Tempo del DAW" }, 1);
    beatsBox.addItemList ({ "1/4 tiempo", "1/2 tiempo", "1 tiempo", "2 tiempos", u8 ("1 compás") }, 1);
    addAndMakeVisible (syncBox);
    addAndMakeVisible (beatsBox);

    for (auto* s : { &timeSlider, &gainSlider })
    {
        s->setSliderStyle (Slider::LinearHorizontal);
        s->setTextBoxStyle (Slider::TextBoxRight, false, 64, 20);
        s->setColour (Slider::textBoxOutlineColourId, ScopeColours::gridStrong);
        addAndMakeVisible (*s);
    }

    for (auto* btn : { &splitButton, &freezeButton })
    {
        btn->setClickingTogglesState (true);
        addAndMakeVisible (*btn);
    }

    syncAtt   = std::make_unique<ComboBoxAttachment> (proc.apvts, "sync",  syncBox);
    beatsAtt  = std::make_unique<ComboBoxAttachment> (proc.apvts, "beats", beatsBox);
    timeAtt   = std::make_unique<SliderAttachment>   (proc.apvts, "time",  timeSlider);
    gainAtt   = std::make_unique<SliderAttachment>   (proc.apvts, "gain",  gainSlider);
    splitAtt  = std::make_unique<ButtonAttachment>   (proc.apvts, "split",  splitButton);
    freezeAtt = std::make_unique<ButtonAttachment>   (proc.apvts, "freeze", freezeButton);

    setResizable (true, true);
    setResizeLimits (720, 460, 2400, 1500);
    setSize (980, 620);

    startTimerHz (60);
}

ScopeLabAudioProcessorEditor::~ScopeLabAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ScopeLabAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (10);
    headerArea = b.removeFromTop (40);
    controlsArea = b.removeFromBottom (52);
    b.removeFromBottom (8);

    auto top = b.removeFromTop (roundToInt ((float) b.getHeight() * 0.58f));
    b.removeFromTop (8);
    gonio.setBounds (top.removeFromRight (top.getHeight()));
    top.removeFromRight (8);
    wave.setBounds (top);
    spectrum.setBounds (b);

    auto c = controlsArea;
    auto column = [&c] (int w) { auto r = c.removeFromLeft (w); c.removeFromLeft (14); return r; };
    auto place = [] (Rectangle<int> col, Label& l, Component& comp)
    {
        l.setBounds (col.removeFromTop (18));
        comp.setBounds (col.removeFromTop (28));
    };

    place (column (140), syncLabel, syncBox);
    place (column (200), timeLabel, timeSlider);
    place (column (130), beatsLabel, beatsBox);
    place (column (180), gainLabel, gainSlider);

    auto buttons = c.withTrimmedTop (18).withHeight (28);
    freezeButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    splitButton.setBounds (buttons.removeFromRight (130));
}

void ScopeLabAudioProcessorEditor::drawMeter (Graphics& g, Rectangle<float> r, float level, Colour colour, const String& name)
{
    const float db = Decibels::gainToDecibels (level, -60.0f);
    g.setFont (uiFont (11.0f, true));
    g.setColour (colour);
    g.drawText (name, r.removeFromLeft (14.0f), Justification::centredLeft);

    auto valueArea = r.removeFromRight (58.0f);
    g.setColour (db > -0.1f ? ScopeColours::warn : ScopeColours::textBright);
    g.setFont (uiFont (12.0f));
    g.drawText (level > 0.0f ? String (db, 1) + " dB" : "-inf", valueArea, Justification::centredRight);

    auto bar = r.reduced (0.0f, r.getHeight() * 0.5f - 3.0f);
    g.setColour (ScopeColours::grid);
    g.fillRoundedRectangle (bar, 2.0f);
    const float frac = jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, frac)), 2.0f);
}

void ScopeLabAudioProcessorEditor::paint (Graphics& g)
{
    g.fillAll (ScopeColours::bg);

    auto h = headerArea.toFloat();
    g.setColour (ScopeColours::textBright);
    g.setFont (uiFont (20.0f, true));
    g.drawText ("SCOPE", h.removeFromLeft (68.0f), Justification::centredLeft);
    g.setColour (ScopeColours::left);
    g.drawText ("LAB", h.removeFromLeft (52.0f), Justification::centredLeft);
    g.setColour (ScopeColours::text);
    g.setFont (uiFont (12.0f));
    g.drawText (u8 ("osciloscopio  ·  estéreo  ·  espectro"), h.removeFromLeft (260.0f), Justification::centredLeft);

    auto meters = headerArea.toFloat().removeFromRight (420.0f);
    drawMeter (g, meters.removeFromLeft (200.0f).reduced (0, 8), holdL, ScopeColours::left, "L");
    meters.removeFromLeft (20.0f);
    drawMeter (g, meters.reduced (0, 8), holdR, ScopeColours::right, "R");

    if (freezeParam->load() > 0.5f)
    {
        auto badge = Rectangle<float> (96.0f, 22.0f).withCentre (headerArea.toFloat().getCentre().translated (40.0f, 0.0f));
        g.setColour (ScopeColours::warn.withAlpha (0.2f));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (ScopeColours::warn);
        g.setFont (uiFont (11.0f, true));
        g.drawText ("CONGELADO", badge, Justification::centred);
    }
}

void ScopeLabAudioProcessorEditor::refresh()
{
    const double sr   = proc.getScopeSampleRate();
    const float  gain = Decibels::decibelsToGain (gainParam->load());
    const int    sync = roundToInt (syncParam->load());
    const bool   frozen = freezeParam->load() > 0.5f;

    timeSlider.setEnabled (sync != 2);
    beatsBox.setEnabled (sync == 2);

    // Medidores con retención de pico
    auto updateMeter = [] (float peak, float& hold, int& counter)
    {
        if (peak >= hold) { hold = peak; counter = 45; }
        else if (counter > 0) --counter;
        else hold *= 0.94f;
    };
    updateMeter (proc.peakLeft.exchange (0.0f),  holdL, holdCounterL);
    updateMeter (proc.peakRight.exchange (0.0f), holdR, holdCounterR);
    repaint (headerArea);

    if (frozen)
        return;

    const int maxWindow = ScopeRingBuffer::capacity / 2 - 1;
    const int64_t end = proc.scope.getWritePosition();
    int W = jlimit (16, maxWindow, roundToInt (timeParam->load() * sr / 1000.0));

    String status, divLabel;
    int divisions = 10;
    float sweep = -1.0f;
    bool done = false;

    if (sync == 2)
    {
        const auto t = proc.getTransport();
        if (t.playing && t.bpm > 0.0)
        {
            static constexpr double beatChoices[] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
            const double bw  = beatChoices[jlimit (0, 4, roundToInt (beatsParam->load()))];
            const double spb = sr * 60.0 / t.bpm;
            W = jlimit (16, maxWindow, (int) std::llround (spb * bw));

            const double ppqEnd = t.ppq + (double) (end - t.sample) / spb;
            const double wsPpq  = std::floor (ppqEnd / bw) * bw;
            const int64_t wsSample = t.sample + (int64_t) std::llround ((wsPpq - t.ppq) * spb);

            bufL.resize ((size_t) W); bufR.resize ((size_t) W);
            dispL.resize ((size_t) W); dispR.resize ((size_t) W);
            proc.scope.read (end, W, bufL.data(), bufR.data());
            const int64_t base = end - W;

            // Barrido tipo osciloscopio real: lo nuevo pisa a lo anterior
            for (int k = 0; k < W; ++k)
            {
                int64_t idx = wsSample + k;
                if (idx >= end) idx -= W;
                const int local = jlimit (0, W - 1, (int) (idx - base));
                dispL[(size_t) k] = bufL[(size_t) local];
                dispR[(size_t) k] = bufR[(size_t) local];
            }

            sweep = jlimit (0.0f, 1.0f, (float) (end - wsSample) / (float) W);
            divisions = jmax (4, roundToInt (bw * 4.0));
            divLabel = u8 ("1/16 por div  ·  ") + String (t.bpm, 1) + " BPM";
            done = true;
        }
        else
        {
            status = "DAW detenido: usando Trigger";
        }
    }

    if (! done && sync >= 1)
    {
        // Trigger: busca el último cruce por cero ascendente de la señal filtrada (graves)
        const int R = W * 2;
        bufL.resize ((size_t) R); bufR.resize ((size_t) R); mono.resize ((size_t) R);
        proc.scope.read (end, R, bufL.data(), bufR.data());

        const float a = 1.0f - std::exp (-MathConstants<float>::twoPi * 180.0f / (float) sr);
        float lp = 0.0f, energy = 0.0f;
        for (int i = 0; i < R; ++i)
        {
            lp += a * ((bufL[(size_t) i] + bufR[(size_t) i]) * 0.5f - lp);
            mono[(size_t) i] = lp;
            energy = jmax (energy, std::abs (lp));
        }

        const float hyst = energy * 0.05f;
        int start = W;
        for (int t = W; t > 1; --t)
        {
            if (mono[(size_t) t - 1] <= 0.0f && mono[(size_t) t] > 0.0f)
            {
                // histéresis: exige que la señal haya bajado de verdad antes del cruce
                bool valid = false;
                for (int k = t - 1; k >= jmax (0, t - W / 2); --k)
                    if (mono[(size_t) k] < -hyst) { valid = true; break; }
                    else if (mono[(size_t) k] > hyst) break;
                if (valid) { start = t; break; }
            }
        }

        dispL.assign (bufL.begin() + start, bufL.begin() + start + W);
        dispR.assign (bufR.begin() + start, bufR.begin() + start + W);
        done = true;
    }

    if (! done)
    {
        dispL.resize ((size_t) W); dispR.resize ((size_t) W);
        proc.scope.read (end, W, dispL.data(), dispR.data());
    }

    if (sync != 2 || divLabel.isEmpty())
        divLabel = formatSeconds ((double) W / sr / divisions) + " por div";

    wave.setData (dispL, dispR, gain, splitParam->load() > 0.5f, divisions, divLabel, sweep, status);

    // Goniómetro: últimas ~43 ms
    const int gn = 2048;
    gL.resize ((size_t) gn); gR.resize ((size_t) gn);
    proc.scope.read (end, gn, gL.data(), gR.data());
    gonio.setData (gL, gR, gain);

    // Espectro: suma mono de las últimas 4096 muestras
    specMono.resize ((size_t) SpectrumView::fftSize); specR.resize ((size_t) SpectrumView::fftSize);
    proc.scope.read (end, SpectrumView::fftSize, specMono.data(), specR.data());
    for (size_t i = 0; i < specMono.size(); ++i)
        specMono[i] = (specMono[i] + specR[i]) * 0.5f;
    spectrum.pushSamples (specMono.data(), sr);
}
