#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

PastelProcessor::PastelProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PastelPlayer", createParameterLayout())
{
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
        {
            juce::ScopedLock sl(activeLock);
            activeNotes.clear();
        }
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
        {
            juce::ScopedLock sl(activeLock);
            activeNotes.clear();
        }
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
juce::StringArray PastelProcessor::sf2PresetNames() const { return sampler.getPresetNames(); }
bool PastelProcessor::isSoundLoaded() const { return sampler.isLoaded(); }

void PastelProcessor::panic()
{
    sampler.panic();
    arpReset();
    juce::ScopedLock sl(activeLock);
    activeNotes.clear();
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
    {
        juce::ScopedLock sl(activeLock);
        activeNotes.insert(note);
    }
    sampler.noteOn(note, vel);
}

void PastelProcessor::engineNoteOff(int note)
{
    {
        juce::ScopedLock sl(activeLock);
        activeNotes.erase(note);
    }
    sampler.noteOff(note);
}

std::vector<int> PastelProcessor::getActiveNotes() const
{
    juce::ScopedLock sl(activeLock);
    return { activeNotes.begin(), activeNotes.end() };
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

    p.fType = getInt(PP::FTYPE); p.fCut = get(PP::FCUT); p.fRes = get(PP::FRES);
    p.fDrive = get(PP::FDRIVE); p.fSlope = getInt(PP::FSLOPE);
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
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (pos->getBpm()) p.bpm = *pos->getBpm();
    arpLastBpm = p.bpm;
    return p;
}

void PastelProcessor::arpReset()
{
    arpHeld.clear();
    arpPendingOffs.clear();
    arpSamplesUntilNext = 0.0;
    arpStep = 0; arpDir = 1; arpLastNote = -1;
}

int PastelProcessor::arpNextNote()
{
    if (arpHeld.empty()) return -1;
    std::vector<int> notes = arpHeld;
    std::sort(notes.begin(), notes.end());
    int mode = (int) *apvts.getRawParameterValue(PP::ARPMODE);
    int octs = juce::jlimit(1, 4, (int) *apvts.getRawParameterValue(PP::ARPOCT));
    std::vector<int> seq;
    for (int o = 0; o < octs; ++o)
        for (int n : notes)
        {
            int v = n + o * 12;
            if (v <= 127) seq.push_back(v);
        }
    if (seq.empty()) return -1;
    int idx = 0;
    if (mode == 0) idx = arpStep % (int) seq.size();
    else if (mode == 1) idx = (int) seq.size() - 1 - (arpStep % (int) seq.size());
    else if (mode == 2)
    {
        int n = (int) seq.size();
        if (n == 1) idx = 0;
        else
        {
            int cyc = n * 2 - 2;
            int s = arpStep % cyc;
            idx = s < n ? s : cyc - s;
        }
    }
    else idx = juce::Random::getSystemRandom().nextInt((int) seq.size());
    return seq[(size_t) idx];
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

void PastelProcessor::handleMidi(juce::MidiBuffer& midi, int numSamples, double bpm)
{
    bool arpOn = apvts.getRawParameterValue(PP::ARPON)->load() > 0.5f;
    double sr = getSampleRate();

    // collect incoming note events
    struct Ev { int sample; int note; float vel; bool on; };
    std::vector<Ev> evs;
    for (auto m : midi)
    {
        auto msg = m.getMessage();
        if (msg.isNoteOn()) evs.push_back({ m.samplePosition, msg.getNoteNumber(), msg.getFloatVelocity(), true });
        else if (msg.isNoteOff()) evs.push_back({ m.samplePosition, msg.getNoteNumber(), 0.0f, false });
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            sampler.allNotesOff();
            arpHeld.clear();
        }
        else if (msg.isController()) sampler.setCC(msg.getControllerNumber(), msg.getControllerValue());
    }

    if (!arpOn)
    {
        if (!arpHeld.empty() || arpLastNote >= 0) arpReset();
        for (auto& e : evs)
        {
            if (e.on) engineNoteOn(e.note, e.vel);
            else engineNoteOff(e.note);
        }
        return;
    }

    // arp mode: update held set, generate pattern
    for (auto& e : evs)
    {
        if (e.on)
        {
            if (std::find(arpHeld.begin(), arpHeld.end(), e.note) == arpHeld.end())
                arpHeld.push_back(e.note);
        }
        else
        {
            arpHeld.erase(std::remove(arpHeld.begin(), arpHeld.end(), e.note), arpHeld.end());
            // stop ringing arp voice if it matches released key class? keep ringing till gate ends
        }
    }

    int div = (int) apvts.getRawParameterValue(PP::ARPDIV)->load();
    float gate = apvts.getRawParameterValue(PP::ARPGATE)->load();
    double stepSamples = arpBeats(div) * 60.0 / (bpm > 20 ? bpm : 120.0) * sr;

    // process pending note-offs
    for (auto it = arpPendingOffs.begin(); it != arpPendingOffs.end();)
    {
        it->second -= numSamples;
        if (it->second <= 0) { engineNoteOff(it->first); it = arpPendingOffs.erase(it); }
        else ++it;
    }

    if (arpHeld.empty())
    {
        arpSamplesUntilNext = 0.0;
        arpStep = 0;
        return;
    }

    arpSamplesUntilNext -= numSamples;
    while (arpSamplesUntilNext <= 0.0)
    {
        int n = arpNextNote();
        if (n >= 0)
        {
            if (arpLastNote >= 0) engineNoteOff(arpLastNote);
            engineNoteOn(n, 0.9f);
            arpLastNote = n;
            int lenSamples = juce::jmax(1, (int)(stepSamples * juce::jlimit(0.1f, 1.0f, gate) * 0.98));
            arpPendingOffs.push_back({ n, lenSamples });
        }
        arpStep++;
        arpSamplesUntilNext += stepSamples;
        if (stepSamples <= 0) break;
    }
}

void PastelProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    DspParams p = collectParams();

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
    int bank = (int) apvts.getRawParameterValue(PP::BANK)->load();
    int preset = (int) apvts.getRawParameterValue(PP::PRESET)->load();
    if ((bank != lastBank || preset != lastPreset) && sampler.isLoaded() && sampler.isSf2())
    {
        sampler.setSf2Preset(bank, preset);
        lastBank = bank; lastPreset = preset;
    }

    handleMidi(midi, buffer.getNumSamples(), p.bpm);
    midi.clear();

    buffer.clear();
    int nCh = juce::jmin(2, buffer.getNumChannels());
    if (nCh == 1)
    {
        // render mono via temp stereo then fold
        juce::AudioBuffer<float> tmp(2, buffer.getNumSamples());
        tmp.clear();
        sampler.renderAdding(tmp.getWritePointer(0), tmp.getWritePointer(1), buffer.getNumSamples());
        auto* dst = buffer.getWritePointer(0);
        auto* tl = tmp.getReadPointer(0);
        auto* tr = tmp.getReadPointer(1);
        for (int i = 0; i < buffer.getNumSamples(); ++i) dst[i] = 0.5f * (tl[i] + tr[i]);
    }
    else if (nCh >= 2)
    {
        sampler.renderAdding(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
    }

    dsp.process(buffer, p);
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
