#include "PastelViews.h"
#include <complex>

namespace pastel
{
// ---------------- theme ----------------
juce::Colour accentFor(juce::Component& c)
{
    for (juce::Component* p = &c; p != nullptr; p = p->getParentComponent())
    {
        auto v = p->getProperties()["accent"];
        if (v.isInt())
            return juce::Colour((juce::uint32)(int)v);
    }
    return Theme::accent;
}

juce::Font font(float h, bool bold)
{
    auto* fonts = ThemeFonts::getInstanceWithoutCreating();
    juce::Typeface::Ptr tf = fonts == nullptr ? nullptr : (bold ? fonts->bold : fonts->regular);
    if (tf != nullptr) return juce::Font(juce::FontOptions(tf)).withHeight(h);
    return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain));
}

// ---------------- waves ----------------
static float waveSample(Wave k, float t, float pw, int i)
{
    const float f = t - std::floor(t);
    switch (k)
    {
        case Wave::tri:    return 1.0f - 4.0f * std::abs(f - 0.5f);
        case Wave::saw:    return 1.0f - 2.0f * f;
        case Wave::pulse:  return f < pw ? 1.0f : -1.0f;
        case Wave::square: return f < 0.5f ? 1.0f : -1.0f;
        case Wave::sine:   return std::sin(juce::MathConstants<float>::twoPi * t);
        case Wave::noise:  { const float r = std::sin((float)i * 12.9898f) * 43758.5453f; return (r - std::floor(r)) * 2.0f - 1.0f; }
        case Wave::snh:    { const float r = std::sin(std::floor(t * 4.0f) * 78.233f) * 43758.5453f; return (r - std::floor(r)) * 2.0f - 1.0f; }
        case Wave::digital: return 0.55f * std::sin(juce::MathConstants<float>::twoPi * t)
                                 + 0.3f * std::sin(3.0f * juce::MathConstants<float>::twoPi * t + 1.0f)
                                 + 0.2f * (1.0f - 2.0f * (t * 3.0f - std::floor(t * 3.0f)));
        case Wave::none:   break;
    }
    return 0.0f;
}

void drawWave(juce::Graphics& g, juce::Rectangle<float> r, Wave k, float pw, float cycles,
              juce::Colour c, float thickness, bool fill, float phase)
{
    const int n = juce::jmax(24, (int)r.getWidth());
    const float cy = r.getCentreY(), amp = r.getHeight() * 0.5f;
    juce::Path p;
    float prevY = 0.0f;
    for (int i = 0; i <= n; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float)i / (float)n;
        const float y = cy - amp * waveSample(k, (float)i / (float)n * cycles + phase, pw, i);
        if (i == 0) p.startNewSubPath(x, y);
        else
        {
            // hard edges: vertical steps for the rectangular shapes
            if ((k == Wave::pulse || k == Wave::square || k == Wave::snh) && std::abs(y - prevY) > 1.0f)
                p.lineTo(x, prevY);
            p.lineTo(x, y);
        }
        prevY = y;
    }
    if (fill)
    {
        juce::Path f(p);
        f.lineTo(r.getRight(), cy);
        f.lineTo(r.getX(), cy);
        f.closeSubPath();
        g.setColour(c.withAlpha(0.13f));
        g.fillPath(f);
    }
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ---------------- params ----------------
Params::Params(juce::Component& owner, juce::AudioProcessorValueTreeState& s, juce::StringArray idList)
    : ids(idList)
{
    for (int k = 0; k < ids.size(); ++k)
    {
        auto* p = s.getParameter(ids[k]);
        jassert(p != nullptr);
        params.push_back(p);
        vals.push_back(p->convertFrom0to1(p->getValue()));
        const size_t i = (size_t)k;
        auto* o = &owner;
        atts.push_back(std::make_unique<juce::ParameterAttachment>(*p, [this, i, o](float v)
        {
            vals[i] = v;
            o->repaint();
        }, nullptr));
    }
    for (auto& a : atts) a->sendInitialUpdate();
}

void Params::setNorm(int i, float n)
{
    atts[(size_t)i]->setValueAsPartOfGesture(params[(size_t)i]->convertFrom0to1(juce::jlimit(0.0f, 1.0f, n)));
}

// ---------------- view helpers ----------------
bool View::hitTest(int x, int y)
{
    if (! overlay) return juce::Component::hitTest(x, y);
    const juce::Point<float> p((float) x, (float) y);
    for (auto& h : handlePts)
        if (h.getDistanceFrom(p) < 14.0f) return true;
    return false;
}

void View::drawWell(juce::Graphics& g, juce::Rectangle<float> r, int cols, int rows)
{
    if (overlay) return;
    g.setColour(Theme::inner);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(Theme::grid);
    for (int i = 1; i < cols; ++i)
        g.drawVerticalLine((int)(r.getX() + r.getWidth() * (float)i / (float)cols), r.getY() + 1, r.getBottom() - 1);
    for (int j = 1; j < rows; ++j)
        g.drawHorizontalLine((int)(r.getY() + r.getHeight() * (float)j / (float)rows), r.getX() + 1, r.getRight() - 1);
    g.setColour(juce::Colour(0xff202328));
    g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
}

void View::drawHandle(juce::Graphics& g, float x, float y, juce::Colour c, float r)
{
    handlePts.push_back({ x, y });
    g.setColour(c.withAlpha(0.22f));
    g.fillEllipse(x - r - 3, y - r - 3, (r + 3) * 2, (r + 3) * 2);
    g.setColour(Theme::handle);
    g.fillEllipse(x - r, y - r, r * 2, r * 2);
    g.setColour(c);
    g.drawEllipse(x - r, y - r, r * 2, r * 2, 2.0f);
}

void View::drawReadout(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& s, bool right)
{
    const auto f = font(10.5f);
    const float w = juce::GlyphArrangement::getStringWidth(f, s) + 14.0f;
    const float ry = area.getY() + 4 + 23.0f * (float) readoutRow;
    auto r = right ? juce::Rectangle<float>(area.getRight() - w - 4, ry, w, 19)
                   : juce::Rectangle<float>(area.getX() + 4, ry, w, 19);
    g.setColour(juce::Colour(0xe022252a));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(Theme::edge);
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    g.setColour(Theme::text);
    g.setFont(f);
    g.drawText(s, r, juce::Justification::centred, false);
}

// ---------------- segmented choice ----------------
SegChoice::SegChoice(juce::AudioProcessorValueTreeState& s, const juce::String& id,
                     juce::StringArray l, std::vector<Wave> ic)
    : ps(*this, s, { id }), labels(l), icons(std::move(ic))
{
    if (labels.isEmpty())
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(&ps.param(0)))
            for (auto& ch : c->choices) labels.add(ch.toUpperCase());
}

void SegChoice::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const int n = juce::jmax(1, labels.size());
    const int sel = juce::roundToInt(ps.get(0));
    const auto acc = isEnabled() ? accent() : Theme::faint;
    g.setColour(Theme::comboBg);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(Theme::edge);
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
    const float cw = r.getWidth() / (float)n;
    g.setFont(font(r.getHeight() > 21.0f ? 10.5f : 9.5f));
    for (int i = 0; i < n; ++i)
    {
        auto c = juce::Rectangle<float>(r.getX() + cw * (float)i, r.getY(), cw, r.getHeight());
        if (i == sel)
        {
            g.setColour(acc);
            g.fillRoundedRectangle(c.reduced(2.0f), 4.0f);
        }
        else if (i > 0 && i != sel + 1)
        {
            g.setColour(Theme::edge);
            g.drawVerticalLine((int)c.getX(), c.getY() + 6, c.getBottom() - 6);
        }
        const auto ink = i == sel ? Theme::ink : Theme::text.withAlpha(isEnabled() ? 0.85f : 0.4f);
        if (i < (int)icons.size() && icons[(size_t)i] != Wave::none)
            drawWave(g, c.reduced(juce::jmax(5.0f, cw * 0.22f), c.getHeight() * 0.3f), icons[(size_t)i], 0.3f, 1.0f,
                     i == sel ? Theme::ink : Theme::textDim, 1.5f, false);
        else
        {
            g.setColour(ink);
            g.drawText(labels[i], c, juce::Justification::centred, false);
        }
    }
}

void SegChoice::mouseDown(const juce::MouseEvent& e)
{
    const int n = juce::jmax(1, labels.size());
    const int i = juce::jlimit(0, n - 1, (int)((float)e.x / (float)getWidth() * (float)n));
    ps.setOnce(0, (float)i);
}

// ---------------- oscillator wave ----------------
WaveView::WaveView(juce::AudioProcessorValueTreeState& s, const juce::String& waveId, const juce::String& pwId,
                   std::vector<Wave> k)
    : ps(*this, s, { waveId, pwId }), kinds(std::move(k)) {}

Wave WaveView::kind() const
{
    const int i = juce::roundToInt(ps.get(0));
    return i >= 0 && i < (int)kinds.size() ? kinds[(size_t)i] : Wave::saw;
}

void WaveView::paint(juce::Graphics& g)
{
    handlePts.clear();
    auto r = getLocalBounds().toFloat();
    drawWell(g, r, 4, 2);
    const auto plot = r.reduced(8.0f, 10.0f);
    const float pw = 0.05f + 0.9f * ps.norm(1);   // the cycle fraction, whatever the parameter's range
    drawWave(g, plot, kind(), pw, 2.0f, accent(), 2.0f, true);
    if (kind() == Wave::pulse)
    {
        drawHandle(g, plot.getX() + plot.getWidth() * 0.5f * pw, plot.getCentreY(), accent(), 4.5f);
        drawReadout(g, r.withTrimmedTop(r.getHeight() - 27.0f), "PW " + juce::String(juce::roundToInt(pw * 100.0f)) + "%");
    }
}

void WaveView::mouseDown(const juce::MouseEvent& e)
{
    if (kind() != Wave::pulse) return;
    dragging = true;
    ps.begin(1);
    mouseDrag(e);
}

void WaveView::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging) return;
    const auto plot = getLocalBounds().toFloat().reduced(8.0f, 10.0f);
    const float pw = juce::jlimit(0.05f, 0.95f, ((float)e.x - plot.getX()) / (plot.getWidth() * 0.5f));
    ps.setNorm(1, (pw - 0.05f) / 0.9f);
}

void WaveView::mouseUp(const juce::MouseEvent&)
{
    if (dragging) ps.end(1);
    dragging = false;
}

// ---------------- filter ----------------
FilterView::FilterView(juce::AudioProcessorValueTreeState& s, const juce::String& c, const juce::String& r,
                       const juce::String& sl, const juce::String& hc, const juce::String& hp, std::vector<Mode> m)
    : ps(*this, s, [&]
      {
          juce::StringArray ids { c, r };
          if (sl.isNotEmpty()) ids.add(sl);
          if (hc.isNotEmpty()) { ids.add(hc); ids.add(hp); }
          return ids;
      }()),
      hasSlope(sl.isNotEmpty()), hasHp(hc.isNotEmpty()), modes(std::move(m)) {}

juce::Rectangle<float> FilterView::plot() const { return getLocalBounds().toFloat().reduced(6.0f, 8.0f); }

float FilterView::xOf(float hz) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * std::log(juce::jmax(20.0f, hz) / 20.0f) / std::log(1000.0f);
}

void FilterView::paint(juce::Graphics& g)
{
    handlePts.clear();
    auto r = getLocalBounds().toFloat();
    drawWell(g, r, 5, 4);
    const auto p = plot();
    const float fc = cutToHz(ps.get(0)), reso = ps.norm(1);
    Mode mode = Mode::lp12;
    if (hasSlope && ! modes.empty())
        mode = modes[(size_t)juce::jlimit(0, (int)modes.size() - 1, juce::roundToInt(ps.get(2)))];
    const int hi = hasSlope ? 3 : 2;
    const float hc = hasHp ? ps.get(hi) : 0.0f, hpk = hasHp ? ps.norm(hi + 1) : 0.0f;
    const float k = reso * 3.85f;          // ladder feedback, self-oscillates near the top
    const float q = 0.55f + reso * 7.5f;   // 2-pole resonance
    const float qh = 0.55f + hpk * 7.5f;
    auto dbAt = [&](float f)
    {
        const std::complex<float> jw(0.0f, f / fc);
        std::complex<float> h;
        const auto den = 1.0f + jw / q + jw * jw;   // 2-pole state variable
        switch (mode)
        {
            case Mode::lp24:
            {
                const auto gp = 1.0f / (1.0f + jw);
                const auto g4 = gp * gp * gp * gp;
                h = g4 * (1.0f + k) / (1.0f + k * g4);  // unity at DC (gain compensated)
                break;
            }
            case Mode::bp12: h = (jw / q) / den; break;
            case Mode::hp12: h = (jw * jw) / den; break;
            case Mode::lp12: default: h = 1.0f / den; break;
        }
        if (hasHp)
        {
            const std::complex<float> jh(0.0f, f / hc);
            h *= (jh * jh) / (1.0f + jh / qh + jh * jh);
        }
        return 20.0f * std::log10(juce::jmax(1.0e-6f, std::abs(h)));
    };
    auto yOf = [&](float db) { return juce::jlimit(p.getY() + 1.0f, p.getBottom(), p.getY() + p.getHeight() * (18.0f - db) / 54.0f); };
    juce::Path curve;
    const int n = (int)p.getWidth();
    for (int i = 0; i <= n; ++i)
    {
        const float t = (float)i / (float)n;
        const float f = 20.0f * std::pow(1000.0f, t);
        const float x = p.getX() + p.getWidth() * t, y = yOf(dbAt(f));
        if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
    }
    juce::Path fill(curve);
    fill.lineTo(p.getRight(), p.getBottom());
    fill.lineTo(p.getX(), p.getBottom());
    fill.closeSubPath();
    g.setColour(accent().withAlpha(0.15f));
    g.fillPath(fill);
    g.setColour(accent());
    g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    auto handleAt = [&](float hz)
    {
        const float hx = xOf(hz);
        g.setColour(accent().withAlpha(0.35f));
        g.drawVerticalLine((int)hx, p.getY(), p.getBottom());
        drawHandle(g, hx, yOf(dbAt(hz)), accent());
    };
    handleAt(fc);
    juce::String read = ps.text(0) + "  ·  " + ps.text(1);
    if (hasHp)
    {
        handleAt(hc);
        g.setColour(Theme::textDim);
        g.setFont(font(9.5f));
        g.drawText("HPF " + ps.text(hi) + " · " + ps.text(hi + 1), r.reduced(8.0f, 6.0f).withHeight(14.0f),
                   juce::Justification::centredLeft, false);
        read = "LPF " + read;
    }
    drawReadout(g, r, read);
}

void FilterView::apply(juce::Point<float> m)
{
    const auto p = plot();
    const float t = juce::jlimit(0.0f, 1.0f, (m.x - p.getX()) / p.getWidth());
    const float f = 20.0f * std::pow(1000.0f, t);
    const int ci = active == 0 ? 0 : (hasSlope ? 3 : 2);
    const float v = active == 0 ? cutFromHz(f) : f;
    ps.setNorm(ci, ps.param(ci).convertTo0to1(juce::jlimit(ps.param(ci).getNormalisableRange().start,
                                                           ps.param(ci).getNormalisableRange().end, v)));
    ps.setNorm(ci + 1, 1.0f - (m.y - p.getY()) / (p.getHeight() * 0.75f));
}

void FilterView::mouseDown(const juce::MouseEvent& e)
{
    const int hi = hasSlope ? 3 : 2;
    active = hasHp && std::abs(e.position.x - xOf(ps.get(hi))) < std::abs(e.position.x - xOf(cutToHz(ps.get(0)))) ? 1 : 0;
    const int ci = active == 0 ? 0 : hi;
    ps.begin(ci); ps.begin(ci + 1);
    apply(e.position);
}
void FilterView::mouseDrag(const juce::MouseEvent& e) { apply(e.position); }
void FilterView::mouseUp(const juce::MouseEvent&)
{
    const int ci = active == 0 ? 0 : (hasSlope ? 3 : 2);
    ps.end(ci); ps.end(ci + 1);
}

// ---------------- LFO ----------------
LfoView::LfoView(juce::AudioProcessorValueTreeState& s, const juce::String& w, const juce::String& d,
                 std::vector<Wave> k)
    : ps(*this, s, d.isNotEmpty() ? juce::StringArray { w, d } : juce::StringArray { w }),
      hasDelay(d.isNotEmpty()), kinds(std::move(k)) {}

void LfoView::paint(juce::Graphics& g)
{
    handlePts.clear();
    auto r = getLocalBounds().toFloat();
    drawWell(g, r, 4, 2);
    const auto p = r.reduced(8.0f, 10.0f);
    const int wi = juce::jlimit(0, juce::jmax(0, (int)kinds.size() - 1), juce::roundToInt(ps.get(0)));
    const Wave kind = kinds.empty() ? Wave::sine : kinds[(size_t)wi];
    const float dNorm = hasDelay ? ps.norm(1) : 0.0f;    // delay across the first 60%
    const float fadeEnd = p.getX() + p.getWidth() * 0.6f * dNorm;
    // the wave, faded in by the delay (clipped to the widening fade wedge)
    {
        juce::Graphics::ScopedSaveState ss(g);
        if (fadeEnd > p.getX() + 2.0f)
        {
            g.setColour(Theme::textDim);
            const float dash[] = { 3.0f, 3.0f };
            for (float edge : { p.getY(), p.getBottom() })
            {
                juce::Path line, dashed;
                line.startNewSubPath(p.getX(), p.getCentreY());
                line.lineTo(fadeEnd, edge);
                juce::PathStrokeType(1.0f).createDashedStroke(dashed, line, dash, 2);
                g.fillPath(dashed);
            }
            juce::Path wedge;
            wedge.startNewSubPath(p.getX(), p.getCentreY());
            wedge.lineTo(fadeEnd, p.getY() - 2.0f);
            wedge.lineTo(p.getRight(), p.getY() - 2.0f);
            wedge.lineTo(p.getRight(), p.getBottom() + 2.0f);
            wedge.lineTo(fadeEnd, p.getBottom() + 2.0f);
            wedge.closeSubPath();
            g.reduceClipRegion(wedge);
        }
        drawWave(g, p, kind, 0.5f, 3.0f, accent(), 2.0f, false);
    }
    if (! hasDelay) return;
    drawHandle(g, juce::jmax(p.getX() + 2.0f, fadeEnd), p.getY(), accent(), 4.5f);
    drawReadout(g, r.withTrimmedTop(r.getHeight() - 27.0f), "DELAY " + ps.text(1));
}

void LfoView::mouseDown(const juce::MouseEvent& e) { if (hasDelay) { ps.begin(1); mouseDrag(e); } }
void LfoView::mouseDrag(const juce::MouseEvent& e)
{
    if (! hasDelay) return;
    const auto p = getLocalBounds().toFloat().reduced(8.0f, 10.0f);
    ps.setNorm(1, ((float)e.x - p.getX()) / (p.getWidth() * 0.6f));
}
void LfoView::mouseUp(const juce::MouseEvent&) { if (hasDelay) ps.end(1); }

// ---------------- envelope ----------------
// ids: A, D, S, R (+ delay or hold as the fifth)
EnvView::EnvView(juce::AudioProcessorValueTreeState& s, juce::StringArray ids, const juce::String& t, Extra x)
    : ps(*this, s, ids), title(t), extra(ids.size() > 4 ? x : Extra::none) {}

EnvView::Geo EnvView::geo() const
{
    auto r = getLocalBounds().toFloat();
    const auto p = (showValues ? r.withTrimmedBottom(22.0f) : r).reduced(10.0f, 10.0f);
    Geo g {};
    g.x0 = p.getX(); g.top = p.getY() + 4.0f; g.bot = p.getBottom();
    const float W = p.getWidth();
    const bool ex = extra != Extra::none;
    // each segment's drag zone; sustain is fixed
    g.zE = ex ? W * 0.14f : 0.0f;
    g.zA = W * (ex ? 0.2f : 0.24f); g.zD = W * (ex ? 0.22f : 0.26f); g.zR = W * (ex ? 0.28f : 0.32f);
    auto len = [](float zone, float n) { return zone * (0.06f + 0.94f * n); };
    auto zero = [](float zone, float n) { return zone * n; };           // delay / hold may be 0
    const float e = ex ? zero(g.zE, shown(4)) : 0.0f;
    g.xE = g.x0 + (extra == Extra::delay ? e : 0.0f);
    g.xA = g.xE + len(g.zA, shown(0));
    g.xH = g.xA + (extra == Extra::hold ? e : 0.0f);
    g.xD = g.xH + len(g.zD, shown(1));
    g.xS = g.xD + W * (ex ? 0.12f : 0.16f);
    g.xR = g.xS + len(g.zR, shown(3));
    return g;
}

void EnvView::paint(juce::Graphics& gr)
{
    handlePts.clear();
    auto r = getLocalBounds().toFloat();
    drawWell(gr, showValues ? r.withTrimmedBottom(22.0f) : r, 6, 2);
    const auto g = geo();
    const auto acc = accent();
    const float sy = g.bot - (g.bot - g.top) * ps.norm(2);
    juce::Path p;
    p.startNewSubPath(g.x0, g.bot);
    p.lineTo(g.xE, g.bot);
    p.quadraticTo(g.xE + (g.xA - g.xE) * 0.35f, g.top + (g.bot - g.top) * 0.1f, g.xA, g.top);
    p.lineTo(g.xH, g.top);
    p.quadraticTo(g.xH + (g.xD - g.xH) * 0.25f, sy, g.xD, sy);
    p.lineTo(g.xS, sy);
    p.quadraticTo(g.xS + (g.xR - g.xS) * 0.2f, g.bot, g.xR, g.bot);
    juce::Path f(p);
    f.closeSubPath();
    gr.setColour(acc.withAlpha(0.16f));
    gr.fillPath(f);
    gr.setColour(acc);
    gr.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    gr.setColour(Theme::faint);
    for (float y = g.top; y < g.bot; y += 6.0f) gr.drawVerticalLine((int)g.xS, y, y + 3.0f);
    if (extra == Extra::delay) drawHandle(gr, g.xE, g.bot, acc, 4.0f);
    drawHandle(gr, g.xA, g.top, acc, 4.0f);
    if (extra == Extra::hold) drawHandle(gr, g.xH, g.top, acc, 4.0f);
    drawHandle(gr, g.xD, sy, acc, 4.0f);
    drawHandle(gr, g.xR, g.bot, acc, 4.0f);
    // title + values row
    if (! showValues) return;
    const float ty = r.getBottom() - 16.0f;
    juce::StringArray keys { "A", "D", "S", "R" };
    juce::Array<int> order { 0, 1, 2, 3 };
    if (extra == Extra::delay) { keys.insert(0, "DLY"); order.insert(0, 4); }
    if (extra == Extra::hold)  { keys.insert(1, "HLD"); order.insert(1, 4); }
    const float tw = title.isEmpty() ? 0.0f : 92.0f;
    gr.setFont(font(9.5f));
    gr.setColour(Theme::textDim);
    gr.drawText(title, (int)r.getX() + 2, (int)ty - 2, (int)tw, 16, juce::Justification::centredLeft, false);
    const float cw = (r.getWidth() - tw) / (float)keys.size();
    for (int i = 0; i < keys.size(); ++i)
    {
        const float x = r.getX() + tw + cw * (float)i;
        const float kw = keys[i].length() > 1 ? 24.0f : 11.0f;
        gr.setColour(Theme::textDim);
        gr.setFont(font(9.5f));
        gr.drawText(keys[i], (int)x, (int)ty - 2, (int)kw + 2, 16, juce::Justification::centredLeft, false);
        gr.setColour(Theme::text);
        gr.setFont(font(10.5f));
        gr.drawText(ps.text(order[i]), (int)(x + kw), (int)ty - 2, (int)(cw - kw), 16, juce::Justification::centredLeft, false);
    }
}

void EnvView::mouseDown(const juce::MouseEvent& e)
{
    const auto g = geo();
    const float sy = g.bot - (g.bot - g.top) * ps.norm(2);
    // handle ids: 0 attack, 1 decay/sustain, 2 release, 3 delay/hold
    std::vector<std::pair<int, juce::Point<float>>> pts { { 0, { g.xA, g.top } }, { 1, { g.xD, sy } }, { 2, { g.xR, g.bot } } };
    if (extra == Extra::delay) pts.push_back({ 3, { g.xE, g.bot } });
    if (extra == Extra::hold)  pts.push_back({ 3, { g.xH, g.top } });
    active = -1;
    float best = 22.0f;
    for (auto& [id, pt] : pts)
        if (auto d = pt.getDistanceFrom(e.position); d < best) { best = d; active = id; }
    if (active == 0) ps.begin(0);
    if (active == 1) { ps.begin(1); ps.begin(2); }
    if (active == 2) ps.begin(3);
    if (active == 3) ps.begin(4);
}

void EnvView::mouseDrag(const juce::MouseEvent& e)
{
    if (active < 0) return;
    const auto g = geo();
    auto n = [](float d, float zone) { return (d / zone - 0.06f) / 0.94f; };
    if (active == 0) setShown(0, n(e.position.x - g.xE, g.zA));
    if (active == 1)
    {
        setShown(1, n(e.position.x - g.xH, g.zD));
        ps.setNorm(2, (g.bot - e.position.y) / (g.bot - g.top));
    }
    if (active == 2) setShown(3, n(e.position.x - g.xS, g.zR));
    if (active == 3) setShown(4, (e.position.x - (extra == Extra::delay ? g.x0 : g.xA)) / g.zE);
}

void EnvView::mouseUp(const juce::MouseEvent&)
{
    if (active == 0) ps.end(0);
    if (active == 1) { ps.end(1); ps.end(2); }
    if (active == 2) ps.end(3);
    if (active == 3) ps.end(4);
    active = -1;
}

void EnvView::mouseDoubleClick(const juce::MouseEvent&)
{
    for (int i = 0; i < ps.size(); ++i)   // back to the parameter defaults
        ps.setOnce(i, ps.param(i).convertFrom0to1(ps.param(i).getDefaultValue()));
}

// ---------------- effect pads ----------------
FxView::FxView(juce::AudioProcessorValueTreeState& s, Style st, const juce::String& x, const juce::String& y,
               const juce::String& on, const juce::String& sync, const juce::String& div,
               juce::String xl, juce::String yl)
    : ps(*this, s, [&]
      {
          juce::StringArray ids { x, y, on };
          if (sync.isNotEmpty()) ids.add(sync);
          if (div.isNotEmpty()) ids.add(div);
          return ids;
      }()),
      style(st), hasSync(sync.isNotEmpty()), hasDiv(div.isNotEmpty()), xLab(std::move(xl)), yLab(std::move(yl)) {}

bool FxView::xFree() const { return ! hasSync || ps.get(3) < 0.5f; }

void FxView::paint(juce::Graphics& g)
{
    handlePts.clear();
    auto r = getLocalBounds().toFloat();
    drawWell(g, r, 4, 2);
    const bool on = ps.get(2) > 0.5f;
    const auto acc = accent();
    const auto p = r.reduced(8.0f, 8.0f);
    const float nx = ps.norm(0), ny = ps.norm(1);
    if (! on)
    {
        g.setColour(Theme::textDim);
        g.setFont(font(10.0f));
        g.drawText("BYPASSED", r, juce::Justification::centred, false);
        return;
    }
    switch (style)
    {
        case Style::chorus:
        {
            const float cyc = 0.8f + nx * 2.4f;
            auto band = p.withSizeKeepingCentre(p.getWidth(), p.getHeight() * (0.25f + 0.75f * ny));
            drawWave(g, band, Wave::sine, 0.5f, cyc, acc, 2.0f, false);
            drawWave(g, band, Wave::sine, 0.5f, cyc, acc.withAlpha(0.45f), 1.3f, false, 0.22f);
            break;
        }
        case Style::phaser:
        {
            juce::Path c;
            for (int i = 0; i <= 80; ++i)
            {
                const float t = (float)i / 80.0f;
                const float notch = std::pow(0.5f * (1.0f - std::cos(t * juce::MathConstants<float>::twoPi * (2.0f + nx * 2.0f))), 3.0f);
                const float yy = p.getY() + 6.0f + (p.getHeight() - 12.0f) * (0.15f + notch * (0.2f + 0.65f * ny));
                const float xx = p.getX() + p.getWidth() * t;
                if (i == 0) c.startNewSubPath(xx, yy); else c.lineTo(xx, yy);
            }
            g.setColour(acc);
            g.strokePath(c, juce::PathStrokeType(1.8f));
            break;
        }
        case Style::delay:
        {
            const float gap = 8.0f + (xFree() ? nx : 0.35f) * (p.getWidth() - 12.0f) / 5.0f;
            float h = p.getHeight() - 6.0f;
            for (int i = 0; i < 12 && p.getX() + 4.0f + gap * (float)i < p.getRight() - 4.0f; ++i)
            {
                g.setColour(i == 0 ? Theme::handle : acc.withAlpha(juce::jmax(0.25f, 1.0f - (float)i * 0.08f)));
                g.fillRoundedRectangle(p.getX() + 4.0f + gap * (float)i, p.getBottom() - h, 4.0f, h, 1.0f);
                h *= 0.35f + 0.6f * ny;
                if (h < 1.5f) break;
            }
            break;
        }
        case Style::reverb:
        {
            const int n = 34;
            const float len = 0.35f + 0.65f * nx;
            for (int i = 0; i < n; ++i)
            {
                const float t = (float)i / (float)n;
                if (t > len) break;
                const float jag = 1.0f + (std::sin((float)i * 7.1f) * 0.5f) * (1.0f - ny) * 0.5f;
                const float h = (p.getHeight() - 4.0f) * std::exp(-t / len * 3.2f) * jag;
                g.setColour(acc.withAlpha(0.85f));
                g.fillRoundedRectangle(p.getX() + p.getWidth() * t, p.getBottom() - juce::jmin(h, p.getHeight()),
                                       juce::jmax(1.5f, p.getWidth() / (float)n - 2.0f), juce::jmin(h, p.getHeight()), 1.0f);
            }
            break;
        }
    }
    juce::String xs = xFree() ? ps.text(0)
                    : (hasDiv ? ps.param(4).getCurrentValueAsText() : ps.param(3).getCurrentValueAsText());
    if (xLab.isNotEmpty()) xs = xLab + " " + xs;
    juce::String ys = ps.text(1);
    if (yLab.isNotEmpty()) ys = yLab + " " + ys;
    drawHandle(g, p.getX() + p.getWidth() * (xFree() ? nx : 0.5f), p.getBottom() - p.getHeight() * ny, acc, 4.5f);
    drawReadout(g, r, xs + "  ·  " + ys);
}

void FxView::apply(juce::Point<float> m)
{
    const auto p = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
    if (xFree()) ps.setNorm(0, (m.x - p.getX()) / p.getWidth());
    ps.setNorm(1, (p.getBottom() - m.y) / p.getHeight());
}

void FxView::mouseDown(const juce::MouseEvent& e)
{
    if (ps.get(2) < 0.5f) { ps.setOnce(2, 1.0f); return; }   // a click on a bypassed effect turns it on
    ps.begin(0); ps.begin(1); apply(e.position);
}
void FxView::mouseDrag(const juce::MouseEvent& e) { if (ps.get(2) > 0.5f) apply(e.position); }
void FxView::mouseUp(const juce::MouseEvent&) { ps.end(0); ps.end(1); }

// ---------------- drive ----------------
DriveView::DriveView(juce::AudioProcessorValueTreeState& s, const juce::String& a, const juce::String& t)
    : ps(*this, s, t.isNotEmpty() ? juce::StringArray { a, t } : juce::StringArray { a }), hasType(t.isNotEmpty()) {}

float DriveView::shape(float x) const
{
    const float d = 1.0f + ps.norm(0) * 7.0f;
    const int type = hasType ? juce::roundToInt(ps.get(1)) : 0;
    switch (type)
    {
        case 1:  { const float y = x * d + 0.25f * x * x * d; return std::tanh(y) / std::tanh(d * 1.25f); } // tube: asymmetric
        case 2:  return std::sin(x * (1.0f + ps.norm(0) * 3.0f) * juce::MathConstants<float>::halfPi);    // fold
        case 3:  return juce::jlimit(-1.0f, 1.0f, x * d * 1.6f);                                           // fuzz: hard clip
        default: return std::tanh(x * d) / std::tanh(d);                                                   // tape
    }
}

void DriveView::paint(juce::Graphics& g)
{
    handlePts.clear();
    auto r = getLocalBounds().toFloat();
    drawWell(g, r, 4, 4);
    const auto p = r.reduced(10.0f, 10.0f);
    g.setColour(Theme::faint);
    const float dash[] = { 3.0f, 3.0f };
    juce::Path diag, dd;
    diag.startNewSubPath(p.getX(), p.getBottom());
    diag.lineTo(p.getRight(), p.getY());
    juce::PathStrokeType(1.0f).createDashedStroke(dd, diag, dash, 2);
    g.fillPath(dd);
    juce::Path c;
    for (int i = 0; i <= 80; ++i)
    {
        const float x = (float)i / 40.0f - 1.0f;
        const float px = p.getX() + p.getWidth() * (x + 1.0f) * 0.5f;
        const float py = p.getCentreY() - juce::jlimit(-1.0f, 1.0f, shape(x)) * p.getHeight() * 0.5f;
        if (i == 0) c.startNewSubPath(px, py); else c.lineTo(px, py);
    }
    g.setColour(accent());
    g.strokePath(c, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float hx = 0.55f;
    drawHandle(g, p.getX() + p.getWidth() * (hx + 1.0f) * 0.5f,
               p.getCentreY() - juce::jlimit(-1.0f, 1.0f, shape(hx)) * p.getHeight() * 0.5f, accent(), 4.5f);
    drawReadout(g, r, "AMOUNT " + ps.text(0));
}

void DriveView::mouseDown(const juce::MouseEvent& e) { ps.begin(0); startV = ps.norm(0); startY = e.position.y; }
void DriveView::mouseDrag(const juce::MouseEvent& e)
{
    ps.setNorm(0, startV + (startY - e.position.y) / ((float)getHeight() * 0.8f));
}
void DriveView::mouseUp(const juce::MouseEvent&) { ps.end(0); }

// ---------------- EQ ----------------
EqView::EqView(juce::AudioProcessorValueTreeState& s, const juce::String& lo, const juce::String& hi)
    : ps(*this, s, { lo, hi }) {}

void EqView::paint(juce::Graphics& g)
{
    handlePts.clear();
    auto r = getLocalBounds().toFloat();
    drawWell(g, r, 4, 2);
    const auto p = r.reduced(8.0f, 8.0f);
    const float lo = ps.get(0), hi = ps.get(1);
    auto yOf = [&](float db) { return p.getCentreY() - db / 12.0f * p.getHeight() * 0.42f; };
    juce::Path c;
    for (int i = 0; i <= 100; ++i)
    {
        const float t = (float)i / 100.0f;
        const float db = lo / (1.0f + std::exp((t - 0.25f) * 18.0f)) + hi / (1.0f + std::exp(-(t - 0.75f) * 18.0f));
        const float x = p.getX() + p.getWidth() * t;
        if (i == 0) c.startNewSubPath(x, yOf(db)); else c.lineTo(x, yOf(db));
    }
    g.setColour(accent());
    g.strokePath(c, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    drawHandle(g, p.getX() + p.getWidth() * 0.12f, yOf(lo), accent(), 4.0f);
    drawHandle(g, p.getX() + p.getWidth() * 0.88f, yOf(hi), accent(), 4.0f);
    g.setFont(font(9.5f));
    g.setColour(Theme::textDim);
    g.drawText("LOW " + ps.text(0), r.reduced(8, 4), juce::Justification::bottomLeft, false);
    g.drawText("HIGH " + ps.text(1), r.reduced(8, 4), juce::Justification::bottomRight, false);
}

void EqView::mouseDown(const juce::MouseEvent& e)
{
    active = e.x < getWidth() / 2 ? 0 : 1;
    ps.begin(active);
    mouseDrag(e);
}
void EqView::mouseDrag(const juce::MouseEvent& e)
{
    const auto p = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
    const float db = (p.getCentreY() - e.position.y) / (p.getHeight() * 0.42f) * 12.0f;
    ps.setNorm(active, ps.param(active).convertTo0to1(juce::jlimit(-12.0f, 12.0f, db)));
}
void EqView::mouseUp(const juce::MouseEvent&) { ps.end(active); }
void EqView::mouseDoubleClick(const juce::MouseEvent& e)
{
    ps.setOnce(e.x < getWidth() / 2 ? 0 : 1, 0.0f);
}
// ---------------- sweep ----------------
SweepView::SweepView(juce::AudioProcessorValueTreeState& s, const juce::String& a, const juce::String& t,
                     const juce::String& cv, juce::String l)
    : ps(*this, s, cv.isNotEmpty() ? juce::StringArray { a, t, cv } : juce::StringArray { a, t }),
      hasCurve(cv.isNotEmpty()), label(std::move(l)) {}

SweepView::Geo SweepView::geo() const
{
    Geo g;
    g.p = getLocalBounds().toFloat().reduced(12.0f, 12.0f);
    const auto range = const_cast<Params&>(ps).param(0).getNormalisableRange();
    g.bipolar = range.start < 0.0f;
    g.base = g.bipolar ? g.p.getCentreY() : g.p.getBottom();
    g.x0 = g.p.getX();
    g.xT = g.x0 + g.p.getWidth() * 0.9f * (0.06f + 0.94f * std::pow(ps.norm(1), 1.0f / skew));
    const float a = g.bipolar ? ps.norm(0) * 2.0f - 1.0f : ps.norm(0);   // -1..1 or 0..1
    g.yA = g.base - a * (g.bipolar ? g.p.getHeight() * 0.5f : g.p.getHeight());
    return g;
}

float SweepView::yAt(const Geo& g, float u) const
{
    const float k = hasCurve ? std::exp((ps.norm(2) - 0.5f) * 3.0f) : 1.0f;   // > 1 = fast drop, < 1 = slow
    return g.base + (g.yA - g.base) * std::pow(1.0f - juce::jlimit(0.0f, 1.0f, u), k);
}

void SweepView::paint(juce::Graphics& gr)
{
    handlePts.clear();
    auto r = getLocalBounds().toFloat();
    drawWell(gr, r, 6, 2);
    const auto g = geo();
    const auto acc = accent();
    gr.setColour(Theme::faint);
    gr.drawHorizontalLine((int)g.base, g.p.getX(), g.p.getRight());
    juce::Path p;
    for (int i = 0; i <= 60; ++i)
    {
        const float u = (float)i / 60.0f;
        const float x = g.x0 + (g.xT - g.x0) * u, y = yAt(g, u);
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    p.lineTo(g.p.getRight(), g.base);
    juce::Path f(p);
    f.lineTo(g.x0, g.base);
    f.closeSubPath();
    gr.setColour(acc.withAlpha(0.16f));
    gr.fillPath(f);
    gr.setColour(acc);
    gr.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    drawHandle(gr, g.x0, g.yA, acc);
    drawHandle(gr, g.xT, g.base, acc);
    if (hasCurve) drawHandle(gr, g.x0 + (g.xT - g.x0) * 0.35f, yAt(g, 0.35f), acc, 4.0f);
    juce::String read = ps.text(0) + "  " + juce::String::fromUTF8("\xe2\x86\x92") + "  " + ps.text(1);
    if (label.isNotEmpty()) read = label + "  " + read;
    drawReadout(gr, r, read);
}

void SweepView::mouseDown(const juce::MouseEvent& e)
{
    const auto g = geo();
    std::vector<std::pair<int, juce::Point<float>>> pts { { 0, { g.x0, g.yA } }, { 1, { g.xT, g.base } } };
    if (hasCurve) pts.push_back({ 2, { g.x0 + (g.xT - g.x0) * 0.35f, yAt(g, 0.35f) } });
    active = -1;
    float best = 24.0f;
    for (auto& [id, pt] : pts)
        if (auto d = pt.getDistanceFrom(e.position); d < best) { best = d; active = id; }
    if (active >= 0) ps.begin(active);
}

void SweepView::mouseDrag(const juce::MouseEvent& e)
{
    if (active < 0) return;
    const auto g = geo();
    if (active == 0)
    {
        const float a = (g.base - e.position.y) / (g.bipolar ? g.p.getHeight() * 0.5f : g.p.getHeight());
        ps.setNorm(0, g.bipolar ? (a + 1.0f) * 0.5f : a);
    }
    if (active == 1)
    {
        const float t = ((e.position.x - g.x0) / (g.p.getWidth() * 0.9f) - 0.06f) / 0.94f;
        ps.setNorm(1, std::pow(juce::jlimit(0.0f, 1.0f, t), skew));
    }
    if (active == 2)
    {
        // pull the middle down (towards the line) = faster drop
        const float span = std::abs(g.yA - g.base) + 1.0f;
        const float frac = juce::jlimit(0.01f, 0.99f, std::abs(e.position.y - g.base) / span);
        const float k = std::log(frac) / std::log(0.65f);              // (1 - 0.35)^k = frac
        ps.setNorm(2, juce::jlimit(0.0f, 1.0f, std::log(juce::jmax(0.05f, k)) / 3.0f + 0.5f));
    }
}

void SweepView::mouseUp(const juce::MouseEvent&) { if (active >= 0) ps.end(active); active = -1; }
} // namespace pastel
