#include "PastelLookAndFeel.h"
#include "PastelViews.h"
#include "PastelAssets.h"

PastelLookAndFeel::PastelLookAndFeel()
{
    latoReg   = juce::Typeface::createSystemTypefaceFor(PastelAssets::LatoRegular_ttf,
                                                       PastelAssets::LatoRegular_ttfSize);
    latoBold  = juce::Typeface::createSystemTypefaceFor(PastelAssets::LatoBold_ttf,
                                                       PastelAssets::LatoBold_ttfSize);
    latoBlack = juce::Typeface::createSystemTypefaceFor(PastelAssets::LatoBlack_ttf,
                                                       PastelAssets::LatoBlack_ttfSize);
    // the shared v3 views draw in the player's teal + Lato
    pastel::Theme::accent = PastelColors::teal;    pastel::Theme::inner = juce::Colour(0xFF16181C);
    pastel::Theme::grid = juce::Colour(0xFF24272C); pastel::Theme::edge = PastelColors::knobEdge;
    pastel::Theme::comboBg = PastelColors::comboBg; pastel::Theme::text = PastelColors::text;
    pastel::Theme::textDim = PastelColors::textDim; pastel::Theme::faint = juce::Colour(0xFF5C5A56);
    pastel::Theme::handle = juce::Colour(0xFFF4F1EC); pastel::Theme::ink = PastelColors::barText;
    pastel::Theme::regular = latoReg;              pastel::Theme::bold = latoBold;
    pastel::Theme::format = [](const juce::String& id, float v) -> juce::String   // stateless
    {
        if (id == "fcut") return v >= 1000.0f ? juce::String(v / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
        if (id == "atk" || id == "dec" || id == "rel")
            return v < 1.0f ? juce::String(juce::roundToInt(v * 1000.0f)) + " ms" : juce::String(v, 2) + " s";
        return juce::String(juce::roundToInt(v * 100.0f)) + "%";
    };
    setColour(juce::Label::textColourId, PastelColors::text);
    setColour(juce::ComboBox::textColourId, PastelColors::text);
    setColour(juce::ComboBox::backgroundColourId, PastelColors::comboBg);
    setColour(juce::ListBox::backgroundColourId, PastelColors::panel);
    setColour(juce::ListBox::textColourId, PastelColors::text);
    setColour(juce::TextEditor::backgroundColourId, PastelColors::comboBg);
    setColour(juce::TextEditor::textColourId, PastelColors::text);
    setColour(juce::TextEditor::outlineColourId, PastelColors::panelEdge);
    setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, PastelColors::track);
}

juce::Font PastelLookAndFeel::uiFont(float h) const
{
    if (latoReg != nullptr) return juce::Font(latoReg).withHeight(h);
    return juce::Font(h);
}

juce::Font PastelLookAndFeel::titleFont(float h) const
{
    if (latoBold != nullptr) return juce::Font(latoBold).withHeight(h);
    return juce::Font(juce::FontOptions(h, juce::Font::bold));
}

juce::Font PastelLookAndFeel::logoFont(float h) const
{
    if (latoBlack != nullptr) return juce::Font(latoBlack).withHeight(h);
    return titleFont(h);
}

juce::Colour PastelLookAndFeel::accentFor(juce::Component& c)
{
    juce::Component* start = dynamic_cast<juce::GroupComponent*>(&c) != nullptr
                             ? &c : c.getParentComponent();
    for (juce::Component* p = start; p != nullptr; p = p->getParentComponent())
    {
        if (auto* g = dynamic_cast<juce::GroupComponent*>(p))
        {
            auto v = g->getProperties()["accent"];
            if (v.isInt())
                return juce::Colour((juce::uint32)(int)v);
            return PastelColors::teal;
        }
    }
    return PastelColors::teal;
}

juce::Font PastelLookAndFeel::getLabelFont(juce::Label& l) { return l.getFont(); } // honour setFont
juce::Font PastelLookAndFeel::getComboBoxFont(juce::ComboBox&) { return uiFont(13.0f); }

void PastelLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    // arrow is drawn in the last ~18 px; give the text everything else
    label.setBounds(1, 1, box.getWidth() - 20, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}
juce::Font PastelLookAndFeel::getPopupMenuFont() { return uiFont(13.5f); }
juce::Font PastelLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return titleFont(12.5f); }

void PastelLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                       float pos, float start, float end, juce::Slider& slider)
{
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float r = juce::jmin(w, h) * 0.5f - 1.0f;
    float a = start + pos * (end - start);

    float br = r - 5.0f;
    g.setColour(PastelColors::knobBody);
    g.fillEllipse(cx - br, cy - br, br * 2, br * 2);
    g.setColour(PastelColors::knobEdge);
    g.drawEllipse(cx - br + 0.5f, cy - br + 0.5f, br * 2 - 1, br * 2 - 1, 1.0f);
    float arcR = r - 1.5f;
    g.setColour(PastelColors::track);
    juce::Path track;
    track.addArc(cx - arcR, cy - arcR, arcR * 2, arcR * 2, start, end, true);
    g.strokePath(track, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::butt));
    if (pos > 0.001f)
    {
        g.setColour(PastelLookAndFeel::accentFor(slider));
        juce::Path val;
        val.addArc(cx - arcR, cy - arcR, arcR * 2, arcR * 2, start, a, true);
        g.strokePath(val, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::butt));
    }
    float dx = std::cos(a - juce::MathConstants<float>::halfPi);
    float dy = std::sin(a - juce::MathConstants<float>::halfPi);
    g.setColour(PastelLookAndFeel::accentFor(slider));
    g.drawLine(cx + dx * 2.0f, cy + dy * 2.0f,
               cx + dx * (br - 2.0f), cy + dy * (br - 2.0f), 2.5f);
}

void PastelLookAndFeel::drawLinearSliderBackground(juce::Graphics&, int, int, int, int,
    float, float, float, const juce::Slider::SliderStyle, juce::Slider&) {}

void PastelLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                                       float pos, float, float,
                                       const juce::Slider::SliderStyle, juce::Slider& s)
{
    auto accent = PastelLookAndFeel::accentFor(s);
    if (s.isVertical())
    {
        float cx = x + w * 0.5f;
        float top = y + 4.0f, bot = (float)(y + h - 4);
        g.setColour(PastelColors::track);
        g.fillRoundedRectangle(cx - 1.5f, top, 3.0f, bot - top, 1.5f);
        float fy = top + (1.0f - pos) * (bot - top);
        g.setColour(accent);
        g.fillRoundedRectangle(cx - 1.5f, fy, 3.0f, bot - fy, 1.5f);
        g.setColour(PastelColors::comboBg);
        g.fillRoundedRectangle(cx - 8.0f, fy - 6.0f, 16.0f, 12.0f, 2.0f);
        g.setColour(accent);
        g.fillRect(cx - 4.0f, fy - 1.0f, 8.0f, 2.0f);
    }
    else
    {
        float cy = y + h * 0.5f;
        float l = x + 4.0f, rr = (float)(x + w - 4);
        g.setColour(PastelColors::track);
        g.fillRoundedRectangle(l, cy - 1.5f, rr - l, 3.0f, 1.5f);
        float fx = l + pos * (rr - l);
        g.setColour(accent);
        g.fillRoundedRectangle(l, cy - 1.5f, fx - l, 3.0f, 1.5f);
        g.setColour(PastelColors::comboBg);
        g.fillRoundedRectangle(fx - 6.0f, cy - 8.0f, 12.0f, 16.0f, 2.0f);
    }
}

void PastelLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    bool on = b.getToggleState();
    const auto accent = PastelLookAndFeel::accentFor(b);
    float w = (float)b.getWidth(), h = (float)b.getHeight();
    if ((bool) b.getProperties()["power"])
    {
        // v3 section on/off: the EFX Grid power glyph
        const auto c = on ? accent : PastelColors::knobEdge.brighter(0.25f);
        const float cx = w * 0.5f, cy = h * 0.5f, rad = 8.5f, ir = rad * 0.52f;
        g.setColour(c);
        g.drawEllipse(cx - rad, cy - rad, rad * 2, rad * 2, 1.4f);
        juce::Path arc;
        arc.addCentredArc(cx, cy, ir, ir, 0.0f, juce::degreesToRadians(35.0f), juce::degreesToRadians(325.0f), true);
        g.strokePath(arc, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine(cx, cy - ir - 1.5f, cx, cy - 0.5f, 1.4f);
        return;
    }
    // textless pill switch: the track fills with the accent when on
    float pw = juce::jmin(w - 4.0f, 40.0f), ph = juce::jmin(h - 4.0f, 16.0f);
    float px = (w - pw) * 0.5f, py = (h - ph) * 0.5f;
    g.setColour(on ? accent : PastelColors::track);
    g.fillRoundedRectangle(px, py, pw, ph, ph * 0.5f);
    if (! on)
    {
        g.setColour(PastelColors::knobEdge);
        g.drawRoundedRectangle(px + 0.5f, py + 0.5f, pw - 1, ph - 1, ph * 0.5f, 1.0f);
    }
    float knobR = ph - 5.0f;
    float kx = on ? px + pw - knobR - 2.5f : px + 2.5f;
    g.setColour(on ? PastelColors::barText : PastelColors::text);
    g.fillEllipse(kx, py + 2.5f, knobR, knobR);
}

void PastelLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool,
                                   int, int, int, int, juce::ComboBox& box)
{
    g.setColour(PastelColors::comboBg);
    g.fillRoundedRectangle(0, 0, w, h, 4.0f);
    g.setColour(box.hasKeyboardFocus(true) ? PastelLookAndFeel::accentFor(box) : PastelColors::knobEdge);
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1, h - 1, 4.0f, 1.0f);
    juce::Path arrow;
    arrow.startNewSubPath(w - 15.0f, h * 0.5f - 2.0f);
    arrow.lineTo(w - 11.0f, h * 0.5f + 2.0f);
    arrow.lineTo(w - 7.0f, h * 0.5f - 2.0f);
    g.setColour(PastelColors::textDim);
    g.strokePath(arrow, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::ignoreUnused(box);
}

void PastelLookAndFeel::drawGroupComponentOutline(juce::Graphics& g, int w, int h,
                                         const juce::String& text,
                                         const juce::Justification&, juce::GroupComponent& group)
{
    // v3 card: neutral edge, accent dot + title (no solid bar)
    g.setColour(PastelColors::panel);
    g.fillRoundedRectangle(0, 0, (float) w, (float) h, 8.0f);
    g.setColour(PastelColors::panelEdge);
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1.0f, h - 1.0f, 8.0f, 1.0f);
    g.setColour(PastelLookAndFeel::accentFor(group));
    g.fillEllipse(11.0f, 9.4f, 7.2f, 7.2f);
    g.setColour(PastelColors::text);
    g.setFont(logoFont(12.5f));
    // keep clear of the header power glyph on the right; squeeze long titles
    g.drawFittedText(text.toUpperCase(), 24, 0, w - 58, 26,
                     juce::Justification::centredLeft, 1, 0.75f);
}

void PastelLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                           const juce::Colour&, bool hover, bool down)
{
    float w = (float)b.getWidth(), h = (float)b.getHeight();
    g.setColour(down ? juce::Colour(0xFF2B2E34) : PastelColors::comboBg);
    g.fillRoundedRectangle(0, 0, w, h, 5.0f);
    g.setColour(hover || b.hasKeyboardFocus(true) ? PastelColors::teal : PastelColors::knobEdge);
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1, h - 1, 5.0f, hover ? 1.5f : 1.0f);
}

void PastelLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.setColour(PastelColors::panel);
    g.fillRoundedRectangle(0, 0, w, h, 5.0f);
    g.setColour(PastelColors::panelEdge);
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1, h - 1, 5.0f, 1.0f);
}

void PastelLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                        bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                        bool hasSubMenu, const juce::String& text,
                                        const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour(PastelColors::track);
        g.fillRect(area.getX() + 8, area.getCentreY(), area.getWidth() - 16, 1);
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour(PastelColors::teal.withAlpha(0.22f));
        g.fillRoundedRectangle(area.toFloat(), 3.0f);
    }
    g.setColour(!isActive ? PastelColors::textDim.withAlpha(0.5f)
                : isHighlighted ? PastelColors::tealBright : PastelColors::text);
    g.setFont(uiFont(13.0f));
    g.drawText(text, area.getX() + 12, area.getY(), area.getWidth() - 24, area.getHeight(),
               juce::Justification::centredLeft, true);
    if (isTicked)
    {
        g.setColour(PastelColors::teal);
        g.drawText("x", area.getX() + 2, area.getY(), 10, area.getHeight(),
                   juce::Justification::centred, true);
    }
    juce::ignoreUnused(hasSubMenu);
}

void PastelLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&,
                                      int x, int y, int w, int h, bool isVertical,
                                      int thumbStart, int thumbSize,
                                      bool, bool)
{
    g.setColour(PastelColors::track);
    if (isVertical)
        g.fillRoundedRectangle(x + w * 0.5f - 2, thumbStart, 4, thumbSize, 2.0f);
    else
        g.fillRoundedRectangle(thumbStart, y + h * 0.5f - 2, thumbSize, 4, 2.0f);
    juce::ignoreUnused(x); juce::ignoreUnused(y); juce::ignoreUnused(w); juce::ignoreUnused(h);
}
