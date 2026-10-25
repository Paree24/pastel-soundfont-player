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
    envView = std::make_unique<pastel::EnvView>(proc.apvts, juce::StringArray { PP::ATK, PP::DEC, PP::SUS, PP::REL }, juce::String());
    envView->setShowValues(false);
    envView->setTimeSkew(3.0f);   // linear 0..4 s times: spread the short ones out
    envView->setTooltip("Drag the points: attack, decay / sustain, release (double-click = defaults)");
    addAndMakeVisible(*envView);
    filterView = std::make_unique<pastel::FilterView>(proc.apvts, PP::FCUT, PP::FRES, PP::FTYPE, juce::String(), juce::String(),
        std::vector<pastel::FilterView::Mode> { pastel::FilterView::Mode::lp12, pastel::FilterView::Mode::hp12,
                                                pastel::FilterView::Mode::bp12 });
    filterView->setTooltip("Drag: cutoff sideways, resonance up / down");
    addAndMakeVisible(*filterView);
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

    // ---- v3 views + segmented switches (what they stand in for stays attached, hidden) ----
    {
        using pastel::Wave;
        auto keep = [&](auto* c) { addAndMakeVisible(c); v3.emplace_back(c); return c; };
        auto seg = [&](const juce::String& id, juce::StringArray labels, std::vector<Wave> icons = {})
        {
            return keep(new pastel::SegChoice(proc.apvts, id, std::move(labels), std::move(icons)));
        };
        envView->setShowValues(true);                      // the envelope card is just the graph
        for (int i = 0; i < 4; ++i) { knobs[(size_t) i]->s->setVisible(false); knobs[(size_t) i]->l->setVisible(false); }
        for (int i = 4; i < 6; ++i) { knobs[(size_t) i]->s->setVisible(false); knobs[(size_t) i]->l->setVisible(false); } // cutoff / reso: on the graph
        ftypeSeg = seg(PP::FTYPE, { "LP", "HP", "BP" });
        fslopeSeg = seg(PP::FSLOPE, { "6", "12", "24", "48" });
        fslopeSeg->setTooltip("Slope, dB / octave");
        lfoView = keep(new pastel::LfoView(proc.apvts, PP::LFOWAVE, juce::String(),
                                           { Wave::sine, Wave::tri, Wave::saw, Wave::square, Wave::snh }));
        lfoWaveSeg = seg(PP::LFOWAVE, { "", "", "", "", "" }, { Wave::sine, Wave::tri, Wave::saw, Wave::square, Wave::snh });
        chModeSeg = seg(PP::CHMODE, { "MAN", "I", "II", "WIDE" });
        saModeSeg = seg(PP::SAMODE, { "TAPE", "TUBE" });
        arpModeSeg = seg(PP::ARPMODE, { "UP", "DOWN", "UP/DN", "RND" });
        vmodeSeg = seg(PP::VMODE, { "POLY", "MONO", "LEGATO" });
        interpSeg = seg(PP::INTERP, { "LINEAR", "CUBIC", "SINC" });
        interpSeg->setTooltip("Sample interpolation: SINC is cleanest when pitching up, at more CPU");
        for (int i : { 0, 1, 2, 3, 4, 6 }) { choices[(size_t) i]->c->setVisible(false); choices[(size_t) i]->l->setVisible(false); }
        voiceChoice->c->setVisible(false); voiceChoice->l->setVisible(false);
        interpChoice->c->setVisible(false); interpChoice->l->setVisible(false);
        // each effect card's picture: drag it (two parameters each)
        using FS = pastel::FxView::Style;
        fxViews[0] = keep(new pastel::FxView(proc.apvts, FS::chorus, PP::CHRATE, PP::CHDEPTH, PP::CHON));
        fxViews[1] = keep(new pastel::FxView(proc.apvts, FS::phaser, PP::PHRATE, PP::PHDEPTH, PP::PHON));
        fxViews[2] = keep(new pastel::FxView(proc.apvts, FS::chorus, PP::FLRATE, PP::FLDEPTH, PP::FLON));
        fxViews[3] = keep(new pastel::DriveView(proc.apvts, PP::DIDRIVE));
        fxViews[4] = keep(new pastel::DriveView(proc.apvts, PP::SAAMT, PP::SAMODE));
        fxViews[5] = keep(new pastel::FxView(proc.apvts, FS::reverb, PP::RVSIZE, PP::RVDAMP, PP::RVON,
                                             juce::String(), juce::String(), "SIZE", "DAMP"));
        fxViews[6] = keep(new pastel::FxView(proc.apvts, FS::delay, PP::DLTIME, PP::DLFB, PP::DLON, PP::DLSYNC, PP::DLDIV));
        const char* tips[7] = { "Drag: rate sideways, depth up / down", "Drag: rate sideways, depth up / down",
                                "Drag: rate sideways, depth up / down", "Drag up / down: drive",
                                "Drag up / down: amount", "Drag: size sideways, damping up / down",
                                "Drag: time sideways (when not synced), feedback up / down" };
        for (int i = 0; i < 7; ++i)
            if (auto* tc = dynamic_cast<juce::SettableTooltipClient*>(fxViews[i])) tc->setTooltip(tips[i]);
    }
    fxCards.getLast()->setText("Output");

    // piano strip (full-bleed, highlights mapped + keyswitch + active notes)
    addAndMakeVisible(piano);

    // preset browser overlay (hidden until PRESETS)
    presetsBtn.setTooltip("Save and recall the loaded sound with all its settings");
    presetsBtn.onClick = [&] { presetBrowser.setVisible(true); presetBrowser.toFront(true); };
    addAndMakeVisible(presetsBtn);
    // this theme draws no toggled background: LEARN shows as teal text
    midiBtn.setColour(juce::TextButton::buttonOnColourId, PastelColors::teal);
    midiBtn.setColour(juce::TextButton::textColourOnId, PastelColors::teal);
    addAndMakeVisible(midiBtn);
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
    // the Pastel family mark: four rounded squares
    const juce::uint32 cols[4] = { 0xffe88a8a, 0xff8fd0c2, 0xffe9a3b8, 0xffa9d58e };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour(juce::Colour(cols[i]));
        g.fillRoundedRectangle(22.0f + (float) (i % 2) * 10.0f, 17.0f + (float) (i / 2) * 10.0f, 8.0f, 8.0f, 2.0f);
    }
}

void PastelEditor::paintOverChildren(juce::Graphics& g)
{
    if (! meterArea.isEmpty()) paintMeter(g, meterArea);   // inside the OUTPUT card
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
        b.getProperties().set("power", true);   // v3: section on/off is the power glyph
        b.setBounds(card.getRight() - 30, card.getY() + 2, 24, 22);
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
    logoLabel.setBounds(M + pad + 26, 13, 200, 24);
    fileLabel.setBounds(M + pad + 26, 37, 360, 18);
    int btnX = M + 400;
    const int btnY = 22, btnH = 30;
    loadBtn.setBounds(btnX, btnY, 76, btnH); btnX += 76 + 6;
    prevBtn.setBounds(btnX, btnY, 30, btnH); btnX += 30 + 6;
    nextBtn.setBounds(btnX, btnY, 30, btnH); btnX += 30 + 6;
    panicBtn.setBounds(btnX, btnY, 70, btnH); btnX += 70 + 6;
    presetsBtn.setBounds(btnX, btnY, 84, btnH); btnX += 84 + 6;
    midiBtn.setBounds(btnX, btnY, 60, btnH);

    int rightX = W - M - pad;   // v3: the meter lives in the OUTPUT card
    // program box shrinks so it never runs into the transport buttons
    const int bankW = 64;
    const int presetW = juce::jlimit(120, 220, rightX - (midiBtn.getRight() + 20) - bankW - 10);
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
    const int envH = (int)(mainH * 0.30), filtH = (int)(mainH * 0.38);
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
    // env: the graph is the whole card (A / D / S / R values on its bottom row)
    if (envView != nullptr) envView->setBounds(mx + pad, envR.getY() + barH + 2, midW - 2 * pad, envH - barH - 8);
    {   // filter: the response you drag, then type / slope switches and DRIVE
        const int rowY = filtR.getBottom() - 44;
        if (filterView != nullptr) filterView->setBounds(mx + pad, filtR.getY() + barH + 2, midW - 2 * pad, rowY - (filtR.getY() + barH + 2) - 6);
        ftypeSeg->setBounds(mx + pad, rowY + 6, 96, 22);
        fslopeSeg->setBounds(mx + pad + 102, rowY + 6, 120, 22);
        placeKnob(*knobs[6], mx + midW - pad - 26, rowY - 2, 26, 52);
    }
    {   // lfo: its shape on the left; wave, RATE and DEPTH on the right
        const int x0 = mx + pad, y0 = lfoR.getY() + barH + 2, h = lfoH - barH - 8;
        lfoView->setBounds(x0, y0, 120, h);
        lfoWaveSeg->setBounds(x0 + 128, y0, midW - 2 * pad - 128, 20);
        const int kh = juce::jlimit(22, 32, h - 20 - 4 - 15);
        const int kw = (midW - 2 * pad - 128) / 2;
        placeKnob(*knobs[7], x0 + 128 + kw / 2, y0 + 24, kh, kw);
        placeKnob(*knobs[8], x0 + 128 + kw + kw / 2, y0 + 24, kh, kw);
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
        // each card: header power, the picture you drag, a mode switch (if any), a row of knobs
        auto picture = [&](int i, juce::Rectangle<int> c, int bottomReserve)
        {
            fxViews[i]->setBounds(c.getX() + 8, c.getY() + barH + 2, c.getWidth() - 16,
                                  c.getHeight() - barH - 2 - bottomReserve);
        };
        auto knobRow = [&](size_t kStart, int count, juce::Rectangle<int> c)
        {
            const int kw = (c.getWidth() - 8) / count;
            const int kh = juce::jlimit(22, 34, kw - 10);
            for (int i = 0; i < count; ++i)
                placeKnob(*knobs[kStart + (size_t) i], c.getX() + 4 + i * kw + kw / 2, c.getBottom() - 8 - 15 - kh, kh, kw);
        };
        const int rowH = 58, segH = 28;
        auto segRow = [&](juce::Component& s, juce::Rectangle<int> c)
        {
            s.setBounds(c.getX() + 8, c.getBottom() - rowH - segH + 2, c.getWidth() - 16, 20);
        };
        placeHeaderSwitch(*switches[0]->b, cardR[0]);              // chorus
        picture(0, cardR[0], rowH + segH);
        segRow(*chModeSeg, cardR[0]);
        knobRow(9, 3, cardR[0]);
        placeHeaderSwitch(*switches[1]->b, cardR[1]);              // phaser
        picture(1, cardR[1], rowH + 4);
        knobRow(12, 4, cardR[1]);
        placeHeaderSwitch(*switches[2]->b, cardR[2]);              // flanger
        picture(2, cardR[2], rowH + 4);
        knobRow(16, 4, cardR[2]);
        placeHeaderSwitch(*switches[3]->b, cardR[3]);              // distortion
        picture(3, cardR[3], rowH + 4);
        knobRow(20, 2, cardR[3]);
        placeHeaderSwitch(*switches[4]->b, cardR[4]);              // saturation
        picture(4, cardR[4], rowH + segH);
        segRow(*saModeSeg, cardR[4]);
        knobRow(22, 2, cardR[4]);
        placeHeaderSwitch(*switches[5]->b, cardR[5]);              // reverb
        picture(5, cardR[5], rowH + 4);
        knobRow(24, 3, cardR[5]);
        auto d = cardR[6];                                          // delay
        placeHeaderSwitch(*switches[6]->b, d);
        picture(6, d, rowH + segH);
        {   // SYNC switch + division on one row
            const int ry = d.getBottom() - rowH - segH + 2;
            syncCap.setBounds(d.getX() + 6, ry + 3, 34, 13);
            switches[7]->b->setBounds(d.getX() + 38, ry, 40, 20);
            choices[5]->l->setVisible(false);
            choices[5]->c->setBounds(d.getX() + 82, ry - 1, d.getWidth() - 90, 22);
        }
        knobRow(27, 3, d);

        auto m = cardR[7];                                          // output
        meterArea = { m.getX() + 10, m.getY() + barH + 4, m.getWidth() - 20, 34 };
        int kh = std::min({ 56, m.getHeight() - barH - 40 - 15 - 50, m.getWidth() - 24 });
        placeKnob(*knobs[30], m.getCentreX(), m.getY() + barH + 44 + juce::jmax(0, (m.getHeight() - barH - 44 - 50 - kh - 15) / 2), kh, m.getWidth() - 16);
        int swY = m.getBottom() - 8 - 13 - 20;
        int cxm = m.getCentreX();
        switches[8]->b->setBounds(cxm - 50, swY, 46, 20);
        switches[10]->b->setBounds(cxm + 4, swY, 46, 20);
        limCap.setBounds(cxm - 50 - 6, swY + 21, 58, 12);
        monoCap.setBounds(cxm + 4 - 6, swY + 21, 58, 12);
    }

    // ---------- arp + playing rows ----------
    const int voiceW = 470;
    const juce::Rectangle<int> arpR(M, top + mainH + gap, W - 2 * M - voiceW - gap, arpH);
    const juce::Rectangle<int> voiceR(arpR.getRight() + gap, arpR.getY(), voiceW, arpH);
    arpGroup.setBounds(arpR);
    voiceGroup.setBounds(voiceR);
    placeHeaderSwitch(*switches[9]->b, arpR);
    {
        const int cy = arpR.getY() + barH + 4, rowY = cy + 18;   // controls centred on rowY + 11
        int x = arpR.getX() + pad;
        arpModeSeg->setBounds(x, rowY, 200, 22); x += 208;
        auto* dv = choices[7].get();
        dv->l->setBounds(x, cy + 2, 90, 13);
        dv->c->setBounds(x, rowY - 1, 90, 24); x += 100;
        const int kh = 34;
        placeKnob(*knobs[31], x + 26, rowY + 11 - kh / 2 - 6, kh, 52);
        placeKnob(*knobs[32], x + 26 + 56, rowY + 11 - kh / 2 - 6, kh, 52);
        placeKnob(*swingKnob, x + 26 + 112, rowY + 11 - kh / 2 - 6, kh, 52);
        int sx = x + 26 + 112 + 44;
        latchSwitch->b->setBounds(sx, rowY + 1, 46, 20);
        latchCap.setBounds(sx - 6, rowY + 22, 58, 12);
        velSwitch->b->setBounds(sx + 58, rowY + 1, 46, 20);
        velCap.setBounds(sx + 58 - 6, rowY + 22, 58, 12);
    }
    {
        const int cy = voiceR.getY() + barH + 4, rowY = cy + 18;
        int x = voiceR.getX() + pad;
        vmodeSeg->setBounds(x, rowY, 180, 22); x += 188;
        const int kh = 34;
        placeKnob(*glideKnob, x + 26, rowY + 11 - kh / 2 - 6, kh, 52); x += 60;
        interpSeg->setBounds(x, rowY, voiceR.getRight() - pad - x, 22);
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
    // the drive curves dim with their effect's power (the FX pads show BYPASSED themselves)
    const std::pair<int, juce::String> curvePower[] { { 3, PP::DION }, { 4, PP::SAON } };
    for (auto& [i, id] : curvePower)
        if (auto* raw = proc.apvts.getRawParameterValue(id); raw != nullptr && fxViews[i] != nullptr)
        {
            const float a = raw->load() > 0.5f ? 1.0f : 0.4f;
            if (fxViews[i]->getAlpha() != a) fxViews[i]->setAlpha(a);
        }
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
