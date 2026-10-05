#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <juce_audio_processors/juce_audio_processors.h>
#include "SamplerEngine.h"
#include "DSPEngine.h"
#include "FileLibrary.h"

// ============================================================
// Pastel Soundfont Player — AudioProcessor
// ============================================================
class PastelProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster
{
public:
    PastelProcessor();
    ~PastelProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Pastel Soundfont Player"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // sound loading: loadSoundFile is synchronous (harness/internal);
    // requestLoad runs in the background so big banks never freeze the DAW
    // (old sound keeps playing until the swap). Completion and failures are
    // reported via ChangeBroadcaster; use isLoading/getLoadProgress for UI
    // and consumeLoadError for one-shot failure alerts.
    bool loadSoundFile(const juce::String& path, juce::String& errorOut);
    void requestLoad(const juce::String& path);
    bool isLoading() const { return loading.load(); }
    float getLoadProgress() const { return loadProgress.load(); }
    juce::String getLoadingPath() const;
    bool consumeLoadError(juce::String& errOut);
    juce::String currentSoundName() const;
    juce::String currentSoundPath() const;
    juce::String currentProgramName() const;
    juce::StringArray sf2PresetNames() const;
    void panic();

    FileLibrary library;

    // on-screen keyboard audition (message thread, lock-protected)
    void auditionNoteOn(int note, float vel);
    void auditionNoteOff(int note);

    // engine note routing with active-note tracking (audio + message threads)
    void engineNoteOn(int note, float vel);
    void engineNoteOff(int note);
    void setCC(int cc, int value) { sampler.setCC(cc, value); }
    std::vector<int> getActiveNotes() const;
    int getLastKeyswitch() const;
    std::vector<std::pair<int,int>> getMappedRanges() const;
    std::vector<std::pair<int,int>> getSwitchRanges() const;
    int activeVoiceCount() const;
    juce::String getVoiceSamplePath(int voiceIdx) const;
    juce::String getSwitchLabel(int note) const;
    int getVoiceSwReq(int voiceIdx) const;
    int getCC(int cc) const { return sampler.getCC(cc); }

    float getOutLevelL() const { return outL; }
    float getOutLevelR() const { return outR; }
    bool isSoundLoaded() const;

private:
    DspParams collectParams();
    void handleMidi(juce::MidiBuffer& midi, int numSamples, double bpm);
    void arpReset();
    int arpNextNote();
    void refreshSamplerSearchPaths();
    void cancelLoadJob();
    void finishLoad(int gen, std::shared_ptr<SamplerEngine> loader,
                    juce::String path, juce::String error, bool ok);

    SamplerEngine sampler;
    DSPEngine dsp;

    // arp state
    std::vector<int> arpHeld;
    std::vector<std::pair<int,int>> arpPendingOffs; // (note, samplesLeft)
    double arpSamplesUntilNext = 0.0;
    int arpStep = 0;
    int arpDir = 1;
    int arpLastNote = -1;
    double arpLastBpm = 120.0;

    float outL = 0.0f, outR = 0.0f;
    int lastBank = -1, lastPreset = -1;
    float lastA = -1, lastD = -1, lastS = -1, lastR = -1;

    juce::CriticalSection activeLock;
    std::set<int> activeNotes;

    // background sample loading (big banks must not freeze the DAW)
    std::unique_ptr<std::thread> loadThread;
    mutable juce::CriticalSection loadMutex;
    std::atomic<bool> loadCancel { false };
    std::atomic<bool> loading { false };
    std::atomic<float> loadProgress { 0.0f };
    std::atomic<int> loadGen { 0 };
    juce::String loadingPath;
    juce::String pendingLoadError;
    std::atomic<bool> loadErrorPending { false };
    double lastSampleRate = 44100.0;
};
