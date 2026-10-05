#include "PastelLookAndFeel.h"
#include "PastelAssets.h"

PastelLookAndFeel::PastelLookAndFeel()
{
    latoReg   = juce::Typeface::createSystemTypefaceFor(PastelAssets::LatoRegular_ttf,
                                                       PastelAssets::LatoRegular_ttfSize);
    latoBold  = juce::Typeface::createSystemTypefaceFor(PastelAssets::LatoBold_ttf,
                                                       PastelAssets::LatoBold_ttfSize);
    latoBlack = juce::Typeface::createSystemTypefaceFor(PastelAssets::LatoBlack_ttf,
                                                       PastelAssets::LatoBlack_ttfSize);
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

juce::Font PastelLookAndFeel::getLabelFont(juce::Label&) { return uiFont(12.5f); }
juce::Font PastelLookAndFeel::getComboBoxFont(juce::ComboBox&) { return uiFont(13.0f); }
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
    // textless pill switch: identical colors on/off, state shown by knob side
    bool on = b.getToggleState();
    float w = (float)b.getWidth(), h = (float)b.getHeight();
    float pw = juce::jmin(w - 4.0f, 40.0f), ph = juce::jmin(h - 4.0f, 16.0f);
    float px = (w - pw) * 0.5f, py = (h - ph) * 0.5f;
    g.setColour(PastelColors::track);
    g.fillRoundedRectangle(px, py, pw, ph, ph * 0.5f);
    g.setColour(PastelColors::knobEdge);
    g.drawRoundedRectangle(px + 0.5f, py + 0.5f, pw - 1, ph - 1, ph * 0.5f, 1.0f);
    float knobR = ph - 5.0f;
    float kx = on ? px + pw - knobR - 2.5f : px + 2.5f;
    g.setColour(PastelColors::text);
    g.fillEllipse(kx, py + 2.5f, knobR, knobR);
    juce::ignoreUnused(b);
}

void PastelLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool,
                                   int, int, int, int, juce::ComboBox& box)
{
    g.setColour(PastelColors::comboBg);
    g.fillRoundedRectangle(0, 0, w, h, 4.0f);
    g.setColour(box.hasKeyboardFocus(true) ? PastelLookAndFeel::accentFor(box) : PastelColors::knobEdge);
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1, h - 1, 4.0f, 1.0f);
    juce::Path arrow;
    arrow.addTriangle(w - 16.0f, h * 0.5f - 3, w - 8.0f, h * 0.5f - 3, w - 12.0f, h * 0.5f + 3);
    g.setColour(PastelLookAndFeel::accentFor(box));
    g.fillPath(arrow);
    juce::ignoreUnused(box);
}

void PastelLookAndFeel::drawGroupComponentOutline(juce::Graphics& g, int w, int h,
                                         const juce::String& text,
                                         const juce::Justification&, juce::GroupComponent& group)
{
    g.setColour(PastelColors::panel);
    g.fillRoundedRectangle(0, 0, w, h, 5.0f);
    juce::Colour accent = PastelLookAndFeel::accentFor(group);
    g.setColour(accent);
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1, h - 1, 5.0f, 1.0f);
    const float barH = 24.0f;
    g.setColour(accent);
    g.fillRoundedRectangle(1, 1, w - 2, barH, 4.0f);
    g.fillRect(1, (int)(barH - 5), w - 2, 5);
    g.setColour(PastelColors::barText);
    g.setFont(titleFont(13.0f));
    g.drawText(text.toUpperCase(), 10, 0, w - 12, (int)barH, juce::Justification::centredLeft, false);
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
