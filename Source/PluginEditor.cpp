#include "PluginEditor.h"

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
    : juce::AudioProcessorEditor(p), proc(p), piano(p)
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
        int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0)
            if (auto* par = proc.apvts.getParameter(PP::PRESET))
                par->setValueNotifyingHost((float) idx / 127.0f);
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
    loadBar.setVisible(false);
    addAndMakeVisible(loadBar);
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
    addChoice(PP::DLDIV, "Div", { "1/4", "1/8", "1/8 Dot", "1/4 Tri", "1/16", "1/2" });

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

    // master switch captions (switches themselves stay textless)
    for (auto* cap : { &limCap, &monoCap })
    {
        cap->setJustificationType(juce::Justification::centred);
        cap->setFont(lnf.uiFont(9.5f));
        cap->setColour(juce::Label::textColourId, PastelColors::textDim);
        addAndMakeVisible(cap);
    }

    // piano strip (full-bleed, highlights mapped + keyswitch + active notes)
    addAndMakeVisible(piano);

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
    auto ma = juce::Rectangle<int>(getWidth() - 158, 12, 140, 44);
    paintMeter(g, ma);
}

void PastelEditor::paintMeter(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(PastelColors::textDim);
    g.setFont(lnf.uiFont(10.0f));
    g.drawText("OUT", area.getX(), area.getY(), 24, 12, juce::Justification::centredLeft, false);
    auto bar = juce::Rectangle<int>(area.getX() + 28, area.getY() + 1, area.getWidth() - 28, 9);
    auto bar2 = bar.translated(0, 13);
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

    // ---------- header (h 58 + margins) ----------
    logoLabel.setBounds(M + 8, 12, 170, 26);
    fileLabel.setBounds(M + 8, 38, 380, 20);
    int btnX = M + 398;
    loadBtn.setBounds(btnX, 18, 76, 30); btnX += 80;
    prevBtn.setBounds(btnX, 18, 30, 30); btnX += 34;
    nextBtn.setBounds(btnX, 18, 30, 30); btnX += 34;
    panicBtn.setBounds(btnX, 18, 66, 30);

    int rightX = W - M - 8;
    const int meterW = 140;
    // meter painted in paint(); reserve space only
    rightX -= meterW;
    const int presetW = 200, bankW = 60, gapR = 10;
    presetBox.setBounds(rightX - presetW, 26, presetW, 24);
    presetBoxLabel.setBounds(rightX - presetW, 12, presetW, 13);
    rightX -= presetW + gapR;
    bankBox.setBounds(rightX - bankW, 26, bankW, 24);
    bankLabel.setBounds(rightX - bankW, 12, bankW, 13);

    // ---------- main areas ----------
    const int top = 74;
    const int kbH = 70, arpH = 92;
    int mainH = H - top - arpH - kbH - M * 3 - 8;
    mainH = juce::jmax(330, mainH);

    const int libW = 250, midW = 300;
    const int fxX = M + libW + 8 + midW + 8;
    const int fxW = W - fxX - M;

    // ----- browser card -----
    libGroup.setBounds(M, top, libW, mainH);
    const int lx = M + 10, lw = libW - 20;
    int ly = top + 34;
    searchBox.setBounds(lx, ly, lw, 24); ly += 30;
    dirBox.setBounds(lx, ly, lw, 22); ly += 28;
    {
        int bw = (lw - 3 * 6) / 4;
        addDirBtn.setBounds(lx, ly, bw, 22);
        delDirBtn.setBounds(lx + (bw + 6), ly, bw, 22);
        upBtn.setBounds(lx + 2 * (bw + 6), ly, bw, 22);
        refreshBtn.setBounds(lx + 3 * (bw + 6), ly, lw - 3 * (bw + 6), 22);
        ly += 28;
    }
    pathLabel.setBounds(lx, ly, lw, 15); ly += 17;
    loadSelBtn.setBounds(lx + lw - 72, ly, 72, 22);
    loadBar.setBounds(lx, ly + 24, lw, 12);
    fileList.setBounds(lx, ly + 40, lw, mainH - (ly + 40 - top) - 10);

    // ----- mid column -----
    const int mx = M + libW + 8;
    const int envH = (int)(mainH * 0.27), filtH = (int)(mainH * 0.44);
    const int lfoH = mainH - envH - filtH - 16;
    envGroup.setBounds(mx, top, midW, envH);
    filterGroup.setBounds(mx, top + envH + 8, midW, filtH);
    lfoGroup.setBounds(mx, top + envH + 8 + filtH + 8, midW, lfoH);

    // env knobs 0..3 in one row
    {
        int kw = (midW - 16) / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto* k = knobs[(size_t) i].get();
            int kh = juce::jmin(58, envH - 34 - 16);
            int cx = mx + 8 + i * kw + kw / 2;
            k->s->setBounds(cx - kh / 2, top + 34, kh, kh);
            k->l->setBounds(mx + 8 + i * kw, top + 34 + kh + 1, kw, 14);
        }
    }
    // filter knobs 4..6 + choices 0,1
    {
        int fy = top + envH + 8;
        int kw = (midW - 16) / 3;
        int kh = juce::jmin(56, filtH - 34 - 16 - 52);
        for (int i = 0; i < 3; ++i)
        {
            auto* k = knobs[(size_t)(4 + i)].get();
            int cx = mx + 8 + i * kw + kw / 2;
            k->s->setBounds(cx - kh / 2, fy + 34, kh, kh);
            k->l->setBounds(mx + 8 + i * kw, fy + 34 + kh + 1, kw, 14);
        }
        int cw = (midW - 24) / 2;
        auto* t = choices[0].get();
        auto* sl = choices[1].get();
        t->l->setBounds(mx + 8, fy + filtH - 48, cw, 13);
        t->c->setBounds(mx + 8, fy + filtH - 33, cw, 23);
        sl->l->setBounds(mx + 12 + cw, fy + filtH - 48, cw, 13);
        sl->c->setBounds(mx + 12 + cw, fy + filtH - 33, cw, 23);
    }
    // lfo knobs 7,8 + choice 2
    {
        int ly2 = top + envH + 8 + filtH + 8;
        int kh = juce::jmin(52, lfoH - 34 - 16);
        auto* r = knobs[7].get();
        auto* dp = knobs[8].get();
        r->s->setBounds(mx + 16, ly2 + 32, kh, kh);
        r->l->setBounds(mx + 6, ly2 + 32 + kh + 1, kh + 20, 14);
        dp->s->setBounds(mx + 16 + kh + 24, ly2 + 32, kh, kh);
        dp->l->setBounds(mx + 6 + kh + 24, ly2 + 32 + kh + 1, kh + 20, 14);
        auto* wv = choices[2].get();
        int cx = mx + 16 + 2 * (kh + 12);
        wv->l->setBounds(cx, ly2 + 36, midW - (cx - mx) - 8, 13);
        wv->c->setBounds(cx, ly2 + 51, midW - (cx - mx) - 8, 23);
    }

    // ----- fx grid -----
    fxGroup.setBounds(fxX, top, fxW, mainH);
    {
        int gx = fxX + 8, gy = top + 32;
        int gw = fxW - 16, gh = mainH - 40;
        int gap = 6;
        int cw = (gw - gap * 3) / 4, ch = (gh - gap) / 2;
        for (int i = 0; i < 8 && i < fxCards.size(); ++i)
            fxCards[i]->setBounds(gx + (i % 4) * (cw + gap), gy + (i / 4) * (ch + gap), cw, ch);
    }

    auto placeKnobs = [&](size_t kStart, int count, juce::Rectangle<int> card, int cols)
    {
        int rows = (count + cols - 1) / cols;
        int kw = card.getWidth() / cols;
        int khAvail = (card.getHeight() - 34 - 12 - (rows - 1) * 4) / rows;
        int kh = juce::jlimit(30, 52, khAvail - 15);
        for (int i = 0; i < count; ++i)
        {
            auto* k = knobs[kStart + (size_t) i].get();
            int cx = card.getX() + (i % cols) * kw + kw / 2;
            int cy = card.getY() + 36 + (i / cols) * (kh + 15 + 4);
            k->s->setBounds(cx - kh / 2, cy, kh, kh);
            k->l->setBounds(card.getX() + (i % cols) * kw, cy + kh + 1, kw, 13);
        }
        return 36 + rows * (kh + 15 + 4);
    };
    auto placeSwitchTR = [&](size_t sIdx, juce::Rectangle<int> card)
    {
        switches[sIdx]->b->setBounds(card.getX() + card.getWidth() - 50,
                                     card.getY() + 4, 46, 20);
    };
    auto placeChoiceBottom = [&](size_t cIdx, juce::Rectangle<int> card, int x0, int cwidth)
    {
        auto* c = choices[cIdx].get();
        c->l->setBounds(card.getX() + x0, card.getY() + card.getHeight() - 50, cwidth, 12);
        c->c->setBounds(card.getX() + x0, card.getY() + card.getHeight() - 36, cwidth, 23);
    };

    // knob/switch/choice creation order:
    // knobs: 0-3 env, 4-6 filter, 7-8 lfo, 9-11 chorus, 12-15 phaser,
    //   16-19 flanger, 20-21 distortion, 22-23 saturation, 24-26 reverb,
    //   27-29 delay, 30 master-vol, 31-32 arp(oct,gate)
    // switches: 0 ch,1 ph,2 fl,3 di,4 sa,5 rv,6 dl,7 dlsync,8 limit,9 arp,10 mono
    // choices: 0 ftype,1 fslope,2 lfowave,3 chmode,4 samode,5 dldiv,6 arpmode,7 arpdiv
    size_t K = 9, S = 0;
    for (int card = 0; card < 8 && card < fxCards.size(); ++card)
    {
        auto b = fxCards[card]->getBounds();
        if (card == 0) // chorus: 3 knobs + mode
        {
            placeSwitchTR(S++, b);
            placeKnobs(K, 3, b, 3); K += 3;
            placeChoiceBottom(3, b, 8, b.getWidth() - 16);
        }
        else if (card == 1 || card == 2) // phaser / flanger: 4 knobs 2x2
        {
            placeSwitchTR(S++, b);
            placeKnobs(K, 4, b, 2); K += 4;
        }
        else if (card == 3) // distortion: 2 knobs
        {
            placeSwitchTR(S++, b);
            placeKnobs(K, 2, b, 2); K += 2;
        }
        else if (card == 4) // saturation: 2 knobs + mode
        {
            placeSwitchTR(S++, b);
            placeKnobs(K, 2, b, 2); K += 2;
            placeChoiceBottom(4, b, 8, b.getWidth() - 16);
        }
        else if (card == 5) // reverb: 3 knobs
        {
            placeSwitchTR(S++, b);
            placeKnobs(K, 3, b, 3); K += 3;
        }
        else if (card == 6) // delay: 3 knobs + sync + div
        {
            placeSwitchTR(S++, b);
            int used = (int) placeKnobs(K, 3, b, 3); K += 3;
            juce::ignoreUnused(used);
            auto* sy = switches[S++].get();
            sy->b->setBounds(b.getX() + 8, b.getY() + b.getHeight() - 34, 46, 20);
            auto* dv = choices[5].get();
            dv->l->setBounds(b.getX() + 60, b.getY() + b.getHeight() - 50, b.getWidth() - 66, 12);
            dv->c->setBounds(b.getX() + 60, b.getY() + b.getHeight() - 36, b.getWidth() - 66, 23);
        }
        else if (card == 7) // master: volume + limiter + mono
        {
            auto* k = knobs[K++].get();
            int kh = juce::jmin(56, b.getHeight() - 34 - 14 - 56);
            k->s->setBounds(b.getX() + b.getWidth() / 2 - kh / 2, b.getY() + 36, kh, kh);
            k->l->setBounds(b.getX(), b.getY() + 36 + kh + 1, b.getWidth(), 13);
            int swY = b.getY() + b.getHeight() - 44;
            int cxm = b.getX() + b.getWidth() / 2;
            auto* li = switches[S++].get();
            li->b->setBounds(cxm - 48, swY, 46, 20);
            auto* mo = switches[10].get();
            mo->b->setBounds(cxm + 2, swY, 46, 20);
            limCap.setBounds(cxm - 48, swY + 21, 46, 11);
            monoCap.setBounds(cxm + 2, swY + 21, 46, 11);
        }
    }

    // ---------- arp strip ----------
    int arpY = top + mainH + 8;
    arpGroup.setBounds(M, arpY, W - 2 * M, arpH);
    {
        auto* on = switches[9].get();
        on->b->setBounds(M + 16, arpY + 54, 46, 20);
        auto* md = choices[6].get();
        md->l->setBounds(M + 90, arpY + 38, 110, 13);
        md->c->setBounds(M + 90, arpY + 52, 110, 24);
        auto* dv = choices[7].get();
        dv->l->setBounds(M + 210, arpY + 38, 90, 13);
        dv->c->setBounds(M + 210, arpY + 52, 90, 24);
        auto* oc = knobs[31].get();
        oc->s->setBounds(M + 320, arpY + 36, 44, 44);
        oc->l->setBounds(M + 308, arpY + arpH - 15, 68, 13);
        auto* gt = knobs[32].get();
        gt->s->setBounds(M + 388, arpY + 36, 44, 44);
        gt->l->setBounds(M + 376, arpY + arpH - 15, 68, 13);
    }

    piano.setBounds(M, arpY + arpH + 8, W - 2 * M, kbH);
}

void PastelEditor::refreshShortcuts()
{
    auto sc = proc.library.getShortcuts();
    dirBox.clear();
    for (int i = 0; i < sc.size(); ++i)
        dirBox.addItem(juce::File(sc[i]).getFileName() + "  —  " + sc[i], i + 1);
    int sel = sc.indexOf(proc.library.getCurrentDir());
    if (sel >= 0) dirBox.setSelectedId(sel + 1, juce::dontSendNotification);
    else if (sc.size() > 0) dirBox.setTextWhenNothingSelected("(select shortcut)");
    else dirBox.setTextWhenNothingSelected("(no shortcuts — + ADD)");
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
    auto names = proc.sf2PresetNames();
    presetBox.clear();
    if (names.size() > 0)
    {
        for (int i = 0; i < names.size() && i < 128; ++i)
            presetBox.addItem(names[i].isNotEmpty() ? names[i] : ("Preset " + juce::String(i)), i + 1);
        int cur = juce::jlimit(0, juce::jmax(0, names.size() - 1),
            (int) proc.apvts.getRawParameterValue(PP::PRESET)->load());
        presetBox.setSelectedId(cur + 1, juce::dontSendNotification);
        presetBox.setEnabled(true);
    }
    else
    {
        presetBox.addItem("(no SF2 loaded)", 1);
        presetBox.setSelectedId(1, juce::dontSendNotification);
        presetBox.setEnabled(proc.isSoundLoaded());
    }
    int bank = (int) proc.apvts.getRawParameterValue(PP::BANK)->load();
    bankBox.setSelectedId(juce::jlimit(1, 128, bank + 1), juce::dontSendNotification);
}

void PastelEditor::refreshFileLabel()
{
    juce::String t;
    if (proc.isLoading())
    {
        t = "Loading " + juce::File(proc.getLoadingPath()).getFileName() + " … "
            + juce::String((int)(proc.getLoadProgress() * 100.0f)) + "%";
    }
    else if (proc.isSoundLoaded())
    {
        t = proc.currentSoundName() + "   ·   " + proc.currentProgramName();
        int sw = proc.getLastKeyswitch();
        auto nm = sw >= 0 ? proc.getSwitchLabel(sw) : juce::String();
        if (nm.isNotEmpty()) t += "   ·   ART " + nm;
    }
    else
        t = "No file loaded — browse on the left, double-click to load";
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
        repaint(getWidth() - 168, 8, 168, 52);
    }
}

void PastelEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.eventComponent == &fileList || fileList.isParentOf(e.eventComponent))
        loadSelectedFile();
    juce::AudioProcessorEditor::mouseDoubleClick(e);
}
