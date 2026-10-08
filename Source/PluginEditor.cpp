#include "PluginEditor.h"

using namespace juce;

namespace
{
    Font uiFont (float size, bool bold = false)
    {
        return Font (FontOptions (size, bold ? Font::bold : Font::plain));
    }

    String u8 (const char* text) { return String::fromUTF8 (text); }

    void drawPanel (Graphics& g, Rectangle<float> b)
    {
        g.setColour (ScopeColours::panel);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (ScopeColours::panelEdge);
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
    }

    String formatSeconds (double s)
    {
        if (s < 0.001)  return String (s * 1.0e6, 0) + " us";
        if (s < 0.01)   return String (s * 1000.0, 2) + " ms";
        if (s < 1.0)    return String (s * 1000.0, 1) + " ms";
        return String (s, 2) + " s";
    }

    String signedValue (float v) { if (std::abs (v) < 0.005f) v = 0.0f; return (v >= 0.0f ? "+" : "") + String (v, 2); }

    Colour correlationColour (float c)
    {
        if (c < -0.1f) return ScopeColours::warn;
        if (c < 0.3f)  return ScopeColours::caution;
        return ScopeColours::mid;
    }

    // Ícono de ampliar / reducir (dos flechas en diagonal)
    void drawExpandIcon (Graphics& g, Rectangle<float> r, bool expanded)
    {
        g.setColour (ScopeColours::text);
        const auto c = r.getCentre();
        const float d = r.getWidth() * 0.5f - 2.0f, a = 4.0f;
        Path p;
        if (! expanded)
        {
            p.startNewSubPath (c.x + d - a, c.y - d); p.lineTo (c.x + d, c.y - d); p.lineTo (c.x + d, c.y - d + a);
            p.startNewSubPath (c.x - d + a, c.y + d); p.lineTo (c.x - d, c.y + d); p.lineTo (c.x - d, c.y + d - a);
            p.startNewSubPath (c.x + d, c.y - d); p.lineTo (c.x + 1.5f, c.y - 1.5f);
            p.startNewSubPath (c.x - d, c.y + d); p.lineTo (c.x - 1.5f, c.y + 1.5f);
        }
        else
        {
            p.startNewSubPath (c.x + 1.5f, c.y - 1.5f - a); p.lineTo (c.x + 1.5f, c.y - 1.5f); p.lineTo (c.x + 1.5f + a, c.y - 1.5f);
            p.startNewSubPath (c.x - 1.5f, c.y + 1.5f + a); p.lineTo (c.x - 1.5f, c.y + 1.5f); p.lineTo (c.x - 1.5f - a, c.y + 1.5f);
            p.startNewSubPath (c.x + d, c.y - d); p.lineTo (c.x + 1.5f, c.y - 1.5f);
            p.startNewSubPath (c.x - d, c.y + d); p.lineTo (c.x - 1.5f, c.y + 1.5f);
        }
        g.strokePath (p, PathStrokeType (1.4f, PathStrokeType::mitered, PathStrokeType::rounded));
    }

    String formatHz (float f)
    {
        if (f < 100.0f)  return String (f, 1) + " Hz";
        if (f < 1000.0f) return String (roundToInt (f)) + " Hz";
        return String (f / 1000.0f, 2) + " kHz";
    }

    // Nota con la convención de Ableton (Do central = C3) y desvío en cents
    String noteName (float f)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const float midi = 69.0f + 12.0f * std::log2 (f / 440.0f);
        const int n = roundToInt (midi);
        const int cents = roundToInt ((midi - (float) n) * 100.0f);
        String s = String (names[((n % 12) + 12) % 12]) + String (n / 12 - 2);
        if (cents != 0) s << "  " << (cents > 0 ? "+" : "") << cents << u8 ("¢");
        return s;
    }

    String formatDb (float db)
    {
        if (std::abs (db) < 0.05f) return "0";
        const bool whole = std::abs (db - (float) roundToInt (db)) < 0.15f;   // -6.02 -> "-6"
        if (whole) db = (float) roundToInt (db);
        return (db > 0.0f ? "+" : "") + (whole ? String (roundToInt (db)) : String (db, 1));
    }

    constexpr float specMinF = 20.0f, specMaxF = 20000.0f, specTopDb = 6.0f, specBottomDb = -90.0f;
    float specTilt (float f) { return 4.5f * std::log2 (f / 1000.0f); }

    // Filtro pasa-bajos de 2 polos (aprox. 150 Hz) para medir la fase en graves
    void lowpass (const std::vector<float>& in, std::vector<float>& out, double sr, float cutoff)
    {
        out.resize (in.size());
        const float a = 1.0f - std::exp (-MathConstants<float>::twoPi * cutoff / (float) sr);
        float s1 = 0.0f, s2 = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
        {
            s1 += a * (in[i] - s1);
            s2 += a * (s1 - s2);
            out[i] = s2;
        }
    }

    bool computeCorrelation (const std::vector<float>& a, const std::vector<float>& b, size_t skip, float& result)
    {
        double sab = 0, saa = 0, sbb = 0;
        for (size_t i = skip; i < a.size() && i < b.size(); ++i)
        {
            sab += (double) a[i] * b[i];
            saa += (double) a[i] * a[i];
            sbb += (double) b[i] * b[i];
        }
        const auto n = (double) jmax ((size_t) 1, a.size() - skip);
        if (saa / n < 1.0e-8 || sbb / n < 1.0e-8)   // alguna de las dos está en silencio
            return false;
        result = (float) (sab / std::sqrt (saa * sbb));
        return true;
    }
}

//==============================================================================
ScopeLookAndFeel::ScopeLookAndFeel()
{
    using namespace ScopeColours;
    setColour (ResizableWindow::backgroundColourId, bg);
    setColour (Label::textColourId, text);
    setColour (Slider::textBoxTextColourId, textBright);
    setColour (Slider::textBoxOutlineColourId, gridStrong);
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
    g.setColour (ScopeColours::left.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);
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
void WaveformView::setData (std::vector<ScopeSeries> newSeries, std::vector<std::vector<int>> newLanes, float g,
                            int divs, const String& label, float sw, const String& st, bool showLRLegend)
{
    series = std::move (newSeries);
    lanes = std::move (newLanes);
    gain = g; divisions = jmax (1, divs);
    divLabel = label; sweep = sw; status = st; showLegend = showLRLegend;
    repaint();
}

void WaveformView::drawSeries (Graphics& g, Rectangle<float> lane, const ScopeSeries& s)
{
    const auto& data = s.data;
    const int n = (int) data.size();
    if (n < 2) return;

    const float w = lane.getWidth();
    const float cy = lane.getCentreY();
    const float halfH = lane.getHeight() * 0.5f * 0.92f;
    auto yOf = [&] (float v) { return cy - jlimit (-1.0f, 1.0f, v * gain) * halfH; };

    const float glowAlpha = s.isSum ? 0.0f : 0.15f;
    const float lineWidth = s.isSum ? 1.1f : 1.5f;
    const auto colour = s.isSum ? ScopeColours::textBright.withAlpha (0.8f) : s.colour;

    if ((float) n <= w * 1.5f)
    {
        Path p;
        for (int i = 0; i < n; ++i)
        {
            const float x = lane.getX() + w * (float) i / (float) (n - 1);
            if (i == 0) p.startNewSubPath (x, yOf (data[0]));
            else        p.lineTo (x, yOf (data[(size_t) i]));
        }
        if (glowAlpha > 0.0f)
        {
            g.setColour (colour.withAlpha (glowAlpha));
            g.strokePath (p, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        g.setColour (colour);
        g.strokePath (p, PathStrokeType (lineWidth, PathStrokeType::curved, PathStrokeType::rounded));
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
        for (int i = s0; i < s1 && i < n; ++i)
        {
            mn = jmin (mn, data[(size_t) i]);
            mx = jmax (mx, data[(size_t) i]);
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

    if (! s.isSum)
    {
        g.setColour (colour.withAlpha (0.26f));
        g.fillPath (env);
        g.setColour (colour.withAlpha (glowAlpha));
        g.strokePath (top, PathStrokeType (4.0f));
        g.strokePath (bottom, PathStrokeType (4.0f));
    }
    g.setColour (colour.withAlpha (0.95f));
    g.strokePath (top, PathStrokeType (s.isSum ? 1.0f : 1.3f));
    g.strokePath (bottom, PathStrokeType (s.isSum ? 1.0f : 1.3f));
}

void WaveformView::drawLane (Graphics& g, Rectangle<float> lane, const std::vector<int>& indices)
{
    auto axis = lane.removeFromLeft (dbAxisWidth);
    const float cy = lane.getCentreY();
    const float halfH = lane.getHeight() * 0.5f * 0.92f;   // igual que en drawSeries

    // Grilla vertical (tiempo)
    for (int i = 1; i < divisions; ++i)
    {
        const float x = lane.getX() + lane.getWidth() * (float) i / (float) divisions;
        g.setColour (divisions >= 8 && i % 4 == 0 ? ScopeColours::gridStrong : ScopeColours::grid);
        g.drawVerticalLine (roundToInt (x), lane.getY(), lane.getBottom());
    }

    // Grilla horizontal en dB: 1 = pico a 0 dBFS con zoom 0; cada línea es la mitad (-6 dB)
    g.setFont (uiFont (10.0f));
    float lastLabelY = -1000.0f;
    for (float frac : { 1.0f, 0.5f, 0.25f, 0.125f })
    {
        const float off = frac * halfH;
        if (off < 24.0f) break;
        const float db = Decibels::gainToDecibels (frac / gain);
        for (float sign : { 1.0f, -1.0f })
        {
            const float y = cy - sign * off;
            g.setColour (frac > 0.9f ? ScopeColours::gridStrong.withAlpha (0.7f) : ScopeColours::grid);
            g.drawHorizontalLine (roundToInt (y), lane.getX(), lane.getRight());
        }
        // etiqueta solo arriba del centro, si no se pisa con la anterior
        const float y = cy - off;
        if (std::abs (y - lastLabelY) >= 11.0f)
        {
            g.setColour (ScopeColours::text);
            g.drawText (formatDb (db), Rectangle<float> (axis.getX(), y - 6.0f, axis.getWidth() - 4.0f, 12.0f),
                        Justification::centredRight);
            if (lane.getHeight() > 150.0f)
                g.drawText (formatDb (db), Rectangle<float> (axis.getX(), cy + off - 6.0f, axis.getWidth() - 4.0f, 12.0f),
                            Justification::centredRight);
            lastLabelY = y;
        }
    }
    g.setColour (ScopeColours::gridStrong);
    g.drawHorizontalLine (roundToInt (cy), lane.getX(), lane.getRight());

    g.saveState();
    g.reduceClipRegion (lane.toNearestInt());
    for (auto idx : indices)
        if (isPositiveAndBelow (idx, (int) series.size()))
            drawSeries (g, lane, series[(size_t) idx]);
    g.restoreState();

    // Etiqueta de la pista cuando hay una por carril
    if (indices.size() == 1 && lanes.size() > 1 && isPositiveAndBelow (indices[0], (int) series.size()))
    {
        const auto& s = series[(size_t) indices[0]];
        g.setFont (uiFont (11.0f, true));
        g.setColour (s.colour.withAlpha (0.9f));
        g.drawText (s.label, lane.reduced (4.0f, 2.0f), Justification::topLeft);
    }
}

void WaveformView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b);
    const auto fullArea = b.reduced (10.0f, 8.0f).withTrimmedTop (18.0f);
    const auto plotArea = fullArea.withTrimmedLeft (dbAxisWidth);

    const int numLanes = jmax (1, (int) lanes.size());
    const float gap = numLanes > 1 ? 4.0f : 0.0f;
    const float laneH = (fullArea.getHeight() - gap * (float) (numLanes - 1)) / (float) numLanes;
    for (int i = 0; i < (int) lanes.size(); ++i)
        drawLane (g, Rectangle<float> (fullArea.getX(), fullArea.getY() + (float) i * (laneH + gap), fullArea.getWidth(), laneH),
                  lanes[(size_t) i]);

    if (sweep >= 0.0f)
    {
        const float x = plotArea.getX() + plotArea.getWidth() * sweep;
        g.setColour (Colours::black.withAlpha (0.28f));
        g.fillRect (Rectangle<float>::leftTopRightBottom (x, plotArea.getY(), plotArea.getRight(), plotArea.getBottom()));
        g.setColour (ScopeColours::textBright.withAlpha (0.55f));
        g.drawVerticalLine (roundToInt (x), plotArea.getY(), plotArea.getBottom());
    }

    auto header = b.reduced (12.0f, 6.0f).removeFromTop (16.0f);
    expandIcon = header.removeFromRight (16.0f);
    drawExpandIcon (g, expandIcon, expanded);
    header.removeFromRight (10.0f);

    g.setFont (uiFont (12.0f, true));
    g.setColour (ScopeColours::textBright);
    g.drawText ("OSCILOSCOPIO", header, Justification::centredLeft);
    g.setFont (uiFont (10.0f));
    g.setColour (ScopeColours::text.withAlpha (0.7f));
    g.drawText ("dB", Rectangle<float> (fullArea.getX(), fullArea.getBottom() - 12.0f, dbAxisWidth - 4.0f, 12.0f),
                Justification::centredRight);

    auto legend = header.withTrimmedLeft (110.0f);
    g.setFont (uiFont (12.0f, true));
    if (showLegend)
    {
        for (auto& s : series)
        {
            g.setColour (s.colour);
            g.drawText (s.label, legend.removeFromLeft (18.0f), Justification::centredLeft);
        }
    }
    else if (series.size() > 2 || (series.size() == 2 && lanes.size() == 1))
    {
        g.setColour (ScopeColours::textBright.withAlpha (0.8f));
        g.setFont (uiFont (11.0f));
        g.drawText ("blanco = suma", legend.removeFromLeft (100.0f), Justification::centredLeft);
    }

    g.setFont (uiFont (12.0f));
    g.setColour (ScopeColours::text);
    g.drawText (divLabel, header, Justification::centredRight);
    if (status.isNotEmpty())
    {
        g.setColour (ScopeColours::caution);
        g.drawText (status, header.withTrimmedLeft (220.0f).withTrimmedRight (190.0f), Justification::centred);
    }
}

void WaveformView::mouseDown (const MouseEvent& e)
{
    if (expandIcon.expanded (4.0f).contains (e.position) && onExpandToggle)
        onExpandToggle();
}

void WaveformView::mouseDoubleClick (const MouseEvent& e)
{
    if (! expandIcon.expanded (4.0f).contains (e.position) && onExpandToggle)
        onExpandToggle();
}

//==============================================================================
void GoniometerView::setData (const std::vector<float>& l, const std::vector<float>& r, float g)
{
    left = l; right = r; gain = g;
    float c = 0.0f;
    if (! computeCorrelation (left, right, 0, c)) c = 0.0f;
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

    const int n = (int) left.size();
    const int groups = 6;
    for (int grp = 0; grp < groups; ++grp)
    {
        g.setColour (ScopeColours::mid.withAlpha (0.08f + 0.55f * (float) (grp + 1) / (float) groups));
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
    g.drawText (u8 ("correlación L/R ") + String (correlation, 2), labels, Justification::centred);
    g.drawText ("+1", labels, Justification::centredRight);
}

//==============================================================================
void PhaseView::setRows (std::vector<Row> newRows, const String& rn, Colour rc, const String& msg, bool isThis)
{
    rows = std::move (newRows);
    refName = rn; refColour = rc; message = msg; refIsThis = isThis;
    repaint();
}

void PhaseView::mouseDown (const MouseEvent& e)
{
    if (! onSelectReference)
        return;
    if (refBounds.contains (e.position) && ! refIsThis)
    {
        onSelectReference (0);
        return;
    }
    for (size_t i = 0; i < rowBounds.size() && i < rows.size(); ++i)
        if (rowBounds[i].contains (e.position))
        {
            onSelectReference (rows[i].key);
            return;
        }
}

void PhaseView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b);
    auto area = b.reduced (12.0f, 8.0f);
    rowBounds.clear();

    g.setFont (uiFont (12.0f, true));
    g.setColour (ScopeColours::textBright);
    g.drawText ("FASE ENTRE PISTAS", area.removeFromTop (16.0f), Justification::centredLeft);

    // Referencia actual (clic = volver a esta pista)
    refBounds = area.removeFromTop (18.0f);
    {
        auto sub = refBounds;
        g.setFont (uiFont (11.0f));
        g.setColour (ScopeColours::text);
        g.drawText ("comparado con", sub.removeFromLeft (86.0f), Justification::centredLeft);
        g.setColour (refColour);
        g.setFont (uiFont (11.0f, true));
        const String shown = refName + (refIsThis ? u8 ("  (esta)") : String());
        g.drawText (shown, sub, Justification::centredLeft);
        if (! refIsThis)
        {
            g.setFont (uiFont (10.0f));
            g.setColour (ScopeColours::text);
            g.drawText (u8 ("volver a esta ×"), sub, Justification::centredRight);
        }
    }

    auto footer = area.removeFromBottom (30.0f);
    g.setFont (uiFont (10.0f));
    g.setColour (ScopeColours::text);
    g.drawFittedText (u8 ("clic en una pista = compararlas contra ella\nTrack Delay: botón D del mezclador de Ableton"),
                      footer.toNearestInt(), Justification::bottomLeft, 2);

    area.removeFromTop (4.0f);
    if (rows.empty() || message.isNotEmpty())
    {
        g.setFont (uiFont (12.0f));
        g.setColour (message.isNotEmpty() ? ScopeColours::caution : ScopeColours::text);
        g.drawFittedText (message.isNotEmpty() ? message : u8 ("Poné ScopeLab en otra pista para compararla."),
                          area.reduced (0, 10).toNearestInt(), Justification::centredTop, 3);
        if (rows.empty())
            return;
        area.removeFromTop (40.0f);
    }

    const float rowH = jmin (98.0f, area.getHeight() / (float) rows.size());
    for (auto& row : rows)
    {
        auto r = area.removeFromTop (rowH);
        rowBounds.push_back (r);
        auto line1 = r.removeFromTop (16.0f);
        g.setColour (row.colour);
        g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre ({ line1.getX() + 4.0f, line1.getCentreY() }));
        line1.removeFromLeft (14.0f);
        g.setFont (uiFont (12.0f, true));
        g.setColour (ScopeColours::textBright);
        g.drawText (row.name, line1, Justification::centredLeft);

        g.setFont (uiFont (11.0f));
        if (! row.hasSignal)
        {
            g.setColour (ScopeColours::text);
            g.drawText (row.overlapPct <= 0.0f ? u8 ("no suenan juntas") : u8 ("midiendo…"), line1, Justification::centredRight);
            continue;
        }

        g.setColour (correlationColour (row.lowCorr));
        g.drawText ("graves " + signedValue (row.lowCorr), line1, Justification::centredRight);

        auto bar = r.removeFromTop (8.0f).withTrimmedTop (2.0f);
        g.setColour (ScopeColours::grid);
        g.fillRoundedRectangle (bar, 2.0f);
        const float cx = bar.getCentreX();
        const float vx = cx + jlimit (-1.0f, 1.0f, row.lowCorr) * bar.getWidth() * 0.5f;
        g.setColour (correlationColour (row.lowCorr));
        g.fillRect (Rectangle<float>::leftTopRightBottom (jmin (cx, vx), bar.getY(), jmax (cx, vx), bar.getBottom()));
        g.setColour (ScopeColours::gridStrong);
        g.drawVerticalLine (roundToInt (cx), bar.getY() - 2.0f, bar.getBottom() + 2.0f);

        auto info = r.removeFromTop (13.0f);
        g.setColour (ScopeColours::text);
        g.setFont (uiFont (10.0f));
        g.drawText ("suenan juntas " + String (roundToInt (row.overlapPct)) + "% del tiempo", info, Justification::centredLeft);
        g.drawText ("todo el rango " + signedValue (row.fullCorr), info, Justification::centredRight);

        // Alineación sugerida
        String text;
        Colour colour = ScopeColours::text;
        if (! row.alignValid)
        {
            text = u8 ("alineación: sin relación clara en graves");
        }
        else
        {
            const float ms = std::abs (row.lagMs);
            const String msText = String (ms, ms < 10.0f ? 1 : 0) + " ms";
            if (ms < 0.25f)
            {
                text = u8 ("alineadas en el tiempo ✓");
                colour = ScopeColours::mid;
            }
            else
            {
                text = (row.lagMs > 0.0f ? "llega " + msText + " tarde  " : "llega " + msText + " antes  ")
                     + u8 ("→  Track Delay ") + (row.lagMs > 0.0f ? "-" : "+") + msText;
                colour = ScopeColours::caution;
            }
            if (row.alignCorr < 0.0f)
            {
                text << "\n" << u8 ("y está con la fase invertida: probá Utility → Ø");
                colour = ScopeColours::warn;
            }
        }
        g.setColour (colour);
        g.setFont (uiFont (11.0f, true));
        g.drawFittedText (text, r.withTrimmedTop (2.0f).toNearestInt(), Justification::topLeft, 2, 0.9f);
    }
}

//==============================================================================
void TrackBar::setChips (std::vector<Chip> newChips, const String& newHint)
{
    chips = std::move (newChips);
    hint = newHint;
    repaint();
}

void TrackBar::paint (Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setFont (uiFont (11.0f, true));
    g.setColour (ScopeColours::text);
    g.drawText ("PISTAS", area.removeFromLeft (52.0f), Justification::centredLeft);

    chipBounds.clear();
    g.setFont (uiFont (12.0f));
    for (auto& c : chips)
    {
        const String label = c.name + (c.isThis ? u8 ("  (esta)") : String());
        const float w = jmin (220.0f, GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 34.0f);
        if (w > area.getWidth()) break;
        auto r = area.removeFromLeft (w).reduced (0.0f, 2.0f);
        area.removeFromLeft (6.0f);
        chipBounds.push_back (r);

        g.setColour (c.visible ? c.colour.withAlpha (0.16f) : ScopeColours::panel);
        g.fillRoundedRectangle (r, 11.0f);
        g.setColour (c.visible ? c.colour.withAlpha (0.8f) : ScopeColours::gridStrong);
        g.drawRoundedRectangle (r.reduced (0.5f), 11.0f, 1.0f);

        auto dot = Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 13.0f, r.getCentreY() });
        if (c.visible) { g.setColour (c.colour); g.fillEllipse (dot); }
        else           { g.setColour (ScopeColours::text); g.drawEllipse (dot, 1.0f); }

        g.setColour (c.visible ? ScopeColours::textBright : ScopeColours::text);
        g.drawText (label, r.withTrimmedLeft (24.0f).withTrimmedRight (8.0f), Justification::centredLeft, true);
    }

    if (hint.isNotEmpty() && area.getWidth() > 60.0f)
    {
        g.setFont (uiFont (11.0f));
        g.setColour (ScopeColours::text);
        g.drawText (hint, area, Justification::centredRight, true);
    }
}

void TrackBar::mouseDown (const MouseEvent& e)
{
    for (size_t i = 0; i < chipBounds.size() && i < chips.size(); ++i)
        if (chipBounds[i].contains (e.position) && onToggle)
            onToggle (chips[i].key);
}

//==============================================================================
SpectrumView::SpectrumView()
    : fftData ((size_t) fftSize * 2, 0.0f)
{
    std::vector<float> ones ((size_t) fftSize, 1.0f);
    window.multiplyWithWindowingTable (ones.data(), (size_t) fftSize);
    float sum = 0.0f;
    for (auto v : ones) sum += v;
    windowGain = 2.0f / sum;   // un seno a 0 dBFS marca ~0 dB

    // Paleta de la cascada: oscuro -> azul -> cian -> verde -> amarillo -> rojo
    const std::pair<float, Colour> stops[] = { { 0.0f, ScopeColours::bg }, { 0.3f, Colour (0xff1c2a72) },
                                               { 0.55f, Colour (0xff1fa7d6) }, { 0.75f, Colour (0xffb6ff5c) },
                                               { 0.9f, Colour (0xffffc84a) }, { 1.0f, Colour (0xffff4f6a) } };
    for (int i = 0; i < 256; ++i)
    {
        const float t = (float) i / 255.0f;
        int k = 0;
        while (k < 4 && t > stops[k + 1].first) ++k;
        const float u = (t - stops[k].first) / (stops[k + 1].first - stops[k].first);
        lut[(size_t) i] = stops[k].second.interpolatedWith (stops[k + 1].second, jlimit (0.0f, 1.0f, u));
    }
}

void SpectrumView::setOptions (bool msAvail, bool msEnabled, bool spectrogramEnabled,
                               int newMode, const String& trigName, const String& newStatus)
{
    msAvailable = msAvail;
    msOn = msEnabled;
    mode = newMode;
    triggerName = trigName;
    status = newStatus;
    if (spectroOn != spectrogramEnabled)
        spectroImage = {};   // empezar la cascada limpia
    spectroOn = spectrogramEnabled;
}

void SpectrumView::beginFrame (double sr, bool showCollisions)
{
    sampleRate = sr;
    collisions = showCollisions;
    order.clear();
    for (auto& [key, c] : curves)
        c.touched = false;
}

SpectrumView::Curve& SpectrumView::curveFor (int key, Colour colour, const String& name, bool isSum)
{
    auto& curve = curves[key];
    if (curve.smoothed.empty())
    {
        curve.smoothed.assign ((size_t) fftSize / 2 + 1, -120.0f);
        curve.peaks.assign ((size_t) fftSize / 2 + 1, -120.0f);
        curve.average.assign ((size_t) fftSize / 2 + 1, -120.0f);
    }
    curve.colour = colour;
    curve.name = name;
    curve.isSum = isSum;
    curve.touched = true;
    order.push_back (key);
    return curve;
}

void SpectrumView::pushDb (int key, const std::vector<float>& db, Colour colour, const String& name, bool isSum)
{
    auto& curve = curveFor (key, colour, name, isSum);
    const bool fresh = curve.smoothed.front() <= -119.9f && curve.smoothed[10] <= -119.9f;
    for (size_t i = 0; i < curve.smoothed.size() && i < db.size(); ++i)
    {
        auto& s = curve.smoothed[i];
        s = fresh ? db[i] : s + (db[i] - s) * 0.3f;
        curve.peaks[i] = jmax (db[i], curve.peaks[i] - 0.35f);
        curve.average[i] = s;   // ya es un promedio: la referencia guarda esto
    }
}

void SpectrumView::pushTrack (int key, const float* mono, Colour colour, const String& name, bool isSum)
{
    auto& curve = curveFor (key, colour, name, isSum);

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (mono, mono + fftSize, fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    for (size_t i = 0; i < curve.smoothed.size(); ++i)
    {
        const float db = Decibels::gainToDecibels (fftData[i] * windowGain, -120.0f);
        auto& s = curve.smoothed[i];
        s += (db - s) * (db > s ? 0.6f : 0.12f);
        curve.peaks[i] = jmax (db, curve.peaks[i] - 0.35f);
        curve.average[i] += (db - curve.average[i]) * 0.02f;   // promedio largo (~1 s) para la referencia
    }
}

int SpectrumView::mainCurveKey() const
{
    // La suma si existe; si no, la primera curva (esta pista, o Mid en modo M/S)
    if (order.empty()) return 0;
    for (auto k : order)
        if (curves.at (k).isSum) return k;
    return order.front();
}

void SpectrumView::endFrame()
{
    for (auto it = curves.begin(); it != curves.end();)
        it = it->second.touched ? std::next (it) : curves.erase (it);
    if (spectroOn)
        pushSpectrogramColumn();
    repaint();
}

void SpectrumView::pushSpectrogramColumn()
{
    if (order.empty() || lastPlot.isEmpty())
        return;
    const int w = (int) lastPlot.getWidth(), h = (int) lastPlot.getHeight();
    if (w < 4 || h < 4)
        return;

    if (! spectroImage.isValid() || spectroImage.getWidth() != w || spectroImage.getHeight() != h)
    {
        spectroImage = Image (Image::RGB, w, h, true);
        Graphics ig (spectroImage);
        ig.fillAll (ScopeColours::bg);
    }

    spectroKey = mainCurveKey();
    const auto& bins = curves.at (spectroKey).smoothed;

    spectroImage.moveImageSection (0, 0, 1, 0, w - 1, h);
    Image::BitmapData bd (spectroImage, w - 1, 0, 1, h, Image::BitmapData::writeOnly);
    constexpr float floorDb = -84.0f, topDb = 0.0f;
    for (int y = 0; y < h; ++y)
    {
        const float t0 = 1.0f - (float) (y + 1) / (float) h, t1 = 1.0f - (float) y / (float) h;   // arriba = agudos
        const float f0 = specMinF * std::pow (specMaxF / specMinF, t0), f1 = specMinF * std::pow (specMaxF / specMinF, t1);
        const float db = valueAt (bins, f0, f1) + specTilt (std::sqrt (f0 * f1));
        const int idx = jlimit (0, 255, roundToInt ((db - floorDb) / (topDb - floorDb) * 255.0f));
        bd.setPixelColour (0, y, lut[(size_t) idx]);
    }
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

void SpectrumView::toggleReference()
{
    if (reference == nullptr)
        return;
    if (! reference->empty())
    {
        reference->clear();
        if (referenceName != nullptr) referenceName->clear();
    }
    else if (! order.empty())
    {
        const auto& c = curves.at (mainCurveKey());
        *reference = c.average;
        if (referenceName != nullptr) *referenceName = c.name;
    }
    repaint();
}

bool SpectrumView::hitsHeaderControl (Point<float> p) const
{
    return expandIcon.expanded (4.0f).contains (p) || msChip.contains (p) || refChip.contains (p)
        || spectroChip.contains (p) || modeChip.contains (p);
}

void SpectrumView::mouseDown (const MouseEvent& e)
{
    const auto p = e.position;
    if (expandIcon.expanded (4.0f).contains (p)) { if (onExpandToggle) onExpandToggle(); return; }
    if (msChip.contains (p))                     { if (onToggleMs) onToggleMs(); return; }
    if (modeChip.contains (p))                   { if (onCycleMode) onCycleMode(); return; }
    if (spectroChip.contains (p))                { if (onToggleSpectrogram) onToggleSpectrogram(); return; }
    if (refChip.contains (p))                    { toggleReference(); return; }
}

void SpectrumView::mouseDoubleClick (const MouseEvent& e)
{
    if (! hitsHeaderControl (e.position) && onExpandToggle)
        onExpandToggle();
}

void SpectrumView::paintSpectrogram (Graphics& g, Rectangle<float> area, Rectangle<float> leftLabels, Rectangle<float> bottomLabels)
{
    if (spectroImage.isValid())
        g.drawImage (spectroImage, area, RectanglePlacement::stretchToFit);
    else
    {
        g.setColour (ScopeColours::bg);
        g.fillRect (area);
    }

    auto yOfF = [&] (float f) { return area.getBottom() - area.getHeight() * std::log (f / specMinF) / std::log (specMaxF / specMinF); };
    g.setFont (uiFont (10.0f));
    for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
    {
        const float y = yOfF (f);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine (roundToInt (y), area.getX(), area.getRight());
        g.setColour (ScopeColours::text);
        const String label = f >= 1000.0f ? String (f / 1000.0f, 0) + "k" : String ((int) f);
        g.drawText (label, Rectangle<float> (leftLabels.getX(), y - 6.0f, leftLabels.getWidth() - 4.0f, 12.0f), Justification::centredRight);
    }
    g.setColour (ScopeColours::text);
    g.drawText (u8 ("← pasado"), bottomLabels, Justification::centredLeft);
    g.drawText (u8 ("ahora"), bottomLabels, Justification::centredRight);

    if (hoverPos.has_value() && area.contains (*hoverPos))
    {
        const float f = specMinF * std::pow (specMaxF / specMinF, (area.getBottom() - hoverPos->y) / area.getHeight());
        g.setColour (ScopeColours::textBright.withAlpha (0.5f));
        g.drawHorizontalLine (roundToInt (hoverPos->y), area.getX(), area.getRight());

        auto box = Rectangle<float> (hoverPos->x + 12.0f, hoverPos->y - 20.0f, 140.0f, 40.0f);
        if (box.getRight() > area.getRight()) box.setX (hoverPos->x - 152.0f);
        box.setY (jlimit (area.getY(), area.getBottom() - box.getHeight(), box.getY()));
        g.setColour (ScopeColours::bg.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (ScopeColours::gridStrong);
        g.drawRoundedRectangle (box, 5.0f, 1.0f);
        auto inner = box.reduced (9.0f, 5.0f);
        g.setFont (uiFont (13.0f, true));
        g.setColour (ScopeColours::textBright);
        g.drawText (formatHz (f), inner.removeFromTop (15.0f), Justification::centredLeft);
        g.setFont (uiFont (11.0f));
        g.setColour (ScopeColours::text);
        g.drawText (u8 ("nota ") + noteName (f), inner, Justification::centredLeft);
    }
}

void SpectrumView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b);

    auto area = b.reduced (10.0f, 8.0f);
    auto header = area.removeFromTop (16.0f);
    area.removeFromTop (4.0f);
    auto bottomLabels = area.removeFromBottom (14.0f);
    auto leftLabels = area.removeFromLeft (30.0f);
    bottomLabels.removeFromLeft (30.0f);
    lastPlot = area;

    expandIcon = header.removeFromRight (16.0f).translated (-2.0f, 0.0f);
    drawExpandIcon (g, expandIcon, expanded);
    header.removeFromRight (10.0f);

    g.setFont (uiFont (12.0f, true));
    g.setColour (ScopeColours::textBright);
    g.drawText (spectroOn ? "CASCADA" : "ESPECTRO", header.removeFromLeft (78.0f), Justification::centredLeft);

    // Botones del encabezado
    auto chip = [&] (Rectangle<float>& r, const String& text, bool on)
    {
        g.setFont (uiFont (10.0f, true));
        const float w = GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
        r = header.removeFromLeft (w).reduced (0.0f, 0.5f);
        header.removeFromLeft (6.0f);
        g.setColour (on ? ScopeColours::left.withAlpha (0.22f) : ScopeColours::panel);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? ScopeColours::left : ScopeColours::gridStrong);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (on ? ScopeColours::textBright : ScopeColours::text);
        g.drawText (text, r, Justification::centred);
    };
    const bool hasRef = reference != nullptr && ! reference->empty();
    if (! spectroOn)
    {
        String modeText = mode == 1 ? "PROMEDIO" : mode == 2 ? "GOLPES: " + triggerName.toUpperCase().substring (0, 12) : "INSTANTE";
        chip (modeChip, modeText, mode != 0);
    }
    else modeChip = {};
    if (msAvailable) chip (msChip, "MID/SIDE", msOn);
    else             msChip = {};
    if (! spectroOn) chip (refChip, hasRef ? "BORRAR REF" : "GUARDAR REF", hasRef);
    else             refChip = {};
    chip (spectroChip, "CASCADA", spectroOn);
    header.removeFromLeft (6.0f);

    g.setFont (uiFont (11.0f));
    if (spectroOn)
    {
        auto it = curves.find (spectroKey);
        g.setColour (ScopeColours::text);
        if (it != curves.end())
            g.drawText ("muestra: " + it->second.name, header, Justification::centredRight);
        paintSpectrogram (g, area, leftLabels, bottomLabels);
        return;
    }

    g.setColour (ScopeColours::text);
    g.drawText (u8 ("4.5 dB/oct"), header, Justification::centredRight);
    if (status.isNotEmpty())
    {
        g.setColour (ScopeColours::caution);
        g.drawText (status, header, Justification::centredLeft);
    }
    else if (collisions)
    {
        g.setColour (ScopeColours::warn);
        g.drawText ("rojo = se pisan", header, Justification::centredLeft);
    }

    auto xOf = [&] (float f) { return area.getX() + area.getWidth() * std::log (f / specMinF) / std::log (specMaxF / specMinF); };
    auto yOf = [&] (float db) { return jmap (jlimit (specBottomDb, specTopDb, db), specTopDb, specBottomDb, area.getY(), area.getBottom()); };

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
        auto lr = Rectangle<float> (36.0f, bottomLabels.getHeight()).withCentre ({ x, bottomLabels.getCentreY() });
        if (f <= 20.0f) lr.setX (x);
        if (f >= 20000.0f) lr.setRight (x);
        g.drawText (label, lr, just);
    }
    const float dbStep = area.getHeight() > 360.0f ? 6.0f : 12.0f;
    for (float db = 0.0f; db >= specBottomDb + 1.0f; db -= dbStep)
    {
        const float y = yOf (db);
        g.setColour (std::abs (db) < 0.5f ? ScopeColours::gridStrong : ScopeColours::grid);
        g.drawHorizontalLine (roundToInt (y), area.getX(), area.getRight());
        g.setColour (ScopeColours::text);
        g.drawText (String ((int) db), Rectangle<float> (leftLabels.getX(), y - 6.0f, leftLabels.getWidth() - 4.0f, 12.0f), Justification::centredRight);
    }

    if (order.empty())
        return;

    // Valores por columna de píxel
    const int cols = jmax (2, (int) area.getWidth());
    std::vector<std::vector<float>> levels (order.size(), std::vector<float> ((size_t) cols));
    std::vector<std::vector<float>> peakLevels (order.size(), std::vector<float> ((size_t) cols));
    std::vector<float> refLevels (hasRef ? (size_t) cols : 0);
    for (int c = 0; c < cols; ++c)
    {
        const float t0 = (float) c / (float) cols, t1 = (float) (c + 1) / (float) cols;
        const float f0 = specMinF * std::pow (specMaxF / specMinF, t0), f1 = specMinF * std::pow (specMaxF / specMinF, t1);
        const float tl = specTilt (std::sqrt (f0 * f1));
        for (size_t k = 0; k < order.size(); ++k)
        {
            const auto& curve = curves.at (order[k]);
            levels[k][(size_t) c] = valueAt (curve.smoothed, f0, f1) + tl;
            peakLevels[k][(size_t) c] = valueAt (curve.peaks, f0, f1) + tl;
        }
        if (hasRef)
            refLevels[(size_t) c] = valueAt (*reference, f0, f1) + tl;
    }
    auto xCol = [&] (int c) { return area.getX() + area.getWidth() * (float) c / (float) (cols - 1); };

    int numTracks = 0;
    for (auto key : order)
        if (! curves.at (key).isSum) ++numTracks;

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());

    // Zonas de choque (solo entre pistas, no con la suma)
    if (collisions && numTracks > 1)
    {
        constexpr float floorDb = -54.0f;
        for (int c = 0; c < cols; ++c)
        {
            float a = -200.0f, s = -200.0f;
            for (size_t k = 0; k < order.size(); ++k)
            {
                if (curves.at (order[k]).isSum) continue;
                const float v = levels[k][(size_t) c];
                if (v > a) { s = a; a = v; }
                else if (v > s) s = v;
            }
            const float diff = a - s;
            if (s < floorDb || diff > 12.0f) continue;
            const float alpha = 0.6f * jlimit (0.0f, 1.0f, (s - floorDb) / 24.0f) * (1.0f - diff / 12.0f);
            if (alpha < 0.02f) continue;
            const float x = xCol (c);
            g.setColour (ScopeColours::warn.withAlpha (alpha));
            g.fillRect (Rectangle<float>::leftTopRightBottom (x - 0.5f, yOf (s), x + 1.5f, area.getBottom()));
        }
    }

    // Referencia guardada (línea punteada)
    if (hasRef)
    {
        Path refPath;
        for (int c = 0; c < cols; ++c)
        {
            const float x = xCol (c), y = yOf (refLevels[(size_t) c]);
            if (c == 0) refPath.startNewSubPath (x, y);
            else        refPath.lineTo (x, y);
        }
        Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        PathStrokeType (1.4f).createDashedStroke (dashed, refPath, dashes, 2);
        g.setColour (ScopeColours::textBright.withAlpha (0.6f));
        g.fillPath (dashed);
    }

    // Curvas: primero las pistas, la suma al final (blanca, encima)
    for (int pass = 0; pass < 2; ++pass)
    {
        for (size_t k = 0; k < order.size(); ++k)
        {
            const auto& curve = curves.at (order[k]);
            if (curve.isSum != (pass == 1)) continue;

            Path fill, line, peakLine;
            for (int c = 0; c < cols; ++c)
            {
                const float x = xCol (c), y = yOf (levels[k][(size_t) c]), yp = yOf (peakLevels[k][(size_t) c]);
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

            if (curve.isSum)
            {
                g.setColour (ScopeColours::bg.withAlpha (0.6f));
                g.strokePath (line, PathStrokeType (3.5f));
                g.setColour (ScopeColours::textBright);
                g.strokePath (line, PathStrokeType (1.6f));
                continue;
            }

            if (numTracks > 1)
            {
                g.setColour (curve.colour.withAlpha (0.07f));
                g.fillPath (fill);
            }
            else
            {
                g.setGradientFill (ColourGradient (curve.colour.withAlpha (0.42f), 0.0f, area.getY(),
                                                   curve.colour.withAlpha (0.02f), 0.0f, area.getBottom(), false));
                g.fillPath (fill);
                g.setColour (ScopeColours::textBright.withAlpha (0.28f));
                g.strokePath (peakLine, PathStrokeType (1.0f));
            }
            g.setColour (curve.colour.withAlpha (0.18f));
            g.strokePath (line, PathStrokeType (4.0f));
            g.setColour (curve.colour);
            g.strokePath (line, PathStrokeType (1.5f));
        }
    }
    g.restoreState();

    // Leyenda de la suma / referencia
    {
        auto legend = area.reduced (8.0f, 4.0f).removeFromTop (14.0f);
        g.setFont (uiFont (10.0f, true));
        bool anySum = false;
        for (auto key : order) anySum = anySum || curves.at (key).isSum;
        if (anySum)
        {
            g.setColour (ScopeColours::textBright);
            g.drawText (u8 ("— suma de las pistas"), legend, Justification::topRight);
            legend.removeFromTop (14.0f);
        }
        if (hasRef)
        {
            g.setColour (ScopeColours::textBright.withAlpha (0.7f));
            const String nm = referenceName != nullptr && referenceName->isNotEmpty() ? *referenceName : String ("guardada");
            g.drawText (u8 ("- - referencia: ") + nm, area.reduced (8.0f, 4.0f).withTrimmedTop (anySum ? 14.0f : 0.0f).removeFromTop (14.0f),
                        Justification::topRight);
        }
    }

    // ---------- Lectura con el mouse ----------
    if (hoverPos.has_value() && area.contains (*hoverPos))
    {
        const float x = hoverPos->x;
        const float f = specMinF * std::pow (specMaxF / specMinF, (x - area.getX()) / area.getWidth());

        g.setColour (ScopeColours::textBright.withAlpha (0.45f));
        g.drawVerticalLine (roundToInt (x), area.getY(), area.getBottom());
        g.setColour (ScopeColours::textBright.withAlpha (0.15f));
        g.drawHorizontalLine (roundToInt (hoverPos->y), area.getX(), area.getRight());

        struct Item { String name; Colour colour; float db; };
        std::vector<Item> items;
        for (auto key : order)
        {
            const auto& curve = curves.at (key);
            const float v = valueAt (curve.smoothed, f / 1.02f, f * 1.02f) + specTilt (f);
            const auto col = curve.isSum ? ScopeColours::textBright : curve.colour;
            items.push_back ({ curve.name, col, v });
            g.setColour (col);
            g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ x, yOf (v) }));
            g.setColour (ScopeColours::bg);
            g.drawEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ x, yOf (v) }), 1.0f);
        }
        if (hasRef)
            items.push_back ({ "Referencia", ScopeColours::textBright.withAlpha (0.6f), valueAt (*reference, f / 1.02f, f * 1.02f) + specTilt (f) });

        const bool named = items.size() > 1;
        const float lineH = 15.0f;
        const float boxW = named ? 190.0f : 150.0f;
        const float boxH = 10.0f + lineH * (2.0f + (float) items.size());
        auto box = Rectangle<float> (x + 12.0f, area.getY() + 6.0f, boxW, boxH);
        if (box.getRight() > area.getRight()) box.setX (x - 12.0f - boxW);

        g.setColour (ScopeColours::bg.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (ScopeColours::gridStrong);
        g.drawRoundedRectangle (box, 5.0f, 1.0f);

        auto inner = box.reduced (9.0f, 5.0f);
        g.setFont (uiFont (13.0f, true));
        g.setColour (ScopeColours::textBright);
        g.drawText (formatHz (f), inner.removeFromTop (lineH), Justification::centredLeft);
        g.setFont (uiFont (11.0f));
        g.setColour (ScopeColours::text);
        g.drawText (u8 ("nota ") + noteName (f), inner.removeFromTop (lineH), Justification::centredLeft);

        for (auto& it : items)
        {
            auto row = inner.removeFromTop (lineH);
            g.setColour (it.colour);
            g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ row.getX() + 3.5f, row.getCentreY() }));
            row.removeFromLeft (12.0f);
            g.setFont (uiFont (11.0f));
            g.setColour (ScopeColours::textBright);
            if (named)
                g.drawText (it.name, row, Justification::centredLeft, true);
            g.setFont (uiFont (11.0f, true));
            g.drawText (String (it.db, 1) + " dB", row, named ? Justification::centredRight : Justification::centredLeft);
        }
    }
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
    viewParam   = proc.apvts.getRawParameterValue ("view");
    splitParam  = proc.apvts.getRawParameterValue ("split");
    freezeParam = proc.apvts.getRawParameterValue ("freeze");
    msParam     = proc.apvts.getRawParameterValue ("ms");
    spectroParam = proc.apvts.getRawParameterValue ("spectro");
    specModeParam = proc.apvts.getRawParameterValue ("specmode");

    auto toggleParam = [this] (const String& id)
    {
        if (auto* param = proc.apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
            param->endChangeGesture();
        }
        refresh();
    };
    spectrum.onToggleMs          = [toggleParam] { toggleParam ("ms"); };
    spectrum.onToggleSpectrogram = [toggleParam] { toggleParam ("spectro"); };
    spectrum.onCycleMode = [this]
    {
        if (auto* param = proc.apvts.getParameter ("specmode"))
        {
            const int next = (roundToInt (specModeParam->load()) + 1) % 3;
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) next));
            param->endChangeGesture();
        }
        refresh();
    };
    phase.onSelectReference = [this] (int key)
    {
        proc.phaseReferenceKey = key;
        alignInfo.clear();
        specCacheMode = -1;
        refresh();
    };
    spectrum.setReference (&proc.referenceSpectrum, &proc.referenceName);

    addAndMakeVisible (trackBar);
    addAndMakeVisible (wave);
    addChildComponent (gonio);
    addChildComponent (phase);
    addAndMakeVisible (spectrum);

    wave.onExpandToggle     = [this] { setFocusPanel (focusPanel == 1 ? 0 : 1); };
    spectrum.onExpandToggle = [this] { setFocusPanel (focusPanel == 2 ? 0 : 2); };

    trackBar.onToggle = [this] (int key)
    {
        if (hiddenKeys.count (key)) hiddenKeys.erase (key);
        else                        hiddenKeys.insert (key);
        refresh();
    };

    auto setupLabel = [this] (Label& l, const String& t)
    {
        l.setText (t, dontSendNotification);
        l.setFont (uiFont (11.0f, true));
        l.setJustificationType (Justification::bottomLeft);
        addAndMakeVisible (l);
    };
    setupLabel (viewLabel,  "VISTA");
    setupLabel (syncLabel,  u8 ("SINCRONÍA"));
    setupLabel (timeLabel,  "VENTANA");
    setupLabel (beatsLabel, u8 ("DURACIÓN (TEMPO)"));
    setupLabel (gainLabel,  "ZOOM VERTICAL");

    viewBox.addItemList ({ "Esta pista (L/R)", "Multipista" }, 1);
    syncBox.addItemList ({ "Libre", "Trigger", "Tempo del DAW" }, 1);
    beatsBox.addItemList ({ "1/4 tiempo", "1/2 tiempo", "1 tiempo", "2 tiempos", u8 ("1 compás"), "2 compases", "4 compases" }, 1);
    for (auto* box : { &viewBox, &syncBox, &beatsBox })
        addAndMakeVisible (*box);

    for (auto* s : { &timeSlider, &gainSlider })
    {
        s->setSliderStyle (Slider::LinearHorizontal);
        s->setTextBoxStyle (Slider::TextBoxRight, false, 64, 20);
        addAndMakeVisible (*s);
    }

    for (auto* btn : { &splitButton, &freezeButton })
    {
        btn->setClickingTogglesState (true);
        addAndMakeVisible (*btn);
    }

    viewAtt   = std::make_unique<ComboBoxAttachment> (proc.apvts, "view",  viewBox);
    syncAtt   = std::make_unique<ComboBoxAttachment> (proc.apvts, "sync",  syncBox);
    beatsAtt  = std::make_unique<ComboBoxAttachment> (proc.apvts, "beats", beatsBox);
    timeAtt   = std::make_unique<SliderAttachment>   (proc.apvts, "time",  timeSlider);
    gainAtt   = std::make_unique<SliderAttachment>   (proc.apvts, "gain",  gainSlider);
    splitAtt  = std::make_unique<ButtonAttachment>   (proc.apvts, "split",  splitButton);
    freezeAtt = std::make_unique<ButtonAttachment>   (proc.apvts, "freeze", freezeButton);

    setResizable (true, true);
    setResizeLimits (900, 540, 2400, 1500);
    setSize (1040, 660);

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
    headerArea = b.removeFromTop (38);
    trackBar.setBounds (b.removeFromTop (26));
    b.removeFromTop (8);
    controlsArea = b.removeFromBottom (52);
    b.removeFromBottom (8);

    wave.setVisible (focusPanel != 2);
    spectrum.setVisible (focusPanel != 1);
    if (focusPanel == 1)
    {
        wave.setBounds (b);
    }
    else if (focusPanel == 2)
    {
        spectrum.setBounds (b);
    }
    else
    {
        auto top = b.removeFromTop (roundToInt ((float) b.getHeight() * 0.58f));
        b.removeFromTop (8);
        auto side = top.removeFromRight (jmax (top.getHeight(), 260));
        gonio.setBounds (side);
        phase.setBounds (side);
        top.removeFromRight (8);
        wave.setBounds (top);
        spectrum.setBounds (b);
    }

    auto c = controlsArea;
    auto column = [&c] (int w) { auto r = c.removeFromLeft (w); c.removeFromLeft (14); return r; };
    auto place = [] (Rectangle<int> col, Label& l, Component& comp)
    {
        l.setBounds (col.removeFromTop (18));
        comp.setBounds (col.removeFromTop (28));
    };

    place (column (140), viewLabel, viewBox);
    place (column (130), syncLabel, syncBox);
    place (column (180), timeLabel, timeSlider);
    place (column (120), beatsLabel, beatsBox);
    place (column (170), gainLabel, gainSlider);

    auto buttons = c.withTrimmedTop (18).withHeight (28);
    freezeButton.setBounds (buttons.removeFromRight (96));
    buttons.removeFromRight (8);
    splitButton.setBounds (buttons.removeFromRight (100));
}

void ScopeLabAudioProcessorEditor::setFocusPanel (int panel)
{
    focusPanel = jlimit (0, 2, panel);
    wave.setExpanded (focusPanel == 1);
    spectrum.setExpanded (focusPanel == 2);
    resized();
    refresh();
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
    g.setColour (colour);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, jmap (db, -60.0f, 0.0f, 0.0f, 1.0f))), 2.0f);
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
    h.removeFromLeft (14.0f);
    loudnessArea = h.removeFromLeft (390.0f).toNearestInt();
    drawLoudness (g, loudnessArea.toFloat());

    auto meters = headerArea.toFloat().removeFromRight (340.0f);
    drawMeter (g, meters.removeFromLeft (160.0f).reduced (0, 8), holdL, ScopeColours::left, "L");
    meters.removeFromLeft (20.0f);
    drawMeter (g, meters.reduced (0, 8), holdR, ScopeColours::right, "R");

    if (freezeParam->load() > 0.5f)
    {
        auto badge = Rectangle<float> (96.0f, 22.0f).withCentre ({ (float) headerArea.getRight() - 400.0f, headerArea.toFloat().getCentreY() });
        g.setColour (ScopeColours::warn.withAlpha (0.2f));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (ScopeColours::warn);
        g.setFont (uiFont (11.0f, true));
        g.drawText ("CONGELADO", badge, Justification::centred);
    }
}

void ScopeLabAudioProcessorEditor::drawLoudness (Graphics& g, Rectangle<float> r)
{
    auto fmt = [] (float v) { return v < -150.0f ? String (u8 ("—")) : String (v, 1); };
    const float tpDb = Decibels::gainToDecibels (proc.loudness.getTruePeak(), -200.0f);
    struct Item { const char* label; String value; Colour colour; };
    const Item items[] = {
        { "M",   fmt (proc.loudness.getMomentary()), ScopeColours::textBright },
        { "S",   fmt (proc.loudness.getShortTerm()), ScopeColours::textBright },
        { "INT", fmt (integratedLufs),               ScopeColours::mid },
        { "TP",  fmt (tpDb),                         tpDb > -1.0f ? ScopeColours::warn : ScopeColours::textBright } };

    g.setFont (uiFont (10.0f, true));
    g.setColour (ScopeColours::text);
    g.drawText ("LUFS", r.removeFromLeft (34.0f), Justification::centredLeft);
    for (auto& it : items)
    {
        g.setFont (uiFont (10.0f, true));
        g.setColour (ScopeColours::text);
        const float lw = GlyphArrangement::getStringWidth (g.getCurrentFont(), it.label) + 4.0f;
        g.drawText (it.label, r.removeFromLeft (lw), Justification::centredLeft);
        g.setFont (uiFont (13.0f, true));
        g.setColour (it.colour);
        g.drawText (it.value, r.removeFromLeft (50.0f), Justification::centredLeft);
        r.removeFromLeft (4.0f);
    }
    g.setFont (uiFont (10.0f));
    g.setColour (ScopeColours::text.withAlpha (0.7f));
    g.drawText ("clic = reset", r, Justification::centredLeft);
}

void ScopeLabAudioProcessorEditor::mouseDown (const MouseEvent& e)
{
    if (loudnessArea.contains (e.getPosition()))
    {
        proc.loudness.requestReset();
        integratedLufs = -200.0f;
        repaint (headerArea);
    }
}

namespace
{
    // Busca el desplazamiento (en muestras de la señal decimada) que mejor alinea b con a.
    // Devuelve la correlación con signo en ese punto; lag > 0 significa que b llega tarde.
    float findBestLag (const std::vector<float>& a, const std::vector<float>& b, int maxLag, float& lagOut, float& energyA, float& energyB)
    {
        const int n = (int) jmin (a.size(), b.size());
        const int start = maxLag, end = n - maxLag;
        double ea = 0.0;
        for (int t = start; t < end; ++t) ea += (double) a[(size_t) t] * a[(size_t) t];
        energyA = (float) (ea / jmax (1, end - start));

        std::vector<double> r ((size_t) (2 * maxLag + 1), 0.0);
        int best = 0;
        double bestAbs = -1.0, ebBest = 0.0;
        for (int L = -maxLag; L <= maxLag; ++L)
        {
            double sab = 0.0, eb = 0.0;
            for (int t = start; t < end; ++t)
            {
                const double bv = b[(size_t) (t + L)];
                sab += a[(size_t) t] * bv;
                eb += bv * bv;
            }
            const double v = (ea > 1.0e-12 && eb > 1.0e-12) ? sab / std::sqrt (ea * eb) : 0.0;
            r[(size_t) (L + maxLag)] = v;
            if (std::abs (v) > bestAbs) { bestAbs = std::abs (v); best = L; ebBest = eb; }
        }
        energyB = (float) (ebBest / jmax (1, end - start));

        // Refinamiento parabólico para tener precisión por debajo de una muestra
        double frac = 0.0;
        const int i = best + maxLag;
        if (i > 0 && i < (int) r.size() - 1)
        {
            const double y0 = std::abs (r[(size_t) i - 1]), y1 = std::abs (r[(size_t) i]), y2 = std::abs (r[(size_t) i + 1]);
            const double den = y0 - 2.0 * y1 + y2;
            if (std::abs (den) > 1.0e-12)
                frac = jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / den);
        }
        lagOut = (float) (best + frac);
        return (float) r[(size_t) i];
    }
}

void ScopeLabAudioProcessorEditor::selectPhaseReferenceByName (const String& name)
{
    auto& hub = proc.getHub();
    for (int i = 0; i < ScopeHub::maxSlots; ++i)
    {
        auto& slot = hub.slot (i);
        if (slot.inUse.load() && slot.getName() == name)
        {
            proc.phaseReferenceKey = (i + 1) + 32 * (int) (slot.generation.load() % 1000000u);
            alignInfo.clear();
            specCacheMode = -1;
            return;
        }
    }
}

void ScopeLabAudioProcessorEditor::updatePhase (std::vector<Track*>& tracks, Track& me, bool aligned, int windowSamples)
{
    String message;
    if (! aligned)
        message = u8 ("Dale Play en Ableton para medir la fase entre pistas.");

    // Referencia: la pista elegida (si sigue visible); si no, esta pista
    Track* ref = &me;
    for (auto* t : tracks)
        if (proc.phaseReferenceKey != 0 && t->key == proc.phaseReferenceKey && t->visible)
            ref = t;
    const bool refIsThis = ref == &me;

    // Ventana de análisis: la duración elegida, entre 0.5 y 4 segundos
    static constexpr int block = 512, decim = 8;
    const double sr = ref->sampleRate;
    int L = jlimit ((int) (0.5 * sr), (int) (4.0 * sr), windowSamples);
    L = jmax (block * 4, (L / block) * block);
    const bool doAnalysis = aligned && (++frameCounter % 6 == 0);

    auto readMono = [this, L] (Track& t, std::vector<float>& out)
    {
        alignL.resize ((size_t) L); alignR.resize ((size_t) L); out.resize ((size_t) L);
        t.slot->buffer->read (t.alignedEnd, L, alignL.data(), alignR.data());
        for (int i = 0; i < L; ++i)
            out[(size_t) i] = (alignL[(size_t) i] + alignR[(size_t) i]) * 0.5f;
    };
    auto decimate = [] (const std::vector<float>& in, std::vector<float>& out)
    {
        out.resize (in.size() / decim);
        for (size_t i = 0; i < out.size(); ++i) out[i] = in[i * decim];
    };
    auto blockEnergy = [] (const std::vector<float>& x, std::vector<float>& e, float& maxE)
    {
        e.resize (x.size() / block);
        maxE = 0.0f;
        for (size_t b = 0; b < e.size(); ++b)
        {
            double s = 0.0;
            for (size_t i = b * block; i < (b + 1) * block; ++i) s += (double) x[i] * x[i];
            e[b] = (float) (s / block);
            maxE = jmax (maxE, e[b]);
        }
    };

    std::vector<float> eRef, eOth;
    float maxRef = 0.0f;
    if (doAnalysis)
    {
        readMono (*ref, refFull);
        lowpass (refFull, refLowL, ref->sampleRate, 150.0f);
        decimate (refLowL, refDec);
        blockEnergy (refFull, eRef, maxRef);
    }
    const int maxLag = (int) std::ceil (0.02 * sr / decim);

    std::vector<PhaseView::Row> rows;
    std::set<int> seen;
    for (auto* t : tracks)
    {
        if (t == ref || ! t->visible) continue;
        PhaseView::Row row;
        row.key = t->key;
        row.name = t->name + (t->isThis ? u8 ("  (esta)") : String());
        row.colour = t->colour;
        seen.insert (t->key);
        auto& info = alignInfo[t->key];

        if (doAnalysis)
        {
            readMono (*t, othFull);
            lowpass (othFull, othLowL, t->sampleRate, 150.0f);
            decimate (othLowL, othDec);
            float maxOth = 0.0f;
            blockEnergy (othFull, eOth, maxOth);

            // Solo cuentan los tramos donde suenan las dos (si no, los silencios diluyen la medida)
            // "Suena" = a menos de 24 dB de su propio máximo en la ventana (las colas largas no cuentan)
            const float thrRef = jmax (1.0e-8f, maxRef * 0.004f), thrOth = jmax (1.0e-8f, maxOth * 0.004f);
            double sabF = 0, saaF = 0, sbbF = 0, sabL = 0, saaL = 0, sbbL = 0;
            int both = 0, othActive = 0;
            const size_t skipBlocks = 1;   // arranque del filtro
            for (size_t bIdx = skipBlocks; bIdx < eRef.size() && bIdx < eOth.size(); ++bIdx)
            {
                const bool aOn = eRef[bIdx] > thrRef, bOn = eOth[bIdx] > thrOth;
                if (bOn) ++othActive;
                if (! (aOn && bOn)) continue;
                ++both;
                for (size_t i = bIdx * block; i < (bIdx + 1) * block; ++i)
                {
                    sabF += (double) refFull[i] * othFull[i];
                    saaF += (double) refFull[i] * refFull[i];
                    sbbF += (double) othFull[i] * othFull[i];
                    sabL += (double) refLowL[i] * othLowL[i];
                    saaL += (double) refLowL[i] * refLowL[i];
                    sbbL += (double) othLowL[i] * othLowL[i];
                }
            }

            const float overlap = othActive > 0 ? 100.0f * (float) both / (float) othActive : 0.0f;
            const bool hasSignal = both > 0 && saaF > 1.0e-9 && sbbF > 1.0e-9;
            const float full = hasSignal ? (float) (sabF / std::sqrt (saaF * sbbF)) : 0.0f;
            const float low = (saaL > 1.0e-9 && sbbL > 1.0e-9) ? (float) (sabL / std::sqrt (saaL * sbbL)) : 0.0f;

            const float k = info.measured ? 0.5f : 1.0f;
            info.full += (full - info.full) * k;
            info.low += (low - info.low) * k;
            info.overlap += (overlap - info.overlap) * k;
            info.hasSignal = hasSignal;
            info.measured = true;

            // Alineación en graves sobre toda la ventana
            float lag = 0.0f, ea = 0.0f, eb = 0.0f;
            const float corr = findBestLag (refDec, othDec, maxLag, lag, ea, eb);
            const float lagMs = lag * (float) decim / (float) sr * 1000.0f;
            const bool valid = hasSignal && ea > 1.0e-7f && eb > 1.0e-7f && std::abs (corr) > 0.5f;
            if (valid)
            {
                if (info.valid && std::abs (lagMs - info.lagMs) < 1.0f) info.lagMs += (lagMs - info.lagMs) * 0.35f;
                else                                                    info.lagMs = lagMs;
                info.corr = corr;
            }
            info.valid = valid;
        }

        if (aligned && info.measured)
        {
            row.hasSignal = info.hasSignal;
            row.lowCorr = info.low;
            row.fullCorr = info.full;
            row.overlapPct = info.overlap;
            row.alignValid = info.valid;
            row.lagMs = info.lagMs;
            row.alignCorr = info.corr;
        }
        else if (aligned)
        {
            row.hasSignal = false;
            row.overlapPct = 1.0f;   // "midiendo…"
        }
        rows.push_back (row);
    }

    for (auto it = alignInfo.begin(); it != alignInfo.end();)
        it = seen.count (it->first) ? std::next (it) : alignInfo.erase (it);

    phase.setRows (std::move (rows), ref->name, ref->colour, message, refIsThis);
}

void ScopeLabAudioProcessorEditor::refresh()
{
    const float gain  = Decibels::decibelsToGain (gainParam->load());
    const int   sync  = roundToInt (syncParam->load());
    const bool  frozen = freezeParam->load() > 0.5f;
    const bool  split = splitParam->load() > 0.5f;
    const bool  wantMulti = roundToInt (viewParam->load()) == 1;

    timeSlider.setEnabled (sync != 2);
    beatsBox.setEnabled (sync == 2);

    auto updateMeter = [] (float peak, float& hold, int& counter)
    {
        if (peak >= hold) { hold = peak; counter = 45; }
        else if (counter > 0) --counter;
        else hold *= 0.94f;
    };
    updateMeter (proc.peakLeft.exchange (0.0f),  holdL, holdCounterL);
    updateMeter (proc.peakRight.exchange (0.0f), holdR, holdCounterR);
    integratedLufs = proc.loudness.computeIntegrated();
    repaint (headerArea);

    // ---------- Pistas disponibles ----------
    const auto now = Time::getMillisecondCounter();
    auto& hub = proc.getHub();
    std::vector<Track> all;

    auto addTrack = [&] (ScopeSlot& s, int index, bool isThis)
    {
        Track t;
        t.key = (index + 1) + 32 * (int) (s.generation.load() % 1000000u);
        t.slot = &s;
        t.name = s.getName();
        t.colour = s.getColour();
        t.isThis = isThis;
        t.visible = hiddenKeys.count (t.key) == 0;
        t.transport = s.getTransport();
        t.sampleRate = s.sampleRate.load();
        t.end = s.buffer->getWritePosition();
        t.alignedEnd = t.end;
        all.push_back (std::move (t));
    };

    addTrack (proc.getSlot(), proc.getSlotIndex(), true);
    for (int i = 0; i < ScopeHub::maxSlots; ++i)
        if (i != proc.getSlotIndex() && hub.slot (i).isAlive (now))
            addTrack (hub.slot (i), i, false);

    const bool multi = wantMulti && all.size() > 1;

    {
        std::vector<TrackBar::Chip> chips;
        for (auto& t : all)
            chips.push_back ({ t.key, t.name, t.colour, multi ? t.visible : t.isThis, t.isThis });
        String hint;
        if (wantMulti && all.size() == 1)
            hint = u8 ("Agregá ScopeLab (mismo formato) en otras pistas para compararlas");
        else if (multi)
            hint = "clic en una pista para mostrarla u ocultarla";
        else
            hint = u8 ("Vista: esta pista. Elegí \"Multipista\" para comparar");
        trackBar.setChips (std::move (chips), hint);
    }

    if (frozen)
        return;

    // Pistas que participan: en multipista todas (la propia siempre, como referencia)
    std::vector<Track*> used;
    for (auto& t : all)
        if (t.isThis || (multi && t.visible))
            used.push_back (&t);
    Track& me = all.front();

    // ---------- Alineación por tempo (todas las pistas al mismo instante musical) ----------
    bool allPlaying = true;
    for (auto* t : used)
        if (! (t->transport.playing && t->transport.bpm > 0.0)) allPlaying = false;

    double commonPpq = std::numeric_limits<double>::max();
    if (allPlaying)
    {
        for (auto* t : used)
        {
            const double spb = t->sampleRate * 60.0 / t->transport.bpm;
            commonPpq = jmin (commonPpq, t->transport.ppq + (double) (t->end - t->transport.sample) / spb);
        }
        for (auto* t : used)
        {
            const double spb = t->sampleRate * 60.0 / t->transport.bpm;
            const auto target = t->transport.sample + (int64_t) std::llround ((commonPpq - t->transport.ppq) * spb);
            t->alignedEnd = jlimit (t->end - (int64_t) ScopeRingBuffer::capacity / 4, t->end, target);
        }
    }

    // ---------- Ventana del osciloscopio ----------
    const double sr = me.sampleRate;
    const int maxWindow = ScopeRingBuffer::capacity / 2 - 1;
    int W = jlimit (16, maxWindow, roundToInt (timeParam->load() * sr / 1000.0));
    String status, divLabel;
    int divisions = 10;
    float sweep = -1.0f;
    bool done = false;

    if (sync == 2)
    {
        if (allPlaying)
        {
            static constexpr double beatChoices[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
            const double bw = beatChoices[jlimit (0, 6, roundToInt (beatsParam->load()))];
            W = jlimit (16, maxWindow, (int) std::llround (sr * 60.0 / me.transport.bpm * bw));
            const double wsPpq = std::floor (commonPpq / bw) * bw;

            for (auto* t : used)
            {
                const double spb = t->sampleRate * 60.0 / t->transport.bpm;
                const int64_t wsSample = t->transport.sample + (int64_t) std::llround ((wsPpq - t->transport.ppq) * spb);
                tmpL.resize ((size_t) W); tmpR.resize ((size_t) W);
                t->l.resize ((size_t) W); t->r.resize ((size_t) W);
                t->slot->buffer->read (t->alignedEnd, W, tmpL.data(), tmpR.data());
                const int64_t base = t->alignedEnd - W;

                // Barrido tipo osciloscopio real: lo nuevo pisa a lo anterior
                for (int k = 0; k < W; ++k)
                {
                    int64_t idx = wsSample + k;
                    if (idx >= t->alignedEnd) idx -= W;
                    const int local = jlimit (0, W - 1, (int) (idx - base));
                    t->l[(size_t) k] = tmpL[(size_t) local];
                    t->r[(size_t) k] = tmpR[(size_t) local];
                }
                if (t->isThis)
                    sweep = jlimit (0.0f, 1.0f, (float) (t->alignedEnd - wsSample) / (float) W);
            }

            if (bw >= 8.0)
            {
                divisions = roundToInt (bw);   // una división por tiempo, línea fuerte en cada compás
                divLabel = u8 ("1 tiempo por div  ·  ") + String (me.transport.bpm, 1) + " BPM";
            }
            else
            {
                divisions = jmax (4, roundToInt (bw * 4.0));
                divLabel = u8 ("1/16 por div  ·  ") + String (me.transport.bpm, 1) + " BPM";
            }
            done = true;
        }
        else
        {
            status = multi ? u8 ("Dale Play para alinear las pistas") : u8 ("DAW detenido: usando Trigger");
        }
    }
    else if (multi && ! allPlaying)
    {
        status = u8 ("Dale Play para alinear las pistas");
    }

    if (! done && sync >= 1)
    {
        // Trigger sobre esta pista; las demás usan el mismo desplazamiento
        const int R = W * 2;
        tmpL.resize ((size_t) R); tmpR.resize ((size_t) R); tmpMono.resize ((size_t) R);
        me.slot->buffer->read (me.alignedEnd, R, tmpL.data(), tmpR.data());

        const float a = 1.0f - std::exp (-MathConstants<float>::twoPi * 180.0f / (float) sr);
        float lp = 0.0f, energy = 0.0f;
        for (int i = 0; i < R; ++i)
        {
            lp += a * ((tmpL[(size_t) i] + tmpR[(size_t) i]) * 0.5f - lp);
            tmpMono[(size_t) i] = lp;
            energy = jmax (energy, std::abs (lp));
        }

        const float hyst = energy * 0.05f;
        int start = W;
        for (int t = W; t > 1; --t)
        {
            if (tmpMono[(size_t) t - 1] <= 0.0f && tmpMono[(size_t) t] > 0.0f)
            {
                bool valid = false;
                for (int k = t - 1; k >= jmax (0, t - W / 2); --k)
                    if (tmpMono[(size_t) k] < -hyst) { valid = true; break; }
                    else if (tmpMono[(size_t) k] > hyst) break;
                if (valid) { start = t; break; }
            }
        }

        for (auto* t : used)
        {
            t->l.resize ((size_t) W); t->r.resize ((size_t) W);
            if (! t->isThis)
                t->slot->buffer->read (t->alignedEnd, R, tmpL.data(), tmpR.data());
            else
                me.slot->buffer->read (me.alignedEnd, R, tmpL.data(), tmpR.data());
            std::copy (tmpL.begin() + start, tmpL.begin() + start + W, t->l.begin());
            std::copy (tmpR.begin() + start, tmpR.begin() + start + W, t->r.begin());
        }
        done = true;
    }

    if (! done)
        for (auto* t : used)
        {
            t->l.resize ((size_t) W); t->r.resize ((size_t) W);
            t->slot->buffer->read (t->alignedEnd, W, t->l.data(), t->r.data());
        }

    if (divLabel.isEmpty())
        divLabel = formatSeconds ((double) W / sr / divisions) + " por div";

    // ---------- Series a dibujar ----------
    std::vector<ScopeSeries> series;
    std::vector<std::vector<int>> lanes;
    std::vector<Track*> shown;
    for (auto* t : used)
        if (t->visible || ! multi)
            shown.push_back (t);

    if (! multi)
    {
        series.push_back ({ me.l, ScopeColours::left, "L" });
        series.push_back ({ me.r, ScopeColours::right, "R" });
        lanes = split ? std::vector<std::vector<int>> { { 0 }, { 1 } } : std::vector<std::vector<int>> { { 1, 0 } };
    }
    else
    {
        std::vector<float> sum ((size_t) W, 0.0f);
        for (auto* t : shown)
        {
            ScopeSeries s;
            s.data.resize ((size_t) W);
            for (size_t i = 0; i < (size_t) W; ++i)
            {
                s.data[i] = (t->l[i] + t->r[i]) * 0.5f;
                sum[i] += s.data[i];
            }
            s.colour = t->colour;
            s.label = t->name;
            series.push_back (std::move (s));
        }

        if (split)
        {
            for (int i = 0; i < (int) series.size(); ++i)
                lanes.push_back ({ i });
        }
        else
        {
            std::vector<int> lane;
            for (int i = (int) series.size() - 1; i >= 0; --i)
                lane.push_back (i);
            if (series.size() >= 2)
            {
                series.push_back ({ std::move (sum), ScopeColours::textBright, "Suma", true });
                lane.push_back ((int) series.size() - 1);
            }
            lanes.push_back (lane);
        }
    }
    wave.setData (std::move (series), std::move (lanes), gain, divisions, divLabel, sweep, status, ! multi);

    // ---------- Panel derecho: estéreo (una pista) o fase entre pistas ----------
    gonio.setVisible (! multi && focusPanel == 0);
    phase.setVisible (multi && focusPanel == 0);
    if (multi)
    {
        updatePhase (used, me, allPlaying, W);
    }
    else
    {
        const int gn = 2048;
        gL.resize ((size_t) gn); gR.resize ((size_t) gn);
        me.slot->buffer->read (me.end, gn, gL.data(), gR.data());
        gonio.setData (gL, gR, gain);
    }

    // ---------- Espectro ----------
    constexpr int N = SpectrumView::fftSize;
    specL.resize (N); specR.resize (N); specMono.resize (N);
    const bool msOn = ! multi && msParam->load() > 0.5f;
    const bool spectroOn = spectroParam->load() > 0.5f;
    const int specMode = spectroOn ? 0 : jlimit (0, 2, roundToInt (specModeParam->load()));

    // Pista que dispara el modo Golpes: la referencia elegida en el panel de fase
    Track* trigger = &me;
    for (auto& t : all)
        if (proc.phaseReferenceKey != 0 && t.key == proc.phaseReferenceKey)
            trigger = &t;

    // Curvas a mostrar
    struct Source { int key; Track* track; int kind; Colour colour; String name; bool isSum; };   // kind: 0 mono, 1 mid, 2 side, 3 suma
    std::vector<Source> sources;
    if (msOn)
    {
        sources.push_back ({ -2, &me, 1, ScopeColours::left, "Mid", false });
        sources.push_back ({ -3, &me, 2, ScopeColours::right, "Side", false });
    }
    else
    {
        for (auto* t : shown)
            sources.push_back ({ t->key, t, 0, multi ? t->colour : ScopeColours::left, t->name, false });
        if (multi && shown.size() >= 2)
            sources.push_back ({ -1, nullptr, 3, ScopeColours::textBright, "Suma", true });
    }

    // Lee N muestras de una fuente, terminando 'back' muestras antes del "ahora" (alineado entre pistas)
    auto readTrack = [&] (Track& t, int64_t back, float* dst, bool add, int kind) -> bool
    {
        const int64_t endPos = (multi ? t.alignedEnd : t.end) - back;
        if (endPos - N < t.end - (int64_t) ScopeRingBuffer::capacity + 4096)
            return false;
        t.slot->buffer->read (endPos, N, specL.data(), specR.data());
        for (int i = 0; i < N; ++i)
        {
            const float l = specL[(size_t) i], r = specR[(size_t) i];
            const float v = kind == 2 ? (l - r) * 0.5f : (l + r) * 0.5f;
            dst[i] = add ? dst[i] + v : v;
        }
        return true;
    };
    auto readSource = [&] (const Source& s, int64_t back, float* out) -> bool
    {
        if (s.kind == 3)
        {
            std::fill (out, out + N, 0.0f);
            bool ok = true;
            for (auto* t : shown)
                ok = readTrack (*t, back, out, true, 0) && ok;
            return ok;
        }
        return readTrack (*s.track, back, out, false, s.kind);
    };

    if (specMode == 0)
    {
        specStatus.clear();
        spectrum.setOptions (! multi, msOn, spectroOn, specMode, trigger->name, specStatus);
        spectrum.beginFrame (sr, multi);
        for (auto& s : sources)
        {
            readSource (s, 0, specMono.data());
            spectrum.pushTrack (s.key, specMono.data(), s.colour, s.name, s.isSum);
        }
    }
    else
    {
        // Se recalcula ~10 veces por segundo; entre medio se dibuja lo último calculado
        if (specCacheMode != specMode || ++specCounter % 6 == 1)
        {
            specCacheMode = specMode;
            const int A = jlimit (2 * N, ScopeRingBuffer::capacity / 4, jmax (W, (int) (sr * (specMode == 2 ? 2.0 : 0.5))));
            std::vector<int64_t> backs;

            if (specMode == 1)
            {
                // Promedio: hasta 48 segmentos repartidos en la ventana
                const int nSeg = jlimit (1, 48, A / N);
                const double hop = nSeg > 1 ? (double) (A - N) / (nSeg - 1) : 0.0;
                for (int k = 0; k < nSeg; ++k)
                    backs.push_back ((int64_t) std::llround (k * hop));
                specStatus.clear();
            }
            else
            {
                // Golpes: detecta los ataques de la pista de referencia y analiza los 85 ms siguientes
                trigL.resize ((size_t) A); trigR.resize ((size_t) A); trigMono.resize ((size_t) A);
                const int64_t trigEnd = multi ? trigger->alignedEnd : trigger->end;
                trigger->slot->buffer->read (trigEnd, A, trigL.data(), trigR.data());
                for (int i = 0; i < A; ++i)
                    trigMono[(size_t) i] = (trigL[(size_t) i] + trigR[(size_t) i]) * 0.5f;

                constexpr int blk = 256;
                const int nb = A / blk;
                std::vector<float> e ((size_t) nb, 0.0f);
                float maxE = 0.0f;
                for (int bIdx = 0; bIdx < nb; ++bIdx)
                {
                    double acc = 0.0;
                    for (int i = bIdx * blk; i < (bIdx + 1) * blk; ++i) acc += (double) trigMono[(size_t) i] * trigMono[(size_t) i];
                    e[(size_t) bIdx] = (float) (acc / blk);
                    maxE = jmax (maxE, e[(size_t) bIdx]);
                }
                const int minGap = jmax (1, (int) (0.08 * sr / blk));
                int last = -minGap;
                for (int bIdx = 2; bIdx < nb; ++bIdx)
                {
                    const float floorE = jmin (e[(size_t) bIdx - 1], e[(size_t) bIdx - 2]);
                    if (e[(size_t) bIdx] > maxE * 0.05f && e[(size_t) bIdx] > floorE * 8.0f && bIdx - last >= minGap)
                    {
                        const int onset = jmax (0, (bIdx - 1) * blk);
                        const int64_t back = (int64_t) A - (onset + N);
                        if (back >= 0)
                            backs.push_back (back);
                        last = bIdx;
                    }
                }
                if (backs.size() > 24)
                    backs.erase (backs.begin(), backs.end() - 24);
                specStatus = backs.empty() ? u8 ("sin golpes de ") + trigger->name + u8 (" en la ventana")
                                           : String ((int) backs.size()) + u8 (" golpes de ") + trigger->name;
            }

            specCache.clear();
            segBuf.resize ((size_t) N);
            const float gainDb = 20.0f * std::log10 (spectrum.getWindowGain());
            for (auto& s : sources)
            {
                std::vector<double> acc ((size_t) N / 2 + 1, 0.0);
                int count = 0;
                for (auto back : backs)
                    if (readSource (s, back, segBuf.data()))
                    {
                        analyzer.addSegment (segBuf.data(), acc);
                        ++count;
                    }
                if (count == 0) continue;
                std::vector<float> db (acc.size());
                for (size_t i = 0; i < acc.size(); ++i)
                    db[i] = jmax (-120.0f, (float) (10.0 * std::log10 (acc[i] / count + 1.0e-24)) + gainDb);
                specCache[s.key] = std::move (db);
            }
        }

        spectrum.setOptions (! multi, msOn, spectroOn, specMode, trigger->name, specStatus);
        spectrum.beginFrame (sr, multi);
        for (auto& s : sources)
        {
            auto it = specCache.find (s.key);
            if (it != specCache.end())
                spectrum.pushDb (s.key, it->second, s.colour, s.name, s.isSum);
        }
    }
    spectrum.endFrame();


    // Con ventanas muy largas (varios compases) bajamos a 30 cuadros por segundo para no cargar la Mac
    const int hz = W > 150000 ? 30 : 60;
    if (hz != currentHz)
    {
        currentHz = hz;
        startTimerHz (hz);
    }
}
