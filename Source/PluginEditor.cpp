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
void PhaseView::setRows (std::vector<Row> newRows, const String& rn, Colour rc, const String& msg)
{
    rows = std::move (newRows);
    refName = rn; refColour = rc; message = msg;
    repaint();
}

void PhaseView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b);
    auto area = b.reduced (12.0f, 8.0f);

    g.setFont (uiFont (12.0f, true));
    g.setColour (ScopeColours::textBright);
    g.drawText ("FASE ENTRE PISTAS", area.removeFromTop (16.0f), Justification::centredLeft);

    auto sub = area.removeFromTop (18.0f);
    g.setFont (uiFont (11.0f));
    g.setColour (ScopeColours::text);
    g.drawText ("comparado con", sub.removeFromLeft (86.0f), Justification::centredLeft);
    g.setColour (refColour);
    g.setFont (uiFont (11.0f, true));
    g.drawText (refName, sub, Justification::centredLeft);

    auto footer = area.removeFromBottom (30.0f);
    g.setFont (uiFont (10.0f));
    g.setColour (ScopeColours::text);
    g.drawFittedText (u8 ("+1 = suman · 0 = no se relacionan\nnegativo = se cancelan al sumarse"), footer.toNearestInt(),
                      Justification::bottomLeft, 2);

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

    const float rowH = jmin (52.0f, area.getHeight() / (float) rows.size());
    for (auto& row : rows)
    {
        auto r = area.removeFromTop (rowH);
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
            g.drawText (u8 ("sin señal compartida"), line1, Justification::centredRight);
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

        g.setColour (ScopeColours::text);
        g.setFont (uiFont (10.0f));
        g.drawText ("todo el rango " + signedValue (row.fullCorr), r.removeFromTop (14.0f), Justification::centredRight);
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
}

void SpectrumView::beginFrame (double sr)
{
    sampleRate = sr;
    order.clear();
    for (auto& [key, c] : curves)
        c.touched = false;
}

void SpectrumView::pushTrack (int key, const float* mono, Colour colour, const String& name)
{
    auto& curve = curves[key];
    if (curve.smoothed.empty())
    {
        curve.smoothed.assign ((size_t) fftSize / 2 + 1, -120.0f);
        curve.peaks.assign ((size_t) fftSize / 2 + 1, -120.0f);
    }
    curve.colour = colour;
    curve.name = name;
    curve.touched = true;
    order.push_back (key);

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
    }
}

void SpectrumView::endFrame()
{
    for (auto it = curves.begin(); it != curves.end();)
        it = it->second.touched ? std::next (it) : curves.erase (it);
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

void SpectrumView::mouseDown (const MouseEvent& e)
{
    if (expandIcon.expanded (4.0f).contains (e.position) && onExpandToggle)
        onExpandToggle();
}

void SpectrumView::mouseDoubleClick (const MouseEvent& e)
{
    if (! expandIcon.expanded (4.0f).contains (e.position) && onExpandToggle)
        onExpandToggle();
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

    expandIcon = header.removeFromRight (16.0f).translated (-2.0f, 0.0f);
    drawExpandIcon (g, expandIcon, expanded);
    header.removeFromRight (10.0f);

    const bool multi = order.size() > 1;

    g.setFont (uiFont (12.0f, true));
    g.setColour (ScopeColours::textBright);
    g.drawText ("ESPECTRO", header, Justification::centredLeft);
    g.setFont (uiFont (11.0f));
    g.setColour (ScopeColours::text);
    g.drawText (u8 ("FFT 4096  ·  pendiente 4.5 dB/oct"), header, Justification::centredRight);
    if (multi)
    {
        g.setColour (ScopeColours::warn);
        g.drawText (u8 ("rojo = las pistas se pisan en esa frecuencia"), header.withTrimmedLeft (90.0f), Justification::centredLeft);
    }

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
    const float dbStep = area.getHeight() > 360.0f ? 6.0f : 12.0f;
    for (float db = 0.0f; db >= bottomDb + 1.0f; db -= dbStep)
    {
        const float y = yOf (db);
        g.setColour (std::abs (db) < 0.5f ? ScopeColours::gridStrong : ScopeColours::grid);
        g.drawHorizontalLine (roundToInt (y), area.getX(), area.getRight());
        g.setColour (ScopeColours::text);
        g.drawText (String ((int) db), Rectangle<float> (dbLabels.getX(), y - 6.0f, dbLabels.getWidth() - 4.0f, 12.0f), Justification::centredRight);
    }

    if (order.empty())
        return;

    // Valores por columna de píxel, para cada pista
    const int cols = jmax (2, (int) area.getWidth());
    std::vector<std::vector<float>> levels (order.size(), std::vector<float> ((size_t) cols));
    std::vector<std::vector<float>> peakLevels (order.size(), std::vector<float> ((size_t) cols));
    for (int c = 0; c < cols; ++c)
    {
        const float t0 = (float) c / (float) cols, t1 = (float) (c + 1) / (float) cols;
        const float f0 = minF * std::pow (maxF / minF, t0), f1 = minF * std::pow (maxF / minF, t1);
        const float tl = tilt (std::sqrt (f0 * f1));
        for (size_t k = 0; k < order.size(); ++k)
        {
            const auto& curve = curves.at (order[k]);
            levels[k][(size_t) c] = valueAt (curve.smoothed, f0, f1) + tl;
            peakLevels[k][(size_t) c] = valueAt (curve.peaks, f0, f1) + tl;
        }
    }
    auto xCol = [&] (int c) { return area.getX() + area.getWidth() * (float) c / (float) (cols - 1); };

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());

    // Zonas de choque: donde la segunda pista más fuerte está cerca de la primera
    if (multi)
    {
        constexpr float floorDb = -54.0f;
        for (int c = 0; c < cols; ++c)
        {
            float a = -200.0f, s = -200.0f;
            for (auto& lv : levels)
            {
                const float v = lv[(size_t) c];
                if (v > a) { s = a; a = v; }
                else if (v > s) s = v;
            }
            const float diff = a - s;
            if (s < floorDb || diff > 12.0f) continue;
            const float loud = jlimit (0.0f, 1.0f, (s - floorDb) / 24.0f);
            const float close = 1.0f - diff / 12.0f;
            const float alpha = 0.6f * loud * close;
            if (alpha < 0.02f) continue;
            const float x = xCol (c);
            g.setColour (ScopeColours::warn.withAlpha (alpha));
            g.fillRect (Rectangle<float>::leftTopRightBottom (x - 0.5f, yOf (s), x + 1.5f, area.getBottom()));
        }
    }

    for (size_t k = 0; k < order.size(); ++k)
    {
        const auto colour = curves.at (order[k]).colour;
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

        if (multi)
        {
            g.setColour (colour.withAlpha (0.07f));
            g.fillPath (fill);
        }
        else
        {
            g.setGradientFill (ColourGradient (colour.withAlpha (0.42f), 0.0f, area.getY(),
                                               colour.withAlpha (0.02f), 0.0f, area.getBottom(), false));
            g.fillPath (fill);
            g.setColour (ScopeColours::textBright.withAlpha (0.28f));
            g.strokePath (peakLine, PathStrokeType (1.0f));
        }
        g.setColour (colour.withAlpha (0.18f));
        g.strokePath (line, PathStrokeType (4.0f));
        g.setColour (colour);
        g.strokePath (line, PathStrokeType (1.5f));
    }
    g.restoreState();

    // ---------- Lectura con el mouse ----------
    if (hoverPos.has_value() && area.contains (*hoverPos))
    {
        const float x = hoverPos->x;
        const float f = minF * std::pow (maxF / minF, (x - area.getX()) / area.getWidth());

        g.setColour (ScopeColours::textBright.withAlpha (0.45f));
        g.drawVerticalLine (roundToInt (x), area.getY(), area.getBottom());
        g.setColour (ScopeColours::textBright.withAlpha (0.15f));
        g.drawHorizontalLine (roundToInt (hoverPos->y), area.getX(), area.getRight());

        struct Item { String name; Colour colour; float db; };
        std::vector<Item> items;
        for (auto key : order)
        {
            const auto& curve = curves.at (key);
            const float v = valueAt (curve.smoothed, f / 1.02f, f * 1.02f) + tilt (f);
            items.push_back ({ curve.name, curve.colour, v });
            g.setColour (curve.colour);
            g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ x, yOf (v) }));
            g.setColour (ScopeColours::bg);
            g.drawEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ x, yOf (v) }), 1.0f);
        }

        // Cuadro con frecuencia, nota y nivel de cada pista
        const float lineH = 15.0f;
        const float boxW = multi ? 190.0f : 150.0f;
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
            if (multi)
                g.drawText (it.name, row, Justification::centredLeft, true);
            g.setFont (uiFont (11.0f, true));
            g.drawText (String (it.db, 1) + " dB", row, multi ? Justification::centredRight : Justification::centredLeft);
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
    g.setColour (ScopeColours::text);
    g.setFont (uiFont (12.0f));
    g.drawText (u8 ("osciloscopio  ·  estéreo  ·  espectro  ·  multipista"), h.removeFromLeft (330.0f), Justification::centredLeft);

    auto meters = headerArea.toFloat().removeFromRight (400.0f);
    drawMeter (g, meters.removeFromLeft (190.0f).reduced (0, 8), holdL, ScopeColours::left, "L");
    meters.removeFromLeft (20.0f);
    drawMeter (g, meters.reduced (0, 8), holdR, ScopeColours::right, "R");

    if (freezeParam->load() > 0.5f)
    {
        auto badge = Rectangle<float> (96.0f, 22.0f).withCentre ({ (float) headerArea.getRight() - 470.0f, headerArea.toFloat().getCentreY() });
        g.setColour (ScopeColours::warn.withAlpha (0.2f));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (ScopeColours::warn);
        g.setFont (uiFont (11.0f, true));
        g.drawText ("CONGELADO", badge, Justification::centred);
    }
}

void ScopeLabAudioProcessorEditor::updatePhase (std::vector<Track*>& tracks, Track& me, bool aligned)
{
    std::vector<PhaseView::Row> rows;
    String message;
    if (! aligned)
        message = u8 ("Dale Play en Ableton para medir la fase entre pistas.");

    constexpr int N = SpectrumView::fftSize;
    const size_t skip = 256;   // descarta el arranque del filtro

    if (aligned)
    {
        tmpL.resize (N); tmpR.resize (N); refMono.resize (N);
        me.slot->buffer->read (me.alignedEnd, N, tmpL.data(), tmpR.data());
        for (int i = 0; i < N; ++i) refMono[(size_t) i] = (tmpL[(size_t) i] + tmpR[(size_t) i]) * 0.5f;
        lowpass (refMono, refLow, me.sampleRate, 150.0f);
    }

    std::set<int> seen;
    for (auto* t : tracks)
    {
        if (t->isThis || ! t->visible) continue;
        PhaseView::Row row;
        row.name = t->name;
        row.colour = t->colour;

        if (aligned)
        {
            otherMono.resize (N);
            t->slot->buffer->read (t->alignedEnd, N, tmpL.data(), tmpR.data());
            for (int i = 0; i < N; ++i) otherMono[(size_t) i] = (tmpL[(size_t) i] + tmpR[(size_t) i]) * 0.5f;
            lowpass (otherMono, otherLow, t->sampleRate, 150.0f);

            float full = 0.0f, low = 0.0f;
            const bool okFull = computeCorrelation (refMono, otherMono, skip, full);
            const bool okLow  = computeCorrelation (refLow, otherLow, skip, low);
            row.hasSignal = okFull;
            if (! okLow) low = 0.0f;

            auto& sm = corrSmooth[t->key];
            sm.first  += (low - sm.first) * 0.15f;
            sm.second += (full - sm.second) * 0.15f;
            row.lowCorr = sm.first;
            row.fullCorr = sm.second;
            seen.insert (t->key);
        }
        rows.push_back (row);
    }

    for (auto it = corrSmooth.begin(); it != corrSmooth.end();)
        it = seen.count (it->first) ? std::next (it) : corrSmooth.erase (it);

    phase.setRows (std::move (rows), me.name + u8 ("  (esta)"), me.colour, message);
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
        updatePhase (used, me, allPlaying);
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
    spectrum.beginFrame (sr);
    for (auto* t : shown)
    {
        t->slot->buffer->read (multi ? t->alignedEnd : t->end, N, specL.data(), specR.data());
        for (int i = 0; i < N; ++i)
            specMono[(size_t) i] = (specL[(size_t) i] + specR[(size_t) i]) * 0.5f;
        spectrum.pushTrack (t->key, specMono.data(), multi ? t->colour : ScopeColours::left, t->name);
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
