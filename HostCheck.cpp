// VST3 host check (dev only, never shipped): loads the BUILT .vst3 through
// JUCE's VST3 hosting, like a DAW would: buses, params, latency, MIDI +
// audio, state, editor. Ported from the Pastel Drum Sampler HostCheck.
//   HostCheck <path/to/Pastel Soundfont Player.vst3>
#include <JuceHeader.h>
#include <cmath>
#include <cstdio>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { printf("usage: HostCheck <plugin.vst3>\n"); return 1; }
    int fails = 0;
    auto check = [&](bool ok, const juce::String& what) { printf("%s  %s\n", ok ? "  ok " : "FAIL ", what.toRawUTF8()); if (!ok) ++fails; };
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile(descs, juce::File(argv[1]).getFullPathName());
    check(descs.size() == 1, "VST3 scanned: " + juce::String(descs.size()) + " plugin(s)");
    if (descs.isEmpty()) return 1;
    auto& d = *descs[0];
    check(d.isInstrument, "registered as instrument: " + d.name + " / " + d.category);
    juce::String err;
    auto inst = fmt.createInstanceFromDescription(d, 48000.0, 512, err);
    check(inst != nullptr, "instantiated " + err);
    if (!inst) return 1;
    check(inst->getBusCount(true) == 0, "no audio input bus: " + juce::String(inst->getBusCount(true)));
    check(inst->getBusCount(false) == 1, "one output bus");
    check(inst->getParameters().size() >= 60, "parameters: " + juce::String(inst->getParameters().size()));
    inst->prepareToPlay(48000.0, 512);
    check(inst->getLatencySamples() > 0, "limiter lookahead reported: " + juce::String(inst->getLatencySamples()) + " samples");
    int nch = inst->getTotalNumOutputChannels();
    check(nch == 2, "2 output channels");
    juce::AudioBuffer<float> buf(nch, 512);
    bool finite = true;
    for (int b = 0; b < 40; ++b)
    {
        juce::MidiBuffer mb;
        if (b == 1) mb.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 110), 17);
        if (b == 2) mb.addEvent(juce::MidiMessage::pitchWheel(1, 12000), 0);
        if (b == 3) mb.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
        if (b == 5) mb.addEvent(juce::MidiMessage::noteOff(1, 60), 100);
        if (b == 9) mb.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
        buf.clear();
        inst->processBlock(buf, mb);
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < 512; ++i)
                if (!std::isfinite(buf.getSample(c, i))) finite = false;
    }
    check(finite, "MIDI (notes, bend, sustain) through an empty instance stays finite");
    juce::MemoryBlock st;
    inst->getStateInformation(st);
    check(st.getSize() > 500, "state saved (" + juce::String((int) st.getSize()) + " bytes)");
    inst->setStateInformation(st.getData(), (int) st.getSize());
    check(true, "state restored");
    std::unique_ptr<juce::AudioProcessorEditor> ed(inst->createEditorIfNeeded());
    check(ed != nullptr && ed->getWidth() >= 1100, "editor opens (" + juce::String(ed ? ed->getWidth() : 0) + " wide)");
    ed.reset();
    inst->releaseResources();
    inst.reset();
    printf(fails == 0 ? "HOST CHECK PASSED\n" : "%d HOST CHECK(S) FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
