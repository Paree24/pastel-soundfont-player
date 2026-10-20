#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Pastel v3 direct-manipulation views (shared by the Pastel plugins; keep the
// copies in sync). Every view edits real APVTS parameters through
// ParameterAttachment (same gesture path as a knob, so undo, automation and
// host "touch" all behave). The plugin fills in pastel::Theme once at startup.
namespace pastel
{
struct Theme
{
    static inline juce::Colour accent  { 0xfff5a05a }; // default when no "accent" property is found
    static inline juce::Colour inner   { 0xff16181c }; // view wells
    static inline juce::Colour grid    { 0xff24272c };
    static inline juce::Colour edge    { 0xff3a3d43 };
    static inline juce::Colour comboBg { 0xff232529 };
    static inline juce::Colour text    { 0xffd9d5d0 };
    static inline juce::Colour textDim { 0xff8f8b85 };
    static inline juce::Colour faint   { 0xff5c5a56 };
    static inline juce::Colour handle  { 0xfff4f1ec };
    static inline juce::Colour ink     { 0xff1b1d22 }; // text on a filled accent
    static inline juce::Typeface::Ptr regular, bold;
    // readable value text for a parameter id (Hz, s/ms, dB, cents, %)
    static inline std::function<juce::String(const juce::String& id, float v)> format =
        [](const juce::String&, float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; };
};
juce::Colour accentFor(juce::Component&);   // nearest "accent" property up the parent chain
juce::Font font(float h, bool bold = true);

enum class Wave { none, tri, saw, pulse, square, sine, noise, snh, digital };
void drawWave(juce::Graphics&, juce::Rectangle<float>, Wave, float pw, float cycles,
              juce::Colour, float thickness, bool fill, float phase = 0.0f);

class Params
{
public:
    Params(juce::Component& owner, juce::AudioProcessorValueTreeState&, juce::StringArray ids);
    float get(int i) const { return vals[(size_t)i]; }
    float norm(int i) const { return params[(size_t)i]->convertTo0to1(vals[(size_t)i]); }
    void begin(int i) { atts[(size_t)i]->beginGesture(); }
    void end(int i) { atts[(size_t)i]->endGesture(); }
    void setNorm(int i, float n);
    void setOnce(int i, float v) { atts[(size_t)i]->setValueAsCompleteGesture(v); }
    juce::String text(int i) const { return Theme::format(ids[i], vals[(size_t)i]); }
    int size() const { return (int)vals.size(); }
    juce::RangedAudioParameter& param(int i) { return *params[(size_t)i]; }
private:
    juce::StringArray ids;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    std::vector<float> vals;
};

// Shared drawing for the views.
class View : public juce::Component, public juce::SettableTooltipClient
{
public:
    // overlay: no well behind the curve, and the mouse only lands near this
    // view's own handles (so views can be stacked over a waveform)
    void setOverlay(bool b) { overlay = b; repaint(); }
    void setReadoutRow(int row) { readoutRow = row; repaint(); }   // stack readouts: 0, 1, ...
    bool hitTest(int x, int y) override;
protected:
    bool overlay = false;
    int readoutRow = 0;
    // handle positions for overlay hit-testing (filled by paint)
    std::vector<juce::Point<float>> handlePts;
    juce::Colour accent() { return accentFor(*this); }
    void drawWell(juce::Graphics&, juce::Rectangle<float>, int cols, int rows);
    void drawHandle(juce::Graphics&, float x, float y, juce::Colour, float r = 5.0f);
    void drawReadout(juce::Graphics&, juce::Rectangle<float> area, const juce::String&, bool right = true);
};

// Segmented choice bound to an AudioParameterChoice; optional wave icons.
class SegChoice : public View
{
public:
    SegChoice(juce::AudioProcessorValueTreeState&, const juce::String& id,
              juce::StringArray labels = {}, std::vector<Wave> icons = {});
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    Params ps;
    juce::StringArray labels;
    std::vector<Wave> icons;
};

// Oscillator shape; drag sideways on a pulse wave to set the pulse width.
class WaveView : public View
{
public:
    WaveView(juce::AudioProcessorValueTreeState&, const juce::String& waveId, const juce::String& pwId,
             std::vector<Wave> kinds);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    Params ps; std::vector<Wave> kinds; bool dragging = false;
    Wave kind() const;
};

// Filter response; drag: cutoff sideways, resonance up/down. modeId (a choice)
// picks the response through `modes` (default { lp12, lp24 }: a 12 / 24 dB
// slope switch); empty = fixed 12 dB low-pass. With hpCutId/hpPeakId a
// resonant high-pass sits in front (MS-20 style) with its own handle.
class FilterView : public View
{
public:
    enum class Mode { lp12, lp24, bp12, hp12 };
    FilterView(juce::AudioProcessorValueTreeState&, const juce::String& cutoffId,
               const juce::String& resoId, const juce::String& modeId,
               const juce::String& hpCutId = {}, const juce::String& hpPeakId = {},
               std::vector<Mode> modes = { Mode::lp12, Mode::lp24 });
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    // for a cutoff parameter that is not stored in Hz (e.g. 0..1): value <-> Hz
    void setHzMapping(std::function<float(float)> toHz, std::function<float(float)> fromHz)
    {
        cutToHz = std::move(toHz); cutFromHz = std::move(fromHz); repaint();
    }
private:
    std::function<float(float)> cutToHz = [](float v) { return v; }, cutFromHz = [](float hz) { return hz; };
    Params ps; bool hasSlope, hasHp; std::vector<Mode> modes; int active = 0; // 0 = main handle, 1 = high-pass
    juce::Rectangle<float> plot() const;
    float xOf(float hz) const;
    void apply(juce::Point<float>);
};

// LFO shape with its delayed fade-in; drag the fade handle to set the delay
// (empty delayId = just the shape). kinds maps the wave choice to a shape.
class LfoView : public View
{
public:
    LfoView(juce::AudioProcessorValueTreeState&, const juce::String& waveId, const juce::String& delayId,
            std::vector<Wave> kinds);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    Params ps; bool hasDelay; std::vector<Wave> kinds;
};

// ADSR you drag: attack peak, decay/sustain corner, release end. An optional
// fifth id adds a DELAY segment before the attack or a HOLD after it.
class EnvView : public View
{
public:
    enum class Extra { none, delay, hold };
    EnvView(juce::AudioProcessorValueTreeState&, juce::StringArray ids, const juce::String& title,
            Extra extra = Extra::none);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    // > 1 spreads short times out for linear-range time parameters (drawing and
    // dragging only; the parameters are untouched). 1 = follow the parameter.
    void setTimeSkew(float s) { skew = juce::jmax(1.0f, s); repaint(); }
    // false: no A/D/S/R value row (when knobs underneath already show the values)
    void setShowValues(bool b) { showValues = b; repaint(); }
private:
    Params ps; juce::String title; Extra extra; int active = -1; float skew = 1.0f; bool showValues = true;
    float shown(int i) const { return std::pow(ps.norm(i), 1.0f / skew); }
    void setShown(int i, float t) { ps.setNorm(i, std::pow(juce::jlimit(0.0f, 1.0f, t), skew)); }
    struct Geo { float x0, xE, xA, xH, xD, xS, xR, top, bot, zE, zA, zD, zR; };
    Geo geo() const;
};

// Effect picture + XY pad (two parameters); dims when the effect is off.
class FxView : public View
{
public:
    enum class Style { chorus, phaser, delay, reverb };
    // syncId: when non-zero the x parameter is replaced by a tempo division
    // (its text, or divId's text if given, shows in the readout)
    FxView(juce::AudioProcessorValueTreeState&, Style, const juce::String& xId, const juce::String& yId,
           const juce::String& onId, const juce::String& syncId = {}, const juce::String& divId = {},
           juce::String xLabel = {}, juce::String yLabel = {});
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    Params ps; Style style; bool hasSync, hasDiv; juce::String xLab, yLab;
    bool xFree() const;
    void apply(juce::Point<float>);
};

// Two-shelf EQ curve; drag the left / right half up or down.
class EqView : public View
{
public:
    EqView(juce::AudioProcessorValueTreeState&, const juce::String& lowId, const juce::String& highId);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
private:
    Params ps; int active = 0;
};
// Saturation transfer curve; drag up/down for the amount. typeId (optional)
// picks the curve: Tape, Tube, Fold, Fuzz.
class DriveView : public View
{
public:
    DriveView(juce::AudioProcessorValueTreeState&, const juce::String& amountId, const juce::String& typeId = {});
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    Params ps; bool hasType; float startV = 0.0f; float startY = 0.0f;
    float shape(float x) const;
};
// A decaying sweep (pitch envelope): drag the start point up / down for the
// amount (bipolar ranges sweep from either side of the centre line), the end
// point sideways for the time, the middle up / down for the curve (optional).
class SweepView : public View
{
public:
    SweepView(juce::AudioProcessorValueTreeState&, const juce::String& amountId, const juce::String& timeId,
              const juce::String& curveId = {}, juce::String label = {});
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void setTimeSkew(float s) { skew = juce::jmax(1.0f, s); repaint(); }
private:
    Params ps; bool hasCurve; juce::String label; int active = -1; float skew = 1.0f;
    struct Geo { juce::Rectangle<float> p; float base, x0, xT, yA; bool bipolar; };
    Geo geo() const;
    float yAt(const Geo&, float u) const;   // u = 0..1 along the sweep
};
} // namespace pastel
