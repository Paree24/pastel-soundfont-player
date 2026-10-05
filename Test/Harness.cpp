// PastelHarness: headless smoke test for the sampler + DSP chain.
// Exit 0 = pass.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Parameters.h"
#include <vector>
#include <cstring>
#include <cstdlib>
#include <set>

static int failures = 0;
static void check(bool cond, const char* name)
{
    juce::Logger::writeToLog(juce::String(cond ? "[PASS] " : "[FAIL] ") + name);
    if (!cond) ++failures;
}

static float bufferRMS(juce::AudioBuffer<float>& b)
{
    double sum = 0; int n = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float v = b.getSample(ch, i);
            sum += v * v; ++n;
        }
    return n > 0 ? (float) std::sqrt(sum / n) : 0.0f;
}

// ---- ground-truth helpers: compare engine output against source files ----
static juce::AudioBuffer<float> readAudioFile(const juce::File& f, double& rateOut)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    juce::AudioBuffer<float> buf;
    if (r == nullptr) return buf;
    rateOut = r->sampleRate;
    buf.setSize((int) r->numChannels, (int) juce::jmin<int64_t>(r->lengthInSamples, 1 << 20));
    r->read(&buf, 0, buf.getNumSamples(), 0, true, true);
    return buf;
}

static float bufferPeak(juce::AudioBuffer<float>& b)
{
    float peak = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            peak = juce::jmax(peak, std::abs(b.getSample(ch, i)));
    return peak;
}

// zero-mean normalized correlation over the overlap (gain-independent)
static double normCorr(const float* a, const float* b, int n)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < n; ++i)
    {
        double da = a[i] - ma, db = b[i] - mb;
        sab += da * db; saa += da * da; sbb += db * db;
    }
    if (saa <= 0 || sbb <= 0) return 0;
    return sab / std::sqrt(saa * sbb);
}

// dominant frequency via zero crossings over a window (for steady tones)
static float estimateFreq(const float* d, int n, double sr)
{
    int zc = 0;
    for (int i = 1; i < n; ++i)
        if ((d[i - 1] < 0) != (d[i] < 0)) ++zc;
    return (float)(zc * sr / (2.0 * n));
}

// render a held note, return the sustain tail (voice left running; caller panics)
static juce::AudioBuffer<float> renderHeld(PastelProcessor& proc, int note, float vel, int preBlocks)
{
    juce::MidiBuffer midiB;
    midiB.addEvent(juce::MidiMessage::noteOn(1, note, vel), 0);
    juce::AudioBuffer<float> bufB(2, 512);
    bufB.clear();
    proc.processBlock(bufB, midiB);
    for (int i = 0; i < preBlocks; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
    return bufB;
}

// render a held note, return the FULL stereo take (all blocks concatenated)
static void renderTakeStereo(PastelProcessor& proc, int note, float vel, int blocks,
                             std::vector<float>& outL, std::vector<float>& outR)
{
    outL.clear(); outR.clear();
    outL.reserve((size_t) blocks * 512); outR.reserve((size_t) blocks * 512);
    juce::MidiBuffer midiB;
    midiB.addEvent(juce::MidiMessage::noteOn(1, note, vel), 0);
    juce::AudioBuffer<float> bufB(2, 512);
    for (int i = 0; i < blocks; ++i)
    {
        bufB.clear();
        juce::MidiBuffer m = (i == 0) ? midiB : juce::MidiBuffer();
        proc.processBlock(bufB, m);
        for (int s = 0; s < 512; ++s)
        {
            outL.push_back(bufB.getSample(0, s));
            outR.push_back(bufB.getSample(1, s));
        }
    }
}

// render a held note, return the FULL mono take (all blocks concatenated)
static std::vector<float> renderTake(PastelProcessor& proc, int note, float vel, int blocks)
{
    std::vector<float> out;
    out.reserve((size_t) blocks * 512);
    juce::MidiBuffer midiB;
    midiB.addEvent(juce::MidiMessage::noteOn(1, note, vel), 0);
    juce::AudioBuffer<float> bufB(2, 512);
    for (int i = 0; i < blocks; ++i)
    {
        bufB.clear();
        juce::MidiBuffer m = (i == 0) ? midiB : juce::MidiBuffer();
        proc.processBlock(bufB, m);
        for (int s = 0; s < 512; ++s)
            out.push_back(0.5f * (bufB.getSample(0, s) + bufB.getSample(1, s)));
    }
    return out;
}

// ---- minimal SF2 writer: 1 preset (bank 0, preset 0), 1 mono sample ----
static void sf2PutU16(std::vector<char>& o, uint16_t v)
{
    o.push_back((char)(v & 255)); o.push_back((char)(v >> 8));
}
static void sf2PutU32(std::vector<char>& o, uint32_t v)
{
    for (int i = 0; i < 4; ++i) o.push_back((char)((v >> (8 * i)) & 255));
}
static void sf2PutStr(std::vector<char>& o, const char* s, size_t w)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < w; ++i) o.push_back((char)(i < n ? s[i] : 0));
}
static void sf2Chunk(std::vector<char>& o, const char* id, const std::vector<char>& d)
{
    for (int i = 0; i < 4; ++i) o.push_back(id[i]);
    sf2PutU32(o, (uint32_t) d.size());
    o.insert(o.end(), d.begin(), d.end());
    if (d.size() & 1) o.push_back(0);
}
static void sf2List(std::vector<char>& o, const char* type, const std::vector<char>& d)
{
    for (int i = 0; i < 4; ++i) o.push_back("LIST"[i]);
    sf2PutU32(o, (uint32_t)(d.size() + 4));
    for (int i = 0; i < 4; ++i) o.push_back(type[i]);
    o.insert(o.end(), d.begin(), d.end());
    if ((d.size() + 4) & 1) o.push_back(0);
}
static void writeMinimalSf2(const juce::File& f, float freq)
{
    const int N = 44100;
    std::vector<char> smpl;
    for (int i = 0; i < N; ++i)
    {
        float v = 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / 44100.0);
        sf2PutU16(smpl, (uint16_t)(int16_t) juce::jlimit(-32768, 32767, (int) std::lround(v * 32767)));
    }
    std::vector<char> shdr;
    sf2PutStr(shdr, "TestSmpl", 20);
    sf2PutU32(shdr, 0); sf2PutU32(shdr, N); sf2PutU32(shdr, 0); sf2PutU32(shdr, 0);
    sf2PutU32(shdr, 44100);
    shdr.push_back(69); shdr.push_back(0); // origPitch A4, correction
    sf2PutU16(shdr, 0); sf2PutU16(shdr, 1); // link, mono
    shdr.insert(shdr.end(), 46, 0); // terminal
    std::vector<char> inst;
    sf2PutStr(inst, "TestInst", 20); sf2PutU16(inst, 0);
    sf2PutStr(inst, "EOS", 20); sf2PutU16(inst, 1);
    std::vector<char> ibag;
    sf2PutU16(ibag, 0); sf2PutU16(ibag, 0);
    sf2PutU16(ibag, 2); sf2PutU16(ibag, 1); // terminal: 2 gens, 1 mod
    std::vector<char> imod(10, 0); // terminal only
    std::vector<char> igen;
    sf2PutU16(igen, 53); sf2PutU16(igen, 0); // sampleID 0
    sf2PutU16(igen, 0); sf2PutU16(igen, 0);  // terminal
    std::vector<char> phdr;
    sf2PutStr(phdr, "TestPreset", 20);
    sf2PutU16(phdr, 0); sf2PutU16(phdr, 0); sf2PutU16(phdr, 0); // preset 0 bank 0 bag 0
    sf2PutU32(phdr, 0); sf2PutU32(phdr, 0); sf2PutU32(phdr, 0);
    sf2PutStr(phdr, "EOP", 20);
    sf2PutU16(phdr, 0); sf2PutU16(phdr, 0); sf2PutU16(phdr, 1); // terminal bagNdx=1
    sf2PutU32(phdr, 0); sf2PutU32(phdr, 0); sf2PutU32(phdr, 0);
    std::vector<char> pbag;
    sf2PutU16(pbag, 0); sf2PutU16(pbag, 0);
    sf2PutU16(pbag, 2); sf2PutU16(pbag, 1); // terminal: 2 gens, 1 mod
    std::vector<char> pmod(10, 0);
    std::vector<char> pgen;
    sf2PutU16(pgen, 41); sf2PutU16(pgen, 0); // instrument 0
    sf2PutU16(pgen, 0); sf2PutU16(pgen, 0);  // terminal

    std::vector<char> info, sdta, pdta, tmp;
    sf2Chunk(info, "ifil", []{ std::vector<char> v; sf2PutU16(v, 2); sf2PutU16(v, 1); return v; }());
    sf2Chunk(info, "isng", std::vector<char>{ 'E','M','U','8','0','0','0',0 });
    sf2Chunk(info, "INAM", std::vector<char>{ 'T','e','s','t','S','F','2',0 });
    sf2Chunk(sdta, "smpl", smpl);
    sf2Chunk(pdta, "phdr", phdr); sf2Chunk(pdta, "pbag", pbag);
    sf2Chunk(pdta, "pmod", pmod); sf2Chunk(pdta, "pgen", pgen);
    sf2Chunk(pdta, "inst", inst); sf2Chunk(pdta, "ibag", ibag);
    sf2Chunk(pdta, "imod", imod); sf2Chunk(pdta, "igen", igen);
    sf2Chunk(pdta, "shdr", shdr);
    std::vector<char> body;
    sf2List(body, "INFO", info); sf2List(body, "sdta", sdta); sf2List(body, "pdta", pdta);
    std::vector<char> riff;
    for (int i = 0; i < 4; ++i) riff.push_back("RIFF"[i]);
    sf2PutU32(riff, (uint32_t)(body.size() + 4));
    for (int i = 0; i < 4; ++i) riff.push_back("sfbk"[i]);
    riff.insert(riff.end(), body.begin(), body.end());
    juce::FileOutputStream os(f);
    if (os.openedOk()) os.write(riff.data(), riff.size());
}

static void writeTone(const juce::File& f, float freq, int numChans = 1, float freqR = 0.0f)
{
    juce::AudioBuffer<float> tone(numChans, 44100);
    for (int i = 0; i < 44100; ++i)
    {
        tone.setSample(0, i, 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / 44100.0));
        if (numChans > 1)
            tone.setSample(1, i, 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi
                * (freqR > 0 ? freqR : freq) * i / 44100.0));
    }
    std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream());
    if (os != nullptr && os->openedOk())
    {
        juce::WavAudioFormat wav;
        // writer takes ownership of the stream; destroying the writer
        // finalises the header and closes/flushes the file
        std::unique_ptr<juce::AudioFormatWriter> w(
            wav.createWriterFor(os.release(), 44100.0, (unsigned) numChans, 16, {}, 0));
        if (w != nullptr) w->writeFromAudioSampleBuffer(tone, 0, 44100);
    }
}

int main(int argc, char** argv)
{
    juce::ignoreUnused(argc, argv);
#if !defined(_WIN32)
    setenv("PASTEL_SETTINGS_FOLDER", "PastelPlayerTest", 1); // isolated settings
#endif
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("pastel_harness_test");
    tmp.createDirectory();
    auto wavFile = tmp.getChildFile("tone.wav");
    writeTone(wavFile, 440.0f);
    auto sfzFile = tmp.getChildFile("test.sfz");
    sfzFile.replaceWithText("<region> sample=tone.wav key=60 lokey=0 hikey=127 pitch_keycenter=60\n");

    // realistic fixture: spaced names, default_path, backslashes, note names
    auto realDir = tmp.getChildFile("real");
    auto samplesDir = realDir.getChildFile("Samples");
    samplesDir.createDirectory();
    writeTone(samplesDir.getChildFile("Snare Drum.wav"), 220.0f);
    writeTone(samplesDir.getChildFile("Kick Drum.wav"), 110.0f);
    realDir.getChildFile("kit.sfz").replaceWithText(
        "<control> default_path=Samples/\n"
        "<group> lovel=0 hivel=127\n"
        "<region> sample=Snare Drum.wav key=38\n"
        "<region> sample=Kick Drum.wav key=C1\n");
    realDir.getChildFile("win.sfz").replaceWithText(
        "<region> sample=Samples\\Snare Drum.wav key=40\n"
        "<region> sample=.\\Samples\\Kick Drum.wav lokey=36 hikey=36 pitch_keycenter=C1\n");

    PastelProcessor proc;
    proc.prepareToPlay(44100.0, 512);

    {
        juce::String err;
        bool ok = proc.loadSoundFile(tmp.getChildFile("nope.sfz").getFullPathName(), err);
        check(!ok && err.isNotEmpty(), "missing file rejected with message");
    }
    {
        juce::String err;
        bool ok = proc.loadSoundFile(sfzFile.getFullPathName(), err);
        check(ok, "simple sfz loads");
    }
    {
        juce::String err;
        bool ok = proc.loadSoundFile(realDir.getChildFile("kit.sfz").getFullPathName(), err);
        check(ok, "sfz with default_path + spaced names + note name loads");
        if (!ok) juce::Logger::writeToLog("kit.sfz error: " + err);
    }
    {
        juce::String err;
        bool ok = proc.loadSoundFile(realDir.getChildFile("win.sfz").getFullPathName(), err);
        check(ok, "sfz with backslash paths loads");
        if (!ok) juce::Logger::writeToLog("win.sfz error: " + err);
    }
    // broken sfz reports *why*
    {
        auto bad = tmp.getChildFile("bad.sfz");
        bad.replaceWithText("<region> sample=No Such File.wav key=60\n");
        juce::String err;
        bool ok = proc.loadSoundFile(bad.getFullPathName(), err);
        check(!ok && err.contains("No Such File"), "missing sample reported by name");
    }
    // reload full-range fixture for render tests
    {
        juce::String err;
        proc.loadSoundFile(sfzFile.getFullPathName(), err);
    }
    check(proc.isSoundLoaded(), "sound marked loaded");

    // renders a note and returns its SUSTAIN level (last block before
    // note-off): "does it sound". Release-tail silence has dedicated tests.
    auto renderNote = [&](int note, int blocks, float& rmsOut)
    {
        proc.panic(); // hermetic: no ringing voices from earlier measurements
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
        juce::AudioBuffer<float> buf(2, 512);
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            juce::MidiBuffer m = (b == 0) ? midi : juce::MidiBuffer();
            proc.processBlock(buf, m);
        }
        rmsOut = bufferRMS(buf);
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        for (int b = 0; b < 4; ++b) { buf.clear(); proc.processBlock(buf, off); off.clear(); }
    };

    // reload simple fixture for render tests
    {
        juce::String err;
        proc.loadSoundFile(sfzFile.getFullPathName(), err);
    }
    // stereo balance: mono sample must come out of both speakers
    {
        juce::MidiBuffer midiB;
        midiB.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        juce::AudioBuffer<float> bufB(2, 512);
        bufB.clear();
        proc.processBlock(bufB, midiB);
        double sumL = 0, sumR = 0;
        for (int i = 0; i < 25; ++i)
        {
            bufB.clear();
            juce::MidiBuffer e;
            proc.processBlock(bufB, e);
            if (i >= 5) // skip attack transient
                for (int s = 0; s < 512; ++s)
                {
                    sumL += bufB.getSample(0, s) * bufB.getSample(0, s);
                    sumR += bufB.getSample(1, s) * bufB.getSample(1, s);
                }
        }
        float ratio = (float) std::sqrt(sumL / juce::jmax(sumR, 1e-12));
        juce::Logger::writeToLog("L/R energy ratio: " + juce::String(ratio));
        check(ratio > 0.5f && ratio < 2.0f, "mono sfz centered L/R");
        juce::MidiBuffer offB;
        offB.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        for (int i = 0; i < 120; ++i) { bufB.clear(); proc.processBlock(bufB, offB); offB.clear(); }
    }

    // stereo sample passthrough: L content must stay L, R content stay R
    {
        auto stDir = tmp.getChildFile("stereo");
        stDir.createDirectory();
        writeTone(stDir.getChildFile("wide.wav"), 220.0f, 2, 440.0f);
        stDir.getChildFile("wide.sfz").replaceWithText("<region> sample=wide.wav key=60\n");
        juce::String err;
        bool ok = proc.loadSoundFile(stDir.getChildFile("wide.sfz").getFullPathName(), err);
        check(ok, "stereo sfz loads");
        juce::MidiBuffer midiB;
        midiB.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        juce::AudioBuffer<float> bufB(2, 512);
        bufB.clear();
        proc.processBlock(bufB, midiB);
        double eL220 = 0, eL440 = 0, eR220 = 0, eR440 = 0;
        for (int i = 0; i < 40; ++i) // goertzel-ish: correlate with 220/440 refs
        {
            bufB.clear();
            juce::MidiBuffer e;
            proc.processBlock(bufB, e);
            if (i >= 10)
                for (int s = 0; s < 512; ++s)
                {
                    int gi = i * 512 + s;
                    float r220 = std::sin(2.0f * (float) juce::MathConstants<double>::pi * 220.0f * gi / 44100.0f);
                    float r440 = std::sin(2.0f * (float) juce::MathConstants<double>::pi * 440.0f * gi / 44100.0f);
                    float l = bufB.getSample(0, s), r = bufB.getSample(1, s);
                    eL220 += l * r220; eL440 += l * r440;
                    eR220 += r * r220; eR440 += r * r440;
                }
        }
        // L should be mostly 220, R mostly 440 (allow 6dB slop for envelope/filter)
        bool chOk = std::abs(eL220) > 2.0 * std::abs(eL440) && std::abs(eR440) > 2.0 * std::abs(eR220);
        juce::Logger::writeToLog("stereo sep L220=" + juce::String(std::abs(eL220))
            + " L440=" + juce::String(std::abs(eL440))
            + " R220=" + juce::String(std::abs(eR220)) + " R440=" + juce::String(std::abs(eR440)));
        check(chOk, "stereo channels not swapped/collapsed");
        juce::MidiBuffer offB;
        offB.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        for (int i = 0; i < 60; ++i) { bufB.clear(); proc.processBlock(bufB, offB); offB.clear(); }
        juce::String err2;
        proc.loadSoundFile(sfzFile.getFullPathName(), err2);
    }

    // SF2: survives all-notes-off + repeated note cycles (master EG gate)
    {
        auto sf2File = tmp.getChildFile("mini.sf2");
        writeMinimalSf2(sf2File, 440.0f);
        juce::String err;
        bool ok = proc.loadSoundFile(sf2File.getFullPathName(), err);
        check(ok, "sf2 loads");
        if (!ok) juce::Logger::writeToLog("sf2 error: " + err);
        auto playAndMeasure = [&](int note, int blocks)
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < blocks; ++i)
            {
                bufB.clear();
                juce::MidiBuffer e;
                proc.processBlock(bufB, e);
            }
            return bufferRMS(bufB);
        };
        float r1 = playAndMeasure(69, 15);
        check(r1 > 0.001f, "sf2 renders audio");
        // DAW-stop style all-notes-off, let everything die out
        juce::MidiBuffer stop;
        stop.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        juce::AudioBuffer<float> quiet(2, 512);
        for (int i = 0; i < 150; ++i)
        {
            quiet.clear();
            juce::MidiBuffer m = (i == 0) ? stop : juce::MidiBuffer();
            proc.processBlock(quiet, m);
        }
        float r2 = playAndMeasure(69, 15);
        juce::Logger::writeToLog("sf2 rms after all-notes-off: " + juce::String(r2));
        check(r2 > 0.001f, "sf2 still sounds after all-notes-off");
        // staccato cycles (voice-count-zero noteOff edge)
        bool cyclesOk = true;
        for (int c = 0; c < 3; ++c)
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 69, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < 8; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            float rr = bufferRMS(bufB);
            juce::MidiBuffer offB;
            offB.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
            for (int i = 0; i < 120; ++i) { bufB.clear(); proc.processBlock(bufB, offB); offB.clear(); }
            if (rr < 0.001f) cyclesOk = false;
        }
        check(cyclesOk, "sf2 survives staccato cycles");
        juce::String err3;
        proc.loadSoundFile(sfzFile.getFullPathName(), err3);
    }

    // mono switch collapses stereo to identical L/R
    {
        juce::String err;
        proc.loadSoundFile(tmp.getChildFile("stereo/wide.sfz").getFullPathName(), err);
        proc.apvts.getParameter(PP::MONO)->setValueNotifyingHost(1.0f);
        juce::MidiBuffer midiB;
        midiB.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        juce::AudioBuffer<float> bufB(2, 512);
        bufB.clear();
        proc.processBlock(bufB, midiB);
        double maxDiff = 0;
        for (int i = 0; i < 30; ++i)
        {
            bufB.clear();
            juce::MidiBuffer e;
            proc.processBlock(bufB, e);
            if (i >= 10)
                for (int s = 0; s < 512; ++s)
                    maxDiff = juce::jmax(maxDiff, (double) std::abs(bufB.getSample(0, s) - bufB.getSample(1, s)));
        }
        juce::Logger::writeToLog("mono max L-R diff: " + juce::String(maxDiff));
        check(maxDiff < 1e-6, "mono switch sums to dual-mono");
        proc.apvts.getParameter(PP::MONO)->setValueNotifyingHost(0.0f);
        proc.panic();
        juce::String err4;
        proc.loadSoundFile(sfzFile.getFullPathName(), err4);
    }

    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        juce::AudioBuffer<float> buf(2, 512);
        buf.clear();
        proc.processBlock(buf, midi);
        for (int i = 0; i < 20; ++i) { buf.clear(); juce::MidiBuffer e; proc.processBlock(buf, e); }
        float rms = bufferRMS(buf);
        check(rms > 0.001f, "sfz renders audio");
        juce::Logger::writeToLog("sustain rms: " + juce::String(rms));
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        for (int i = 0; i < 120; ++i) { buf.clear(); proc.processBlock(buf, off); off.clear(); }
        check(bufferRMS(buf) < 0.001f, "release goes silent");
    }

    // realistic kit renders on mapped keys incl. note-name root
    {
        juce::String err;
        proc.loadSoundFile(realDir.getChildFile("kit.sfz").getFullPathName(), err);
        float rmsKick = 0, rmsSnare = 0;
        renderNote(24, 12, rmsKick);   // C1
        renderNote(38, 12, rmsSnare);
        check(rmsKick > 0.001f && rmsSnare > 0.001f, "default_path kit renders kick+snare");
    }

    // keyswitch articulations: select + gate + highlight queries
    {
        auto ksDir = tmp.getChildFile("ks");
        ksDir.createDirectory();
        writeTone(ksDir.getChildFile("kick.wav"), 110.0f);
        writeTone(ksDir.getChildFile("snare.wav"), 220.0f);
        ksDir.getChildFile("ks.sfz").replaceWithText(
            "<region> sample=kick.wav key=36\n"
            "<region> sample=snare.wav key=38 keyswitch=24\n"
            "<region> sample=snare.wav key=40 keyswitch=25\n");
        juce::String err;
        bool ok = proc.loadSoundFile(ksDir.getChildFile("ks.sfz").getFullPathName(), err);
        check(ok, "keyswitch sfz loads");
        auto sw = proc.getSwitchRanges();
        bool has24 = false, has25 = false;
        for (auto& r : sw)
        {
            if (r.first <= 24 && r.second >= 24) has24 = true;
            if (r.first <= 25 && r.second >= 25) has25 = true;
        }
        check(has24 && has25, "keyswitch ranges reported");
        auto mp = proc.getMappedRanges();
        check(!mp.empty(), "mapped ranges reported");
        float rmsFresh = 0;
        renderNote(38, 12, rmsFresh);
        check(rmsFresh > 0.001f, "fresh keyswitch bank plays before any selection");
        proc.auditionNoteOn(24, 0.8f);
        proc.auditionNoteOff(24);
        check(proc.getLastKeyswitch() == 24, "keyswitch press selects articulation");
        float rmsSel = 0;
        renderNote(38, 12, rmsSel);
        check(rmsSel > 0.001f, "selected articulation sounds");
        float rmsGated = 0;
        renderNote(40, 12, rmsGated);
        check(rmsGated < 0.0005f, "unselected articulation stays silent");
        proc.panic();
        // switching banks after an articulation was selected must not silence
        // the new bank (sync + async paths)
        {
            juce::String err;
            bool ok2 = proc.loadSoundFile(sfzFile.getFullPathName(), err);
            check(ok2, "reload plain sfz after articulation select");
            float rr = 0;
            renderNote(60, 12, rr);
            check(rr > 0.001f, "new bank sounds after articulation select (sync)");
        }
        {
            proc.requestLoad(sfzFile.getFullPathName());
            juce::uint32 t0 = juce::Time::getMillisecondCounter();
            while (proc.isLoading() && juce::Time::getMillisecondCounter() - t0 < 30000)
                juce::Thread::sleep(20);
            float rr = 0;
            renderNote(60, 12, rr);
            check(rr > 0.001f, "new bank sounds after articulation select (async)");
        }
        proc.panic();
    }

    // relocated samples: parent-relative, Windows absolute, wrong subdir
    {
        auto reloc = tmp.getChildFile("reloc");
        auto patches = reloc.getChildFile("Patches");
        auto samples = reloc.getChildFile("Samples");
        auto elsewhere = reloc.getChildFile("Elsewhere");
        samples.createDirectory();
        elsewhere.createDirectory();
        patches.createDirectory();
        writeTone(samples.getChildFile("Hit String.wav"), 330.0f);
        writeTone(elsewhere.getChildFile("Lead Line.wav"), 550.0f);
        patches.getChildFile("[Guitar - Electric] - UI_METAL-GTX.sfz").replaceWithText(
            "<control> default_path=Nope/\n"
            "<region> sample=../Samples/Hit String.wav key=60\n"
            "<region> sample=C:/UI_METAL-GTX/Samples/Hit String.wav key=62\n"
            "<region> sample=Samples/Lead Line.wav key=64\n");
        proc.library.addShortcut(reloc.getFullPathName());
        juce::String err;
        bool ok = proc.loadSoundFile(
            patches.getChildFile("[Guitar - Electric] - UI_METAL-GTX.sfz").getFullPathName(), err);
        check(ok, "relocated-samples sfz loads");
        if (!ok) juce::Logger::writeToLog("reloc error: " + err);
        juce::Logger::writeToLog("reloc program: " + proc.currentProgramName());
        check(proc.currentProgramName() == "3 regions", "all 3 relocated regions found");
        bool allSound = true;
        for (int n : { 60, 62, 64 })
        {
            float rr = 0.0f;
            renderNote(n, 12, rr);
            if (rr < 0.001f) allSound = false;
        }
        check(allSound, "relocated regions all render");
        proc.panic();
    }

    // Cross-file scope isolation: opcodes (esp. accumulating gain_cc and
    // trigger=release) must not leak from one #included file into later ones.
    // set_cc is GLOBAL (sfizz: textual include, last wins): the top file's
    // knob init voices every file, including ones without their own set_cc.
    {
        auto iso = tmp.getChildFile("iso");
        iso.createDirectory();
        writeTone(iso.getChildFile("t.wav"), 440.0f);
        iso.getChildFile("a.sfz").replaceWithText(
            "<control> set_cc10=127\n<global> gain_cc10=6\n<region> sample=t.wav key=60 pitch_keycenter=60\n");
        iso.getChildFile("b.sfz").replaceWithText(
            "<control> set_cc10=127\n<global> gain_cc10=6\n<region> sample=t.wav key=62 pitch_keycenter=62\n");
        iso.getChildFile("c.sfz").replaceWithText(
            "<global> gain_cc10=6\n<region> sample=t.wav key=64 pitch_keycenter=64\n");
        iso.getChildFile("top.sfz").replaceWithText(
            "<control> set_cc10=127\n#include \"a.sfz\"\n#include \"b.sfz\"\n#include \"c.sfz\"\n");
        juce::String err;
        bool ok = proc.loadSoundFile(iso.getChildFile("top.sfz").getFullPathName(), err);
        check(ok, "isolation fixture loads");
        auto susRms = [&](int note)
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < 12; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            float r = bufferRMS(bufB);
            proc.panic();
            return r;
        };
        float rA = susRms(60), rB = susRms(62), rC = susRms(64);
        juce::Logger::writeToLog("iso rA=" + juce::String(rA) + " rB=" + juce::String(rB)
            + " rC(no set_cc)=" + juce::String(rC));
        check(rA > 0.001f && std::abs(rB / juce::jmax(rA, 1e-6f) - 1.0f) < 0.15f,
              "no cross-file gain_cc accumulation");
        check(rC > 0.001f && std::abs(rC / juce::jmax(rA, 1e-6f) - 1.0f) < 0.15f,
              "set_cc is global: top-file init voices files without set_cc");
        {
            juce::String err2;
            proc.loadSoundFile(sfzFile.getFullPathName(), err2);
        }
    }

    // Metal-GTX shape: many #includes, parent-relative ..\Samples paths from
    // nested include dirs, heavy sample reuse. Must load fast (no per-region
    // disk walk) and share duplicate samples.
    {
        auto big = tmp.getChildFile("biglib");
        auto patches = big.getChildFile("Patches").getChildFile("Set");
        auto samples = big.getChildFile("Samples");
        samples.createDirectory();
        patches.createDirectory();
        writeTone(samples.getChildFile("s1.wav"), 220.0f);
        writeTone(samples.getChildFile("s2.wav"), 330.0f);
        juce::String top;
        const int nInc = 6, nReg = 60;
        for (int f = 0; f < nInc; ++f)
        {
            juce::String body = "<global>sw_lokey=e-1 sw_hikey=c8 sw_last=g0 group=1 off_by=2\n";
            for (int r = 0; r < nReg; ++r)
            {
                int key = 36 + (r % 24);
                juce::String smp = (r % 2 == 0) ? "..\\Samples\\s1.wav" : "..\\Samples\\s2.wav";
                body += "<region> sample=" + smp + " lokey=" + juce::String(key)
                      + " hikey=" + juce::String(key) + " pitch_keycenter=" + juce::String(key)
                      + " seq_length=2 seq_position=" + juce::String(1 + (r % 2)) + "\n";
            }
            patches.getChildFile("part" + juce::String(f) + ".sfz").replaceWithText(body);
            top += "#include \"Set/part" + juce::String(f) + ".sfz\"\n";
        }
        big.getChildFile("Patches").getChildFile("bank.sfz").replaceWithText(top);
        juce::uint32 t0 = juce::Time::getMillisecondCounter();
        juce::String err;
        bool ok = proc.loadSoundFile(
            big.getChildFile("Patches").getChildFile("bank.sfz").getFullPathName(), err);
        float secs = (juce::Time::getMillisecondCounter() - t0) / 1000.0f;
        juce::Logger::writeToLog("biglib load secs: " + juce::String(secs));
        check(ok, "multi-include parent-relative sfz loads");
        if (!ok) juce::Logger::writeToLog("biglib error: " + err);
        check(proc.currentProgramName() == juce::String(nInc * nReg) + " regions",
              "all multi-include regions found");
        check(secs < 20.0f, "large library loads without walk storm");
        float rr = 0;
        {
            // sfizz semantics: sw_last layers stay silent until selected —
            // press the g0 switch (MIDI 19) first, like a player would
            proc.auditionNoteOn(19, 0.8f);
            proc.auditionNoteOff(19);
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 40, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < 12; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            rr = bufferRMS(bufB); // sustain level, before note-off
            proc.panic();
        }
        check(rr > 0.001f, "multi-include library renders");
        proc.panic();
    }

    // extended opcodes: include/define/comments, seq, one_shot, release,
    // off_by choke, offset/end, UTF-16
    {
        auto ext = tmp.getChildFile("ext");
        auto drums = ext.getChildFile("Drums");
        drums.createDirectory();
        writeTone(drums.getChildFile("a.wav"), 220.0f);
        writeTone(drums.getChildFile("b.wav"), 440.0f);
        writeTone(ext.getChildFile("long.wav"), 330.0f);
        ext.getChildFile("ext_regions.sfz").replaceWithText(
            "/* block comment: <region> sample=ghost.wav key=60 must be ignored */\n"
            "<control> default_path=./\n"
            "<group> group=10\n"
            "<region> sample=$D/a.wav key=60 seq_length=2 seq_position=1\n"
            "<region> sample=$D/b.wav key=60 seq_length=2 seq_position=2\n"
            "<group> group=0\n"
            "<region> sample=long.wav key=61 loop_mode=one_shot\n"
            "<region> sample=long.wav key=62 trigger=release\n"
            "<region> sample=long.wav key=63 group=20\n"
            "<region> sample=a.wav key=64 group=21 off_by=20\n"
            "<region> sample=Drums/a.wav key=65 offset=11025 end=33075\n");
        ext.getChildFile("patch.sfz").replaceWithText(
            "#define $D Drums/\n"
            "#include \"ext_regions.sfz\"\n");
        juce::String err;
        bool ok = proc.loadSoundFile(ext.getChildFile("patch.sfz").getFullPathName(), err);
        check(ok, "include+define+comment sfz loads");
        if (!ok) juce::Logger::writeToLog("ext error: " + err);
        check(proc.currentProgramName() == "7 regions", "extended regions all found");

        auto corrNote = [&](int note, int blocks)
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            double c220 = 0, c440 = 0;
            int gi = 0;
            for (int i = 0; i < blocks; ++i)
            {
                bufB.clear();
                juce::MidiBuffer e;
                proc.processBlock(bufB, e);
                if (i >= 8)
                    for (int s = 0; s < 512; ++s, ++gi)
                    {
                        float r220 = std::sin(2.0f * (float) juce::MathConstants<double>::pi * 220.0f * gi / 44100.0f);
                        float r440 = std::sin(2.0f * (float) juce::MathConstants<double>::pi * 440.0f * gi / 44100.0f);
                        float m = 0.5f * (bufB.getSample(0, s) + bufB.getSample(1, s));
                        c220 += m * r220; c440 += m * r440;
                    }
            }
            juce::MidiBuffer offB;
            offB.addEvent(juce::MidiMessage::noteOff(1, note), 0);
            for (int i = 0; i < 40; ++i) { bufB.clear(); proc.processBlock(bufB, offB); offB.clear(); }
            return std::make_pair(std::abs(c220), std::abs(c440));
        };
        auto h1 = corrNote(60, 25);
        proc.panic();
        auto h2 = corrNote(60, 25);
        proc.panic();
        juce::Logger::writeToLog("seq1 220=" + juce::String(h1.first) + " 440=" + juce::String(h1.second));
        juce::Logger::writeToLog("seq2 220=" + juce::String(h2.first) + " 440=" + juce::String(h2.second));
        check(h1.first > 2.0 * h1.second && h2.second > 2.0 * h2.first,
              "round-robin alternates samples");

        float rmsOne = 0, rmsOneSustain = 0;
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 61, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < 5; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            rmsOneSustain = bufferRMS(bufB);
            juce::MidiBuffer offB;
            offB.addEvent(juce::MidiMessage::noteOff(1, 61), 0);
            for (int i = 0; i < 40; ++i) { bufB.clear(); proc.processBlock(bufB, offB); offB.clear(); }
            rmsOne = bufferRMS(bufB);
        }
        juce::Logger::writeToLog("oneshot sustain=" + juce::String(rmsOneSustain) + " after-off=" + juce::String(rmsOne));
        check(rmsOneSustain > 0.001f && rmsOne > 0.001f, "one_shot plays past note-off");
        proc.panic();

        float rmsHold = 0, rmsRel = 0;
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 62, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < 10; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            rmsHold = bufferRMS(bufB);
            juce::MidiBuffer offB;
            offB.addEvent(juce::MidiMessage::noteOff(1, 62), 0);
            for (int i = 0; i < 15; ++i) { bufB.clear(); proc.processBlock(bufB, offB); offB.clear(); }
            rmsRel = bufferRMS(bufB);
        }
        check(rmsHold < 0.0005f && rmsRel > 0.001f, "release trigger sounds on key-up only");
        proc.panic();

        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 63, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < 15; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            bool oneVoice = proc.activeVoiceCount() == 1;
            juce::MidiBuffer midi2;
            midi2.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 0);
            bufB.clear();
            proc.processBlock(bufB, midi2);
            for (int i = 0; i < 8; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            // off_mode default is normal: the choked voice is still audibly
            // releasing here (count 2), and goes inactive after its release
            int cNow = proc.activeVoiceCount();
            bool releasing = cNow == 2;
            for (int i = 0; i < 100; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            int cEnd = proc.activeVoiceCount();
            juce::Logger::writeToLog("offby counts: one=" + juce::String(oneVoice ? 1 : 0)
                + " now=" + juce::String(cNow) + " end=" + juce::String(cEnd));
            // end==0: choked voice finished its release and the 1 s killer
            // sample ended too — no stuck voices either way
            check(oneVoice && releasing && cEnd == 0,
                  "off_by chokes muted group via release");
        }
        proc.panic();

        float rmsOff = 0;
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 65, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < 12; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            rmsOff = bufferRMS(bufB);
            proc.panic();
        }
        check(rmsOff > 0.001f, "offset/end region renders");
        proc.panic();
    }

    // ---- ground truth: engine output vs source files ----
    {
        auto gt = tmp.getChildFile("gt");
        gt.createDirectory();
        writeTone(gt.getChildFile("a220.wav"), 220.0f);
        writeTone(gt.getChildFile("b440.wav"), 440.0f);
        // decaying tone (distinguishes forward vs reverse playback)
        {
            juce::AudioBuffer<float> tone(1, 44100);
            for (int i = 0; i < 44100; ++i)
                tone.setSample(0, i, 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / 44100.0)
                    * (1.0f - (float) i / 44100.0f));
            std::unique_ptr<juce::FileOutputStream> os(gt.getChildFile("dec.wav").createOutputStream());
            if (os != nullptr && os->openedOk())
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w(
                    wav.createWriterFor(os.release(), 44100.0, 1, 16, {}, 0));
                if (w != nullptr) w->writeFromAudioSampleBuffer(tone, 0, 44100);
            }
        }
        gt.getChildFile("id.sfz").replaceWithText(
            "<region> sample=a220.wav key=60 pitch_keycenter=60\n"
            "<region> sample=b440.wav key=61 pitch_keycenter=61\n");
        juce::String err;
        check(proc.loadSoundFile(gt.getChildFile("id.sfz").getFullPathName(), err), "gt identity loads");

        double rate = 0;
        auto srcA = readAudioFile(gt.getChildFile("a220.wav"), rate);
        auto srcB = readAudioFile(gt.getChildFile("b440.wav"), rate);
        check(std::abs(bufferPeak(srcA) - 0.5f) < 0.03f, "decode fidelity (16-bit peak 0.5)");
        auto corrNote = [&](int note, int nBlocks)
        {
            auto m = renderTake(proc, note, 0.9f, nBlocks);
            proc.panic();
            return m;
        };
        {
            auto m60 = corrNote(60, 25);
            auto m61 = corrNote(61, 25);
            const int skip = 3000, n = 8000;
            double cAA = normCorr(m60.data() + skip, srcA.getReadPointer(0) + skip, n);
            double cAB = normCorr(m60.data() + skip, srcB.getReadPointer(0) + skip, n);
            double cBB = normCorr(m61.data() + skip, srcB.getReadPointer(0) + skip, n);
            double cBA = normCorr(m61.data() + skip, srcA.getReadPointer(0) + skip, n);
            juce::Logger::writeToLog("gt corr AA=" + juce::String(cAA) + " AB=" + juce::String(cAB)
                + " BB=" + juce::String(cBB) + " BA=" + juce::String(cBA));
            check(cAA > 0.9 && cBB > 0.9, "rendered note matches its own sample");
            check(cAB < 0.4 && cBA < 0.4, "rendered note rejects the other sample");
        }
        // pitch accuracy: octave up from 220 Hz root (wide-range region)
        gt.getChildFile("pitch.sfz").replaceWithText(
            "<region> sample=a220.wav lokey=0 hikey=127 pitch_keycenter=60\n");
        check(proc.loadSoundFile(gt.getChildFile("pitch.sfz").getFullPathName(), err), "gt pitch loads");
        {
            auto take72 = renderTake(proc, 72, 0.9f, 25);
            proc.panic();
            float f = estimateFreq(take72.data() + 3000, (int) take72.size() - 4000, 44100.0);
            juce::Logger::writeToLog("gt octave freq=" + juce::String(f));
            check(std::abs(f - 440.0f) < 440.0f * 0.03f, "octave-up pitch accurate");
        }
        // gain + pan staging
        gt.getChildFile("mix.sfz").replaceWithText(
            "<region> sample=a220.wav key=60 pitch_keycenter=60 volume=0\n"
            "<region> sample=a220.wav key=61 pitch_keycenter=61 volume=-6\n"
            "<region> sample=a220.wav key=62 pitch_keycenter=62 pan=-50\n"
            "<region> sample=a220.wav key=63 pitch_keycenter=63 pan=50\n");
        check(proc.loadSoundFile(gt.getChildFile("mix.sfz").getFullPathName(), err), "gt mix loads");
        {
            auto m0 = renderHeld(proc, 60, 0.9f, 25); proc.panic();
            auto m6 = renderHeld(proc, 61, 0.9f, 25); proc.panic();
            double ratio = bufferRMS(m6) / juce::jmax(1e-6f, bufferRMS(m0));
            juce::Logger::writeToLog("gt -6dB ratio=" + juce::String(ratio));
            check(std::abs(ratio - 0.5) < 0.5 * 0.15, "volume=-6 halves level");
            auto mL = renderHeld(proc, 62, 0.9f, 25); proc.panic();
            auto mR = renderHeld(proc, 63, 0.9f, 25); proc.panic();
            double eL = 0, eR = 0, eL2 = 0, eR2 = 0;
            for (int i = 3000; i < mL.getNumSamples(); ++i)
            {
                eL += mL.getSample(0, i) * mL.getSample(0, i);
                eR += mL.getSample(1, i) * mL.getSample(1, i);
                eL2 += mR.getSample(0, i) * mR.getSample(0, i);
                eR2 += mR.getSample(1, i) * mR.getSample(1, i);
            }
            check(eR / juce::jmax(eL, 1e-9) < 0.05, "pan left kills right");
            check(eL2 / juce::jmax(eR2, 1e-9) < 0.05, "pan right kills left");
        }
        // loop sustain past sample end
        gt.getChildFile("loop.sfz").replaceWithText(
            "<region> sample=a220.wav key=60 pitch_keycenter=60 loop_mode=loop_continuous loop_start=22050 loop_end=44100\n");
        check(proc.loadSoundFile(gt.getChildFile("loop.sfz").getFullPathName(), err), "gt loop loads");
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            float early = 0, late = 0;
            for (int i = 0; i < 130; ++i) // ~1.5 s held, sample is 1 s
            {
                bufB.clear();
                juce::MidiBuffer m = (i == 0) ? midiB : juce::MidiBuffer();
                proc.processBlock(bufB, m);
                if (i == 40) early = bufferRMS(bufB);
                if (i == 129) late = bufferRMS(bufB);
            }
            proc.panic();
            juce::Logger::writeToLog("gt loop early=" + juce::String(early) + " late=" + juce::String(late));
            check(early > 0.001f && late > early * 0.5f, "loop sustains past sample end");
        }
        // reverse + delay onset + offset slice
        gt.getChildFile("misc.sfz").replaceWithText(
            "<region> sample=dec.wav key=60 pitch_keycenter=60 direction=reverse\n"
            "<region> sample=a220.wav key=61 pitch_keycenter=61 delay=0.1\n"
            "<region> sample=a220.wav key=62 pitch_keycenter=62 offset=11025 end=22050\n");
        check(proc.loadSoundFile(gt.getChildFile("misc.sfz").getFullPathName(), err), "gt misc loads");
        {
            double rrate = 0;
            auto srcDec = readAudioFile(gt.getChildFile("dec.wav"), rrate);
            auto m = corrNote(60, 25);
            const int skip = 3000, n = 8000, N = srcDec.getNumSamples();
            std::vector<float> rev((size_t) n), fwd((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                rev[(size_t) i] = srcDec.getSample(0, N - 1 - (skip + i));
                fwd[(size_t) i] = srcDec.getSample(0, skip + i);
            }
            double cRev = normCorr(m.data() + skip, rev.data(), n);
            double cFwd = normCorr(m.data() + skip, fwd.data(), n);
            juce::Logger::writeToLog("gt reverse corr=" + juce::String(cRev) + " fwd=" + juce::String(cFwd));
            check(cRev > 0.85 && cFwd < cRev, "reverse playback matches reversed source");
        }
        {
            juce::String errR;
            proc.loadSoundFile(gt.getChildFile("misc.sfz").getFullPathName(), errR); // hermetic
            proc.panic();
            juce::AudioBuffer<float> preB(2, 512);
            preB.clear();
            juce::MidiBuffer noMidi;
            proc.processBlock(preB, noMidi);
            float preRms = bufferRMS(preB);
            juce::Logger::writeToLog("gt delay pre-note silence rms=" + juce::String(preRms));
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 61, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            int onset = -1, gi = 0;
            float peak = 0;
            std::vector<float> all;
            for (int i = 0; i < 30; ++i)
            {
                bufB.clear();
                juce::MidiBuffer m = (i == 0) ? midiB : juce::MidiBuffer();
                proc.processBlock(bufB, m);
                for (int s = 0; s < 512; ++s, ++gi)
                {
                    float v = 0.5f * (bufB.getSample(0, s) + bufB.getSample(1, s));
                    all.push_back(v);
                    peak = juce::jmax(peak, std::abs(v));
                }
            }
            proc.panic();
            for (int i = 0; i < (int) all.size(); ++i)
                if (std::abs(all[(size_t) i]) > 0.05f * peak) { onset = i; break; }
            juce::Logger::writeToLog("gt delay onset=" + juce::String(onset));
            check(onset >= 3800 && onset <= 5200, "delay=0.1 offsets note start");
        }
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 62, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            int last = -1, gi = 0;
            for (int i = 0; i < 90; ++i)
            {
                bufB.clear();
                juce::MidiBuffer m = (i == 0) ? midiB : juce::MidiBuffer();
                proc.processBlock(bufB, m);
                for (int s = 0; s < 512; ++s, ++gi)
                    if (std::abs(0.5f * (bufB.getSample(0, s) + bufB.getSample(1, s))) > 0.02f) last = gi;
            }
            proc.panic();
            juce::Logger::writeToLog("gt slice end=" + juce::String(last));
            check(last >= 9000 && last <= 13000, "offset/end bounds playback slice");
        }
        {
            juce::String err2;
            proc.loadSoundFile(sfzFile.getFullPathName(), err2);
        }
    }

    // region-authored amplitude EG (ampeg_*) overrides the UI ADSR
    {
        auto eg = tmp.getChildFile("eg");
        eg.createDirectory();
        writeTone(eg.getChildFile("t.wav"), 440.0f);
        eg.getChildFile("e.sfz").replaceWithText(
            "<region> sample=t.wav key=60 pitch_keycenter=60"
            " ampeg_attack=0.01 ampeg_decay=0.1 ampeg_sustain=10 ampeg_release=0.05\n");
        juce::String err;
        check(proc.loadSoundFile(eg.getChildFile("e.sfz").getFullPathName(), err), "gt ampeg loads");
        std::vector<float> take = renderTake(proc, 60, 0.9f, 40);
        proc.panic();
        auto blockRms = [&](int b0, int b1)
        {
            double s = 0; int n = 0;
            for (int i = b0 * 512; i < (b1 + 1) * 512 && i < (int) take.size(); ++i)
            { s += take[(size_t) i] * take[(size_t) i]; ++n; }
            return n > 0 ? (float) std::sqrt(s / n) : 0.0f;
        };
        float r0 = blockRms(0, 1), rMid = blockRms(6, 7), rSus = blockRms(36, 39);
        juce::Logger::writeToLog("gt ampeg attack=" + juce::String(r0)
            + " decayed=" + juce::String(rMid) + " sustain=" + juce::String(rSus));
        check(rMid < r0 * 0.4f, "region decay follows ampeg_decay=0.1");
        check(rSus > r0 * 0.03f && rSus < r0 * 0.3f, "region sustain follows ampeg_sustain=10");
        {
            juce::String err2;
            proc.loadSoundFile(sfzFile.getFullPathName(), err2);
        }
    }

    // unstartable first match falls through: offset past EOF must not mute
    {
        auto fb = tmp.getChildFile("fallback");
        fb.createDirectory();
        writeTone(fb.getChildFile("s.wav"), 440.0f);
        fb.getChildFile("f.sfz").replaceWithText(
            "<region> sample=s.wav key=60 pitch_keycenter=60 offset=50000\n"
            "<region> sample=s.wav key=60 pitch_keycenter=60\n");
        juce::String err;
        check(proc.loadSoundFile(fb.getChildFile("f.sfz").getFullPathName(), err), "fallback loads");
        juce::MidiBuffer midiB;
        midiB.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        juce::AudioBuffer<float> bufB(2, 512);
        double sum = 0; int n = 0;
        for (int i = 0; i < 6; ++i) // first 6 blocks: offset blip or fallback tone
        {
            bufB.clear();
            juce::MidiBuffer m = (i == 0) ? midiB : juce::MidiBuffer();
            proc.processBlock(bufB, m);
            for (int s = 0; s < 512; ++s, ++n)
            {
                float v = 0.5f * (bufB.getSample(0, s) + bufB.getSample(1, s));
                sum += v * v;
            }
        }
        float rr = (float) std::sqrt(sum / juce::jmax(1, n));
        check(rr > 0.001f, "unstartable first match falls through to next");
        proc.panic();
        {
            juce::String err2;
            proc.loadSoundFile(sfzFile.getFullPathName(), err2);
        }
    }

    // humanization: amp_random spreads hit levels, offset_random still plays
    {
        auto hu = tmp.getChildFile("human");
        hu.createDirectory();
        writeTone(hu.getChildFile("t.wav"), 440.0f);
        hu.getChildFile("h.sfz").replaceWithText(
            "<region> sample=t.wav key=60 pitch_keycenter=60 amp_random=6 offset_random=4410\n");
        juce::String err;
        check(proc.loadSoundFile(hu.getChildFile("h.sfz").getFullPathName(), err), "human loads");
        float lo = 1e9f, hi = 0;
        for (int h = 0; h < 6; ++h)
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < 12; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            float r = bufferRMS(bufB);
            lo = juce::jmin(lo, r); hi = juce::jmax(hi, r);
            proc.panic();
        }
        juce::Logger::writeToLog("human spread=" + juce::String(lo) + ".." + juce::String(hi));
        check(hi > 0.001f && (hi - lo) / juce::jmax(hi, 1e-6f) > 0.05f, "amp_random humanizes hits");
        {
            juce::String err2;
            proc.loadSoundFile(sfzFile.getFullPathName(), err2);
        }
    }

    // per-region filter: cutoff LP darkens vs bypass, bypass unchanged
    {
        auto flt = tmp.getChildFile("filt");
        flt.createDirectory();
        writeTone(flt.getChildFile("t.wav"), 440.0f);
        flt.getChildFile("f.sfz").replaceWithText(
            "<region> sample=t.wav key=66\n"
            "<region> sample=t.wav key=67 cutoff=150\n"
            "<region> sample=t.wav key=68 cutoff=150 fil_veltrack=9600\n");
        juce::String err;
        bool ok = proc.loadSoundFile(flt.getChildFile("f.sfz").getFullPathName(), err);
        check(ok, "filter sfz loads");
        auto sustainRms = [&](int note)
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < 12; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            float r = bufferRMS(bufB);
            proc.panic();
            return r;
        };
        float open = sustainRms(66), dark = sustainRms(67), vel = sustainRms(68);
        juce::Logger::writeToLog("filter open=" + juce::String(open)
            + " dark=" + juce::String(dark) + " vel=" + juce::String(vel));
        check(open > 0.001f && dark < open * 0.3f, "region lowpass darkens tone");
        check(vel > dark, "velocity opens region filter");
        {
            juce::String err2;
            proc.loadSoundFile(sfzFile.getFullPathName(), err2);
        }
    }

    // UTF-16 encoded sfz
    {
        juce::MemoryBlock mb;
        const char bom[] = { (char) 0xFF, (char) 0xFE };
        mb.append(bom, 2);
        juce::String body = "<region> sample=tone.wav key=70\n";
        for (int i = 0; i < body.length(); ++i)
        {
            char lo = (char) body[i];
            const char pair[] = { lo, 0 };
            mb.append(pair, 2);
        }
        auto u16 = tmp.getChildFile("utf16.sfz");
        juce::FileOutputStream os(u16);
        if (os.openedOk()) os.write(mb.getData(), mb.getSize());
        os.flush();
        juce::String err;
        bool ok = proc.loadSoundFile(u16.getFullPathName(), err);
        check(ok, "utf-16 sfz loads");
        float rr = 0;
        renderNote(70, 12, rr);
        check(rr > 0.001f, "utf-16 sfz renders");
        proc.panic();
    }

    // sw_last articulations: discrete switch keys select, playing keys don't
    {
        auto swd = tmp.getChildFile("swlast");
        swd.createDirectory();
        writeTone(swd.getChildFile("artA.wav"), 220.0f);
        writeTone(swd.getChildFile("artB.wav"), 330.0f);
        swd.getChildFile("art.sfz").replaceWithText(
            "<group> sw_last=20 sw_label=Pick\n"
            "<region> sample=artA.wav key=50\n"
            "<group> sw_last=21 sw_label=Bow\n"
            "<region> sample=artB.wav key=50\n");
        juce::String err;
        bool ok = proc.loadSoundFile(swd.getChildFile("art.sfz").getFullPathName(), err);
        check(ok, "sw_last sfz loads");
        auto sw = proc.getSwitchRanges();
        bool has20 = false, has21 = false;
        for (auto& r : sw)
        {
            if (r.first <= 20 && r.second >= 20) has20 = true;
            if (r.first <= 21 && r.second >= 21) has21 = true;
        }
        check(has20 && has21, "sw_last keys reported as switches");
        check(proc.getSwitchLabel(20) == "Pick" && proc.getSwitchLabel(21) == "Bow",
              "sw_label names reported");
        auto corrAt = [&](int note)
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            double c220 = 0, c330 = 0;
            int gi = 0;
            for (int i = 0; i < 20; ++i)
            {
                bufB.clear();
                juce::MidiBuffer e;
                proc.processBlock(bufB, e);
                if (i >= 8)
                    for (int s = 0; s < 512; ++s, ++gi)
                    {
                        float r220 = std::sin(2.0f * (float) juce::MathConstants<double>::pi * 220.0f * gi / 44100.0f);
                        float r330 = std::sin(2.0f * (float) juce::MathConstants<double>::pi * 330.0f * gi / 44100.0f);
                        float m = 0.5f * (bufB.getSample(0, s) + bufB.getSample(1, s));
                        c220 += m * r220; c330 += m * r330;
                    }
            }
            proc.panic();
            return std::make_pair(std::abs(c220), std::abs(c330));
        };
        auto hDef = corrAt(50); // fresh load, no sw_default: must be silent (sfizz)
        proc.auditionNoteOn(20, 0.8f);
        proc.auditionNoteOff(20);
        check(proc.getLastKeyswitch() == 20, "switch press selects articulation");
        auto hA = corrAt(50);
        check(hDef.first + hDef.second < 0.05 * (hA.first + hA.second),
              "fresh bank silent until first switch (sfizz)");
        check(hA.first > 2.0 * hA.second, "selected articulation sounds after switch");
        proc.auditionNoteOn(21, 0.8f);
        proc.auditionNoteOff(21);
        auto hB = corrAt(50);
        check(hB.second > 2.0 * hB.first, "other articulation sounds after re-switch");
        // playing keys must not reselect: still articulation B afterwards
        auto hB2 = corrAt(50);
        check(hB2.second > 2.0 * hB2.first, "playing keys don't reselect articulation");
        proc.panic();
    }

    // off_by honors off_mode: normal = full release stage (smooth legato
    // mute), fast = quick fade. The 8 ms-always choke cut legato lines.
    {
        auto off = tmp.getChildFile("offby");
        off.createDirectory();
        writeTone(off.getChildFile("sus.wav"), 220.0f);
        writeTone(off.getChildFile("kill.wav"), 330.0f);
        off.getChildFile("o.sfz").replaceWithText(
            "<group> group=1 ampeg_release=0.5\n"
            "<region> sample=sus.wav key=60 pitch_keycenter=60\n"
            "<group> group=9 off_by=1\n"
            "<region> sample=kill.wav key=61 pitch_keycenter=61\n");
        juce::String err;
        check(proc.loadSoundFile(off.getChildFile("o.sfz").getFullPathName(), err), "off_by fixture loads");
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < 20; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            float preKill = bufferRMS(bufB);
            // Goertzel energy at the sustain pitch (220 Hz) — blind to the
            // 330 Hz killer, so this truly measures the choked voice
            auto goertzel220 = [&](juce::AudioBuffer<float>& bb)
            {
                double re = 0, im = 0;
                for (int s = 0; s < bb.getNumSamples(); ++s)
                {
                    float v = 0.5f * (bb.getSample(0, s) + bb.getSample(1, s));
                    double ph = 2.0 * juce::MathConstants<double>::pi * 220.0 * s / 44100.0;
                    re += v * std::cos(ph); im += v * std::sin(ph);
                }
                return (float) std::sqrt(re * re + im * im) / bb.getNumSamples();
            };
            float pre220 = goertzel220(bufB);
            juce::MidiBuffer midi2;
            midi2.addEvent(juce::MidiMessage::noteOn(1, 61, 0.9f), 0);
            bufB.clear();
            proc.processBlock(bufB, midi2); // killer starts, sustain -> release stage
            // ~0.15 s later the 0.5 s-release voice must still be clearly audible
            // (with the old 8 ms choke it would be long gone)
            juce::AudioBuffer<float> b2(2, 512);
            float post220 = 0;
            for (int i = 0; i < 13; ++i)
            {
                b2.clear();
                juce::MidiBuffer e;
                proc.processBlock(b2, e);
                if (i == 12) post220 = goertzel220(b2);
            }
            juce::Logger::writeToLog("offby pre=" + juce::String(preKill)
                + " 220pre=" + juce::String(pre220) + " 220post=" + juce::String(post220));
            check(preKill > 0.001f && post220 > pre220 * 0.2f, "off_by normal releases smoothly");
            proc.panic();
        }
        // off_mode=fast control: gone within 50 ms
        off.getChildFile("f.sfz").replaceWithText(
            "<group> group=1 ampeg_release=0.5 off_mode=fast\n"
            "<region> sample=sus.wav key=60 pitch_keycenter=60\n"
            "<group> group=9 off_by=1\n"
            "<region> sample=kill.wav key=61 pitch_keycenter=61\n");
        {
            juce::String err2;
            proc.loadSoundFile(off.getChildFile("f.sfz").getFullPathName(), err2);
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            for (int i = 0; i < 20; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            juce::MidiBuffer midi2;
            midi2.addEvent(juce::MidiMessage::noteOn(1, 61, 0.9f), 0);
            bufB.clear();
            proc.processBlock(bufB, midi2);
            for (int i = 0; i < 5; ++i) { bufB.clear(); juce::MidiBuffer e; proc.processBlock(bufB, e); }
            float tail = bufferRMS(bufB);
            proc.panic();
            juce::Logger::writeToLog("offby fast tail=" + juce::String(tail));
            // killer still rings (its own 330 Hz tone); just require no explosion
            check(std::isfinite(tail) && tail < 2.0f, "off_by fast stays bounded");
        }
        {
            juce::String err3;
            proc.loadSoundFile(sfzFile.getFullPathName(), err3);
        }
    }

    // release choke is note-scoped: releasing A must fade A's sustain via
    // its release tail but leave legato-held B ringing untouched
    {
        auto rel = tmp.getChildFile("relscope");
        rel.createDirectory();
        writeTone(rel.getChildFile("a.wav"), 261.0f);
        writeTone(rel.getChildFile("b.wav"), 659.0f);
        writeTone(rel.getChildFile("tail.wav"), 110.0f);
        rel.getChildFile("r.sfz").replaceWithText(
            "<group> group=1 ampeg_release=0.4\n"
            "<region> sample=a.wav key=60 pitch_keycenter=60\n"
            "<region> sample=b.wav key=76 pitch_keycenter=76\n"
            "<group> group=3 off_by=1\n"
            "<region> sample=tail.wav key=60 pitch_keycenter=60 trigger=release\n");
        juce::String err;
        check(proc.loadSoundFile(rel.getChildFile("r.sfz").getFullPathName(), err), "relscope loads");
        auto goertzelAt = [&](const juce::AudioBuffer<float>& bb, float freq)
        {
            double re = 0, im = 0;
            for (int s = 0; s < bb.getNumSamples(); ++s)
            {
                float v = 0.5f * (bb.getSample(0, s) + bb.getSample(1, s));
                double ph = 2.0 * juce::MathConstants<double>::pi * freq * s / 44100.0;
                re += v * std::cos(ph); im += v * std::sin(ph);
            }
            return (float) std::sqrt(re * re + im * im) / bb.getNumSamples();
        };
        auto renderWindow = [&]()
        {
            juce::AudioBuffer<float> acc(2, 2048);
            acc.clear();
            for (int i = 0; i < 4; ++i)
            {
                juce::AudioBuffer<float> bb(2, 512);
                bb.clear();
                juce::MidiBuffer e;
                proc.processBlock(bb, e);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < 512; ++s)
                        acc.setSample(ch, i * 512 + s, bb.getSample(ch, s));
            }
            return acc;
        };
        juce::MidiBuffer onA, onB, offA;
        onA.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        onB.addEvent(juce::MidiMessage::noteOn(1, 76, 0.9f), 0);
        offA.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        juce::AudioBuffer<float> bb(2, 512);
        bb.clear();
        proc.processBlock(bb, onA);
        for (int i = 0; i < 20; ++i) { bb.clear(); juce::MidiBuffer e; proc.processBlock(bb, e); }
        proc.processBlock(bb, onB);
        for (int i = 0; i < 10; ++i) { bb.clear(); juce::MidiBuffer e; proc.processBlock(bb, e); }
        float bHeld = goertzelAt(renderWindow(), 659.0f);
        bb.clear();
        proc.processBlock(bb, offA); // A released: tail fires, must spare B
        for (int i = 0; i < 10; ++i) { bb.clear(); juce::MidiBuffer e; proc.processBlock(bb, e); }
        float bAfter = goertzelAt(renderWindow(), 659.0f);
        juce::Logger::writeToLog("relscope Bheld=" + juce::String(bHeld) + " Bafter=" + juce::String(bAfter));
        check(bHeld > 1e-4f && bAfter > bHeld * 0.7f, "release tail spares other held notes");
        proc.panic();
        {
            juce::String err2;
            proc.loadSoundFile(sfzFile.getFullPathName(), err2);
        }
    }

    // voice capacity: 40 overlapping one_shot notes must all survive (no steal)
    {
        auto cap = tmp.getChildFile("cap");
        cap.createDirectory();
        writeTone(cap.getChildFile("t.wav"), 220.0f);
        cap.getChildFile("c.sfz").replaceWithText(
            "<region> sample=t.wav lokey=0 hikey=127 pitch_keycenter=60 loop_mode=one_shot\n");
        juce::String err;
        proc.loadSoundFile(cap.getChildFile("c.sfz").getFullPathName(), err);
        juce::MidiBuffer midiB;
        for (int n = 0; n < 40; ++n)
            midiB.addEvent(juce::MidiMessage::noteOn(1, 30 + (n % 40), 0.9f), n * 10);
        juce::AudioBuffer<float> bufB(2, 512);
        for (int i = 0; i < 30; ++i) { bufB.clear(); juce::MidiBuffer m = (i == 0) ? midiB : juce::MidiBuffer(); proc.processBlock(bufB, m); }
        int nv = proc.activeVoiceCount();
        juce::Logger::writeToLog("cap voices=" + juce::String(nv));
        check(nv == 40, "40 overlapping one_shots all survive");
        float r = bufferRMS(bufB);
        check(std::isfinite(r), "massed voices stay finite");
        proc.panic();
        {
            juce::String err2;
            proc.loadSoundFile(sfzFile.getFullPathName(), err2);
        }
    }

    // CC-gated sub-layers (sfizz strategy): loccN/hiccN gates evaluate per
    // trigger against live CC state. Same-switch variants with disjoint
    // ranges must never stack (Metal GTX Slide_In_2ST..8ST on CC27).
    {
        auto ccg = tmp.getChildFile("ccg");
        ccg.createDirectory();
        writeTone(ccg.getChildFile("a.wav"), 220.0f);
        writeTone(ccg.getChildFile("b.wav"), 330.0f);
        ccg.getChildFile("c.sfz").replaceWithText(
            "<control> set_cc27=0\n"
            "<group> sw_last=20 sw_label=Low locc27=0 hicc27=15\n"
            "<region> sample=a.wav key=50 pitch_keycenter=50\n"
            "<group> sw_last=20 sw_label=Low locc27=80 hicc27=95\n"
            "<region> sample=b.wav key=50 pitch_keycenter=50\n");
        juce::String err;
        check(proc.loadSoundFile(ccg.getChildFile("c.sfz").getFullPathName(), err), "ccg loads");
        auto vf = [&]()
        {
            for (int v = 0; v < 64; ++v)
            {
                auto p = proc.getVoiceSamplePath(v);
                if (p.isNotEmpty()) return juce::File(p).getFileName();
            }
            return juce::String("(none)");
        };
        auto hit = [&](int note)
        {
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
            juce::AudioBuffer<float> b(2, 512);
            b.clear();
            proc.processBlock(b, m);
            for (int i = 0; i < 4; ++i) { b.clear(); juce::MidiBuffer e; proc.processBlock(b, e); }
        };
        proc.auditionNoteOn(20, 0.8f);
        proc.auditionNoteOff(20);
        hit(50);
        juce::String fLo = vf();
        proc.panic();
        check(fLo.contains("a.wav"), "CC default selects the in-range variant only");
        proc.setCC(27, 82);
        hit(50);
        juce::String fHi = vf();
        proc.panic();
        check(fHi.contains("b.wav"), "live CC switches to the other variant");
        proc.setCC(27, 40);
        hit(50);
        juce::String fMid = vf();
        proc.panic();
        check(fMid == "(none)", "out-of-range CC silences all variants (no stacking)");
        proc.panic();
    }

    // gain_cc repeats must not stack: the same target+CC restated in every
    // <global> block (Metal GTX restates gain_cc30=50 eight times) keeps ONE
    // depth (sfizz: same connection overwritten). Summing repeats voice-
    // stacked the noise layers into +195 dB full-scale harshness.
    {
        auto rep = tmp.getChildFile("rep");
        rep.createDirectory();
        writeTone(rep.getChildFile("t.wav"), 440.0f);
        rep.getChildFile("r.sfz").replaceWithText(
            "<control> set_cc10=127\n<global> gain_cc10=6\n<global> gain_cc10=6\n<global> gain_cc10=6\n"
            "<region> sample=t.wav key=60 pitch_keycenter=60\n");
        juce::String err;
        check(proc.loadSoundFile(rep.getChildFile("r.sfz").getFullPathName(), err), "rep fixture loads");
        float rRep = 0; renderNote(60, 12, rRep);
        juce::Logger::writeToLog("rep sustain rms=" + juce::String(rRep));
        // one 6 dB application sustains ~0.25 post-master; 18 dB would limit ~0.9
        check(rRep > 0.1f && rRep < 0.5f, "repeated gain_cc folds once (no dB stacking)");
        proc.panic();
    }

    // DSP bypass defaults: ADSR/Filter/LFO/FX all off, master -6 dB.
    // The soundfont plays as authored until a toggle is engaged.
    {
        auto getParam = [&](const char* id) -> float
        {
            if (auto* v = proc.apvts.getRawParameterValue(id)) return v->load();
            return -1.0f;
        };
        auto setP = [&](const char* id, float norm)
        {
            if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(norm);
        };
        check(getParam(PP::ADSRON) == 0.0f, "ADSR off by default");
        check(getParam(PP::ATK) == 0.001f && getParam(PP::DEC) == 4.0f
            && getParam(PP::SUS) == 1.0f && getParam(PP::REL) == 0.01f,
            "ADSR defaults neutral (fast atk, full dec/sus, tiny rel)");
        check(getParam(PP::FTON) == 0.0f, "filter off by default");
        check(getParam(PP::LFOON) == 0.0f, "LFO off by default");
        check(getParam(PP::CHON) == 0.0f && getParam(PP::PHON) == 0.0f
            && getParam(PP::FLON) == 0.0f && getParam(PP::DION) == 0.0f
            && getParam(PP::SAON) == 0.0f && getParam(PP::RVON) == 0.0f
            && getParam(PP::DLON) == 0.0f, "FX off by default");
        check(std::abs(getParam(PP::VOLUME) - 0.5f) < 0.001f, "master -6 dB by default");
        auto dryd = tmp.getChildFile("dry");
        dryd.createDirectory();
        writeTone(dryd.getChildFile("t.wav"), 440.0f);
        dryd.getChildFile("d.sfz").replaceWithText(
            "<region> sample=t.wav key=60 pitch_keycenter=60\n");
        juce::String err;
        check(proc.loadSoundFile(dryd.getChildFile("d.sfz").getFullPathName(), err), "dry fixture loads");
        float rDry = 0; renderNote(60, 12, rDry);
        check(rDry > 0.05f, "dry path renders full level");
        setP(PP::FTON, 1.0f); setP(PP::FCUT, 0.0f); // cutoff to 40 Hz minimum
        float rDark = 0; renderNote(60, 12, rDark);
        check(rDark < rDry * 0.3f, "filter toggle engages the filter");
        setP(PP::FTON, 0.0f); setP(PP::FCUT, 1.0f);
        float rDry2 = 0; renderNote(60, 12, rDry2);
        check(std::abs(rDry2 / juce::jmax(rDry, 1e-6f) - 1.0f) < 0.15f,
              "filter toggle restores the dry path");
        setP(PP::ADSRON, 1.0f); setP(PP::SUS, 0.0f); setP(PP::DEC, 0.0f); // fast decay to zero
        float rAdsr = 0; renderNote(60, 12, rAdsr);
        check(rAdsr < rDry * 0.3f, "ADSR toggle engages the UI envelope");
        // UI ADSR at neutral defaults must match the engine neutral gate:
        // no systematic gain jump from the toggle itself on plain regions.
        setP(PP::SUS, 1.0f); setP(PP::DEC, 1.0f);
        setP(PP::ATK, 0.0f); setP(PP::REL, 0.0f);
        float rOn = 0; renderNote(60, 12, rOn);
        juce::Logger::writeToLog("adsr neutral on=" + juce::String(rOn) + " off=" + juce::String(rDry2));
        check(std::abs(rOn / juce::jmax(rDry2, 1e-6f) - 1.0f) < 0.2f,
              "UI ADSR at neutral matches gate (no toggle gain jump)");
        setP(PP::ADSRON, 0.0f); // restore defaults
        // governor: at neutral defaults the toggle must not move authored
        // voices (never boost); Sustain below the authored level trims it.
        dryd.getChildFile("e.sfz").replaceWithText(
            "<region> sample=t.wav key=60 pitch_keycenter=60 ampeg_attack=0.02 ampeg_sustain=10\n");
        check(proc.loadSoundFile(dryd.getChildFile("e.sfz").getFullPathName(), err), "eg fixture loads");
        float rEgOff = 0; renderNote(60, 12, rEgOff);
        setP(PP::ADSRON, 1.0f);
        float rEgOn = 0; renderNote(60, 12, rEgOn);
        juce::Logger::writeToLog("adsr authored off=" + juce::String(rEgOff) + " on=" + juce::String(rEgOn));
        check(std::abs(rEgOn / juce::jmax(rEgOff, 1e-6f) - 1.0f) < 0.05f,
              "ADSR toggle never boosts authored EGs");
        setP(PP::SUS, 0.05f); // below the authored 0.1: trims audibly
        float rEgTrim = 0; renderNote(60, 12, rEgTrim);
        check(rEgTrim < rEgOff * 0.7f && rEgTrim > rEgOff * 0.3f,
              "Sustain knob trims authored sustain down");
        setP(PP::ADSRON, 0.0f); setP(PP::SUS, 1.0f);
        proc.panic();
    }

    // articulation isolation: choke groups and round-robins are scoped per
    // articulation, so an active articulation behaves like its file alone
    {
        auto iso2 = tmp.getChildFile("iso2");
        iso2.createDirectory();
        writeTone(iso2.getChildFile("a1.wav"), 220.0f);
        writeTone(iso2.getChildFile("a2.wav"), 247.0f);
        writeTone(iso2.getChildFile("b1.wav"), 330.0f);
        writeTone(iso2.getChildFile("b2.wav"), 349.0f);
        writeTone(iso2.getChildFile("k.wav"), 440.0f);
        iso2.getChildFile("s.sfz").replaceWithText(
            "<group> sw_last=20 group=1 seq_length=2\n"
            "<region> sample=a1.wav key=50 pitch_keycenter=50 seq_position=1\n"
            "<region> sample=a2.wav key=50 pitch_keycenter=50 seq_position=2\n"
            "<group> sw_last=21 group=1 seq_length=2\n"
            "<region> sample=b1.wav key=50 pitch_keycenter=50 seq_position=1\n"
            "<region> sample=b2.wav key=50 pitch_keycenter=50 seq_position=2\n"
            "<group> sw_last=21 group=9 off_by=1\n"
            "<region> sample=k.wav key=51 pitch_keycenter=51\n");
        juce::String err;
        check(proc.loadSoundFile(iso2.getChildFile("s.sfz").getFullPathName(), err), "iso2 loads");
        auto voiceFile = [&]()
        {
            for (int v = 0; v < 64; ++v)
            {
                auto p = proc.getVoiceSamplePath(v);
                if (p.isNotEmpty()) return juce::File(p).getFileName();
            }
            return juce::String("(none)");
        };
        auto playOnce = [&](int note)
        {
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
            juce::AudioBuffer<float> b(2, 512);
            b.clear();
            proc.processBlock(b, m);
            for (int i = 0; i < 4; ++i) { b.clear(); juce::MidiBuffer e; proc.processBlock(b, e); }
        };
        proc.auditionNoteOn(20, 0.8f);
        proc.auditionNoteOff(20);
        playOnce(50); // A pos 1 -> a1
        juce::String fA1 = voiceFile();
        proc.panic();
        playOnce(50); // A pos 2 -> a2
        juce::String fA2 = voiceFile();
        proc.panic();
        proc.auditionNoteOn(21, 0.8f); // switch to B: B's RR must start at 1...
        proc.auditionNoteOff(21);
        playOnce(50); // ...not continue A's counter
        juce::String fB1 = voiceFile();
        juce::Logger::writeToLog("iso2 A=" + fA1 + "," + fA2 + " B=" + fB1);
        check(fA1.contains("a1") && fA2.contains("a2"), "articulation A round-robins");
        check(fB1.contains("b1"), "articulation B has an independent cycle");
        // cross-articulation choke isolation: ring A, then fire B's killer;
        // A must survive (same-articulation choking still verified by offby test)
        proc.panic();
        proc.auditionNoteOn(20, 0.8f);
        proc.auditionNoteOff(20);
        playOnce(50); // A voice rings (no panic: keep it ringing)
        proc.auditionNoteOn(21, 0.8f);
        proc.auditionNoteOff(21);
        playOnce(51); // B killer (group 9, off_by=1)
        int nv = proc.activeVoiceCount();
        juce::Logger::writeToLog("iso2 voices after cross killer=" + juce::String(nv));
        check(nv >= 2, "killer does not choke other articulations");
        proc.panic();
        {
            juce::String err2;
            proc.loadSoundFile(sfzFile.getFullPathName(), err2);
        }
    }

    // back to full-range fixture for filter/FX/arp tests
    {
        juce::String err;
        proc.loadSoundFile(sfzFile.getFullPathName(), err);
    }

    {
        proc.apvts.getParameter(PP::FTON)->setValueNotifyingHost(1.0f);
        proc.apvts.getParameter(PP::FCUT)->setValueNotifyingHost(0.0f);
        float rms = 0.0f; renderNote(60, 12, rms);
        juce::Logger::writeToLog("filtered rms: " + juce::String(rms));
        check(rms < 0.05f, "lowpass at 40Hz kills 440Hz tone");
        auto* p = proc.apvts.getParameter(PP::FCUT);
        p->setValueNotifyingHost(p->convertTo0to1(18000.0f));
        proc.apvts.getParameter(PP::FTON)->setValueNotifyingHost(0.0f);
    }

    const char* fxIds[] = { PP::CHON, PP::PHON, PP::FLON, PP::DION, PP::SAON, PP::RVON, PP::DLON };
    for (auto id : fxIds)
    {
        proc.apvts.getParameter(id)->setValueNotifyingHost(1.0f);
        float rms = 0.0f; renderNote(64, 30, rms);
        bool sane = std::isfinite(rms) && rms < 5.0f;
        check(sane, (juce::String("fx on stable: ") + id).toRawUTF8());
        // L/R balance per effect (modulation may wobble, allow wide tolerance)
        {
            juce::MidiBuffer midiB;
            midiB.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 0);
            juce::AudioBuffer<float> bufB(2, 512);
            bufB.clear();
            proc.processBlock(bufB, midiB);
            double sL = 0, sR = 0;
            for (int i = 0; i < 40; ++i)
            {
                bufB.clear();
                juce::MidiBuffer e;
                proc.processBlock(bufB, e);
                if (i >= 10)
                    for (int s = 0; s < 512; ++s)
                    {
                        sL += bufB.getSample(0, s) * bufB.getSample(0, s);
                        sR += bufB.getSample(1, s) * bufB.getSample(1, s);
                    }
            }
            float ratio = (float) std::sqrt(sL / juce::jmax(sR, 1e-12));
            juce::Logger::writeToLog(juce::String(id) + " L/R ratio: " + juce::String(ratio));
            check(ratio > 0.35f && ratio < 2.8f,
                  (juce::String("fx stereo balanced: ") + id).toRawUTF8());
            juce::MidiBuffer offB;
            offB.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
            for (int i = 0; i < 60; ++i) { bufB.clear(); proc.processBlock(bufB, offB); offB.clear(); }
        }
        proc.apvts.getParameter(id)->setValueNotifyingHost(0.0f);
    }

    {
        proc.apvts.getParameter(PP::ARPON)->setValueNotifyingHost(1.0f);
        // every division (incl. dotted + triplets) must produce audio, no crash
        bool divsOk = true;
        for (int div = 0; div < 11; ++div)
        {
            proc.apvts.getParameter(PP::ARPDIV)->setValueNotifyingHost((float) div / 10.0f);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 10);
            juce::AudioBuffer<float> buf(2, 512);
            float peak = 0.0f;
            for (int i = 0; i < 120; ++i)
            {
                buf.clear();
                juce::MidiBuffer m = (i == 0) ? midi : juce::MidiBuffer();
                proc.processBlock(buf, m);
                peak = juce::jmax(peak, buf.getMagnitude(0, 512));
            }
            if (!(peak > 0.001f)) divsOk = false;
            proc.panic();
        }
        check(divsOk, "arp produces audio on all 11 divisions");
        proc.apvts.getParameter(PP::ARPON)->setValueNotifyingHost(0.0f);
        proc.panic();
    }

    {
        // fresh instance starts empty (no last-patch autoload)
        PastelProcessor fresh;
        fresh.prepareToPlay(44100.0, 512);
        check(!fresh.isSoundLoaded(), "new instance starts empty");
    }

    {
        juce::MemoryBlock mb;
        proc.getStateInformation(mb);
        PastelProcessor proc2;
        proc2.prepareToPlay(44100.0, 512);
        proc2.setStateInformation(mb.getData(), (int) mb.getSize()); // async recall
        juce::uint32 t0 = juce::Time::getMillisecondCounter();
        while (!proc2.isSoundLoaded()
               && juce::Time::getMillisecondCounter() - t0 < 30000)
            juce::Thread::sleep(20); // completion needs no message loop (lock-safe swap)
        check(proc2.isSoundLoaded(), "state restores sound path");
    }

    // background load: returns immediately, old sound keeps working, swap lands
    {
        proc.requestLoad(sfzFile.getFullPathName());
        check(proc.isLoading(), "async load starts in background");
        juce::uint32 t0 = juce::Time::getMillisecondCounter();
        while (proc.isLoading() && juce::Time::getMillisecondCounter() - t0 < 30000)
            juce::Thread::sleep(20);
        check(!proc.isLoading() && proc.isSoundLoaded(), "async load completes via swap");
        float rr = 0.0f;
        renderNote(60, 12, rr);
        check(rr > 0.0005f, "sound plays after background swap");
        juce::String err;
        check(!proc.consumeLoadError(err), "no spurious load error");
    }

    // alternate rate/blocksize: engine must stay clean at 48 kHz / 128 frames
    {
        PastelProcessor p48;
        p48.prepareToPlay(48000.0, 128);
        juce::String err;
        p48.loadSoundFile(sfzFile.getFullPathName(), err);
        juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        float peak = 0;
        bool finite = true;
        for (int i = 0; i < 60; ++i)
        {
            juce::AudioBuffer<float> bb(2, 128);
            bb.clear();
            juce::MidiBuffer mm = (i == 0) ? m : juce::MidiBuffer();
            p48.processBlock(bb, mm);
            for (int s = 0; s < 128; ++s)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float v = bb.getSample(ch, s);
                    if (!std::isfinite(v)) finite = false;
                    peak = juce::jmax(peak, std::abs(v));
                }
        }
        juce::Logger::writeToLog("rate48 peak=" + juce::String(peak));
        check(finite && peak < 1.1f && peak > 0.001f, "clean at 48kHz/128 frames");
        p48.panic();
    }

    // shortcuts browser lists fixture dir contents
    {
        proc.library.addShortcut(realDir.getFullPathName());
        proc.library.setCurrentDir(realDir.getFullPathName());
        auto entries = proc.library.listEntries("");
        bool hasKit = false, hasSamplesDir = false;
        for (auto& e : entries)
        {
            if (!e.isDir && e.path.contains("kit.sfz")) hasKit = true;
            if (e.isDir && e.name == "Samples") hasSamplesDir = true;
        }
        check(hasKit && hasSamplesDir, "browser lists files + subfolders");
        auto sub = proc.library.listEntries("kit");
        check(sub.size() == 1 && sub[0].path.contains("kit.sfz"), "browser search filters");
    }

    // Real-bank ground truth: PASTEL_GT_BANK=/path/to/patch.sfz (defaults to
    // the Metal GTX Lite bank when present, skipped otherwise). Verifies the
    // engine output against the actual sample file behind each voice.
    {
        juce::String gtBank = juce::String::fromUTF8(std::getenv("PASTEL_GT_BANK") != nullptr
            ? std::getenv("PASTEL_GT_BANK")
            : "/mnt/Data/Audio Resources/Decent Sampler and SFZ/SFZ/[Guitar - Electric] - UI_METAL-GTX/Programs/02-METAL-GTX Lite.sfz");
        if (juce::File(gtBank).existsAsFile())
        {
            PastelProcessor gtp;
            gtp.prepareToPlay(44100.0, 512);
            juce::String err;
            bool ok = gtp.loadSoundFile(gtBank, err);
            check(ok, "gt bank loads");
            if (ok)
            {
                std::vector<float> takeL, takeR;
                auto playNote = [&](int note, int blocks, std::vector<float>& takeOut,
                                    juce::StringArray& filesOut, float& rmsOut)
                {
                    juce::MidiBuffer midiB;
                    midiB.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
                    juce::AudioBuffer<float> bufB(2, 512);
                    takeOut.clear(); takeL.clear(); takeR.clear();
                    for (int i = 0; i < blocks; ++i)
                    {
                        bufB.clear();
                        juce::MidiBuffer m = (i == 0) ? midiB : juce::MidiBuffer();
                        gtp.processBlock(bufB, m);
                        for (int s = 0; s < 512; ++s)
                        {
                            takeL.push_back(bufB.getSample(0, s));
                            takeR.push_back(bufB.getSample(1, s));
                            takeOut.push_back(0.5f * (bufB.getSample(0, s) + bufB.getSample(1, s)));
                        }
                    }
                    filesOut.clear();
                    for (int v = 0; v < 64; ++v)
                    {
                        auto p = gtp.getVoiceSamplePath(v);
                        if (p.isNotEmpty() && !filesOut.contains(p)) filesOut.add(p);
                    }
                    double sum = 0;
                    for (auto x : takeOut) sum += x * x;
                    rmsOut = takeOut.empty() ? 0.0f : (float) std::sqrt(sum / takeOut.size());
                };
                // select Slide_In (sw_last d#1 = 27); articulations owning note 32
                // here are {27, 22, 21, 20} (verified layer map)
                std::vector<float> take;
                juce::StringArray files;
                float rms = 0;
                auto voiceSwReqs = [&]()
                {
                    std::set<int> out;
                    for (int v = 0; v < 64; ++v)
                    {
                        int q = gtp.getVoiceSwReq(v);
                        if (q != -2) out.insert(q);
                    }
                    return out;
                };
                gtp.auditionNoteOn(27, 0.8f);
                gtp.auditionNoteOff(27);
                playNote(32, 25, take, files, rms);
                auto reqs27 = voiceSwReqs();
                juce::Logger::writeToLog("gt bank art27 layers=" + juce::String(files.size())
                    + " rms=" + juce::String(rms));
                for (auto& f : files)
                    juce::Logger::writeToLog("gt bank layer file=" + juce::File(f).getFileName());
                check(!files.isEmpty(), "articulation plays its layers");
                check(reqs27.size() == 1 && *reqs27.begin() == 27,
                      "all layers belong to the selected articulation");
                check(rms > 1e-4f, "articulation layer is audible");
                {
                    bool allExist = true;
                    for (auto& f : files) allExist &= juce::File(f).existsAsFile();
                    check(allExist, "voice sample files exist on disk");
                }
                // every voiced file must decode to real audio (per-voice
                // content fidelity is proven by the synthetic 0.999 identity
                // test; a layered mix cannot correlate against one source)
                {
                    bool allOk = !files.isEmpty();
                    for (auto& f : files)
                    {
                        double rrate = 0;
                        auto src = readAudioFile(juce::File(f), rrate);
                        allOk &= src.getNumSamples() > 0 && bufferPeak(src) > 1e-4f;
                    }
                    gtp.panic();
                    check(allOk, "all layered voice files decode to audio");
                }
                gtp.panic();
                // switching articulation swaps the whole layer set
                gtp.auditionNoteOn(22, 0.8f);
                gtp.auditionNoteOff(22);
                playNote(32, 25, take, files, rms);
                auto reqs22 = voiceSwReqs();
                juce::Logger::writeToLog("gt bank art22 layers=" + juce::String(files.size()));
                check(!files.isEmpty() && reqs22.size() == 1 && *reqs22.begin() == 22,
                      "switched articulation owns all layers");
                gtp.panic();
                // playable sweep: probed notes sound, never cross articulations
                {
                    int audible = 0;
                    for (int note : { 30, 32, 36, 40, 45, 52 })
                    {
                        playNote(note, 15, take, files, rms);
                        auto rq = voiceSwReqs();
                        check(rq.empty() || (rq.size() == 1 && (*rq.begin() == 22 || *rq.begin() == -1)),
                              "no cross-articulation stacking on bank note");
                        if (rms > 1e-4f) ++audible;
                        gtp.panic();
                    }
                    juce::Logger::writeToLog("gt bank audible=" + juce::String(audible) + "/6");
                    check(audible >= 3, "bank playable across its range");
                }
                // chord: layers per note, still one articulation, bounded
                {
                    juce::MidiBuffer midiB;
                    midiB.addEvent(juce::MidiMessage::noteOn(1, 32, 0.9f), 0);
                    midiB.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 10);
                    midiB.addEvent(juce::MidiMessage::noteOn(1, 40, 0.9f), 20);
                    juce::AudioBuffer<float> bufB(2, 512);
                    for (int i = 0; i < 10; ++i)
                    {
                        bufB.clear();
                        juce::MidiBuffer m = (i == 0) ? midiB : juce::MidiBuffer();
                        gtp.processBlock(bufB, m);
                    }
                    int nv = gtp.activeVoiceCount();
                    std::set<int> rq;
                    for (int v = 0; v < 64; ++v)
                    {
                        int q = gtp.getVoiceSwReq(v);
                        if (q != -2) rq.insert(q);
                    }
                    juce::Logger::writeToLog("gt bank chord voices=" + juce::String(nv));
                    check(nv <= 12 && rq.size() <= 1, "chord layers stay in-articulation");
                    gtp.panic();
                }
                // legato phrase: overlapping notes + releases must stay finite
                // and bounded (off_by mutes release smoothly, nothing clips)
                {
                    juce::AudioBuffer<float> bufB(2, 512);
                    float peak = 0;
                    bool finite = true;
                    int ev = 0;
                    for (int b = 0; b < 120; ++b)
                    {
                        juce::MidiBuffer m;
                        if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 32, 0.9f), 0);
                        if (b == 20) m.addEvent(juce::MidiMessage::noteOn(1, 33, 0.9f), 0);
                        if (b == 40) m.addEvent(juce::MidiMessage::noteOff(1, 32), 0);
                        if (b == 45) m.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 0);
                        if (b == 70) m.addEvent(juce::MidiMessage::noteOff(1, 33), 0);
                        if (b == 75) m.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
                        bufB.clear();
                        gtp.processBlock(bufB, m);
                        for (int s = 0; s < 512; ++s, ++ev)
                            for (int ch = 0; ch < 2; ++ch)
                            {
                                float v = bufB.getSample(ch, s);
                                if (!std::isfinite(v)) finite = false;
                                peak = juce::jmax(peak, std::abs(v));
                            }
                    }
                    juce::Logger::writeToLog("gt bank legato peak=" + juce::String(peak));
                    check(finite && peak < 1.1f, "legato phrase stays finite and bounded");
                    gtp.panic();
                }
            }
        }
        else juce::Logger::writeToLog("gt bank skipped (no bank file)");
    }

    // Render-out for DAW/Sforzando A/B: PASTEL_RENDER="bank.sfz,note,vel,seconds,out.wav[,switch[,adsr]]"
    // e.g. PASTEL_RENDER="kit.sfz,36,100,2.0,/tmp/ref.wav,27". adsr=1 engages the UI ADSR. Not part of pass/fail.
    if (const char* renv = std::getenv("PASTEL_RENDER"))
    {
        juce::StringArray parts = juce::StringArray::fromTokens(renv, ",", "");
        if (parts.size() >= 5)
        {
            PastelProcessor rp;
            rp.prepareToPlay(44100.0, 512);
            juce::String err;
            if (rp.loadSoundFile(parts[0].trim(), err))
            {
                if (parts.size() >= 6)
                {
                    int sw = parts[5].trim().getIntValue();
                    rp.auditionNoteOn(sw, 0.8f);
                    rp.auditionNoteOff(sw);
                }
                if (parts.size() >= 7 && parts[6].trim().getIntValue() > 0)
                    if (auto* p = rp.apvts.getParameter(PP::ADSRON))
                        p->setValueNotifyingHost(1.0f);
                int note = parts[1].trim().getIntValue();
                float vel = parts[2].trim().getIntValue() / 127.0f;
                double secs = parts[3].trim().getDoubleValue();
                int total = (int)(secs * 44100.0);
                juce::AudioBuffer<float> out(2, total);
                juce::MidiBuffer midiB;
                midiB.addEvent(juce::MidiMessage::noteOn(1, note, vel), 0);
                int done = 0, bi = 0;
                bool loggedVoices = false;
                while (done < total)
                {
                    juce::AudioBuffer<float> blk(2, juce::jmin(512, total - done));
                    blk.clear();
                    juce::MidiBuffer m = (bi++ == 0) ? midiB : juce::MidiBuffer();
                    // render dry: bypass UI DSP by sending through processBlock
                    // with neutral DSP (defaults are neutral enough for A/B)
                    rp.processBlock(blk, m);
                    if (!loggedVoices)
                    {
                        loggedVoices = true;
                        juce::StringArray vf0;
                        for (int v = 0; v < 64; ++v)
                        {
                            auto p = rp.getVoiceSamplePath(v);
                            if (p.isNotEmpty() && !vf0.contains(p)) vf0.add(p);
                        }
                        for (auto& f : vf0)
                            juce::Logger::writeToLog("render onset file=" + juce::File(f).getFileName());
                        if (vf0.isEmpty())
                            juce::Logger::writeToLog("render onset: NO VOICE");
                    }
                    for (int ch = 0; ch < 2; ++ch)
                        out.copyFrom(ch, done, blk, ch, 0, blk.getNumSamples());
                    done += blk.getNumSamples();
                }
                std::unique_ptr<juce::FileOutputStream> os(juce::File(parts[4].trim()).createOutputStream());
                if (os != nullptr && os->openedOk())
                {
                    juce::WavAudioFormat wav;
                    std::unique_ptr<juce::AudioFormatWriter> w(
                        wav.createWriterFor(os.release(), 44100.0, 2, 24, {}, 0));
                    if (w != nullptr) w->writeFromAudioSampleBuffer(out, 0, total);
                }
                juce::Logger::writeToLog("rendered " + juce::String(total) + " samples to " + parts[4].trim());
                juce::StringArray vf;
                for (int v = 0; v < 64; ++v)
                {
                    auto p = rp.getVoiceSamplePath(v);
                    if (p.isNotEmpty() && !vf.contains(p)) vf.add(p);
                }
                for (auto& f : vf)
                    juce::Logger::writeToLog("render voice file=" + juce::File(f).getFileName());
            }
            else juce::Logger::writeToLog("render load failed: " + err);
        }
    }

    // Legato transition diagnostics: PASTEL_LEGATO2=bank.sfz — two overlapping
    // notes; tracks the first note's pitch energy across the second's onset
    // (choke cliff?) and across its own release. Info only.
    if (const char* leg2 = std::getenv("PASTEL_LEGATO2"))
    {
        PastelProcessor lp;
        lp.prepareToPlay(44100.0, 512);
        juce::String err;
        if (lp.loadSoundFile(juce::String::fromUTF8(leg2), err))
        {
            lp.auditionNoteOn(27, 0.8f);
            lp.auditionNoteOff(27);
            juce::AudioBuffer<float> bufB(2, 512);
            // 50-note legato line: count dead hits (no voice started) —
            // sparse round-robin positions under a shared counter cause these
            int dead = 0, played = 0;
            for (int i = 0; i < 50; ++i)
            {
                int note = 32 + (i * 5) % 24; // walk the range
                int before = lp.activeVoiceCount();
                juce::MidiBuffer m;
                m.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
                bufB.clear();
                lp.processBlock(bufB, m);
                // voice started? render 6 blocks and measure
                double sum = 0;
                for (int b = 0; b < 6; ++b)
                {
                    if (b > 0) { bufB.clear(); juce::MidiBuffer e; lp.processBlock(bufB, e); }
                    for (int s = 0; s < 512; ++s)
                    {
                        float v = 0.5f * (bufB.getSample(0, s) + bufB.getSample(1, s));
                        sum += v * v;
                    }
                }
                float rms = std::sqrt(sum / (6 * 512));
                if (rms < 1e-4f) ++dead;
                else ++played;
                juce::MidiBuffer off;
                off.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                bufB.clear();
                lp.processBlock(bufB, off);
                juce::ignoreUnused(before);
            }
            juce::Logger::writeToLog("LEG played=" + juce::String(played)
                + " dead=" + juce::String(dead));
        }
        else juce::Logger::writeToLog("legato load failed: " + err);
    }

    // A/B: PASTEL_AB=bank.sfz,switch,indiv.sfz — same legato phrase through
    // the bank (with articulation selected) and through the individual patch
    // file; logs per-note voice files + waveform similarity. Info only.
    if (const char* ab = std::getenv("PASTEL_AB"))
    {
        juce::StringArray parts = juce::StringArray::fromTokens(ab, ",", "");
        if (parts.size() >= 3)
        {
            // per-onset voice files: fresh processor per take for determinism
            auto onsetFiles = [&](const juce::String& path, int selSw)
            {
                juce::StringArray out;
                PastelProcessor p;
                p.prepareToPlay(44100.0, 512);
                juce::String err;
                p.loadSoundFile(path, err);
                if (selSw >= 0) { p.auditionNoteOn(selSw, 0.8f); p.auditionNoteOff(selSw); }
                int notes[3] = { 30, 32, 33 };
                for (int k = 0; k < 3; ++k)
                {
                    juce::MidiBuffer m;
                    m.addEvent(juce::MidiMessage::noteOn(1, notes[k], 0.9f), 0);
                    juce::AudioBuffer<float> bb(2, 512);
                    bb.clear();
                    p.processBlock(bb, m);
                    juce::String f;
                    for (int v = 0; v < 64; ++v)
                    {
                        auto q = p.getVoiceSamplePath(v);
                        if (q.isNotEmpty()) { f = juce::File(q).getFileName(); break; }
                    }
                    out.add(f.isNotEmpty() ? f : juce::String("(silent)"));
                    p.panic();
                }
                return out;
            };
            juce::StringArray bankFiles = onsetFiles(parts[0].trim(), parts[1].trim().getIntValue());
            juce::StringArray indivFiles = onsetFiles(parts[2].trim(), -1);
            int notes[3] = { 30, 32, 33 };
            for (int k = 0; k < 3; ++k)
                juce::Logger::writeToLog("AB note=" + juce::String(notes[k])
                    + " bank=" + bankFiles[k] + " indiv=" + indivFiles[k]);
        }
    }

    // Harshness sweep: PASTEL_SWEEP=bank.sfz — every switch key x a few notes,
    // asserting finite output and sane peaks. Info only (not pass/fail).
    // PASTEL_SWEEP2=bank.sfz — per (switch, low note) table with peak, crest
    // and HF-fraction (harshness indicators) vs a reference articulation.
    if (const char* swp = std::getenv("PASTEL_SWEEP"))
    {
        PastelProcessor sp;
        sp.prepareToPlay(44100.0, 512);
        juce::String err;
        if (sp.loadSoundFile(juce::String::fromUTF8(swp), err))
        {
            auto switches = sp.getSwitchRanges();
            std::vector<int> keys;
            for (auto& r : switches)
                for (int k = r.first; k <= r.second && (int) keys.size() < 64; ++k)
                    keys.push_back(k);
            juce::AudioBuffer<float> bufB(2, 512);
            for (int sw : keys)
            {
                sp.auditionNoteOn(sw, 0.8f);
                sp.auditionNoteOff(sw);
                for (int note : { 36, 48, 60 })
                {
                    juce::MidiBuffer m;
                    m.addEvent(juce::MidiMessage::noteOn(1, note, 100.0f / 127.0f), 0);
                    float peak = 0;
                    bool finite = true;
                    for (int i = 0; i < 15; ++i)
                    {
                        bufB.clear();
                        juce::MidiBuffer mm = (i == 0) ? m : juce::MidiBuffer();
                        sp.processBlock(bufB, mm);
                        for (int s = 0; s < 512; ++s)
                            for (int ch = 0; ch < 2; ++ch)
                            {
                                float v = bufB.getSample(ch, s);
                                if (!std::isfinite(v)) finite = false;
                                peak = juce::jmax(peak, std::abs(v));
                            }
                    }
                    if (!finite || peak >= 1.0f)
                        juce::Logger::writeToLog("SWEEP harsh sw=" + juce::String(sw)
                            + " note=" + juce::String(note) + " peak=" + juce::String(peak)
                            + " finite=" + juce::String(finite ? 1 : 0));
                    sp.panic();
                }
            }
            // hot path: full velocity + chords + machine-gun repeats on G-A#
            for (int sw : { 19, 20, 21, 22 })
            {
                sp.auditionNoteOn(sw, 1.0f);
                sp.auditionNoteOff(sw);
                juce::MidiBuffer m;
                m.addEvent(juce::MidiMessage::noteOn(1, 31, 1.0f), 0);
                m.addEvent(juce::MidiMessage::noteOn(1, 35, 1.0f), 100);
                m.addEvent(juce::MidiMessage::noteOn(1, 38, 1.0f), 200);
                float peak = 0, clip = 0, total = 0;
                bool finite = true;
                for (int i = 0; i < 60; ++i)
                {
                    bufB.clear();
                    juce::MidiBuffer mm = (i == 0) ? m : juce::MidiBuffer();
                    // machine-gun restrikes of 31 every ~0.14 s
                    if (i > 0 && i % 12 == 0)
                        mm.addEvent(juce::MidiMessage::noteOn(1, 31, 1.0f), 0);
                    if (i == 40)
                    {
                        mm.addEvent(juce::MidiMessage::noteOff(1, 31), 0);
                        mm.addEvent(juce::MidiMessage::noteOff(1, 35), 0);
                        mm.addEvent(juce::MidiMessage::noteOff(1, 38), 0);
                    }
                    sp.processBlock(bufB, mm);
                    for (int s = 0; s < 512; ++s)
                        for (int ch = 0; ch < 2; ++ch)
                        {
                            float v = bufB.getSample(ch, s);
                            ++total;
                            if (!std::isfinite(v)) finite = false;
                            peak = juce::jmax(peak, std::abs(v));
                            if (std::abs(v) >= 0.99f) ++clip;
                        }
                }
                juce::Logger::writeToLog("HOT sw=" + juce::String(sw) + " peak=" + juce::String(peak)
                    + " clipFrac=" + juce::String(clip / juce::jmax(1.0f, total))
                    + " finite=" + juce::String(finite ? 1 : 0));
                sp.panic();
            }
            juce::Logger::writeToLog("SWEEP done");
        }
        else juce::Logger::writeToLog("sweep load failed: " + err);
    }

    if (const char* swp2 = std::getenv("PASTEL_SWEEP2"))
    {
        PastelProcessor sp;
        sp.prepareToPlay(44100.0, 512);
        juce::String err;
        if (sp.loadSoundFile(juce::String::fromUTF8(swp2), err))
        {
            juce::AudioBuffer<float> bufB(2, 512);
            // NOTE: probe notes must not be switch keys themselves (28 = e1
            // reselects!); 32/36 are pure playing notes here
            for (int sw : { 27, 19 })
            {
                sp.auditionNoteOn(sw, 1.0f);
                sp.auditionNoteOff(sw);
                juce::Logger::writeToLog("SW2 pressed=" + juce::String(sw)
                    + " sel=" + juce::String(sp.getLastKeyswitch()));
                juce::Logger::writeToLog("SW2 after-press sel=" + juce::String(sp.getLastKeyswitch()));
                for (int note : { 32, 36, 40 })
                {
                    juce::MidiBuffer m;
                    m.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
                    bufB.clear();
                    sp.processBlock(bufB, m);
                    juce::StringArray vf;
                    for (int v = 0; v < 64; ++v)
                    {
                        auto p = sp.getVoiceSamplePath(v);
                        int q = sp.getVoiceSwReq(v);
                        if (p.isNotEmpty())
                            vf.add(juce::File(p).getFileName() + "(req=" + juce::String(q) + ")");
                    }
                    double peak = 0, sqSum = 0;
                    int n = 0;
                    for (int i = 0; i < 40; ++i)
                    {
                        if (i > 0) { bufB.clear(); juce::MidiBuffer e; sp.processBlock(bufB, e); }
                        for (int s = 0; s < 512; ++s, ++n)
                        {
                            float v = 0.5f * (bufB.getSample(0, s) + bufB.getSample(1, s));
                            peak = juce::jmax(peak, (double) std::abs(v));
                            sqSum += v * v;
                        }
                    }
                    double rms = std::sqrt(sqSum / juce::jmax(1, n));
                    juce::Logger::writeToLog("SW2 sw=" + juce::String(sw) + " n=" + juce::String(note)
                        + " peak=" + juce::String(peak, 4) + " rms=" + juce::String(rms, 5)
                        + " voices=" + vf.joinIntoString("+"));
                    sp.panic();
                }
            }
        }
        else juce::Logger::writeToLog("sweep2 load failed: " + err);
    }

    // Scope probe: PASTEL_SCOPE=file.sfz,switch,note[,ccNum,ccVal] — load one
    // file, press a switch, play a note; log voice files + their requirements.
    // Optional live CC override (sfizz MidiState path). Info only.
    if (const char* scp = std::getenv("PASTEL_SCOPE"))
    {
        juce::StringArray parts = juce::StringArray::fromTokens(scp, ",", "");
        if (parts.size() >= 3)
        {
            PastelProcessor sp;
            sp.prepareToPlay(44100.0, 512);
            juce::String err;
            if (sp.loadSoundFile(parts[0].trim(), err))
            {
                int sw = parts[1].trim().getIntValue();
                int note = parts[2].trim().getIntValue();
                if (parts.size() >= 5)
                    sp.setCC(parts[3].trim().getIntValue(), parts[4].trim().getIntValue());
                sp.auditionNoteOn(sw, 0.8f);
                sp.auditionNoteOff(sw);
                juce::MidiBuffer m;
                m.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
                juce::AudioBuffer<float> bb(2, 512);
                bb.clear();
                sp.processBlock(bb, m);
                juce::StringArray vf;
                for (int v = 0; v < 64; ++v)
                {
                    auto p = sp.getVoiceSamplePath(v);
                    int q = sp.getVoiceSwReq(v);
                    if (p.isNotEmpty())
                        vf.add(juce::File(p).getFileName() + "(req=" + juce::String(q) + ")");
                }
                juce::Logger::writeToLog("SCOPE sel=" + juce::String(sp.getLastKeyswitch())
                    + " cc27=" + juce::String(sp.getCC(27))
                    + " voices=" + vf.joinIntoString("+"));
                sp.panic();
            }
            else juce::Logger::writeToLog("scope load failed: " + err);
        }
    }

    // Whole-bank articulation audit: PASTEL_ARTBANK=bank.sfz — for every
    // switch key, select it and play several notes; log which articulations
    // (swReq keys + labels) actually voiced. FOREIGN = a different
    // articulation's layer sounded under this switch. Info only.
    if (const char* ab2 = std::getenv("PASTEL_ARTBANK"))
    {
        PastelProcessor sp;
        sp.prepareToPlay(44100.0, 512);
        juce::String err;
        if (sp.loadSoundFile(juce::String::fromUTF8(ab2).trim(), err))
        {
            std::vector<int> switches;
            for (auto& r : sp.getSwitchRanges())
                for (int k = r.first; k <= r.second; ++k) switches.push_back(k);
            const int notes[] = { 34, 40, 48, 55, 60, 64, 72 };
            for (int sw : switches)
            {
                sp.auditionNoteOn(sw, 0.8f);
                sp.auditionNoteOff(sw);
                std::map<int,int> reqCount;
                std::map<int, juce::String> reqFile;
                int plainN = 0;
                for (int note : notes)
                {
                    juce::MidiBuffer m;
                    m.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
                    juce::AudioBuffer<float> bb(2, 512);
                    bb.clear();
                    sp.processBlock(bb, m);
                    for (int v = 0; v < 64; ++v)
                    {
                        auto p = sp.getVoiceSamplePath(v);
                        if (p.isNotEmpty())
                        {
                            int q = sp.getVoiceSwReq(v);
                            reqCount[q]++;
                            reqFile[q] = juce::File(p).getFileName();
                        }
                    }
                    juce::MidiBuffer off;
                    off.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                    for (int i = 0; i < 10; ++i) { bb.clear(); sp.processBlock(bb, off); off.clear(); }
                    sp.panic();
                }
                juce::String desc;
                bool foreign = false;
                for (auto& rc : reqCount)
                {
                    juce::String lbl = sp.getSwitchLabel(rc.first);
                    desc += "req=" + juce::String(rc.first)
                        + (lbl.isNotEmpty() ? "(" + lbl + ")" : "(plain)")
                        + "x" + juce::String(rc.second)
                        + "[" + reqFile[rc.first] + "] ";
                    if (rc.first != sw && rc.first >= 0) foreign = true;
                    if (rc.first < 0) plainN += rc.second;
                }
                juce::Logger::writeToLog("ARTBANK sw=" + juce::String(sw)
                    + "(" + sp.getSwitchLabel(sw) + ")"
                    + (foreign ? " FOREIGN" : " clean")
                    + (plainN > 0 ? " PLAINx" + juce::String(plainN) : "")
                    + " :: " + desc);
            }
        }
        else juce::Logger::writeToLog("artbank load failed: " + err);
    }

    // 1:1 test: bank-with-selection must equal the individual patch file.
    // PASTEL_ONE2ONE="bank.sfz,switch,indiv.sfz,note" — same phrase through
    // both; asserts identical voice files in order + waveform match. The
    // individual Sus_Alt_D case also covers "cuts off after first note".
    if (const char* oo = std::getenv("PASTEL_ONE2ONE"))
    {
        juce::StringArray parts = juce::StringArray::fromTokens(oo, ",", "");
        if (parts.size() >= 4)
        {
            auto renderPhrase = [&](const juce::String& path, int selSw,
                                    std::vector<float>& takeOut, juce::StringArray& filesOut)
            {
                PastelProcessor p;
                p.prepareToPlay(44100.0, 512);
                juce::String err;
                takeOut.clear(); filesOut.clear();
                if (!p.loadSoundFile(path, err))
                {
                    juce::Logger::writeToLog("one2one load failed: " + err);
                    return;
                }
                if (selSw >= 0) { p.auditionNoteOn(selSw, 0.8f); p.auditionNoteOff(selSw); }
                int notes[4] = { 36, 36, 38, 36 };
                juce::AudioBuffer<float> bufB(2, 512);
                for (int k = 0; k < 4; ++k)
                {
                    juce::MidiBuffer m;
                    m.addEvent(juce::MidiMessage::noteOn(1, notes[k], 0.9f), 0);
                    for (int i = 0; i < 15; ++i)
                    {
                        bufB.clear();
                        juce::MidiBuffer mm = (i == 0) ? m : juce::MidiBuffer();
                        p.processBlock(bufB, mm);
                        for (int s = 0; s < 512; ++s)
                            takeOut.push_back(0.5f * (bufB.getSample(0, s) + bufB.getSample(1, s)));
                    }
                    for (int v = 0; v < 64; ++v)
                    {
                        auto q = p.getVoiceSamplePath(v);
                        if (q.isNotEmpty()) filesOut.add(juce::File(q).getFileName());
                    }
                    juce::MidiBuffer off;
                    off.addEvent(juce::MidiMessage::noteOff(1, notes[k]), 0);
                    bufB.clear();
                    p.processBlock(bufB, off);
                }
            };
            std::vector<float> takeBank, takeIndiv;
            juce::StringArray filesBank, filesIndiv;
            renderPhrase(parts[0].trim(), parts[1].trim().getIntValue(), takeBank, filesBank);
            renderPhrase(parts[2].trim(), -1, takeIndiv, filesIndiv);
            juce::Logger::writeToLog("one2one bank files=" + filesBank.joinIntoString("+"));
            juce::Logger::writeToLog("one2one indiv files=" + filesIndiv.joinIntoString("+"));
            size_t n = juce::jmin(takeBank.size(), takeIndiv.size());
            double corr = n > 0 ? normCorr(takeBank.data(), takeIndiv.data(), (int) n) : 0;
            juce::Logger::writeToLog("one2one waveform corr=" + juce::String(corr));
        }
    }

    // Cutoff probe: PASTEL_CUT=file.sfz — every note 24..96 at three
    // velocities, repeated 3x with releases; reports silent hits. Info only.
    if (const char* cut = std::getenv("PASTEL_CUT"))
    {
        PastelProcessor cp;
        cp.prepareToPlay(44100.0, 512);
        juce::String err;
        if (cp.loadSoundFile(juce::String::fromUTF8(cut), err))
        {
            juce::AudioBuffer<float> bufB(2, 512);
            int silent = 0, total = 0;
            juce::StringArray silentAt;
            for (int rep = 0; rep < 3; ++rep)
                for (int note = 24; note <= 96; ++note)
                    for (float vel : { 0.4f, 0.8f, 1.0f })
                    {
                        juce::MidiBuffer m;
                        m.addEvent(juce::MidiMessage::noteOn(1, note, vel), 0);
                        double sum = 0; int n = 0;
                        for (int i = 0; i < 10; ++i)
                        {
                            bufB.clear();
                            juce::MidiBuffer mm = (i == 0) ? m : juce::MidiBuffer();
                            cp.processBlock(bufB, mm);
                            for (int s = 0; s < 512; ++s, ++n)
                            {
                                float v = 0.5f * (bufB.getSample(0, s) + bufB.getSample(1, s));
                                sum += v * v;
                            }
                        }
                        float rms = std::sqrt(sum / juce::jmax(1, n));
                        ++total;
                        if (rms < 1e-4f)
                        {
                            ++silent;
                            if (silentAt.size() < 12)
                                silentAt.add("n=" + juce::String(note) + " v=" + juce::String(vel)
                                    + " rep=" + juce::String(rep));
                        }
                        juce::MidiBuffer off;
                        off.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                        bufB.clear();
                        cp.processBlock(bufB, off);
                    }
            juce::Logger::writeToLog("CUT silent=" + juce::String(silent) + "/" + juce::String(total));
            for (auto& s : silentAt) juce::Logger::writeToLog("CUT silent at " + s);
        }
        else juce::Logger::writeToLog("cut load failed: " + err);
    }

    // Manual big-library check: PASTEL_LOAD_TEST=/path/to/file.sfz
    // Loads, reports time/regions, renders one note. Not part of pass/fail.
    if (const char* lt = std::getenv("PASTEL_LOAD_TEST"))
    {
        PastelProcessor big;
        big.prepareToPlay(44100.0, 512);
        juce::uint32 t0 = juce::Time::getMillisecondCounter();
        big.requestLoad(juce::String::fromUTF8(lt));
        float returnMs = (float)(juce::Time::getMillisecondCounter() - t0);
        juce::Logger::writeToLog("LOADTEST async return ms=" + juce::String(returnMs));
        while (big.isLoading()
               && juce::Time::getMillisecondCounter() - t0 < 300000)
            juce::Thread::sleep(50);
        bool ok = big.isSoundLoaded();
        juce::String err;
        big.consumeLoadError(err);
        float secs = (juce::Time::getMillisecondCounter() - t0) / 1000.0f;
        juce::Logger::writeToLog("LOADTEST ok=" + juce::String(ok ? 1 : 0)
            + " secs=" + juce::String(secs) + " program=" + big.currentProgramName());
        if (!ok) juce::Logger::writeToLog("LOADTEST error: " + err);
        else
        {
            juce::String mr;
            for (auto& r : big.getMappedRanges())
                mr += juce::String(r.first) + "-" + juce::String(r.second) + " ";
            juce::String sr;
            for (auto& r : big.getSwitchRanges())
                sr += juce::String(r.first) + "-" + juce::String(r.second) + " ";
            juce::Logger::writeToLog("LOADTEST switches: " + sr.trim());
            juce::Logger::writeToLog("LOADTEST lastSwitch=" + juce::String(big.getLastKeyswitch()));
            juce::Logger::writeToLog("LOADTEST mapped: " + mr.trim());
            // select the Slide_In articulation (sw_last d#1 = 27) first:
            // 28 is itself a switch key and would narrow selection away
            big.auditionNoteOn(27, 0.8f);
            big.auditionNoteOff(27);
            bool allFinite = true;
            float worstPeak = 0;
            for (int note : { 30, 33, 40, 45, 52, 57, 64, 72, 80 })
            {
                juce::MidiBuffer midiB;
                midiB.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
                juce::AudioBuffer<float> bufB(2, 512);
                bufB.clear();
                big.processBlock(bufB, midiB);
                for (int i = 0; i < 12; ++i) { bufB.clear(); juce::MidiBuffer e; big.processBlock(bufB, e); }
                float r = bufferRMS(bufB);
                float pk = bufferPeak(bufB);
                for (int ch = 0; ch < bufB.getNumChannels(); ++ch)
                    for (int s = 0; s < bufB.getNumSamples(); ++s)
                        if (!std::isfinite(bufB.getSample(ch, s))) allFinite = false;
                worstPeak = juce::jmax(worstPeak, pk);
                juce::Logger::writeToLog("LOADTEST note " + juce::String(note)
                    + " rms=" + juce::String(r) + " voices=" + juce::String(big.activeVoiceCount()));
                big.panic();
            }
            juce::Logger::writeToLog("LOADTEST worstPeak=" + juce::String(worstPeak));
            check(allFinite && worstPeak < 1.1f, "bank output finite, no runaway gain");
        }
    }

    juce::Logger::writeToLog(failures == 0 ? "ALL TESTS PASSED" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
