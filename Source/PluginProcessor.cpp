#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "PlayerPresets.h"
#include <limits>

PastelProcessor::PastelProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PastelPlayer", createParameterLayout())
{
    midiLearn.onPresetStep = [this](int delta) { stepSf2Preset(delta); };
}

bool PastelProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

PastelProcessor::~PastelProcessor()
{
    cancelLoadJob();
}

void PastelProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    lastSampleRate = sampleRate;
    sampler.setSampleRate(sampleRate);
    dsp.prepare(sampleRate, samplesPerBlock);
    monoScratch.setSize(2, juce::jmax(1, samplesPerBlock));
    setLatencySamples(dsp.getLatencySamples()); // limiter lookahead (held while bypassed)
    arpReleaseAll();
    arpReset();
    // NOTE: no auto-load here on purpose. A new instance starts empty;
    // only DAW state restore (setStateInformation) recalls a patch.
}

void PastelProcessor::cancelLoadJob()
{
    std::unique_ptr<std::thread> doomed;
    {
        juce::ScopedLock sl(loadMutex);
        loadCancel.store(true);
        doomed = std::move(loadThread);
    }
    if (doomed && doomed->joinable()) doomed->join();
}

void PastelProcessor::requestLoad(const juce::String& path)
{
    cancelLoadJob(); // never nested: takes/joins without holding loadMutex
    juce::StringArray roots = library.getShortcuts();
    auto cur = library.getCurrentDir();
    if (cur.isNotEmpty() && !roots.contains(cur)) roots.add(cur);
    loadCancel.store(false);
    loading.store(true);
    loadProgress.store(0.0f);
    loadErrorPending.store(false);
    int gen = loadGen.load() + 1;
    loadGen.store(gen);
    double sr = lastSampleRate;
    {
        juce::ScopedLock sl(loadMutex);
        loadingPath = path;
        loadThread = std::make_unique<std::thread>([this, path, roots, sr, gen]()
        {
            auto loader = std::make_shared<SamplerEngine>();
            loader->setSampleRate(sr);
            loader->setSearchPaths(roots);
            loader->setLoadMonitor(&loadCancel, &loadProgress);
            juce::String err;
            bool ok = loader->loadFile(path, err);
            // Swap happens right here on the worker: swapWith is lock-safe
            // against the audio thread, so no message loop is involved and
            // the DAW never blocks. sendChangeMessage is thread-safe.
            finishLoad(gen, loader, path, err, ok);
        });
    }
}

void PastelProcessor::finishLoad(int gen, std::shared_ptr<SamplerEngine> loader,
                                  juce::String path, juce::String error, bool ok)
{
    if (gen != loadGen.load() || loadCancel.load()) return; // superseded/cancelled
    loading.store(loadGen.load() != gen); // stay busy if a newer job started mid-swap
    loadProgress.store(ok ? 1.0f : 0.0f);
    if (ok && loader)
    {
        sampler.swapWith(*loader);
        clearActiveNotes();
        sampler.setSf2Preset((int) *apvts.getRawParameterValue(PP::BANK),
                             (int) *apvts.getRawParameterValue(PP::PRESET));
        lastBank = lastPreset = -1;
        library.setLastFile(path);
    }
    else if (!ok)
    {
        pendingLoadError = error.isNotEmpty() ? error : "Could not load sound.";
        loadErrorPending.store(true);
    }
    sendChangeMessage();
}

juce::String PastelProcessor::getLoadingPath() const
{
    juce::ScopedLock sl(loadMutex);
    return loadingPath;
}

bool PastelProcessor::consumeLoadError(juce::String& errOut)
{
    if (!loadErrorPending.load()) return false;
    loadErrorPending.store(false);
    errOut = pendingLoadError;
    return true;
}

void PastelProcessor::refreshSamplerSearchPaths()
{
    juce::StringArray roots = library.getShortcuts();
    auto cur = library.getCurrentDir();
    if (cur.isNotEmpty() && !roots.contains(cur)) roots.add(cur);
    sampler.setSearchPaths(roots);
}

bool PastelProcessor::loadSoundFile(const juce::String& path, juce::String& errorOut)
{
    cancelLoadJob(); // a sync load supersedes any background job
    loading.store(false);
    refreshSamplerSearchPaths();
    bool ok = sampler.loadFile(path, errorOut);
    if (ok)
    {
        clearActiveNotes();
        sampler.setSf2Preset((int) *apvts.getRawParameterValue(PP::BANK),
                             (int) *apvts.getRawParameterValue(PP::PRESET));
        lastBank = lastPreset = -1; // force refresh
        library.setLastFile(path);
        sendChangeMessage();
    }
    return ok;
}

juce::String PastelProcessor::currentSoundName() const { return sampler.currentName(); }
juce::String PastelProcessor::currentSoundPath() const { return sampler.currentPath(); }
juce::String PastelProcessor::currentProgramName() const { return sampler.programName(); }
std::vector<SamplerEngine::Sf2Preset> PastelProcessor::sf2Presets() const { return sampler.getSf2Presets(); }
bool PastelProcessor::getActiveSf2Preset(int& bank, int& program) const { return sampler.getActiveSf2Preset(bank, program); }

void PastelProcessor::stepSf2Preset(int delta)
{
    // same as picking the neighbouring entry in the editor's preset list
    const auto list = sf2Presets();
    const int n = (int) list.size();
    if (n == 0 || delta == 0) return;
    const int bank = (int) apvts.getRawParameterValue(PP::BANK)->load();
    const int prog = (int) apvts.getRawParameterValue(PP::PRESET)->load();
    int cur = -1;
    for (int i = 0; i < n; ++i)
        if (list[(size_t) i].bank == bank && list[(size_t) i].program == prog) { cur = i; break; }
    const int next = cur < 0 ? (delta > 0 ? 0 : n - 1) : ((cur + delta) % n + n) % n;
    if (auto* par = apvts.getParameter(PP::BANK))
        par->setValueNotifyingHost(par->convertTo0to1((float) list[(size_t) next].bank));
    if (auto* par = apvts.getParameter(PP::PRESET))
        par->setValueNotifyingHost(par->convertTo0to1((float) list[(size_t) next].program));
}
bool PastelProcessor::isSoundLoaded() const { return sampler.isLoaded(); }

void PastelProcessor::panic()
{
    sampler.panic();
    arpReset();
    clearActiveNotes();
}

void PastelProcessor::clearActiveNotes()
{
    for (auto& n : noteSounding) n.store(false, std::memory_order_relaxed);
}

void PastelProcessor::auditionNoteOn(int note, float vel)
{
    engineNoteOn(note, vel);
}

void PastelProcessor::auditionNoteOff(int note)
{
    engineNoteOff(note);
}

void PastelProcessor::engineNoteOn(int note, float vel)
{
    if (note >= 0 && note < 128) noteSounding[note].store(true, std::memory_order_relaxed);
    sampler.noteOn(note, vel);
}

void PastelProcessor::engineNoteOff(int note)
{
    if (note >= 0 && note < 128) noteSounding[note].store(false, std::memory_order_relaxed);
    sampler.noteOff(note);
}

// UI poll: reads the per-key flags, never a lock the audio thread takes
std::vector<int> PastelProcessor::getActiveNotes() const
{
    std::vector<int> v;
    for (int n = 0; n < 128; ++n)
        if (noteSounding[n].load(std::memory_order_relaxed)) v.push_back(n);
    return v;
}

int PastelProcessor::getLastKeyswitch() const { return sampler.getLastKeyswitch(); }

std::vector<std::pair<int,int>> PastelProcessor::getMappedRanges() const
{
    return sampler.getMappedRanges();
}

std::vector<std::pair<int,int>> PastelProcessor::getSwitchRanges() const
{
    return sampler.getSwitchRanges();
}

int PastelProcessor::activeVoiceCount() const
{
    return sampler.activeVoiceCount();
}

juce::String PastelProcessor::getVoiceSamplePath(int voiceIdx) const
{
    return sampler.getVoiceSamplePath(voiceIdx);
}

juce::String PastelProcessor::getSwitchLabel(int note) const
{
    return sampler.getSwitchLabel(note);
}

int PastelProcessor::getVoiceSwReq(int voiceIdx) const
{
    return sampler.getVoiceSwReq(voiceIdx);
}

const juce::String PastelProcessor::getProgramName(int)
{
    return currentProgramName();
}

juce::ValueTree PastelProcessor::capturePreset()
{
    juce::ValueTree t(PlayerPresets::kRootType);
    t.setProperty("soundFile", sampler.currentPath(), nullptr);
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
        {
            juce::ValueTree v("PARAM");
            v.setProperty("id", rp->getParameterID(), nullptr);
            v.setProperty("value", rp->convertFrom0to1(rp->getValue()), nullptr);
            t.appendChild(v, nullptr);
        }
    return t;
}

// every parameter not in the preset (or all, for Init) goes back to its
// default; each change is a host gesture so automation lanes record it
void PastelProcessor::applyPreset(const juce::ValueTree& preset, const juce::String& name)
{
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
        {
            float norm = rp->getDefaultValue();
            if (preset.isValid())
            {
                auto v = preset.getChildWithProperty("id", rp->getParameterID());
                if (v.isValid()) norm = rp->convertTo0to1((float) v.getProperty("value"));
            }
            if (std::abs(norm - rp->getValue()) < 1.0e-7f) continue;
            rp->beginChangeGesture();
            rp->setValueNotifyingHost(norm);
            rp->endChangeGesture();
        }
    setPresetName(name);
    auto f = preset.isValid() ? preset.getProperty("soundFile", "").toString() : juce::String();
    if (f.isNotEmpty() && f != sampler.currentPath() && juce::File(f).existsAsFile())
        requestLoad(f);
    sendChangeMessage();
}

void PastelProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("soundFile", sampler.currentPath(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void PastelProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid()) return;
    apvts.replaceState(state);
    auto f = state.getProperty("soundFile", "").toString();
    if (f.isNotEmpty() && juce::File(f).existsAsFile())
        requestLoad(f); // background: existing instances recall, never blocks
}

DspParams PastelProcessor::collectParams()
{
    DspParams p;
    auto get = [&](const char* id) -> float
    {
        if (auto* v = apvts.getRawParameterValue(id)) return v->load();
        return 0.0f;
    };
    auto getInt = [&](const char* id) -> int { return (int) get(id); };

    p.fOn = get(PP::FTON) > 0.5f;
    p.fType = getInt(PP::FTYPE); p.fCut = get(PP::FCUT); p.fRes = get(PP::FRES);
    p.fDrive = get(PP::FDRIVE); p.fSlope = getInt(PP::FSLOPE);
    p.lfoOn = get(PP::LFOON) > 0.5f;
    p.lfoRate = get(PP::LFORATE); p.lfoDepth = get(PP::LFODEPTH); p.lfoWave = getInt(PP::LFOWAVE);
    p.chOn = get(PP::CHON) > 0.5f; p.chMode = getInt(PP::CHMODE);
    p.chRate = get(PP::CHRATE); p.chDepth = get(PP::CHDEPTH); p.chMix = get(PP::CHMIX);
    p.phOn = get(PP::PHON) > 0.5f; p.phRate = get(PP::PHRATE); p.phDepth = get(PP::PHDEPTH);
    p.phFb = get(PP::PHFB); p.phMix = get(PP::PHMIX);
    p.flOn = get(PP::FLON) > 0.5f; p.flRate = get(PP::FLRATE); p.flDepth = get(PP::FLDEPTH);
    p.flFb = get(PP::FLFB); p.flMix = get(PP::FLMIX);
    p.diOn = get(PP::DION) > 0.5f; p.diDrive = get(PP::DIDRIVE); p.diMix = get(PP::DIMIX);
    p.saOn = get(PP::SAON) > 0.5f; p.saAmt = get(PP::SAAMT); p.saMode = getInt(PP::SAMODE);
    p.saTone = get(PP::SATONE);
    p.rvOn = get(PP::RVON) > 0.5f; p.rvSize = get(PP::RVSIZE); p.rvDamp = get(PP::RVDAMP); p.rvMix = get(PP::RVMIX);
    p.dlOn = get(PP::DLON) > 0.5f; p.dlTimeMs = get(PP::DLTIME); p.dlFb = get(PP::DLFB);
    p.dlMix = get(PP::DLMIX); p.dlSync = get(PP::DLSYNC) > 0.5f; p.dlDiv = getInt(PP::DLDIV);
    p.volume = get(PP::VOLUME); p.limiter = get(PP::LIMIT) > 0.5f;
    p.mono = get(PP::MONO) > 0.5f;
    return p;
}

void PastelProcessor::arpReleaseAll()
{
    for (int i = 0; i < arpOffCount; ++i) engineNoteOff(arpOffs[i].note);
    if (arpLastNote >= 0) engineNoteOff(arpLastNote);
}

void PastelProcessor::arpReset()
{
    arpHeldCount = 0;
    arpOffCount = 0;
    std::fill(std::begin(arpKeyDown), std::end(arpKeyDown), false);
    arpSamplesUntilNext = 0.0;
    arpGridIdx = 0;
    arpLastFiredBeat = -std::numeric_limits<double>::infinity();
    arpAnchor = 0.0; arpPhraseStart = false;
    arpStep = 0; arpLastNote = -1;
}

// next pattern note + its velocity (fixed storage: no audio-thread allocation)
bool PastelProcessor::arpNextNote(int& noteOut, float& velOut)
{
    if (arpHeldCount <= 0) return false;
    int sorted[arpMaxHeld]; float vels[arpMaxHeld];
    for (int i = 0; i < arpHeldCount; ++i) { sorted[i] = arpHeld[i]; vels[i] = arpHeldVel[i]; }
    for (int i = 1; i < arpHeldCount; ++i) // insertion sort, pitch order
        for (int j = i; j > 0 && sorted[j - 1] > sorted[j]; --j)
        {
            std::swap(sorted[j - 1], sorted[j]);
            std::swap(vels[j - 1], vels[j]);
        }
    int mode = (int) *apvts.getRawParameterValue(PP::ARPMODE);
    int octs = juce::jlimit(1, 4, (int) *apvts.getRawParameterValue(PP::ARPOCT));
    int seq[arpMaxHeld * 4]; float seqVel[arpMaxHeld * 4];
    int n = 0;
    for (int o = 0; o < octs; ++o)
        for (int i = 0; i < arpHeldCount; ++i)
        {
            int v = sorted[i] + o * 12;
            if (v <= 127) { seq[n] = v; seqVel[n] = vels[i]; ++n; }
        }
    if (n == 0) return false;
    int idx = 0;
    if (mode == 0) idx = arpStep % n;
    else if (mode == 1) idx = n - 1 - (arpStep % n);
    else if (mode == 2)
    {
        if (n == 1) idx = 0;
        else
        {
            int cyc = n * 2 - 2;
            int s = arpStep % cyc;
            idx = s < n ? s : cyc - s;
        }
    }
    else idx = arpRng.nextInt(n);
    noteOut = seq[idx];
    velOut = seqVel[idx];
    return true;
}

static double arpBeats(int div)
{
    switch (div)
    {
        case 0: return 1.0;             // 1/4
        case 1: return 2.0 / 3.0;       // 1/4 triplet
        case 2: return 1.5;             // 1/4 dotted
        case 3: return 0.5;             // 1/8
        case 4: return 1.0 / 3.0;       // 1/8 triplet
        case 5: return 0.75;            // 1/8 dotted
        case 6: return 0.25;            // 1/16
        case 7: return 1.0 / 6.0;       // 1/16 triplet
        case 8: return 0.375;           // 1/16 dotted
        case 9: return 0.125;           // 1/32
        default: return 1.0 / 12.0;     // 1/32 triplet
    }
}

// beat position of arp step k: odd steps sit late by swing * half a step
static double arpStepBeat(long long k, double stepBeats, double swing)
{
    return ((double) k + ((k & 1) ? 0.5 * swing : 0.0)) * stepBeats;
}

void PastelProcessor::arpKeyOn(int note, float vel)
{
    bool latch = apvts.getRawParameterValue(PP::ARPLATCH)->load() > 0.5f;
    bool anyDown = false;
    for (bool d : arpKeyDown) anyDown = anyDown || d;
    if (latch && !anyDown) arpHeldCount = 0; // latched: a fresh press starts a new chord
    if (arpHeldCount == 0) arpPhraseStart = true;
    arpKeyDown[note] = true;
    for (int i = 0; i < arpHeldCount; ++i)
        if (arpHeld[i] == note) { arpHeldVel[i] = vel; return; }
    if (arpHeldCount < arpMaxHeld)
    {
        arpHeld[arpHeldCount] = note;
        arpHeldVel[arpHeldCount] = vel;
        ++arpHeldCount;
    }
}

void PastelProcessor::arpKeyOff(int note)
{
    arpKeyDown[note] = false;
    if (apvts.getRawParameterValue(PP::ARPLATCH)->load() > 0.5f) return; // pattern keeps going
    int k = 0;
    for (int i = 0; i < arpHeldCount; ++i)
        if (arpHeld[i] != note) { arpHeld[k] = arpHeld[i]; arpHeldVel[k] = arpHeldVel[i]; ++k; }
    arpHeldCount = k;
    // the ringing arp voice keeps going until its gate ends
}

void PastelProcessor::handleEvent(const juce::MidiMessage& msg, bool arpOn)
{
    if (msg.isNoteOn())
    {
        int n = msg.getNoteNumber();
        if (arpOn) arpKeyOn(n, msg.getFloatVelocity());
        else engineNoteOn(n, msg.getFloatVelocity());
    }
    else if (msg.isNoteOff())
    {
        int n = msg.getNoteNumber();
        if (arpOn) arpKeyOff(n);
        else engineNoteOff(n);
    }
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        sampler.allNotesOff();
        arpHeldCount = 0;
        std::fill(std::begin(arpKeyDown), std::end(arpKeyDown), false);
        clearActiveNotes();
    }
    else if (msg.isPitchWheel())
    {
        int v = msg.getPitchWheelValue() - 8192; // -8192..8191 -> -1..1
        sampler.setPitchBend(v >= 0 ? v / 8191.0f : v / 8192.0f);
    }
    else if (msg.isController())
    {
        int cc = msg.getControllerNumber(), val = msg.getControllerValue();
        sampler.setCC(cc, val); // state for sfz conditions (on_locc/hicc...) + tsf
        if (cc == 64) sampler.setSustain(val >= 64);
    }
}

void PastelProcessor::renderSampler(juce::AudioBuffer<float>& target, int start, int num)
{
    if (num > 0)
        sampler.renderAdding(target.getWritePointer(0) + start, target.getWritePointer(1) + start, num);
}

// Sample-accurate MIDI: the block is rendered in segments that end on each
// incoming event, arp step and arp gate-off, so every note lands on its sample.
void PastelProcessor::renderWithMidi(juce::AudioBuffer<float>& target, juce::MidiBuffer& midi,
                                     int numSamples, const HostTime& ht)
{
    bool arpOn = apvts.getRawParameterValue(PP::ARPON)->load() > 0.5f;
    if (!arpOn && (arpHeldCount > 0 || arpLastNote >= 0 || arpOffCount > 0))
    {
        arpReleaseAll();
        arpReset();
    }
    if (arpOn && !arpWasOn) arpReset();
    arpWasOn = arpOn;

    const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    const double bpm = ht.bpm > 20 ? ht.bpm : 120.0;
    const double spb = 60.0 / bpm * sr; // samples per beat
    const double stepBeats = arpBeats((int) apvts.getRawParameterValue(PP::ARPDIV)->load());
    const double swing = juce::jlimit(0.0f, 1.0f, apvts.getRawParameterValue(PP::ARPSWING)->load());
    const float gate = juce::jlimit(0.1f, 1.0f, apvts.getRawParameterValue(PP::ARPGATE)->load());
    const bool playedVel = apvts.getRawParameterValue(PP::ARPVEL)->load() > 0.5f;
    const bool grid = arpOn && ht.playing && ht.hasPpq;

    if (arpOn && !apvts.getRawParameterValue(PP::ARPLATCH)->load())
    {
        // latch released while no key is down: drop the latched chord
        bool anyDown = false;
        for (bool d : arpKeyDown) anyDown = anyDown || d;
        if (!anyDown) arpHeldCount = 0;
    }

    // host-locked: steps count in host beats from the phrase's first key (anchor),
    // so they follow the host tempo and dotted rates start where the phrase does
    auto stepAt = [&](long long k) { return arpAnchor + arpStepBeat(k, stepBeats, swing); };
    if (grid)
    {
        // transport start, or a loop / seek jump: the pattern restarts here
        if (std::abs(ht.ppq - arpPrevPpq) > 1.0e-3)
        {
            arpAnchor = ht.ppq;
            arpLastFiredBeat = -std::numeric_limits<double>::infinity();
            arpStep = 0;
        }
        // the first step at/after the block start not fired yet (by beat, so a
        // rate change can't strand the arp behind a stale step number)
        long long k = (long long) std::floor((ht.ppq - arpAnchor) / stepBeats) - 1;
        while (stepAt(k) < ht.ppq - 1.0e-9 || stepAt(k) <= arpLastFiredBeat + 1.0e-9) ++k;
        arpGridIdx = k;
        arpSamplesUntilNext = (stepAt(k) - ht.ppq) * spb;
        arpPrevPpq = ht.ppq + numSamples / spb;
    }
    else arpPrevPpq = -1.0e9;

    auto it = midi.cbegin();
    const auto end = midi.cend();
    int pos = 0;
    for (;;)
    {
        while (it != end && (*it).samplePosition <= pos)
        {
            handleEvent((*it).getMessage(), arpOn);
            ++it;
        }
        if (arpOn)
        {
            for (int i = 0; i < arpOffCount;)
            {
                if (arpOffs[i].samplesLeft > 0) { ++i; continue; }
                engineNoteOff(arpOffs[i].note);
                if (arpOffs[i].note == arpLastNote) arpLastNote = -1;
                arpOffs[i] = arpOffs[--arpOffCount];
            }
            if (arpHeldCount == 0) arpStep = 0;
            if (arpPhraseStart)
            {
                // first key of a phrase: step 0 sounds on it, the rest count from here
                arpPhraseStart = false;
                if (grid) { arpAnchor = ht.ppq + pos / spb; arpGridIdx = 0; arpSamplesUntilNext = 0.0; }
            }
            while (arpSamplesUntilNext <= 0.0)
            {
                if (arpHeldCount == 0 && !grid) { arpSamplesUntilNext = 0.0; arpGridIdx = 0; break; }
                int n = -1; float v = 0.9f;
                if (arpNextNote(n, v))
                {
                    if (arpLastNote >= 0) engineNoteOff(arpLastNote);
                    for (int i = 0; i < arpOffCount;)
                        if (arpOffs[i].note == n || arpOffs[i].note == arpLastNote) arpOffs[i] = arpOffs[--arpOffCount];
                        else ++i;
                    engineNoteOn(n, playedVel ? v : 0.9f);
                    arpLastNote = n;
                    double stepLen = (arpStepBeat(arpGridIdx + 1, stepBeats, swing)
                                      - arpStepBeat(arpGridIdx, stepBeats, swing)) * spb;
                    if (arpOffCount < arpMaxOffs)
                        arpOffs[arpOffCount++] = { n, juce::jmax(1, (int) (stepLen * gate * 0.98)) };
                    arpStep++;
                }
                if (grid) arpLastFiredBeat = stepAt(arpGridIdx);
                arpSamplesUntilNext += (arpStepBeat(arpGridIdx + 1, stepBeats, swing)
                                        - arpStepBeat(arpGridIdx, stepBeats, swing)) * spb;
                ++arpGridIdx;
            }
        }
        if (pos >= numSamples) break;

        int next = numSamples;
        if (it != end) next = juce::jmin(next, juce::jmax(pos + 1, (*it).samplePosition));
        if (arpOn)
        {
            if (arpHeldCount > 0 || grid)
                next = juce::jmin(next, pos + juce::jmax(1, (int) std::ceil(arpSamplesUntilNext)));
            for (int i = 0; i < arpOffCount; ++i) next = juce::jmin(next, pos + arpOffs[i].samplesLeft);
        }
        const int seg = next - pos;
        renderSampler(target, pos, seg);
        if (arpOn)
        {
            if (arpHeldCount > 0 || grid) arpSamplesUntilNext -= seg;
            for (int i = 0; i < arpOffCount; ++i) arpOffs[i].samplesLeft -= seg;
        }
        pos = next;
    }
    // events stamped past the block end (non-conforming hosts): apply now
    for (; it != end; ++it) handleEvent((*it).getMessage(), arpOn);
}

void PastelProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.process(midi); // learned CCs land before this block's params are read
    DspParams p = collectParams();
    HostTime ht;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (pos->getBpm()) ht.bpm = *pos->getBpm();
            ht.playing = pos->getIsPlaying();
            if (pos->getPpqPosition()) { ht.ppq = *pos->getPpqPosition(); ht.hasPpq = true; }
        }
    p.bpm = ht.bpm;

    // envelope (cheap change check to avoid lock churn)
    float a = apvts.getRawParameterValue(PP::ATK)->load();
    float d = apvts.getRawParameterValue(PP::DEC)->load();
    float s = apvts.getRawParameterValue(PP::SUS)->load();
    float r = apvts.getRawParameterValue(PP::REL)->load();
    if (a != lastA || d != lastD || s != lastS || r != lastR)
    {
        sampler.setEnvelope(a, d, s, r);
        lastA = a; lastD = d; lastS = s; lastR = r;
    }
    bool adsrOn = apvts.getRawParameterValue(PP::ADSRON)->load() > 0.5f;
    if (adsrOn != lastAdsrOn) { sampler.setAdsrEnabled(adsrOn); lastAdsrOn = adsrOn; }
    int vmode = (int) apvts.getRawParameterValue(PP::VMODE)->load();
    float glide = apvts.getRawParameterValue(PP::GLIDE)->load();
    if (vmode != lastVoiceMode || glide != lastGlide)
    {
        sampler.setVoiceMode(vmode, glide);
        lastVoiceMode = vmode; lastGlide = glide;
    }
    int interp = (int) apvts.getRawParameterValue(PP::INTERP)->load();
    if (interp != lastInterp) { sampler.setInterpolation(interp); lastInterp = interp; }
    int bank = (int) apvts.getRawParameterValue(PP::BANK)->load();
    int preset = (int) apvts.getRawParameterValue(PP::PRESET)->load();
    if ((bank != lastBank || preset != lastPreset) && sampler.isLoaded() && sampler.isSf2())
    {
        sampler.setSf2Preset(bank, preset);
        lastBank = bank; lastPreset = preset;
    }

    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    int nCh = juce::jmin(2, buffer.getNumChannels());
    if (nCh == 1)
    {
        // Mono bus: render + run the stereo FX chain on a stereo scratch,
        // then fold. (Running DSP in place with L aliasing R processed
        // every sample twice through the stereo effects.)
        if (monoScratch.getNumSamples() < numSamples)
            monoScratch.setSize(2, numSamples, false, false, true);
        juce::AudioBuffer<float> st(monoScratch.getArrayOfWritePointers(), 2, numSamples);
        st.clear();
        renderWithMidi(st, midi, numSamples, ht);
        dsp.process(st, p);
        auto* dst = buffer.getWritePointer(0);
        auto* tl = st.getReadPointer(0);
        auto* tr = st.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i) dst[i] = 0.5f * (tl[i] + tr[i]);
    }
    else if (nCh >= 2)
    {
        renderWithMidi(buffer, midi, numSamples, ht);
        dsp.process(buffer, p);
    }
    midi.clear();
    outL = dsp.getLevelL();
    outR = dsp.getLevelR();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PastelProcessor();
}

juce::AudioProcessorEditor* PastelProcessor::createEditor()
{
    return new PastelEditor(*this);
}
