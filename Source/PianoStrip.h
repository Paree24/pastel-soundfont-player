#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <vector>

class PastelProcessor;

// ============================================================
// PianoStrip: full-bleed on-screen keyboard.
// - white keys always span the full width (no gaps)
// - teal  = key has a sample mapped
// - amber = keyswitch / articulation selector
// - bright = currently sounding (incl. external MIDI + arp)
// Click / drag to audition.
// ============================================================
class PianoStrip : public juce::Component
{
public:
    explicit PianoStrip(PastelProcessor& p);
    void refreshMapping(); // re-read mapped + switch ranges (call on file load)
    void updateActive();   // poll sounding notes (call on a timer)

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    static bool isBlack(int midi);
    void rebuildLayout();
    int keyAt(float x, float y) const;
    void pressAt(float x, float y);

    PastelProcessor& proc;
    int loKey = 36, hiKey = 84;
    std::vector<int> whiteKeys;
    struct BlackGeom { int midi; float x0, x1; };
    std::vector<BlackGeom> blackKeys;
    float whiteW = 20.0f;

    std::set<int> mapped, switches, active;
    int activeSwitch = -1; // currently selected articulation key
    int heldMouseNote = -1;
};
