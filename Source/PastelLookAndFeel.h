#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Dark flat design language: near-black charcoal, thin panel borders,
// solid accent section bars with dark titles, needle knobs, Lato.
struct PastelColors
{
    static inline const juce::Colour bg        { 0xFF141518 };
    static inline const juce::Colour panel     { 0xFF1C1F24 };
    static inline const juce::Colour panelEdge { 0xFF33363C };
    static inline const juce::Colour text      { 0xFFD9D5D0 };
    static inline const juce::Colour textDim   { 0xFF8F8B85 };
    static inline const juce::Colour track     { 0xFF33363C };
    static inline const juce::Colour comboBg   { 0xFF232529 };
    static inline const juce::Colour knobBody  { 0xFF26292F };
    static inline const juce::Colour knobEdge  { 0xFF3A3D43 };
    static inline const juce::Colour barText   { 0xFF1E2A28 };

    // single pastel-teal accent family (no orange anywhere)
    static inline const juce::Colour teal       { 0xFF8FD0C2 };
    static inline const juce::Colour tealBright { 0xFFB5E4D8 };
    static inline const juce::Colour tealDeep   { 0xFF5FAE9F };
    static inline const juce::Colour tealDark   { 0xFF3E7A6E };
    // one functional exception: keyswitch/articulation keys on the piano
    static inline const juce::Colour amber      { 0xFFE8B86A };
};

class PastelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PastelLookAndFeel();

    juce::Font uiFont(float h) const;
    juce::Font titleFont(float h) const;
    juce::Font logoFont(float h) const;
    static juce::Colour accentFor(juce::Component& c);

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h,
                          float pos, float min, float max,
                          const juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh,
                      juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void drawGroupComponentOutline(juce::Graphics&, int w, int h, const juce::String&,
                            const juce::Justification&, juce::GroupComponent&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                           bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void drawLinearSliderBackground(juce::Graphics&, int x, int y, int w, int h,
                                    float pos, float min, float max,
                                    const juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getLabelFont(juce::Label&) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h,
                       bool isVertical, int thumbStart, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override;

private:
    juce::Typeface::Ptr latoReg, latoBold, latoBlack;
};
