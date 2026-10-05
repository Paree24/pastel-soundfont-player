#include "SamplerEngine.h"
#include <functional>

SamplerEngine::SamplerEngine()
{
    formats.registerBasicFormats();
    for (auto& v : voices)
    {
        v.env.setSampleRate(hostRate);
        v.env.setParams(envA, envD, envS, envR);
    }
    sf2MasterEg.setSampleRate(hostRate);
    sf2MasterEg.setParams(envA, envD, envS, envR);
}

SamplerEngine::~SamplerEngine()
{
    if (soundfont != nullptr) { tsf_close(soundfont); soundfont = nullptr; }
}

float SamplerEngine::midiToFreq(int m)
{
    return 440.0f * std::pow(2.0f, (m - 69) / 12.0f);
}

void SamplerEngine::setSampleRate(double sr)
{
    juce::ScopedLock sl(lock);
    hostRate = sr > 0 ? sr : 44100.0;
    for (auto& v : voices) v.env.setSampleRate(hostRate);
    sf2MasterEg.setSampleRate(hostRate);
    if (soundfont != nullptr)
        tsf_set_output(soundfont, TSF_STEREO_INTERLEAVED, (int) hostRate, 0.0f);
}

void SamplerEngine::setEnvelope(float a, float d, float s, float r)
{
    juce::ScopedLock sl(lock);
    envA = juce::jmax(0.001f, a);
    envD = juce::jmax(0.002f, d);
    envS = juce::jlimit(0.0f, 1.0f, s);
    envR = juce::jmax(0.005f, r);
}

void SamplerEngine::setAdsrEnabled(bool on)
{
    juce::ScopedLock sl(lock);
    adsrOn = on;
}

bool SamplerEngine::isAdsrEnabled() const
{
    juce::ScopedLock sl(lock);
    return adsrOn;
}

juce::String SamplerEngine::currentName() const
{
    if (currentFile.isEmpty()) return "No file loaded";
    return juce::File(currentFile).getFileName();
}

juce::String SamplerEngine::programName() const
{
    if (!loaded) return "-";
    if (loadedSf2)
    {
        auto names = getPresetNames();
        if (sf2Preset >= 0 && sf2Preset < names.size() && names[sf2Preset].isNotEmpty())
            return names[sf2Preset];
        return "Preset " + juce::String(sf2Preset);
    }
    return juce::String(regions.size()) + " regions";
}

int SamplerEngine::getPresetCount() const { return 128; }

juce::StringArray SamplerEngine::getPresetNames() const
{
    juce::StringArray names;
    if (soundfont == nullptr) return names;
    int count = tsf_get_presetcount(soundfont);
    for (int i = 0; i < count; ++i)
        names.add(juce::String(tsf_get_presetname(soundfont, i)));
    return names;
}

void SamplerEngine::setSf2Preset(int bank, int preset)
{
    juce::ScopedLock sl(lock);
    sf2Bank = juce::jlimit(0, 127, bank);
    sf2Preset = juce::jlimit(0, 127, preset);
    if (soundfont != nullptr && loadedSf2)
    {
        tsf_channel_set_bank_preset(soundfont, 0, sf2Bank, sf2Preset);
        // also set a few more channels so multi-channel files behave
        for (int ch = 1; ch < 4; ++ch)
            tsf_channel_set_bank_preset(soundfont, ch, sf2Bank, sf2Preset);
    }
}

bool SamplerEngine::loadFile(const juce::String& path, juce::String& errorOut)
{
    juce::ScopedLock sl(lock);
    unloadInternal();
    juce::File f(path);
    if (!f.existsAsFile()) { errorOut = "File not found: " + path; return false; }
    auto ext = f.getFileExtension().toLowerCase();

    if (ext == ".sf2" || ext == ".sf3")
    {
        tsf* handle = tsf_load_filename(f.getFullPathName().toRawUTF8());
        if (handle == nullptr) { errorOut = "Could not parse SF2 file."; return false; }
        soundfont = handle;
        tsf_set_output(soundfont, TSF_STEREO_INTERLEAVED, (int) hostRate, 0.0f);
        tsf_set_max_voices(soundfont, 128);
        tsf_channel_set_bank_preset(soundfont, 0, sf2Bank, sf2Preset);
        for (int ch = 1; ch < 4; ++ch)
            tsf_channel_set_bank_preset(soundfont, ch, sf2Bank, sf2Preset);
        loaded = true; loadedSf2 = true; currentFile = f.getFullPathName();
        return true;
    }
    if (ext == ".sfz")
    {
        juce::String text = loadSfzFileText(f);
        if (text.isEmpty()) { errorOut = "SFZ file is empty."; return false; }
        juce::StringArray visited;
        visited.add(f.getFullPathName());
        expandSfzIncludes(text, f.getParentDirectory(), 0, visited);
        if (!parseSfz(text, f.getParentDirectory()))
        {
            errorOut = lastLoadDetail.isNotEmpty() ? lastLoadDetail : "Loading cancelled.";
            return false;
        }
        if (regions.empty())
        {
            errorOut = "No <region> with a loadable sample found. " + lastLoadDetail;
            return false;
        }
        loaded = true; loadedSf2 = false; currentFile = f.getFullPathName();
        return true;
    }
    errorOut = "Unsupported extension (use .sf2 or .sfz).";
    return false;
}

void SamplerEngine::unload()
{
    juce::ScopedLock sl(lock);
    unloadInternal();
}

// must be called with lock held
void SamplerEngine::unloadInternal()
{
    for (auto& v : voices) { v.active = false; v.region = -1; }
    regions.clear();
    sampleBuffers.clear();
    delayed.clear();
    localIndex.clear();
    localPaths.clear();
    swLastKeys.clear();
    swLabels.clear();
    samplePaths.clear();
    for (int i = 0; i < 128; ++i) lastVel[i] = 0;
    for (int i = 0; i < 128; ++i) ccState[i] = 0;
    if (soundfont != nullptr) { tsf_close(soundfont); soundfont = nullptr; }
    sf2EgGate = false;
    sf2MasterEg.reset();
    lastKeyswitch = -1;
    loaded = false; loadedSf2 = false; currentFile = "";
}

bool SamplerEngine::loadSampleFile(const juce::File& f, int& indexOut, double& rateOut)
{
    std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
    if (r == nullptr || r->lengthInSamples <= 0) return false;
    juce::AudioBuffer<float> buf((int) r->numChannels, (int) r->lengthInSamples);
    // A failed/transient read must never be cached as a silent sample:
    // skip the region so fallback matching can try the next candidate.
    if (!r->read(&buf, 0, (int) r->lengthInSamples, 0, true, true)) return false;
    rateOut = r->sampleRate;
    indexOut = (int) sampleBuffers.size();
    sampleBuffers.push_back(std::move(buf));
    samplePaths.push_back(f.getFullPathName());
    return true;
}

juce::String SamplerEngine::getVoiceSamplePath(int voiceIdx) const
{
    juce::ScopedLock sl(lock);
    if (voiceIdx < 0 || voiceIdx >= maxVoices) return "";
    const auto& v = voices[voiceIdx];
    if (!v.active || v.region < 0 || v.region >= (int) regions.size()) return "";
    int si = regions[(size_t) v.region].sampleIndex;
    if (si < 0 || si >= (int) samplePaths.size()) return "";
    return samplePaths[(size_t) si];
}

juce::String SamplerEngine::getSwitchLabel(int note) const
{
    juce::ScopedLock sl(lock);
    auto it = swLabels.find(note);
    return it == swLabels.end() ? juce::String() : it->second;
}

int SamplerEngine::getVoiceSwReq(int voiceIdx) const
{
    juce::ScopedLock sl(lock);
    if (voiceIdx < 0 || voiceIdx >= maxVoices) return -2;
    const auto& v = voices[voiceIdx];
    if (!v.active || v.region < 0 || v.region >= (int) regions.size()) return -2;
    const auto& r = regions[(size_t) v.region];
    if (r.keyswitch >= 0) return 1000 + r.keyswitch;
    if (r.swDown >= 0) return 2000 + r.swDown;
    return r.swLastReq;
}

static juce::String stripBlockComments(const juce::String& t)
{
    juce::String out;
    out.preallocateBytes(t.length());
    int i = 0, n = t.length();
    while (i < n)
    {
        int s = t.indexOf(i, "/*");
        if (s < 0) { out += t.substring(i); break; }
        out += t.substring(i, s);
        int e = t.indexOf(s + 2, "*/");
        if (e < 0) break; // unterminated block: drop the rest
        i = e + 2;
        out += "\n";
    }
    return out;
}

static juce::String stripComments(juce::String t)
{
    t = stripBlockComments(t);
    // remove // comments — but only when the // is at line start or
    // preceded by whitespace, so paths like Drums//kick.wav
    // (double slash from trailing-slash #defines) survive
    juce::StringArray lines = juce::StringArray::fromLines(t);
    juce::String out;
    for (auto& ln : lines)
    {
        int cut = -1;
        int idx = 0;
        while (true)
        {
            idx = ln.indexOf(idx, "//");
            if (idx < 0) break;
            if (idx == 0 || juce::CharacterFunctions::isWhitespace(ln[idx - 1])) { cut = idx; break; }
            idx += 2;
        }
        out += (cut >= 0 ? ln.substring(0, cut) : ln) + "\n";
    }
    return out;
}

// Bounded recursive audio-file walk. Never descends into symlinked
// directories (cycle-proof; symlinked FILES still match). Stops at
// maxEntries visited or the deadline (0 = no deadline). Returns true
// if the walk completed, false if capped.
static bool walkAudioFiles(const juce::File& root, int maxEntries, juce::uint32 deadlineMs,
                           const std::function<void(const juce::File&)>& cb)
{
    if (!root.isDirectory() || maxEntries <= 0) return true;
    int visited = 0;
    int sinceCheck = 0;
    std::vector<juce::File> stack;
    stack.push_back(root);
    while (!stack.empty())
    {
        if (visited >= maxEntries) return false;
        if (deadlineMs != 0 && ++sinceCheck >= 256)
        {
            sinceCheck = 0;
            if ((juce::int32)(juce::Time::getMillisecondCounter() - deadlineMs) >= 0)
                return false;
        }
        juce::File dir = stack.back(); stack.pop_back();
        juce::DirectoryIterator it(dir, false, "*", juce::File::findFilesAndDirectories,
                                   juce::File::FollowSymlinks::no);
        bool isDir = false;
        while (it.next(&isDir, nullptr, nullptr, nullptr, nullptr, nullptr))
        {
            if (++visited > maxEntries) return false;
            juce::File f = it.getFile();
            if (isDir)
            {
                if (f.getFileName().startsWith(".") || f.isSymbolicLink()) continue;
                stack.push_back(f);
            }
            else if (SamplerEngine::isAudioFile(f)) cb(f);
        }
    }
    return true;
}

// decode .sfz bytes: UTF-16 LE/BE BOM or plain UTF-8/ANSI
static juce::String decodeSfzBytes(const char* data, size_t size)
{
    const auto* u = reinterpret_cast<const unsigned char*>(data);
    if (size >= 2 && u[0] == 0xFF && u[1] == 0xFE)
    {
        juce::String result;
        result.preallocateBytes(size);
        for (size_t i = 2; i + 1 < size; i += 2)
            result += (juce::juce_wchar)(u[i] | (u[i + 1] << 8));
        if (result.startsWithChar((juce::juce_wchar) 0xFEFF)) result = result.substring(1);
        return result;
    }
    if (size >= 2 && u[0] == 0xFE && u[1] == 0xFF)
    {
        juce::String result;
        result.preallocateBytes(size);
        for (size_t i = 2; i + 1 < size; i += 2)
            result += (juce::juce_wchar)((u[i] << 8) | u[i + 1]);
        if (result.startsWithChar((juce::juce_wchar) 0xFEFF)) result = result.substring(1);
        return result;
    }
    return juce::String::fromUTF8(data, (int) size);
}

juce::String SamplerEngine::loadSfzFileText(const juce::File& f)
{
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb) || mb.getSize() == 0) return "";
    return decodeSfzBytes(static_cast<const char*>(mb.getData()), mb.getSize());
}

// Expand #include "rel/path.sfz" recursively. Included content is wrapped
// with #basedir markers so its relative sample paths resolve correctly.
void SamplerEngine::expandSfzIncludes(juce::String& text, const juce::File& dir,
                                      int depth, juce::StringArray& visited)
{
    if (depth > 8) return;
    juce::StringArray lines = juce::StringArray::fromLines(text);
    juce::String out;
    for (auto& ln : lines)
    {
        juce::String t = ln.trim();
        if (t.startsWith("#include"))
        {
            juce::String inc = t.substring(8).trim()
                .removeCharacters("\"<>").trim();
            if (inc.isNotEmpty())
            {
                juce::File incFile = juce::File::isAbsolutePath(inc)
                    ? juce::File(inc) : dir.getChildFile(inc);
                juce::String canon = incFile.getFullPathName();
                if (incFile.existsAsFile() && !visited.contains(canon))
                {
                    visited.add(canon);
                    juce::String sub = loadSfzFileText(incFile);
                    expandSfzIncludes(sub, incFile.getParentDirectory(), depth + 1, visited);
                    // File boundary: sample paths below resolve against the
                    // included file's dir, and <global>/<group> scope resets
                    // here (prevents cross-file opcode leaks, e.g. a
                    // trigger=release global poisoning later files).
                    out += "\n#newfile\n";
                    out += "\n#basedir \"" + incFile.getParentDirectory().getFullPathName() + "\"\n";
                    out += sub + "\n";
                    out += "\n#backfile\n"; // restore dir only; parent scope continues
                    out += "\n#basedir \"" + dir.getFullPathName() + "\"\n";
                    continue;
                }
            }
        }
        out += ln + "\n";
    }
    text = out;
}

// note number or note name (C-1..G9, sharps/flats) -> midi 0..127
static int sfzNoteValue(const juce::String& v, int fallback)
{
    juce::String t = v.trim().toLowerCase();
    if (t.isEmpty()) return fallback;
    if (t.containsOnly("0123456789+-")) return juce::jlimit(0, 127, t.getIntValue());
    if (t[0] < 'a' || t[0] > 'g') return fallback;
    int semi = 0;
    switch (t[0]) { case 'c': semi = 0; break; case 'd': semi = 2; break; case 'e': semi = 4; break;
                    case 'f': semi = 5; break; case 'g': semi = 7; break; case 'a': semi = 9; break;
                    case 'b': semi = 11; break; default: return fallback; }
    int pos = 1;
    if (pos < t.length() && (t[pos] == '#' || t[pos] == 's')) { semi += 1; ++pos; }
    else if (pos < t.length() && t[pos] == 'b') { semi -= 1; ++pos; }
    int oct = juce::String(t.substring(pos)).getIntValue();
    int midi = (oct + 1) * 12 + semi;
    return (midi >= 0 && midi <= 127) ? midi : fallback;
}

// normalize an sfz sample path: "\ " -> space, other backslashes -> '/'
static juce::String normalizeSfzPath(juce::String p)
{
    p = p.trim();
    if (p.length() >= 2 && ((p.startsWith("\"") && p.endsWith("\""))
        || (p.startsWith("'") && p.endsWith("'"))))
        p = p.substring(1, p.length() - 1);
    const juce::String placeholder = juce::String::charToString((juce::juce_wchar) 0x01);
    p = p.replace("\\ ", placeholder);
    p = p.replace("\\", "/");
    p = p.replace(placeholder, " ");
    while (p.contains("//")) p = p.replace("//", "/"); // trailing-slash #define artefacts
    while (p.startsWith("./")) p = p.substring(2);
    return p;
}

void SamplerEngine::setSearchPaths(const juce::StringArray& paths)
{
    juce::ScopedLock sl(lock);
    searchPaths = paths;
    libIndexKey = ""; // invalidate library basename cache
}

void SamplerEngine::setLoadMonitor(std::atomic<bool>* cancel, std::atomic<float>* progress)
{
    cancelFlag = cancel;
    progressOut = progress;
}

void SamplerEngine::swapWith(SamplerEngine& other)
{
    juce::ScopedLock sl(lock);
    std::swap(loaded, other.loaded);
    std::swap(loadedSf2, other.loadedSf2);
    std::swap(currentFile, other.currentFile);
    std::swap(lastLoadDetail, other.lastLoadDetail);
    regions.swap(other.regions);
    sampleBuffers.swap(other.sampleBuffers);
    samplePaths.swap(other.samplePaths);
    for (int i = 0; i < maxVoices; ++i) std::swap(voices[i], other.voices[i]);
    std::swap(voiceCounter, other.voiceCounter);
    sf2MasterEg.reset(); other.sf2MasterEg.reset();
    sf2EgGate = false; other.sf2EgGate = false;
    std::swap(soundfont, other.soundfont);
    std::swap(sf2Bank, other.sf2Bank);
    std::swap(sf2Preset, other.sf2Preset);
    tsfTemp.clear(); other.tsfTemp.clear();
    libIndex.swap(other.libIndex);
    std::swap(libIndexKey, other.libIndexKey);
    std::swap(libIndexedFiles, other.libIndexedFiles);
    std::swap(libIndexedRoots, other.libIndexedRoots);
    localIndex.swap(other.localIndex);
    localPaths.swap(other.localPaths);
    std::swap(searchCapped, other.searchCapped);
    std::swap(lastKeyswitch, other.lastKeyswitch);
    swLastKeys.swap(other.swLastKeys);
    swLabels.swap(other.swLabels);
    for (int i = 0; i < 128; ++i) std::swap(ccState[i], other.ccState[i]);
    for (int i = 0; i < 128; ++i) std::swap(lastVel[i], other.lastVel[i]);
    delayed.swap(other.delayed);
}

bool SamplerEngine::isAudioFile(const juce::File& f)
{
    auto e = f.getFileExtension().toLowerCase();
    return e == ".wav" || e == ".aif" || e == ".aiff" || e == ".flac"
        || e == ".ogg" || e == ".mp3";
}

// Global basename index over the library shortcut folders, built once
// (fingerprinted by folder list) and shared across loads. Nested roots
// are de-duplicated; each root has an entry budget and the whole build
// is time-boxed so slow media can't hang the DAW.
void SamplerEngine::ensureLibraryIndex()
{
    juce::String key = searchPaths.joinIntoString("|");
    if (key == libIndexKey) return;
    libIndex.clear();
    libIndexKey = key;
    libIndexedFiles = 0; libIndexedRoots = 0;
    searchCapped = false;

    std::vector<juce::String> roots;
    for (auto& s : searchPaths)
    {
        juce::File d(s);
        if (!d.isDirectory() || (int) roots.size() >= 16) continue;
        juce::String full = d.getFullPathName();
        bool nested = false;
        for (auto& k : roots)
            if (full == k || full.startsWith(k + "/")) { nested = true; break; }
        if (!nested) roots.push_back(full);
    }
    juce::uint32 deadline = juce::Time::getMillisecondCounter() + 8000;
    for (auto& r : roots)
    {
        juce::File root(r);
        ++libIndexedRoots;
        bool done = walkAudioFiles(root, 50000, deadline, [&](const juce::File& f)
        {
            juce::String b = f.getFileName().toLowerCase();
            if (libIndex.find(b) == libIndex.end())
            {
                libIndex[b] = f.getFullPathName();
                ++libIndexedFiles;
            }
        });
        if (!done) searchCapped = true;
    }
}

// Resolve a sample path: exact locations first (sfz dir, default_path,
// parent dirs for moved trees, Windows drive-letter paths), then
// case-insensitive match under the sfz dir, then basename search across
// the indexed library folders.
juce::File SamplerEngine::resolveSample(const juce::File& sfzDir, const juce::String& defaultPath,
                                        const juce::String& raw, juce::String* triedOut)
{
    juce::String p = normalizeSfzPath(raw);
    juce::StringArray tried;

    // Windows absolute path (C:/Libs/...) -> also try without the drive part
    juce::String stripped;
    if (p.length() >= 2 && p[1] == ':' && ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z')))
        stripped = p.substring(2);

    if (juce::File::isAbsolutePath(p))
    {
        juce::File f(p);
        tried.add(f.getFullPathName());
        if (f.existsAsFile()) return f;
    }
    else
    {
        // exact checks: sfz dir + parents (covers trees moved as a whole)
        std::vector<juce::File> exactDirs;
        exactDirs.push_back(sfzDir);
        juce::File par = sfzDir.getParentDirectory();
        for (int i = 0; i < 2 && par != sfzDir; ++i)
        {
            exactDirs.push_back(par);
            juce::File next = par.getParentDirectory();
            if (next == par) break;
            par = next;
        }
        juce::StringArray rels;
        if (defaultPath.isNotEmpty()) rels.add(defaultPath + "/" + p);
        rels.add(p);
        if (stripped.isNotEmpty())
        {
            if (defaultPath.isNotEmpty()) rels.add(defaultPath + "/" + stripped);
            rels.add(stripped);
        }
        for (auto& base : exactDirs)
            for (auto& r : rels)
            {
                juce::File f = base.getChildFile(r);
                if (tried.size() < 8) tried.add(f.getFullPathName());
                if (f.existsAsFile()) return f;
            }

        // case-insensitive suffix match against the single pre-walked
        // subtree index (CPU only — no disk walk per region)
        juce::String want1 = (defaultPath.isNotEmpty() ? defaultPath + "/" + p : p).toLowerCase();
        juce::String want2 = p.toLowerCase();
        juce::String want3 = stripped.isNotEmpty() ? stripped.toLowerCase() : juce::String();
        for (auto& e : localPaths)
        {
            const juce::String& full = e.first;
            if (full.endsWith(want1) || full.endsWith("/" + want2) || full.endsWith(want2)
                || (!want3.isEmpty() && (full.endsWith(want3) || full.endsWith("/" + want3))))
                return juce::File(e.second);
        }
    }

    // basename search: this sfz subtree first, then the library index
    ensureLibraryIndex();
    juce::String base = juce::File(p).getFileName().toLowerCase();
    auto lit = localIndex.find(base);
    if (lit != localIndex.end())
    {
        juce::File f(lit->second);
        if (f.existsAsFile()) return f;
    }
    auto it = libIndex.find(base);
    if (it != libIndex.end())
    {
        juce::File f(it->second);
        if (f.existsAsFile()) return f;
    }
    if (stripped.isNotEmpty())
    {
        juce::String base2 = juce::File(stripped).getFileName().toLowerCase();
        if (base2 != base)
        {
            lit = localIndex.find(base2);
            if (lit != localIndex.end())
            {
                juce::File f(lit->second);
                if (f.existsAsFile()) return f;
            }
            it = libIndex.find(base2);
            if (it != libIndex.end())
            {
                juce::File f(it->second);
                if (f.existsAsFile()) return f;
            }
        }
    }
    if (triedOut != nullptr)
    {
        *triedOut = tried.joinIntoString(" | ");
        if (!stripped.isEmpty()) *triedOut += " | (win-path, basename '" + juce::File(stripped).getFileName() + "')";
    }
    return {};
}

bool SamplerEngine::parseSfz(const juce::String& text, const juce::File& sfzDir)
{
    regions.clear();
    sampleBuffers.clear();
    samplePaths.clear();
    swLastKeys.clear();
    swLabels.clear();

    // #define $NAME value... -> global text substitution (longest names first)
    juce::String t = text;
    {
        std::vector<std::pair<juce::String, juce::String>> defines;
        juce::StringArray lines = juce::StringArray::fromLines(t);
        juce::String rest;
        for (auto& ln : lines)
        {
            juce::String tr = ln.trim();
            if (tr.startsWith("#define"))
            {
                juce::String tail = tr.substring(7).trim();
                int sp = tail.indexOfChar(' ');
                int tb = tail.indexOfChar('\t');
                int cut = sp < 0 ? tb : (tb < 0 ? sp : juce::jmin(sp, tb));
                juce::String name = cut < 0 ? tail : tail.substring(0, cut).trim();
                juce::String val = cut < 0 ? juce::String() : tail.substring(cut + 1).trim();
                if (name.isNotEmpty()) defines.push_back({ name, val });
            }
            else rest += ln + "\n";
        }
        std::sort(defines.begin(), defines.end(),
            [](const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
        for (auto& d : defines) rest = rest.replace(d.first, d.second);
        t = rest;
    }
    t = stripComments(t);

    // set_ccN initial CC values are GLOBAL (sfizz strategy: textual include,
    // last one wins in stream order). They init live CC state at load;
    // locc/hicc gates, gain_cc/volume_oncc depths and cutoff_cc depths all
    // evaluate against live CCs per trigger (setCC/MIDI CC).
    auto scanSetCC = [](const juce::String& txt)
    {
        std::map<int,int> out;
        int pos = 0;
        while (true)
        {
            int s = txt.indexOf(pos, "set_cc");
            if (s < 0) break;
            int i = s + 6;
            juce::String num;
            while (i < txt.length() && txt[i] >= '0' && txt[i] <= '9') num += txt[i++];
            while (i < txt.length() && (txt[i] == ' ' || txt[i] == '\t' || txt[i] == '=')) ++i;
            juce::String val;
            while (i < txt.length() && ((txt[i] >= '0' && txt[i] <= '9') || txt[i] == '-')) val += txt[i++];
            if (num.isNotEmpty() && val.isNotEmpty())
                out[num.getIntValue()] = juce::jlimit(0, 127, val.getIntValue());
            pos = i;
            if (pos <= s) ++pos; // never stall
        }
        return out;
    };
    std::vector<std::map<int,int>> segCC;
    {
        juce::String cur;
        for (auto& ln : juce::StringArray::fromLines(t))
        {
            if (ln.trim().startsWith("#newfile")) { segCC.push_back(scanSetCC(cur)); cur = ""; }
            else cur += ln + "\n";
        }
        segCC.push_back(scanSetCC(cur));
    }
    std::map<int,int> ccDefaults;
    for (auto& m : segCC) for (auto& kv : m) ccDefaults[kv.first] = kv.second;

    SfzRegion groupDefaults;
    SfzRegion cur = groupDefaults;
    bool inRegion = false;

    // ---- straightforward region parser ----
    groupDefaults = SfzRegion();
    SfzRegion globalVals; // <global> scope: <group> resets to this, not empty
    juce::String grpSample, globalSample;
    bool inGroup = false;
    std::set<int> swLastAll;
    juce::String defaultPath; // from <control> default_path=
    int swLastParsed = -1;
    int swDefaultParsed = -1; // sw_default (any scope): initial switch, last wins (sfizz)
    struct Pending { SfzRegion r; juce::String sample; juce::String baseDir; };
    std::vector<Pending> pendings;
    cur = groupDefaults;
    juce::String curSample;
    inRegion = false;
    int pos = 0;
    juce::String currentBaseDir = sfzDir.getFullPathName();
    auto applyOpcode = [&](SfzRegion& tgt, juce::String& samp, juce::String& defPath, int& swLastOut,
                          const juce::String& key, const juce::String& val)
    {
        if (key == "sample") samp = val;
        else if (key == "default_path") defPath = normalizeSfzPath(val);
        else if (key == "key") { int k = sfzNoteValue(val, 60); tgt.loKey = tgt.hiKey = tgt.rootKey = k; }
        else if (key == "lokey") tgt.loKey = sfzNoteValue(val, tgt.loKey);
        else if (key == "hikey") tgt.hiKey = sfzNoteValue(val, tgt.hiKey);
        else if (key == "pitch_keycenter") tgt.rootKey = sfzNoteValue(val, tgt.rootKey);
        else if (key == "lovel") tgt.loVel = juce::jlimit(0, 127, val.getIntValue());
        else if (key == "hivel") tgt.hiVel = juce::jlimit(0, 127, val.getIntValue());
        else if (key == "tune") tgt.tuneCents = val.getIntValue();
        else if (key == "transpose") tgt.tuneCents += val.getIntValue() * 100;
        else if (key == "volume") tgt.volumeDb = val.getFloatValue();
        else if (key == "pan") tgt.pan = juce::jlimit(0.0f, 1.0f, val.getFloatValue() / 100.0f + 0.5f);
        else if (key == "amp_veltrack") tgt.ampVelTrack = juce::jlimit(0.0f, 1.0f, val.getFloatValue() / 100.0f);
        else if (key == "loop_mode")
        {
            auto lv = val.toLowerCase();
            tgt.loopMode = lv.contains("one_shot") ? 2
                : ((lv.contains("loop_cont") || val == "1" || lv.contains("loop_sustain")) ? 1 : 0);
        }
        else if (key == "loop_start") tgt.loopStart = val.getLargeIntValue();
        else if (key == "loop_end") tgt.loopEnd = val.getLargeIntValue();
        else if (key == "trigger") tgt.trigger = val.toLowerCase().contains("release") ? 1 : 0;
        else if (key == "offset") tgt.offset = juce::jmax((juce::int64) 0, val.getLargeIntValue());
        else if (key == "end") tgt.endSmp = val.getLargeIntValue();
        else if (key == "direction") tgt.direction = val.toLowerCase().contains("reverse") ? 1 : 0;
        else if (key == "pitch_keytrack") tgt.keytrack = val.getIntValue() == 0 ? 0 : 1;
        else if (key == "group") tgt.group = val.getIntValue();
        else if (key == "off_by" || key == "offby") tgt.offBy = val.getIntValue();
        else if (key == "off_mode")
        {
            auto lv = val.toLowerCase();
            tgt.offMode = lv.contains("fast") ? 1 : (lv.contains("time") ? 2 : 0);
        }
        else if (key == "off_time") tgt.offTime = juce::jlimit(0.002f, 2.0f, val.getFloatValue());
        else if (key == "seq_length") tgt.seqLen = juce::jmax(0, val.getIntValue());
        else if (key == "seq_position") tgt.seqPos = juce::jmax(1, val.getIntValue());
        else if (key.startsWith("locc") || key.startsWith("hicc"))
        {
            bool lo = key.startsWith("locc");
            int cc = key.substring(4).getIntValue();
            if (cc >= 0 && cc < 128)
            {
                SfzRegion::CCGate* g = nullptr;
                for (auto& e : tgt.ccGates) if (e.cc == cc) { g = &e; break; }
                if (g == nullptr) { tgt.ccGates.push_back({}); g = &tgt.ccGates.back(); g->cc = cc; }
                if (lo) g->lo = juce::jlimit(0, 127, val.getIntValue());
                else g->hi = juce::jlimit(0, 127, val.getIntValue());
            }
        }
        else if (key == "lorand") tgt.loRand = juce::jlimit(0, 127, val.getIntValue());
        else if (key == "hirand") tgt.hiRand = juce::jlimit(0, 127, val.getIntValue());
        else if (key == "delay") tgt.delaySec = juce::jmax(0.0f, val.getFloatValue());
        else if (key == "amp_random") tgt.ampRandomDb = juce::jlimit(0.0f, 24.0f, std::abs(val.getFloatValue()));
        else if (key == "offset_random") tgt.offsetRandom = juce::jmax((juce::int64) 0, val.getLargeIntValue());
        else if (key == "sw_down") tgt.swDown = sfzNoteValue(val, -1);
        else if (key == "sw_default") swDefaultParsed = sfzNoteValue(val, -1);
        else if (key == "sw_label") tgt.swLabel = val.trim().removeCharacters("\"").substring(0, 48);
        else if (key == "ampeg_attack") { tgt.hasAmpEg = true; tgt.ampA = juce::jmax(0.0f, val.getFloatValue()); }
        else if (key == "ampeg_decay") { tgt.hasAmpEg = true; tgt.ampD = juce::jmax(0.0f, val.getFloatValue()); }
        else if (key == "ampeg_sustain") { tgt.hasAmpEg = true; tgt.ampS = juce::jlimit(0.0f, 1.0f, val.getFloatValue() / 100.0f); }
        else if (key == "ampeg_release") { tgt.hasAmpEg = true; tgt.ampR = juce::jmax(0.0f, val.getFloatValue()); }
        else if (key == "cutoff") { tgt.hasFilter = true; tgt.filtCut = juce::jlimit(30.0f, 19000.0f, val.getFloatValue()); }
        else if (key == "fil_type")
        {
            tgt.hasFilter = true;
            auto lv = val.toLowerCase();
            tgt.filtType = lv.contains("hpf") ? 1 : (lv.contains("bpf") || lv.contains("bp_") ? 2 : 0);
        }
        else if (key == "resonance") { tgt.hasFilter = true; tgt.filtRes = juce::jlimit(0.0f, 1.0f, val.getFloatValue() / 40.0f); }
        else if (key == "fil_keytrack") { tgt.hasFilter = true; tgt.filKeytrack = val.getFloatValue(); }
        else if (key == "fil_veltrack") { tgt.hasFilter = true; tgt.filVeltrack = val.getFloatValue(); }
        else if (key.startsWith("cutoff_cc"))
        {
            // last opcode wins per CC (sfizz connection overwrite); different
            // CCs stack. Evaluated live at voice start (see startVoice).
            tgt.hasFilter = true;
            tgt.filtCcDepth[key.substring(9).getIntValue()] = val.getFloatValue();
        }
        else if (key.startsWith("gain_cc") || key.startsWith("volume_oncc"))
        {
            // last opcode wins per CC (sfizz connection overwrite); different
            // CCs stack. Evaluated live at voice start (see startVoice).
            int cc = key.startsWith("gain_cc")
                ? key.substring(7).getIntValue() : key.substring(11).getIntValue();
            tgt.ccGainDepth[cc] = val.getFloatValue();
        }
        else if (key == "keyswitch") tgt.keyswitch = sfzNoteValue(val, -1);
        else if (key == "sw_lokey") tgt.swLo = sfzNoteValue(val, -1);
        else if (key == "sw_hikey") tgt.swHi = sfzNoteValue(val, -1);
        else if (key == "sw_last")
        {
            int k = sfzNoteValue(val, -1);
            swLastOut = k;
            tgt.swLastReq = k; // region articulation requirement (flows via scope)
            if (k >= 0) swLastAll.insert(k);
        }
    };
    // Path-aware tokenizer: sample=/default_path= values may contain spaces,
    // so they run until the next `word=` opcode (or end of chunk).
    auto parseChunk = [&](const juce::String& chunk, SfzRegion& tgt, juce::String& samp,
                          juce::String& defPath, int& swLastOut)
    {
        auto isWs = [](juce::juce_wchar c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        int i = 0, n = chunk.length();
        while (i < n)
        {
            while (i < n && isWs(chunk[i])) ++i;
            if (i >= n) break;
            int ks = i;
            while (i < n && chunk[i] != '=' && !isWs(chunk[i])) ++i;
            if (i >= n || chunk[i] != '=')
            {
                while (i < n && !isWs(chunk[i])) ++i; // stray token, skip
                continue;
            }
            juce::String key = chunk.substring(ks, i).trim().toLowerCase();
            ++i; // skip '='
            juce::String val;
            if (key == "sample" || key == "default_path" || key == "sw_label")
            {
                while (true)
                {
                    while (i < n && isWs(chunk[i])) ++i;
                    if (i >= n) break;
                    int ws = i;
                    while (ws < n && !isWs(chunk[ws])) ++ws;
                    juce::String word = chunk.substring(i, ws);
                    if (word.contains("=")) break; // next opcode starts here
                    if (val.isNotEmpty()) val += " ";
                    val += word;
                    i = ws;
                }
                val = val.trim();
            }
            else
            {
                int vs = i;
                while (i < n && !isWs(chunk[i])) ++i;
                val = chunk.substring(vs, i);
            }
            if (key.isNotEmpty()) applyOpcode(tgt, samp, defPath, swLastOut, key, val);
        }
    };
    // Line-ordered chunk processing. #directives take effect in stream
    // order: #basedir switches sample resolution from that line on,
    // #newfile (an #included file boundary) first parses what precedes it,
    // then commits any open region and resets header scope — opcodes never
    // leak across files (e.g. a trigger=release or += gain_cc global in one
    // file poisoning all later ones).
    auto commitRegion = [&]()
    {
        if (inRegion)
        {
            // remember the articulation name for this switch (scope label
            // travels with the region; first name wins per key)
            if (cur.swLastReq >= 0 && cur.swLabel.isNotEmpty()
                && swLabels.find(cur.swLastReq) == swLabels.end())
                swLabels[cur.swLastReq] = cur.swLabel;
            pendings.push_back({ cur, curSample, currentBaseDir });
            inRegion = false;
        }
    };
    auto resetScope = [&]()
    {
        groupDefaults = SfzRegion(); globalVals = SfzRegion();
        grpSample = ""; globalSample = "";
        inGroup = false;
    };
    while (pos < t.length())
    {
        int hdr = t.indexOf(pos, "<");
        juce::String chunk = hdr < 0 ? t.substring(pos) : t.substring(pos, hdr);
        juce::String acc; // opcode text in current scope
        auto flushAcc = [&]()
        {
            if (inRegion) parseChunk(acc, cur, curSample, defaultPath, swLastParsed);
            else if (!acc.trim().isEmpty())
            {
                if (inGroup) parseChunk(acc, groupDefaults, grpSample, defaultPath, swLastParsed);
                else parseChunk(acc, globalVals, globalSample, defaultPath, swLastParsed);
            }
            acc = "";
        };
        if (!chunk.contains("#")) { acc = chunk; flushAcc(); }
        else
        {
            for (auto& ln : juce::StringArray::fromLines(chunk))
            {
                juce::String tr = ln.trim();
                if (tr.startsWith("#basedir"))
                {
                    juce::String p = tr.substring(8).trim().removeCharacters("\"").trim();
                    if (p.isNotEmpty()) currentBaseDir = p;
                }
                else if (tr.startsWith("#newfile"))
                {
                    flushAcc(); commitRegion(); resetScope();
                }
                else if (!tr.startsWith("#")) acc += ln + "\n";
                // other #directives dropped
            }
            flushAcc();
        }
        if (hdr < 0) break;
        int end = t.indexOf(hdr, ">");
        if (end < 0) break;
        juce::String header = t.substring(hdr + 1, end).trim().toLowerCase();
        pos = end + 1;
        if (header == "group") { commitRegion(); groupDefaults = globalVals; grpSample = globalSample; inGroup = true; }
        else if (header == "region")
        {
            commitRegion();
            // regions directly under <global>/top level inherit global scope
            if (inGroup) { cur = groupDefaults; curSample = grpSample; }
            else { cur = globalVals; curSample = globalSample; }
            inRegion = true;
        }
        else if (header == "control" || header == "global") { commitRegion(); inGroup = false; }
    }
    commitRegion();

    int regionCount = (int) pendings.size();
    int missingCount = 0;
    juce::String firstMissing, firstTried;
    // Single bounded walk of this sfz subtree per load (not per region).
    // Per-region walks here were the Metal-GTX hang: 9000 regions x disk walk.
    localIndex.clear();
    localPaths.clear();
    walkAudioFiles(sfzDir, 20000, 0, [&](const juce::File& cand)
    {
        juce::String b = cand.getFileName().toLowerCase();
        if (localIndex.find(b) == localIndex.end())
            localIndex[b] = cand.getFullPathName();
        localPaths.push_back({ cand.getFullPathName().toLowerCase(), cand.getFullPathName() });
    });
    std::map<juce::String, int> loadedByPath; // canonical path -> sample index
    std::map<juce::String, double> rateByPath;
    juce::int64 totalSampleBytes = 0;
    static constexpr juce::int64 maxSampleBytes = (juce::int64) 3 * 1024 * 1024 * 1024;
    bool overBudget = false;
    int doneCount = 0;
    for (auto& pr : pendings)
    {
        if ((doneCount++ & 7) == 0) // progress + cancel points (decode dominates)
        {
            if (progressOut != nullptr)
                progressOut->store((float) doneCount / (float) juce::jmax(1, regionCount));
            if (cancelFlag != nullptr && cancelFlag->load())
            {
                lastLoadDetail = "Loading cancelled.";
                return false;
            }
        }
        if (pr.sample.isEmpty()) { ++missingCount; continue; }
        juce::String tried;
        juce::File base = pr.baseDir.isNotEmpty() ? juce::File(pr.baseDir) : sfzDir;
        if (!base.isDirectory()) base = sfzDir;
        juce::File sampleFile = resolveSample(base, defaultPath, pr.sample, &tried);
        if (sampleFile == juce::File())
        {
            ++missingCount;
            if (firstMissing.isEmpty()) { firstMissing = pr.sample; firstTried = tried; }
            continue;
        }
        int idx = -1; double rate = 44100.0;
        juce::String canon = sampleFile.getFullPathName();
        auto known = loadedByPath.find(canon);
        if (known != loadedByPath.end()) // same file reused by many regions: load once
        {
            idx = known->second;
            rate = rateByPath[canon];
        }
        else
        {
            if (totalSampleBytes > maxSampleBytes) { overBudget = true; break; }
            if (!loadSampleFile(sampleFile, idx, rate)) { ++missingCount; continue; }
            loadedByPath[canon] = idx;
            rateByPath[canon] = rate;
            if (idx >= 0 && idx < (int) sampleBuffers.size())
            {
                const auto& buf = sampleBuffers[(size_t) idx];
                totalSampleBytes += (juce::int64) buf.getNumSamples() * buf.getNumChannels() * 4;
            }
        }
        pr.r.sampleIndex = idx;
        pr.r.sampleRate = rate;
        // one_shot needs no loop points (plays through); only loop_continuous
        // without a valid loop falls back to off — clobbering one_shot broke
        // every-modified one-shot voice into a plain gated voice
        if (pr.r.loopMode == 1 && pr.r.loopEnd <= pr.r.loopStart) pr.r.loopMode = 0;
        if (pr.r.swLo >= 0 && pr.r.swHi < 0) pr.r.swHi = pr.r.swLo;
        if (pr.r.swHi >= 0 && pr.r.swLo < 0) pr.r.swLo = pr.r.swHi;
        regions.push_back(pr.r);
    }
    swLastKeys = swLastAll;
    // sfizz strategy: switched layers stay silent until selected; sw_default
    // (last one wins) selects the bank's default articulation on load
    // (Metal GTX: f0 Sus_Down). Banks without sw_default start unselected.
    lastKeyswitch = swDefaultParsed;
    // live CC state: sfizz inits all CCs to 0, set_cc overrides at load
    for (int i = 0; i < 128; ++i) ccState[i] = 0;
    for (auto& kv : ccDefaults)
        if (kv.first >= 0 && kv.first < 128) ccState[kv.first] = kv.second;
    if (overBudget)
    {
        regions.clear();
        lastLoadDetail = "Library too large to preload (>3 GB of samples)."
            + juce::String(regionCount) + " regions found; load a smaller (Lite) patch instead.";
    }
    else if (regions.empty())
    {
        lastLoadDetail = juce::String(regionCount) + " region(s) found, "
            + juce::String(missingCount) + " without a loadable sample."
            + " Searched " + juce::String(libIndexedFiles) + " files in "
            + juce::String(libIndexedRoots) + " shortcut folder(s).";
        if (searchCapped)
            lastLoadDetail += " Search was capped (big/slow folders) —"
                " add the exact Samples folder as a shortcut for a complete search.";
        if (firstMissing.isNotEmpty())
            lastLoadDetail += " First missing: '" + firstMissing + "' (tried: " + firstTried + ")."
                + " Tip: add the folder containing the samples as a shortcut in the Browser.";
    }
    else lastLoadDetail = "";
    if (progressOut != nullptr) progressOut->store(1.0f);
    return true;
}

int SamplerEngine::findRegionIdx(int note, int vel, int wantTrigger, int startAfter,
                                 int randRoll) const
{
    for (size_t i = (size_t)(startAfter + 1); i < regions.size(); ++i)
    {
        const auto& r = regions[i];
        if (r.trigger != wantTrigger) continue;
        if (note < r.loKey || note > r.hiKey || vel < r.loVel || vel > r.hiVel) continue;
        if (!switchOk(r)) continue;
        if (!ccOk(r)) continue;
        if (randRoll >= 0 && (randRoll < r.loRand || randRoll > r.hiRand)) continue;
        if (r.seqLen > 0)
        {
            if (i >= seqGate.size() || !seqGate[i]) continue;
        }
        return (int) i;
    }
    return -1;
}

bool SamplerEngine::fireNote(int note, float vel01, int wantTrigger, bool allowDelay, bool advanceSeq)
{
    float vel = juce::jlimit(0.0f, 1.0f, vel01);
    int r0 = rng.nextInt(128); // one roll per trigger, shared by all candidates
    int v = (int) std::round(vel * 100.0f);
    // sfizz strategy: every key+trigger-matching region evaluates its cycle
    // CHECK-then-advance (post-increment): the first hit plays position 1.
    // Evaluation happens even for regions filtered out later, so sparse
    // cycles can never deadlock, and each region rotates independently.
    seqGate.assign(regions.size(), 1);
    if (advanceSeq)
    {
        for (size_t i = 0; i < regions.size(); ++i)
        {
            auto& r = regions[i];
            if (r.trigger != wantTrigger) continue;
            if (note < r.loKey || note > r.hiKey) continue;
            if (r.seqLen > 0)
            {
                seqGate[i] = ((r.seqCounter % r.seqLen) == (r.seqPos - 1)) ? 1 : 0;
                r.seqCounter++;
            }
        }
    }
    // fire every fully-matching region (layers); unstartable ones
    // (offset past EOF, empty sample) fall through to later matches
    bool any = false;
    for (int startAfter = -1, tries = 0; tries < 512; ++tries)
    {
        int rIdx = findRegionIdx(note, v, wantTrigger, startAfter, r0);
        if (rIdx < 0) break;
        const auto& reg = regions[(size_t) rIdx];
        if (allowDelay && reg.delaySec > 0.0f)
        {
            delayed.push_back({ rIdx, note, vel, wantTrigger, (int)(reg.delaySec * hostRate) });
            if (delayed.size() > 64) delayed.erase(delayed.begin());
            any = true;
        }
        else if (startVoice(rIdx, note, vel, wantTrigger == 0))
        {
            any = true;
        }
        startAfter = rIdx;
    }
    return any;
}

int SamplerEngine::activeVoiceCount() const
{
    juce::ScopedLock sl(lock);
    int n = 0;
    for (auto& v : voices) if (v.active) ++n;
    return n;
}

bool SamplerEngine::isSwitchKey(int note) const
{
    // Discrete selectors only: collected sw_last values + exact
    // keyswitch=/sw_down= keys. Playing keys inside a sw_lokey..sw_hikey
    // span must NOT retune the selection (Metal GTX layout).
    if (swLastKeys.find(note) != swLastKeys.end()) return true;
    for (auto& r : regions)
        if (r.keyswitch == note || r.swDown == note) return true;
    return false;
}

bool SamplerEngine::switchOk(const SfzRegion& r) const
{
    // sfizz strategy: regions WITHOUT keyswitch selectors always play;
    // regions WITH them stay silent until their exact switch is pressed.
    // Discrete selectors only — the sw_lokey..sw_hikey RANGE alone never
    // qualifies a key (Metal GTX spans the playing range with it).
    if (r.keyswitch >= 0 || r.swDown >= 0)
        return r.keyswitch == lastKeyswitch || r.swDown == lastKeyswitch;
    if (r.swLastReq >= 0)
        return lastKeyswitch == r.swLastReq;
    return true;
}

bool SamplerEngine::ccOk(const SfzRegion& r) const
{
    // sfizz strategy: every loccN/hiccN gate evaluates against live CC state
    for (auto& g : r.ccGates)
    {
        int v = (g.cc >= 0 && g.cc < 128) ? ccState[g.cc] : 0;
        if (v < g.lo || v > g.hi) return false;
    }
    return true;
}

void SamplerEngine::setCC(int cc, int value)
{
    juce::ScopedLock sl(lock);
    if (cc >= 0 && cc < 128) ccState[cc] = juce::jlimit(0, 127, value);
}

int SamplerEngine::getCC(int cc) const
{
    juce::ScopedLock sl(lock);
    return (cc >= 0 && cc < 128) ? ccState[cc] : 0;
}

int SamplerEngine::getLastKeyswitch() const
{
    juce::ScopedLock sl(lock);
    return lastKeyswitch;
}

std::vector<std::pair<int,int>> SamplerEngine::getMappedRanges() const
{
    juce::ScopedLock sl(lock);
    std::vector<std::pair<int,int>> out;
    for (auto& r : regions) out.push_back({ r.loKey, r.hiKey });
    std::sort(out.begin(), out.end());
    std::vector<std::pair<int,int>> merged;
    for (auto& p : out)
    {
        if (!merged.empty() && p.first <= merged.back().second + 1)
            merged.back().second = juce::jmax(merged.back().second, p.second);
        else merged.push_back(p);
    }
    return merged;
}

std::vector<std::pair<int,int>> SamplerEngine::getSwitchRanges() const
{
    juce::ScopedLock sl(lock);
    std::vector<std::pair<int,int>> out;
    for (auto& r : regions)
    {
        if (r.keyswitch >= 0) out.push_back({ r.keyswitch, r.keyswitch });
        if (r.swDown >= 0) out.push_back({ r.swDown, r.swDown });
    }
    for (int k : swLastKeys) out.push_back({ k, k });
    std::sort(out.begin(), out.end());
    std::vector<std::pair<int,int>> merged;
    for (auto& p : out)
    {
        if (!merged.empty() && p.first <= merged.back().second + 1)
            merged.back().second = juce::jmax(merged.back().second, p.second);
        else merged.push_back(p);
    }
    return merged;
}

void SamplerEngine::noteOn(int midiNote, float velocity01)
{
    juce::ScopedLock sl(lock);
    float vel = juce::jlimit(0.0f, 1.0f, velocity01);
    if (loadedSf2 && soundfont != nullptr)
    {
        tsf_channel_note_on(soundfont, 0, midiNote, juce::jmax(0.01f, vel));
        // retrigger when idle: the gate may have been released by an
        // earlier all-notes-off while no new note arrived since.
        // UI-ADSR-gated only: toggle off = raw soundfont voice.
        if (adsrOn && (!sf2EgGate || !sf2MasterEg.isActive())) { sf2MasterEg.noteOn(); sf2EgGate = true; }
        return;
    }
    if (!loaded || regions.empty()) return;
    // Articulation select AND play: libraries like Metal GTX put the playing
    // range inside sw_lokey..sw_hikey, so a switch key must still trigger
    // matching regions (pure switch keys match nothing and stay silent).
    if (isSwitchKey(midiNote)) lastKeyswitch = midiNote;
    if (midiNote >= 0 && midiNote < 128) lastVel[midiNote] = (int) std::round(vel * 100.0f);
    fireNote(midiNote, vel, 0, true);
}

bool SamplerEngine::startVoice(int regionIdx, int midiNote, float vel, bool fromNoteOn)
{
    if (regionIdx < 0 || regionIdx >= (int) regions.size()) return false;
    const auto& r = regions[(size_t) regionIdx];
    if (r.sampleIndex < 0 || r.sampleIndex >= (int) sampleBuffers.size()) return false;
    const auto& buf = sampleBuffers[(size_t) r.sampleIndex];
    int frames = buf.getNumSamples();
    if (frames <= 1) return false;

    juce::int64 start64 = r.offset;
    if (r.offsetRandom > 0) // per-hit start jitter (humanization, ARIA-style)
        start64 += rng.nextInt((int) juce::jmin<juce::int64>(r.offsetRandom, 1 << 30));
    int start = (int) juce::jlimit((juce::int64) 0, (juce::int64)(frames - 1), start64);
    int stop = r.endSmp < 0 ? frames : (int) juce::jlimit((juce::int64)(start + 1), (juce::int64) frames, r.endSmp);
    // offset past (near-)EOF: faithful silence, not a 1-sample click
    if (stop <= start + 1) return false;

    // off_by choke (sfizz strategy): the VICTIM's off_by must equal the
    // incoming region's group, and same-note + same-group retriggers never
    // self-choke. Release-triggered voices are exempt from choking entirely.
    // Victims follow their own off_mode (normal = release stage).
    if (fromNoteOn)
    {
        for (auto& v : voices)
        {
            if (!v.active || v.region < 0 || v.region >= (int) regions.size()) continue;
            if (v.trigType != 0) continue; // only attack/CC voices can be choked
            const auto& vr = regions[(size_t) v.region];
            if (vr.offBy <= 0 || vr.offBy != r.group) continue;
            if (vr.group == r.group && v.note == midiNote) continue; // no self-cut
            if (vr.offMode == 1) v.env.choke();
            else if (vr.offMode == 2) v.env.choke(vr.offTime);
            else v.env.noteOff(); // off_mode=normal: release stage, not a cut
        }
    }

    // steal: free slots first, then oldest releasing voice (sfizz order),
    // then quietest active as a last resort instead of dropping the note
    Voice* vp = nullptr;
    for (auto& v : voices) if (!v.active) { vp = &v; break; }
    if (vp == nullptr)
    {
        for (auto& v : voices)
            if (v.env.isReleasing() && (vp == nullptr || v.startedAt < vp->startedAt))
                vp = &v;
    }
    if (vp == nullptr)
    {
        vp = &voices[0];
        for (auto& v : voices)
            if (v.env.ampLevel() < vp->env.ampLevel()
                || (v.env.ampLevel() == vp->env.ampLevel() && v.startedAt < vp->startedAt))
                vp = &v;
        vp->env.noteOff();
    }
    double tuneF = std::pow(2.0, r.tuneCents / 1200.0);
    if (r.keytrack != 0)
    {
        double rootF = midiToFreq(r.rootKey) * tuneF;
        double tgtF = midiToFreq(midiNote);
        vp->ratio = (tgtF / rootF) * (r.sampleRate / hostRate);
    }
    else vp->ratio = tuneF * (r.sampleRate / hostRate);
    vp->region = regionIdx;
    vp->note = midiNote;
    vp->trigType = fromNoteOn ? 0 : 1;
    vp->vel = vel;
    vp->group = r.group;
    vp->dir = r.direction != 0 ? -1 : 1;
    vp->startSamp = start;
    vp->stopSamp = stop;
    vp->pos = vp->dir > 0 ? (double) start : (double)(stop - 1);
    // CC-modulated gain, evaluated LIVE per voice (sfizz strategy): each CC
    // contributes depth * liveValue / 127 dB. Repeated gain_cc lines for the
    // same CC collapsed to one depth at parse (last wins), so restated
    // <global> blocks can't stack into triple-digit dB.
    float ccDb = 0.0f;
    for (auto& kv : r.ccGainDepth)
    {
        int cv = (kv.first >= 0 && kv.first < 128) ? ccState[kv.first] : 0;
        ccDb += kv.second * (float) cv / 127.0f;
    }
    float volGain = juce::Decibels::decibelsToGain(r.volumeDb + ccDb);
    // velocity law (sfizz strategy): squared curve blended by amp_veltrack
    float vt = juce::jlimit(-1.0f, 1.0f, r.ampVelTrack);
    float vg = vel * vel;
    float velGain = std::fabs(vt) * (1.0f - vg);
    velGain = (vt < 0.0f) ? velGain : (1.0f - velGain);
    float g = volGain * velGain;
    if (r.ampRandomDb > 0.0f) // ±dB humanization per hit (ARIA amp_random)
        g *= juce::Decibels::decibelsToGain(r.ampRandomDb * (rng.nextFloat() * 2.0f - 1.0f));
    vp->gainL = g * std::cos(r.pan * juce::MathConstants<float>::halfPi);
    vp->gainR = g * std::sin(r.pan * juce::MathConstants<float>::halfPi);
    // Amplitude EG: UI ADSR when its toggle is on; otherwise the soundfont
    // plays as authored (region ampeg_*), falling back to a neutral gate for
    // plain regions: fastest attack, full sustain, tiny release.
    if (adsrOn) vp->env.setParams(envA, envD, envS, envR);
    else if (r.hasAmpEg) vp->env.setParams(r.ampA, r.ampD, r.ampS, r.ampR);
    else vp->env.setParams(0.001f, 0.0f, 1.0f, 0.01f);
    // Static region filter evaluated at note-on (velocity/key/CC folded in
    // as cents; no per-sample modulation by design).
    vp->filtOn = r.hasFilter;
    if (r.hasFilter)
    {
        float ccCents = 0.0f;
        for (auto& kv : r.filtCcDepth)
        {
            int cv = (kv.first >= 0 && kv.first < 128) ? ccState[kv.first] : 0;
            ccCents += kv.second * (float) cv / 127.0f;
        }
        float cents = r.filKeytrack * ((float) midiNote - 60.0f)
                    + r.filVeltrack * vel + ccCents;
        vp->cutHz = juce::jlimit(30.0f, 19000.0f,
            r.filtCut * std::pow(2.0f, cents / 1200.0f));
        vp->filtType = r.filtType;
        vp->filtRes = r.filtRes;
        vp->vcf[0].reset(); vp->vcf[1].reset();
    }
    vp->env.noteOn();
    vp->active = true;
    vp->startedAt = ++voiceCounter;
    // NOTE: round-robin positions advance in fireNote (once per trigger
    // event per group), not here — one trigger can start many voices.
    return true;
}

void SamplerEngine::noteOff(int midiNote)
{
    juce::ScopedLock sl(lock);
    if (loadedSf2 && soundfont != nullptr)
    {
        tsf_channel_note_off(soundfont, 0, midiNote);
        // release master EG when no voices active
        if (tsf_active_voice_count(soundfont) <= 0 && sf2EgGate)
        {
            sf2MasterEg.noteOff();
            sf2EgGate = false;
        }
        return;
    }
    for (auto& v : voices)
    {
        if (!v.active || v.note != midiNote) continue;
        // one_shot samples ignore note-off and play to the end
        if (v.region >= 0 && v.region < (int) regions.size()
            && regions[(size_t) v.region].loopMode == 2)
            continue;
        v.env.noteOff();
    }
    // release triggers: play matching regions on key release
    if (loaded && !regions.empty() && !isSwitchKey(midiNote))
    {
        int v = (midiNote >= 0 && midiNote < 128) ? lastVel[midiNote] : 100;
        fireNote(midiNote, v / 100.0f, 1, true);
    }
}

void SamplerEngine::allNotesOff()
{
    juce::ScopedLock sl(lock);
    if (loadedSf2 && soundfont != nullptr)
    {
        tsf_note_off_all(soundfont);
        if (sf2EgGate) sf2MasterEg.noteOff();
        return;
    }
    for (auto& v : voices)
        if (v.active) v.env.noteOff();
    delayed.clear();
}

void SamplerEngine::panic()
{
    juce::ScopedLock sl(lock);
    for (auto& v : voices) { v.active = false; v.env.reset(); }
    delayed.clear();
    if (soundfont != nullptr)
    {
        for (int ch = 0; ch < 4; ++ch) tsf_channel_sounds_off_all(soundfont, ch);
        sf2MasterEg.reset();
        sf2EgGate = false;
    }
}

void SamplerEngine::renderAdding(float* destL, float* destR, int numSamples)
{
    juce::ScopedLock sl(lock);
    if (!loaded) return;
    if (loadedSf2 && soundfont != nullptr)
    {
        size_t need = (size_t) numSamples * 2;
        if (tsfTemp.size() < need) tsfTemp.resize(need);
        tsf_render_float(soundfont, tsfTemp.data(), numSamples, 0);
        bool egActive = adsrOn && sf2MasterEg.isActive();
        for (int i = 0; i < numSamples; ++i)
        {
            float eg = 1.0f;
            if (adsrOn && (sf2EgGate || egActive)) { eg = sf2MasterEg.next(); egActive = sf2MasterEg.isActive(); }
            destL[i] += tsfTemp[(size_t) i * 2] * eg;
            destR[i] += tsfTemp[(size_t) i * 2 + 1] * eg;
        }
        // envelope finished with no gate: allow the next noteOn to retrigger
        if (!sf2MasterEg.isActive()) sf2EgGate = false;
        return;
    }
    // fire delayed region starts (timing slop < 1 block): the stored
    // region first, full re-search fallback if it became unstartable
    if (!delayed.empty())
    {
        for (auto it = delayed.begin(); it != delayed.end();)
        {
            it->samplesLeft -= numSamples;
            if (it->samplesLeft <= 0)
            {
                if (!startVoice(it->regionIdx, it->note, it->vel, it->trigger == 0))
                    fireNote(it->note, it->vel, it->trigger, false, false);
                it = delayed.erase(it);
            }
            else ++it;
        }
    }
    for (int i = 0; i < numSamples; ++i)
    {
        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
        {
            if (!v.active) continue;
            if (v.region < 0 || v.region >= (int) regions.size()) { v.active = false; continue; }
            const SfzRegion& reg = regions[(size_t) v.region];
            const auto& buf = sampleBuffers[(size_t) reg.sampleIndex];
            int frames = buf.getNumSamples();
            if (frames <= 0) { v.active = false; continue; }
            float env = v.env.next();
            if (!v.env.isActive()) { v.active = false; continue; }
            int p0 = (int) v.pos;
            float frac = (float)(v.pos - p0);
            bool loopOn = reg.loopMode == 1 && v.dir > 0;
            int loopEnd = (int) juce::jmin((juce::int64) v.stopSamp, reg.loopEnd);
            double loopBase = juce::jmax((double) v.startSamp, (double) reg.loopStart);
            auto readCh = [&](int ch) -> float
            {
                int c = juce::jmin(ch, buf.getNumChannels() - 1);
                const float* d = buf.getReadPointer(c);
                float s0 = (p0 >= v.startSamp && p0 < v.stopSamp) ? d[p0] : 0.0f;
                int p1 = p0 + 1;
                float s1 = 0.0f;
                if (loopOn && loopEnd > loopBase && loopEnd <= frames)
                {
                    if (p1 >= loopEnd) s1 = d[(int) loopBase];
                    else s1 = (p1 < v.stopSamp) ? d[p1] : 0.0f;
                }
                else s1 = (p1 < v.stopSamp) ? d[p1] : 0.0f;
                return s0 + (s1 - s0) * frac;
            };
            float sL = readCh(0);
            float sR = buf.getNumChannels() > 1 ? readCh(1) : sL;
            if (v.filtOn) // region filter runs pre-gain, per channel
            {
                sL = v.vcf[0].process(sL, v.cutHz, v.filtRes, hostRate, v.filtType);
                sR = v.vcf[1].process(sR, v.cutHz, v.filtRes, hostRate, v.filtType);
            }
            l += sL * v.gainL * env;
            r += sR * v.gainR * env;
            v.pos += v.dir * v.ratio;
            if (loopOn && loopEnd > loopBase)
            {
                if (v.pos >= loopEnd)
                    v.pos = loopBase + std::fmod(v.pos - loopBase, (double)(loopEnd - loopBase));
            }
            else if ((v.dir > 0 && v.pos >= v.stopSamp) || (v.dir < 0 && v.pos < v.startSamp))
            {
                v.env.noteOff();
                v.active = false; // sample end reached
            }
        }
        destL[i] += l;
        destR[i] += r;
    }
}
