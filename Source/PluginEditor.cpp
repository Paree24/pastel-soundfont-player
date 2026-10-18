#include "PluginEditor.h"

// non-ASCII literals must be decoded as UTF-8 (plain char* is read as ASCII)
static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }

// ---------------- SoundListModel ----------------
void SoundListModel::setEntries(const std::vector<FileLibrary::Entry>& e) { entries = e; }

int SoundListModel::getNumRows() { return (int) entries.size(); }

FileLibrary::Entry SoundListModel::entryAt(int row) const
{
    if (row >= 0 && row < (int) entries.size()) return entries[(size_t) row];
    return {};
}

int SoundListModel::indexOfPath(const juce::String& path) const
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (!entries[i].isDir && entries[i].path == path) return (int) i;
    return -1;
}

void LoadBar::paint(juce::Graphics& g)
{
    float w = (float) getWidth(), h = (float) getHeight();
    g.setColour(PastelColors::track);
    g.fillRoundedRectangle(0, 0, w, h, h * 0.5f);
    if (progress > 0.001f)
    {
        g.setColour(PastelColors::teal);
        g.fillRoundedRectangle(0, 0, w * progress, h, h * 0.5f);
    }
    g.setColour(PastelColors::textDim);
    g.setFont(juce::Font(9.0f));
    g.drawText("LOADING " + juce::String((int)(progress * 100.0f)) + "%",
               6, 0, (int) w - 12, (int) h, juce::Justification::centredLeft, false);
}

void SoundListModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) entries.size()) return;
    auto& e = entries[(size_t) row];
    if (selected)
    {
        g.setColour(PastelColors::teal.withAlpha(0.22f));
        g.fillRoundedRectangle(2, 1, w - 4, h - 2, 4.0f);
    }
    if (e.isDir)
    {
        g.setColour(PastelColors::tealDark);
        g.fillRoundedRectangle(6, h * 0.5f - 8, 34, 16, 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("DIR", 6, h * 0.5f - 8, 34, 16, juce::Justification::centred, false);
        g.setColour(selected ? PastelColors::text : PastelColors::textDim);
        g.setFont(juce::Font(12.5f));
        g.drawText(e.name, 46, 0, w - 60, h, juce::Justification::centredLeft, true);
        g.setColour(PastelColors::textDim);
        g.drawText(">", w - 18, 0, 12, h, juce::Justification::centred, false);
    }
    else
    {
        g.setColour(e.isSf2 ? PastelColors::teal : PastelColors::tealDeep);
        g.fillRoundedRectangle(6, h * 0.5f - 8, 34, 16, 4.0f);
        g.setColour(e.isSf2 ? PastelColors::barText : juce::Colours::white);
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(e.isSf2 ? "SF2" : "SFZ", 6, h * 0.5f - 8, 34, 16, juce::Justification::centred, false);
        g.setColour(PastelColors::text);
        g.setFont(juce::Font(12.5f));
        g.drawText(e.name, 46, 0, w - 50, h, juce::Justification::centredLeft, true);
    }
}

// ---------------- PastelEditor ----------------
static void styleGroup(juce::GroupComponent& g, juce::Colour accent)
{
    g.getProperties().set("accent", (int) accent.getARGB());
}

PastelEditor::PastelEditor(PastelProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p), piano(p), presetBrowser(p, lnf)
{
    setLookAndFeel(&lnf);

    // ---- header ----
    logoLabel.setText("PASTEL PLAYER", juce::dontSendNotification);
    logoLabel.setFont(lnf.logoFont(21.0f));
    logoLabel.setColour(juce::Label::textColourId, PastelColors::text);
    addAndMakeVisible(logoLabel);

    fileLabel.setFont(lnf.uiFont(13.0f));
    fileLabel.setColour(juce::Label::textColourId, PastelColors::textDim);
    fileLabel.setMinimumHorizontalScale(0.5f);
    addAndMakeVisible(fileLabel);

    addAndMakeVisible(loadBtn);
    addAndMakeVisible(prevBtn);
    addAndMakeVisible(nextBtn);
    addAndMakeVisible(panicBtn);
    loadBtn.onClick = [&]
    {
        auto fc = std::make_shared<juce::FileChooser>("Load SF2 / SFZ",
            juce::File(proc.library.getCurrentDir()),
            "*.sf2;*.sfz;*.sf3");
        fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, fc](const juce::FileChooser&)
            {
                auto f = fc->getResult();
                if (f.existsAsFile())
                    proc.requestLoad(f.getFullPathName()); // background; errors via timer poll
            });
    };
    prevBtn.onClick = [&] { selectRelative(-1); };
    nextBtn.onClick = [&] { selectRelative(1); };
    panicBtn.onClick = [&] { proc.panic(); };

    bankLabel.setFont(lnf.uiFont(11.0f));
    bankLabel.setColour(juce::Label::textColourId, PastelColors::textDim);
    addAndMakeVisible(bankLabel);
    presetBoxLabel.setFont(lnf.uiFont(11.0f));
    presetBoxLabel.setColour(juce::Label::textColourId, PastelColors::textDim);
    addAndMakeVisible(presetBoxLabel);
    addAndMakeVisible(bankBox);
    addAndMakeVisible(presetBox);
    for (int i = 0; i < 128; ++i) bankBox.addItem(juce::String(i), i + 1);
    bankBox.setSelectedId(1, juce::dontSendNotification);
    bankBox.onChange = [&]
    {
        if (auto* par = proc.apvts.getParameter(PP::BANK))
            par->setValueNotifyingHost((float)(bankBox.getSelectedId() - 1) / 127.0f);
    };
    presetBox.onChange = [&]
    {
        // item -> (bank, program) of that preset; the list index is NOT a
        // program number (sparse/multi-bank SF2s)
        int idx = presetBox.getSelectedId() - 1;
        if (idx < 0 || idx >= (int) presetList.size()) return;
        const auto& pr = presetList[(size_t) idx];
        if (auto* par = proc.apvts.getParameter(PP::BANK))
            par->setValueNotifyingHost(par->convertTo0to1((float) pr.bank));
        if (auto* par = proc.apvts.getParameter(PP::PRESET))
            par->setValueNotifyingHost(par->convertTo0to1((float) pr.program));
    };

    // ---- browser ----
    styleGroup(libGroup, PastelColors::teal);
    addAndMakeVisible(libGroup);
    searchBox.setTextToShowWhenEmpty("Search here...", PastelColors::textDim);
    searchBox.setJustification(juce::Justification::centredLeft);
    searchBox.onTextChange = [&] { refreshFileList(); };
    addAndMakeVisible(searchBox);
    addAndMakeVisible(dirBox);
    dirBox.onChange = [&]
    {
        int idx = dirBox.getSelectedItemIndex();
        auto sc = proc.library.getShortcuts();
        if (idx >= 0 && idx < sc.size()) proc.library.setCurrentDir(sc[idx]);
    };
    addAndMakeVisible(addDirBtn);
    addAndMakeVisible(delDirBtn);
    addAndMakeVisible(upBtn);
    addAndMakeVisible(refreshBtn);
    addAndMakeVisible(loadSelBtn);
    addDirBtn.onClick = [&]
    {
        auto fc = std::make_shared<juce::FileChooser>("Add folder shortcut",
            juce::File(proc.library.getCurrentDir()));
        fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, fc](const juce::FileChooser&)
            {
                auto d = fc->getResult();
                if (d.isDirectory())
                {
                    proc.library.addShortcut(d.getFullPathName());
                    proc.library.setCurrentDir(d.getFullPathName());
                }
            });
    };
    delDirBtn.onClick = [&] { proc.library.removeShortcut(dirBox.getSelectedItemIndex()); };
    upBtn.onClick = [&]
    {
        auto parent = juce::File(proc.library.getCurrentDir()).getParentDirectory();
        if (parent.isDirectory()) proc.library.setCurrentDir(parent.getFullPathName());
    };
    refreshBtn.onClick = [&] { refreshFileList(); };
    loadSelBtn.onClick = [&] { loadSelectedFile(); };
    pathLabel.setFont(lnf.uiFont(11.0f));
    pathLabel.setColour(juce::Label::textColourId, PastelColors::textDim);
    addAndMakeVisible(pathLabel);
    addChildComponent(loadBar); // hidden until a load is running
    fileList.setModel(&listModel);
    fileList.setRowHeight(26);
    fileList.addMouseListener(this, true);
    addAndMakeVisible(fileList);

    // ---- groups ----
    styleGroup(envGroup, PastelColors::teal);
    styleGroup(filterGroup, PastelColors::teal);
    styleGroup(lfoGroup, PastelColors::teal);
    styleGroup(fxGroup, PastelColors::teal);
    styleGroup(arpGroup, PastelColors::teal);
    addAndMakeVisible(envGroup);
    addAndMakeVisible(filterGroup);
    addAndMakeVisible(lfoGroup);
    addAndMakeVisible(fxGroup);
    addAndMakeVisible(arpGroup);

    // envelope: knobs
    addKnob(PP::ATK, "Attack"); addKnob(PP::DEC, "Decay");
    addKnob(PP::SUS, "Sustain"); addKnob(PP::REL, "Release");

    // filter
    addKnob(PP::FCUT, "Cutoff");
    addKnob(PP::FRES, "Reso");
    addKnob(PP::FDRIVE, "Drive");
    addChoice(PP::FTYPE, "Type", { "Low Pass", "High Pass", "Band Pass" });
    addChoice(PP::FSLOPE, "Slope", { "6 dB", "12 dB", "24 dB", "48 dB" });

    // lfo
    addKnob(PP::LFORATE, "Rate");
    addKnob(PP::LFODEPTH, "Depth");
    addChoice(PP::LFOWAVE, "Wave", { "Sine", "Triangle", "Saw", "Square", "S&H" });

    // fx cards
    auto card = [&](const juce::String& t, juce::Colour c) { return addFxCard(t, c); };
    card("Chorus", PastelColors::teal);
    card("Phaser", PastelColors::teal);
    card("Flanger", PastelColors::teal);
    card("Distortion", PastelColors::teal);
    card("Saturation", PastelColors::teal);
    card("Reverb", PastelColors::teal);
    card("Delay", PastelColors::teal);
    card("Master", PastelColors::teal);

    addSwitch(PP::CHON, "On");
    addKnob(PP::CHRATE, "Rate"); addKnob(PP::CHDEPTH, "Depth"); addKnob(PP::CHMIX, "Mix");
    addChoice(PP::CHMODE, "Mode", { "Manual", "Vintage I", "Vintage II", "Wide" });

    addSwitch(PP::PHON, "On");
    addKnob(PP::PHRATE, "Rate"); addKnob(PP::PHDEPTH, "Depth");
    addKnob(PP::PHFB, "Fdbk"); addKnob(PP::PHMIX, "Mix");

    addSwitch(PP::FLON, "On");
    addKnob(PP::FLRATE, "Rate"); addKnob(PP::FLDEPTH, "Depth");
    addKnob(PP::FLFB, "Fdbk"); addKnob(PP::FLMIX, "Mix");

    addSwitch(PP::DION, "On");
    addKnob(PP::DIDRIVE, "Drive"); addKnob(PP::DIMIX, "Mix");

    addSwitch(PP::SAON, "On");
    addKnob(PP::SAAMT, "Amount"); addKnob(PP::SATONE, "Tone");
    addChoice(PP::SAMODE, "Mode", { "Tape", "Tube" });

    addSwitch(PP::RVON, "On");
    addKnob(PP::RVSIZE, "Size"); addKnob(PP::RVDAMP, "Damp"); addKnob(PP::RVMIX, "Mix");

    addSwitch(PP::DLON, "On");
    addKnob(PP::DLTIME, "Time"); addKnob(PP::DLFB, "Fdbk"); addKnob(PP::DLMIX, "Mix");
    addSwitch(PP::DLSYNC, "Sync");
    addChoice(PP::DLDIV, "Div", { "1/4", "1/8", "1/8 D", "1/4 T", "1/16", "1/2" });

    addKnob(PP::VOLUME, "Volume");
    addSwitch(PP::LIMIT, "Limiter");

    // arp
    addSwitch(PP::ARPON, "On");
    addChoice(PP::ARPMODE, "Mode", { "Up", "Down", "Up-Down", "Random" });
    addChoice(PP::ARPDIV, "Div", { "1/4", "1/4T", "1/4 D", "1/8", "1/8T", "1/8 D",
                                   "1/16", "1/16T", "1/16 D", "1/32", "1/32T" });
    addKnob(PP::ARPOCT, "Oct");
    addKnob(PP::ARPGATE, "Gate");

    // master mono (after arp so switch indices above stay stable)
    addSwitch(PP::MONO, "Mono");

    // mid-column group toggles: ADSR / Filter / LFO (indices 11,12,13).
    // All default off: the soundfont plays as authored until engaged.
    addSwitch(PP::ADSRON, "ADSR On");
    addSwitch(PP::FTON, "Filter On");
    addSwitch(PP::LFOON, "LFO On");

    // arp groove + voice card (held by pointer, so the indices above stay put)
    swingKnob = addKnob(PP::ARPSWING, "Swing");
    latchSwitch = addSwitch(PP::ARPLATCH, "Latch: keep playing after the keys go up");
    velSwitch = addSwitch(PP::ARPVEL, "Played velocity (off: fixed)");
    styleGroup(voiceGroup, PastelColors::teal);
    addAndMakeVisible(voiceGroup);
    voiceChoice = addChoice(PP::VMODE, "Mode", { "Poly", "Mono", "Legato" });
    glideKnob = addKnob(PP::GLIDE, "Glide");
    interpChoice = addChoice(PP::INTERP, "Quality", { "Linear", "Cubic", "Sinc HQ" });
    voiceChoice->c->setTooltip("Poly, Mono (retrigger) or Legato (overlapping keys slide)");
    glideKnob->s->setTooltip("Glide time between notes in Mono / Legato");
    interpChoice->c->setTooltip("Sample interpolation: Sinc HQ is cleanest when pitching up, at more CPU");

    // master switch captions (switches themselves stay textless)
    for (auto* cap : { &limCap, &monoCap, &syncCap, &latchCap, &velCap })
    {
        cap->setJustificationType(juce::Justification::centred);
        cap->setFont(lnf.uiFont(9.5f));
        cap->setColour(juce::Label::textColourId, PastelColors::textDim);
        addAndMakeVisible(cap);
    }

    // piano strip (full-bleed, highlights mapped + keyswitch + active notes)
    addAndMakeVisible(piano);

    // preset browser overlay (hidden until PRESETS)
    presetsBtn.setTooltip("Save and recall the loaded sound with all its settings");
    presetsBtn.onClick = [&] { presetBrowser.setVisible(true); presetBrowser.toFront(true); };
    addAndMakeVisible(presetsBtn);
    presetBrowser.onClose = [&] { presetBrowser.setVisible(false); };
    addChildComponent(presetBrowser);

    proc.addChangeListener(this);
    proc.library.addChangeListener(this);

    refreshShortcuts();
    refreshFileList();
    refreshPresetBoxes();
    refreshFileLabel();

    setSize(proc.library.getWindowW(), proc.library.getWindowH());
    setResizable(true, true);
    setResizeLimits(1100, 640, 1600, 1000);
    startTimerHz(20);
}

PastelEditor::~PastelEditor()
{
    stopTimer();
    proc.removeChangeListener(this);
    proc.library.removeChangeListener(this);
    setLookAndFeel(nullptr);
    proc.library.setWindowSize(getWidth(), getHeight());
}

PastelEditor::Knob* PastelEditor::addKnob(const juce::String& pid, const juce::String& title)
{
    auto k = std::make_unique<Knob>();
    k->s = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                          juce::Slider::NoTextBox);
    addAndMakeVisible(k->s.get());
    k->l = std::make_unique<juce::Label>("", title);
    k->l->setJustificationType(juce::Justification::centred);
    k->l->setFont(lnf.uiFont(11.5f));
    k->l->setColour(juce::Label::textColourId, PastelColors::text);
    addAndMakeVisible(k->l.get());
    k->a = std::make_unique<Attach::SliderAttachment>(proc.apvts, pid, *k->s);
    // value popup while dragging/hovering, double-click resets to default
    k->s->setPopupDisplayEnabled(true, true, this);
    if (auto* prm = proc.apvts.getParameter(pid))
        k->s->setDoubleClickReturnValue(true, prm->convertFrom0to1(prm->getDefaultValue()));
    k->s->setTooltip(title + "  (double-click: reset)");
    Knob* ptr = k.get();
    knobs.push_back(std::move(k));
    return ptr;
}

PastelEditor::Switch* PastelEditor::addSwitch(const juce::String& pid, const juce::String& title)
{
    auto sw = std::make_unique<Switch>();
    sw->b = std::make_unique<juce::ToggleButton>("");
    sw->b->setTooltip(title);
    addAndMakeVisible(sw->b.get());
    sw->a = std::make_unique<Attach::ButtonAttachment>(proc.apvts, pid, *sw->b);
    Switch* ptr = sw.get();
    switches.push_back(std::move(sw));
    return ptr;
}

PastelEditor::Choice* PastelEditor::addChoice(const juce::String& pid,
    const juce::String& title, const juce::StringArray& items)
{
    auto c = std::make_unique<Choice>();
    c->c = std::make_unique<juce::ComboBox>();
    for (int i = 0; i < items.size(); ++i) c->c->addItem(items[i], i + 1);
    addAndMakeVisible(c->c.get());
    c->l = std::make_unique<juce::Label>("", title);
    c->l->setFont(lnf.uiFont(11.0f));
    c->l->setColour(juce::Label::textColourId, PastelColors::textDim);
    addAndMakeVisible(c->l.get());
    c->a = std::make_unique<Attach::ComboBoxAttachment>(proc.apvts, pid, *c->c);
    Choice* ptr = c.get();
    choices.push_back(std::move(c));
    return ptr;
}

juce::GroupComponent* PastelEditor::addFxCard(const juce::String& title, juce::Colour accent)
{
    auto g = std::make_unique<juce::GroupComponent>("", title);
    styleGroup(*g, accent);
    addAndMakeVisible(g.get());
    fxCards.add(g.release());
    return fxCards.getLast();
}

void PastelEditor::paint(juce::Graphics& g)
{
    g.fillAll(PastelColors::bg);
    g.setColour(PastelColors::panel);
    g.fillRoundedRectangle(10, 8, getWidth() - 20, 58, 6.0f);
    g.setColour(PastelColors::panelEdge);
    g.drawRoundedRectangle(10.5f, 8.5f, getWidth() - 21, 57, 6.0f, 1.0f);
    paintMeter(g, meterArea);
}

void PastelEditor::paintOverChildren(juce::Graphics& g)
{
    if (!dropHover) return;
    g.setColour(PastelColors::teal.withAlpha(0.12f));
    g.fillRect(getLocalBounds());
    g.setColour(PastelColors::teal);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(6.0f), 8.0f, 2.0f);
    g.setFont(lnf.titleFont(20.0f));
    g.drawText("DROP TO LOAD", getLocalBounds(), juce::Justification::centred, false);
}

void PastelEditor::paintMeter(juce::Graphics& g, juce::Rectangle<int> area)
{
    // label left, two bars vertically centred on the header panel
    const int barH = 8, gap = 5, labelW = 30;
    int y0 = area.getCentreY() - (2 * barH + gap) / 2;
    g.setColour(PastelColors::textDim);
    g.setFont(lnf.uiFont(10.0f));
    g.drawText("OUT", area.getX(), y0, labelW - 4, 2 * barH + gap, juce::Justification::centredLeft, false);
    auto bar = juce::Rectangle<int>(area.getX() + labelW, y0, area.getWidth() - labelW, barH);
    auto bar2 = bar.translated(0, barH + gap);
    auto drawBar = [&](juce::Rectangle<int> b, float v)
    {
        g.setColour(PastelColors::track);
        g.fillRoundedRectangle(b.toFloat(), 3.0f);
        float w = juce::jlimit(0.0f, 1.0f, v) * (float) b.getWidth();
        g.setColour(v > 0.98f ? PastelColors::tealBright : PastelColors::teal);
        if (w > 1.0f) g.fillRoundedRectangle(b.withWidth((int) w).toFloat(), 3.0f);
    };
    drawBar(bar, meterL);
    drawBar(bar2, meterR);
}

void PastelEditor::resized()
{
    const int M = 10;
    const int W = getWidth(), H = getHeight();
    const int gap = 8;        // between cards
    const int barH = 26;      // group title bar incl. border
    const int pad = 10;       // inner horizontal inset (matches title text)

    // group-title toggle: vertically centred in the title bar, right inset
    // mirroring the title's left inset — same spot on every card
    auto placeHeaderSwitch = [&](juce::ToggleButton& b, juce::Rectangle<int> card)
    {
        b.setBounds(card.getRight() - 52, card.getY() + 3, 46, 20);
    };
    // caption-above-combo pair anchored to a card's bottom edge
    auto placeChoiceBottom = [&](Choice& c, juce::Rectangle<int> card, int x, int w)
    {
        c.l->setBounds(x, card.getBottom() - 46, w, 13);
        c.c->setBounds(x, card.getBottom() - 32, w, 24);
    };
    auto placeKnob = [&](Knob& k, int cx, int y, int size, int labelW)
    {
        k.s->setBounds(cx - size / 2, y, size, size);
        k.l->setBounds(cx - labelW / 2, y + size + 1, labelW, 14);
    };

    // ---------- header (panel y 8..66, centre 37) ----------
    logoLabel.setBounds(M + pad, 13, 220, 24);
    fileLabel.setBounds(M + pad, 37, 380, 18);
    int btnX = M + 400;
    const int btnY = 22, btnH = 30;
    loadBtn.setBounds(btnX, btnY, 76, btnH); btnX += 76 + 6;
    prevBtn.setBounds(btnX, btnY, 30, btnH); btnX += 30 + 6;
    nextBtn.setBounds(btnX, btnY, 30, btnH); btnX += 30 + 6;
    panicBtn.setBounds(btnX, btnY, 70, btnH); btnX += 70 + 6;
    presetsBtn.setBounds(btnX, btnY, 84, btnH);

    const int meterW = 150;
    meterArea = { W - M - pad - meterW, 15, meterW, 44 };
    int rightX = meterArea.getX() - 20;
    // program box shrinks so it never runs into the transport buttons
    const int bankW = 64;
    const int presetW = juce::jlimit(120, 220, rightX - (presetsBtn.getRight() + 20) - bankW - 10);
    presetBox.setBounds(rightX - presetW, 30, presetW, 24);
    presetBoxLabel.setBounds(rightX - presetW, 15, presetW, 13);
    rightX -= presetW + 10;
    bankBox.setBounds(rightX - bankW, 30, bankW, 24);
    bankLabel.setBounds(rightX - bankW, 15, bankW, 13);

    // ---------- main areas ----------
    const int top = 74;
    const int kbH = 70, arpH = 92;
    int mainH = H - top - arpH - kbH - 2 * gap - M; // bottom margin == side margin
    mainH = juce::jmax(330, mainH);

    const int libW = 250, midW = 300;
    const int mx = M + libW + gap;
    const int fxX = mx + midW + gap;
    const int fxW = W - fxX - M;

    // ----- browser card -----
    libGroup.setBounds(M, top, libW, mainH);
    {
        const int lx = M + pad, lw = libW - 2 * pad;
        int ly = top + barH + 8;
        searchBox.setBounds(lx, ly, lw, 24); ly += 30;
        dirBox.setBounds(lx, ly, lw, 24); ly += 30;
        int bw = (lw - 3 * 6) / 4;
        addDirBtn.setBounds(lx, ly, bw, 24);
        delDirBtn.setBounds(lx + (bw + 6), ly, bw, 24);
        upBtn.setBounds(lx + 2 * (bw + 6), ly, bw, 24);
        refreshBtn.setBounds(lx + 3 * (bw + 6), ly, lw - 3 * (bw + 6), 24);
        ly += 30;
        pathLabel.setBounds(lx, ly, lw, 15); ly += 19;
        // progress bar shares the row with LOAD, aligned to its centre
        loadSelBtn.setBounds(lx + lw - 76, ly, 76, 24);
        loadBar.setBounds(lx, ly + 6, lw - 84, 12);
        ly += 30;
        fileList.setBounds(lx, ly, lw, top + mainH - pad - ly);
    }

    // ----- mid column -----
    const int envH = (int)(mainH * 0.27), filtH = (int)(mainH * 0.44);
    const int lfoH = mainH - envH - filtH - 2 * gap;
    const juce::Rectangle<int> envR(mx, top, midW, envH);
    const juce::Rectangle<int> filtR(mx, envR.getBottom() + gap, midW, filtH);
    const juce::Rectangle<int> lfoR(mx, filtR.getBottom() + gap, midW, lfoH);
    envGroup.setBounds(envR);
    filterGroup.setBounds(filtR);
    lfoGroup.setBounds(lfoR);
    placeHeaderSwitch(*switches[11]->b, envR);
    placeHeaderSwitch(*switches[12]->b, filtR);
    placeHeaderSwitch(*switches[13]->b, lfoR);

    const int ky = barH + 8; // knob top offset inside a card
    // top of a knob block of height `block`, centred in the card's free area
    auto centredY = [&](juce::Rectangle<int> card, int block, int reserveBottom)
    {
        int avail = card.getHeight() - ky - 6 - reserveBottom;
        return card.getY() + ky + juce::jmax(0, (avail - block) / 2);
    };
    {   // env knobs 0..3
        int kw = (midW - 2 * pad) / 4;
        int kh = std::min({ 58, envH - ky - 15 - 6, kw - 8 });
        int y = centredY(envR, kh + 15, 0);
        for (int i = 0; i < 4; ++i)
            placeKnob(*knobs[(size_t) i], mx + pad + i * kw + kw / 2, y, kh, kw);
    }
    {   // filter knobs 4..6 + choices 0,1 (type, slope) at the bottom
        int kw = (midW - 2 * pad) / 3;
        int kh = std::min({ 64, filtH - ky - 15 - 52, kw - 8 });
        int y = centredY(filtR, kh + 15, 52);
        for (int i = 0; i < 3; ++i)
            placeKnob(*knobs[(size_t)(4 + i)], mx + pad + i * kw + kw / 2, y, kh, kw);
        int cw = (midW - 2 * pad - 8) / 2;
        placeChoiceBottom(*choices[0], filtR, mx + pad, cw);
        placeChoiceBottom(*choices[1], filtR, mx + pad + cw + 8, cw);
    }
    {   // lfo knobs 7,8 in columns 0,1; wave choice fills column 2
        int kw = (midW - 2 * pad) / 3;
        int kh = std::min({ 56, lfoH - ky - 15 - 4, kw - 8 });
        kh = juce::jmax(24, kh);
        int y = centredY(lfoR, kh + 15, 0);
        placeKnob(*knobs[7], mx + pad + kw / 2, y, kh, kw);
        placeKnob(*knobs[8], mx + pad + kw + kw / 2, y, kh, kw);
        auto* wv = choices[2].get();
        int wx = mx + pad + 2 * kw, ww = midW - pad - (wx - mx);
        int comboY = juce::jmin(y + kh / 2 - 4, lfoR.getBottom() - 8 - 24);
        wv->l->setBounds(wx, comboY - 15, ww, 13);
        wv->c->setBounds(wx, comboY, ww, 24);
    }

    // ----- fx grid -----
    fxGroup.setBounds(fxX, top, fxW, mainH);
    juce::Rectangle<int> cardR[8];
    {
        int gx = fxX + 8, gy = top + barH + 6;
        int gw = fxW - 16, gh = mainH - (gy - top) - 8;
        const int cg = 6;
        int cw = (gw - cg * 3) / 4, ch = (gh - cg) / 2;
        for (int i = 0; i < 8 && i < fxCards.size(); ++i)
        {
            cardR[i] = { gx + (i % 4) * (cw + cg), gy + (i / 4) * (ch + cg), cw, ch };
            fxCards[i]->setBounds(cardR[i]);
        }
    }

    const int choiceReserve = 52;
    // knob grid inside a card; reserveBottom keeps rows clear of bottom combos
    auto placeKnobs = [&](size_t kStart, int count, juce::Rectangle<int> card, int cols, int reserveBottom)
    {
        int rows = (count + cols - 1) / cols;
        int kw = (card.getWidth() - 8) / cols;
        int rowAvail = (card.getHeight() - ky - 6 - reserveBottom - (rows - 1) * 4) / rows;
        int kh = juce::jlimit(24, 60, juce::jmin(rowAvail - 15, kw - 6));
        // single-row cards centre above the shared bottom-combo zone so
        // knob rows line up across neighbouring cards
        int y0 = centredY(card, rows * (kh + 15) + (rows - 1) * 4, rows == 1 ? choiceReserve : reserveBottom);
        for (int i = 0; i < count; ++i)
        {
            int cx = card.getX() + 4 + (i % cols) * kw + kw / 2;
            int cy = y0 + (i / cols) * (kh + 15 + 4);
            placeKnob(*knobs[kStart + (size_t) i], cx, cy, kh, kw);
        }
    };

    // creation order:
    // knobs: 0-3 env, 4-6 filter, 7-8 lfo, 9-11 chorus, 12-15 phaser,
    //   16-19 flanger, 20-21 distortion, 22-23 saturation, 24-26 reverb,
    //   27-29 delay, 30 master-vol, 31-32 arp(oct,gate)
    // switches: 0 ch,1 ph,2 fl,3 di,4 sa,5 rv,6 dl,7 dlsync,8 limit,9 arp,10 mono,
    //   11 adsr,12 filter,13 lfo (group toggles)
    // choices: 0 ftype,1 fslope,2 lfowave,3 chmode,4 samode,5 dldiv,6 arpmode,7 arpdiv
    if (fxCards.size() >= 8)
    {
        auto inner = [&](juce::Rectangle<int> c) { return std::make_pair(c.getX() + 8, c.getWidth() - 16); };
        placeHeaderSwitch(*switches[0]->b, cardR[0]);              // chorus
        placeKnobs(9, 3, cardR[0], 3, choiceReserve);
        placeChoiceBottom(*choices[3], cardR[0], inner(cardR[0]).first, inner(cardR[0]).second);

        placeHeaderSwitch(*switches[1]->b, cardR[1]);              // phaser
        placeKnobs(12, 4, cardR[1], 2, 0);
        placeHeaderSwitch(*switches[2]->b, cardR[2]);              // flanger
        placeKnobs(16, 4, cardR[2], 2, 0);
        placeHeaderSwitch(*switches[3]->b, cardR[3]);              // distortion
        placeKnobs(20, 2, cardR[3], 2, 0);

        placeHeaderSwitch(*switches[4]->b, cardR[4]);              // saturation
        placeKnobs(22, 2, cardR[4], 2, choiceReserve);
        placeChoiceBottom(*choices[4], cardR[4], inner(cardR[4]).first, inner(cardR[4]).second);

        placeHeaderSwitch(*switches[5]->b, cardR[5]);              // reverb
        placeKnobs(24, 3, cardR[5], 3, 0);

        auto d = cardR[6];                                          // delay
        placeHeaderSwitch(*switches[6]->b, d);
        placeKnobs(27, 3, d, 3, choiceReserve);
        // sync toggle + caption mirror the Div caption/combo pair
        syncCap.setBounds(d.getX() + 6, d.getBottom() - 46, 44, 13);
        switches[7]->b->setBounds(d.getX() + 6, d.getBottom() - 30, 44, 20);
        placeChoiceBottom(*choices[5], d, d.getX() + 54, d.getWidth() - 62);

        auto m = cardR[7];                                          // master
        int kh = std::min({ 64, m.getHeight() - ky - 15 - 50, m.getWidth() - 24 });
        placeKnob(*knobs[30], m.getCentreX(), centredY(m, kh + 15, choiceReserve), kh, m.getWidth() - 16);
        int swY = m.getBottom() - 8 - 13 - 20;
        int cxm = m.getCentreX();
        switches[8]->b->setBounds(cxm - 50, swY, 46, 20);
        switches[10]->b->setBounds(cxm + 4, swY, 46, 20);
        limCap.setBounds(cxm - 50 - 6, swY + 21, 58, 12);
        monoCap.setBounds(cxm + 4 - 6, swY + 21, 58, 12);
    }

    // ---------- arp strip + voice card ----------
    const int voiceW = 330;
    const juce::Rectangle<int> arpR(M, top + mainH + gap, W - 2 * M - voiceW - gap, arpH);
    const juce::Rectangle<int> voiceR(arpR.getRight() + gap, arpR.getY(), voiceW, arpH);
    arpGroup.setBounds(arpR);
    voiceGroup.setBounds(voiceR);
    placeHeaderSwitch(*switches[9]->b, arpR);
    {
        int cy = arpR.getY() + barH + 4;  // content top
        auto* md = choices[6].get();
        md->l->setBounds(arpR.getX() + pad, cy + 4, 120, 13);
        md->c->setBounds(arpR.getX() + pad, cy + 19, 120, 24);
        auto* dv = choices[7].get();
        dv->l->setBounds(arpR.getX() + pad + 132, cy + 4, 100, 13);
        dv->c->setBounds(arpR.getX() + pad + 132, cy + 19, 100, 24);
        // knob centres line up with the combo centres (cy + 31)
        const int kh = 40;
        int kx = arpR.getX() + pad + 132 + 100 + 44;
        placeKnob(*knobs[31], kx, cy + 31 - kh / 2 - 6, kh, 60);
        placeKnob(*knobs[32], kx + 64, cy + 31 - kh / 2 - 6, kh, 60);
        placeKnob(*swingKnob, kx + 128, cy + 31 - kh / 2 - 6, kh, 60);
        // latch / vel toggles + captions, like the master card's
        int sx = kx + 128 + 54;
        latchSwitch->b->setBounds(sx, cy + 21, 46, 20);
        latchCap.setBounds(sx - 6, cy + 42, 58, 12);
        velSwitch->b->setBounds(sx + 58, cy + 21, 46, 20);
        velCap.setBounds(sx + 58 - 6, cy + 42, 58, 12);
    }
    {
        int cy = voiceR.getY() + barH + 4;
        int x = voiceR.getX() + pad;
        voiceChoice->l->setBounds(x, cy + 4, 104, 13);
        voiceChoice->c->setBounds(x, cy + 19, 104, 24);
        const int kh = 40;
        placeKnob(*glideKnob, x + 104 + 40, cy + 31 - kh / 2 - 6, kh, 60);
        int ix = x + 104 + 80;
        interpChoice->l->setBounds(ix, cy + 4, voiceR.getRight() - pad - ix, 13);
        interpChoice->c->setBounds(ix, cy + 19, voiceR.getRight() - pad - ix, 24);
    }

    piano.setBounds(M, arpR.getBottom() + gap, W - 2 * M, kbH);
    presetBrowser.setBounds(getLocalBounds());
}

bool PastelEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
    {
        juce::File file(f);
        if (FileLibrary::isSoundfontFile(file) || SamplerEngine::isAudioFile(file)) return true;
    }
    return false;
}

void PastelEditor::filesDropped(const juce::StringArray& files, int, int)
{
    dropHover = false;
    repaint();
    for (auto& f : files)
    {
        juce::File file(f);
        if (FileLibrary::isSoundfontFile(file) || SamplerEngine::isAudioFile(file))
        {
            proc.requestLoad(file.getFullPathName()); // background; progress via timer
            return;
        }
    }
}

void PastelEditor::refreshShortcuts()
{
    auto sc = proc.library.getShortcuts();
    dirBox.clear();
    for (int i = 0; i < sc.size(); ++i)
        dirBox.addItem(juce::File(sc[i]).getFileName() + U8("  —  ") + sc[i], i + 1);
    int sel = sc.indexOf(proc.library.getCurrentDir());
    if (sel >= 0) dirBox.setSelectedId(sel + 1, juce::dontSendNotification);
    else if (sc.size() > 0) dirBox.setTextWhenNothingSelected("(select shortcut)");
    else dirBox.setTextWhenNothingSelected(U8("(no shortcuts — + ADD)"));
    refreshPathLabel();
}

void PastelEditor::refreshPathLabel()
{
    pathLabel.setText(juce::File(proc.library.getCurrentDir()).getFullPathName(),
                      juce::dontSendNotification);
}

void PastelEditor::refreshFileList()
{
    listModel.setEntries(proc.library.listEntries(searchBox.getText()));
    fileList.updateContent();
    auto cur = proc.currentSoundPath();
    if (cur.isNotEmpty())
    {
        // jump browser to the loaded file's folder
        auto parent = juce::File(cur).getParentDirectory().getFullPathName();
        if (parent != proc.library.getCurrentDir()
            && juce::File(cur).existsAsFile())
        {
            proc.library.setCurrentDir(parent);
            listModel.setEntries(proc.library.listEntries(searchBox.getText()));
            fileList.updateContent();
        }
        int row = listModel.indexOfPath(cur);
        if (row >= 0)
        {
            fileList.selectRow(row);
            fileList.scrollToEnsureRowIsOnscreen(row);
        }
    }
    refreshShortcuts();
}

void PastelEditor::refreshPresetBoxes()
{
    presetList = proc.sf2Presets();
    presetBox.clear(juce::dontSendNotification);
    int bank = (int) proc.apvts.getRawParameterValue(PP::BANK)->load();
    int prog = (int) proc.apvts.getRawParameterValue(PP::PRESET)->load();
    shownBank = bank; shownProgram = prog;
    if (! presetList.empty())
    {
        int sel = -1;
        for (size_t i = 0; i < presetList.size(); ++i)
        {
            const auto& pr = presetList[i];
            auto nm = pr.name.isNotEmpty() ? pr.name : ("Preset " + juce::String(pr.program));
            presetBox.addItem(juce::String(pr.bank) + ":" + juce::String(pr.program).paddedLeft('0', 3)
                              + "  " + nm, (int) i + 1);
            if (pr.bank == bank && pr.program == prog) sel = (int) i;
        }
        if (sel >= 0) presetBox.setSelectedId(sel + 1, juce::dontSendNotification);
        else presetBox.setTextWhenNothingSelected("(empty: " + juce::String(bank) + ":"
                                                  + juce::String(prog) + ")");
        presetBox.setEnabled(true);
    }
    else
    {
        presetBox.setTextWhenNothingSelected(proc.isSoundLoaded() ? "(SFZ: no programs)" : "(no SF2 loaded)");
        presetBox.setEnabled(false);
    }
    bankBox.setSelectedId(juce::jlimit(1, 128, bank + 1), juce::dontSendNotification);
}

void PastelEditor::refreshFileLabel()
{
    juce::String t;
    if (proc.isLoading())
    {
        t = "Loading " + juce::File(proc.getLoadingPath()).getFileName() + U8(" … ")
            + juce::String((int)(proc.getLoadProgress() * 100.0f)) + "%";
    }
    else if (proc.isSoundLoaded())
    {
        t = proc.currentSoundName() + U8("   ·   ") + proc.currentProgramName();
        int sw = proc.getLastKeyswitch();
        auto nm = sw >= 0 ? proc.getSwitchLabel(sw) : juce::String();
        if (nm.isNotEmpty()) t += U8("   ·   ART ") + nm;
    }
    else
        t = U8("No file loaded — browse on the left, double-click to load");
    fileLabel.setText(t, juce::dontSendNotification);
}

void PastelEditor::loadSelectedFile()
{
    int row = fileList.getSelectedRow();
    if (row < 0) return;
    auto e = listModel.entryAt(row);
    if (e.path.isEmpty()) return;
    if (e.isDir) { proc.library.setCurrentDir(e.path); return; }
    proc.requestLoad(e.path); // background; progress + errors surface via timer
}

void PastelEditor::selectRelative(int delta)
{
    int n = listModel.getNumRows();
    if (n <= 0) return;
    // step over files only
    int row = fileList.getSelectedRow();
    for (int step = 0; step < n; ++step)
    {
        row += delta;
        if (row < 0 || row >= n) return;
        if (!listModel.entryAt(row).isDir)
        {
            fileList.selectRow(row);
            fileList.scrollToEnsureRowIsOnscreen(row);
            loadSelectedFile();
            return;
        }
    }
}

void PastelEditor::changeListenerCallback(juce::ChangeBroadcaster* src)
{
    if (src == &proc.library)
    {
        refreshShortcuts();
        refreshPathLabel();
        listModel.setEntries(proc.library.listEntries(searchBox.getText()));
        fileList.updateContent();
    }
    else if (src == &proc)
    {
        refreshFileLabel();
        refreshPresetBoxes();
        refreshFileList();
        piano.refreshMapping();
    }
}

void PastelEditor::timerCallback()
{
    piano.updateActive();
    {
        int b = (int) proc.apvts.getRawParameterValue(PP::BANK)->load();
        int pr = (int) proc.apvts.getRawParameterValue(PP::PRESET)->load();
        if (b != shownBank || pr != shownProgram) { refreshPresetBoxes(); refreshFileLabel(); }
    }
    int swNow = proc.getLastKeyswitch();
    if (swNow != lastShownSwitch) { lastShownSwitch = swNow; refreshFileLabel(); }
    bool ld = proc.isLoading();
    if (loadBar.isVisible() != ld) loadBar.setVisible(ld);
    if (ld)
    {
        loadBar.setProgress(proc.getLoadProgress());
        refreshFileLabel(); // live progress %
    }
    else
    {
        juce::String err;
        if (proc.consumeLoadError(err))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Could not load", err);
    }
    float tl = proc.getOutLevelL(), tr = proc.getOutLevelR();
    if (std::abs(tl - meterL) > 0.001f || std::abs(tr - meterR) > 0.001f)
    {
        meterL = tl; meterR = tr;
        repaint(meterArea.expanded(2));
    }
}

void PastelEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.eventComponent == &fileList || fileList.isParentOf(e.eventComponent))
        loadSelectedFile();
    juce::AudioProcessorEditor::mouseDoubleClick(e);
}
