#include "PianoStrip.h"
#include "PluginProcessor.h"
#include "PastelLookAndFeel.h"

PianoStrip::PianoStrip(PastelProcessor& p) : proc(p)
{
    refreshMapping();
}

bool PianoStrip::isBlack(int midi)
{
    int pc = midi % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

void PianoStrip::refreshMapping()
{
    mapped.clear();
    switches.clear();
    int lo = 128, hi = -1;
    auto expand = [&](const std::vector<std::pair<int,int>>& ranges, std::set<int>& into)
    {
        for (auto& r : ranges)
            for (int k = juce::jmax(0, r.first); k <= juce::jmin(127, r.second); ++k)
            {
                into.insert(k);
                lo = juce::jmin(lo, k); hi = juce::jmax(hi, k);
            }
    };
    expand(proc.getMappedRanges(), mapped);
    expand(proc.getSwitchRanges(), switches);
    if (hi < 0) { loKey = 36; hiKey = 84; }
    else
    {
        // fit ALL content (mapped + switches): no cap, so low keyswitch
        // octaves and high switches are never cut off
        loKey = juce::jmax(0, (lo / 12) * 12 - 12);
        hiKey = juce::jmin(127, ((hi / 12) + 1) * 12 - 1 + 12);
        if (hiKey - loKey < 24) // minimum two octaves for usability
        {
            int mid = (loKey + hiKey) / 2;
            loKey = juce::jmax(0, mid - 12);
            hiKey = juce::jmin(127, mid + 12);
        }
    }
    rebuildLayout();
    repaint();
}

void PianoStrip::updateActive()
{
    auto v = proc.getActiveNotes();
    std::set<int> s(v.begin(), v.end());
    int sel = proc.getLastKeyswitch();
    if (s != active || sel != activeSwitch) { active = std::move(s); activeSwitch = sel; repaint(); }
}

void PianoStrip::rebuildLayout()
{
    whiteKeys.clear();
    blackKeys.clear();
    for (int m = loKey; m <= hiKey; ++m)
        if (!isBlack(m)) whiteKeys.push_back(m);
    float W = (float) juce::jmax(1, getWidth());
    whiteW = whiteKeys.empty() ? W : W / (float) whiteKeys.size();
    float bw = whiteW * 0.62f;
    for (int m = loKey; m <= hiKey; ++m)
    {
        if (!isBlack(m)) continue;
        // center on the boundary with the previous white key
        int wi = 0;
        for (size_t i = 0; i < whiteKeys.size(); ++i)
            if (whiteKeys[i] < m) wi = (int) i + 1;
        float cx = wi * whiteW;
        blackKeys.push_back({ m, cx - bw * 0.5f, cx + bw * 0.5f });
    }
}

void PianoStrip::resized()
{
    rebuildLayout();
}

int PianoStrip::keyAt(float x, float y) const
{
    float H = (float) getHeight();
    float blackH = H * 0.62f;
    if (y <= blackH)
        for (auto& b : blackKeys)
            if (x >= b.x0 && x <= b.x1) return b.midi;
    int wi = juce::jlimit(0, (int) whiteKeys.size() - 1, (int)(x / whiteW));
    return whiteKeys.empty() ? -1 : whiteKeys[(size_t) wi];
}

void PianoStrip::pressAt(float x, float y)
{
    int k = keyAt(x, y);
    if (k == heldMouseNote) return;
    if (heldMouseNote >= 0) proc.auditionNoteOff(heldMouseNote);
    heldMouseNote = k;
    if (heldMouseNote >= 0) proc.auditionNoteOn(heldMouseNote, 0.8f);
}

void PianoStrip::mouseDown(const juce::MouseEvent& e) { pressAt(e.position.x, e.position.y); }
void PianoStrip::mouseDrag(const juce::MouseEvent& e) { pressAt(e.position.x, e.position.y); }
void PianoStrip::mouseUp(const juce::MouseEvent&)
{
    if (heldMouseNote >= 0) { proc.auditionNoteOff(heldMouseNote); heldMouseNote = -1; }
}

void PianoStrip::paint(juce::Graphics& g)
{
    float W = (float) getWidth(), H = (float) getHeight();
    g.setColour(PastelColors::panel);
    g.fillRect(0, 0, (int) W, (int) H);

    // white keys (full-bleed)
    for (size_t i = 0; i < whiteKeys.size(); ++i)
    {
        int m = whiteKeys[i];
        float x = i * whiteW;
        bool isActive = active.count(m) > 0;
        bool isSwitch = switches.count(m) > 0;
        bool isMapped = mapped.count(m) > 0;
        bool isSelected = (m == activeSwitch);
        juce::Colour fill = juce::Colour(0xFFD8D3C6);
        if (isSwitch) fill = isSelected ? juce::Colours::white : PastelColors::amber;
        else if (isMapped) fill = PastelColors::teal;
        if (isActive) fill = juce::Colours::white;
        g.setColour(fill);
        g.fillRect(x, 0.0f, whiteW + 0.5f, H);
        g.setColour(PastelColors::knobEdge);
        g.drawLine(x + whiteW - 0.5f, 0, x + whiteW - 0.5f, H, 1.0f);
        g.drawLine(x, H - 0.5f, x + whiteW, H - 0.5f, 1.0f);
        if (isActive)
        {
            g.setColour(PastelColors::tealDeep);
            g.fillRect(x, 0.0f, whiteW + 0.5f, 3.0f);
        }
        // C labels
        if (m % 12 == 0 && whiteW > 26.0f)
        {
            g.setColour(PastelColors::textDim);
            g.setFont(juce::Font(10.0f));
            g.drawText("C" + juce::String(m / 12 - 1), x, H - 16, whiteW, 14,
                       juce::Justification::centred, false);
        }
    }
    // top edge
    g.setColour(PastelColors::panelEdge);
    g.drawLine(0, 0.5f, W, 0.5f, 1.0f);

    // black keys
    float blackH = H * 0.62f;
    for (auto& b : blackKeys)
    {
        bool isActive = active.count(b.midi) > 0;
        bool isSwitch = switches.count(b.midi) > 0;
        bool isMapped = mapped.count(b.midi) > 0;
        bool isSelected = (b.midi == activeSwitch);
        juce::Colour fill = juce::Colour(0xFF26292F);
        if (isSwitch) fill = isSelected ? PastelColors::amber.brighter(0.45f)
                                        : PastelColors::amber.darker(0.15f);
        else if (isMapped) fill = PastelColors::tealDeep;
        if (isActive) fill = PastelColors::tealBright;
        g.setColour(fill);
        g.fillRoundedRectangle(b.x0, 0, b.x1 - b.x0, blackH, 2.0f);
        g.setColour(PastelColors::knobEdge);
        g.drawRoundedRectangle(b.x0 + 0.5f, 0.5f, b.x1 - b.x0 - 1, blackH - 1, 2.0f, 1.0f);
    }
}
