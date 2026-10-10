#pragma once

#include <JuceHeader.h>
#include "loudio/plugin/ParamTable.h"
#include "loudio/plugin/Transport.h"
#include "cuif/ring_buffer.h"

#include <atomic>
#include <memory>
#include <vector>

/*
 * Shared JUCE/VST3 processor (P3 of the plugin platform, #129), extracted
 * from the Reverb's PluginProcessor. A plugin's VST3 build is its Traits
 * (VSTs/Reverb/src/PluginProcessor.h shows the shape) plus a
 * juce_add_plugin block; this class is the two bridges every plugin needs:
 *
 *   1. APVTS <-> Core parameter sync, each block: drain UI-driven ring
 *      buffer messages into APVTS (keeps the host's own automation-lane /
 *      generic-UI view of every parameter correct), then push every
 *      current APVTS value into the core via setParameterValueDirect()
 *      (same thread, no queue).
 *   2. Core -> UI data (analyzer frames, meters) through dspToUiRingBuffer,
 *      via Traits::afterProcess, which the editor's Traits::pollDspToUi
 *      drains.
 *
 * The APVTS parameter layout is built from Core::table(): every entry in
 * table order, as AudioParameterBool (kParamBoolean), AudioParameterChoice
 * (kParamStepped, labels from Traits::choiceLabels) or AudioParameterFloat
 * (NormalisableRange(min, max, step)). Ids, names and order come from the
 * table, so the APVTS XML preset shape is the table's and nothing else.
 *
 * getStateInformation/setStateInformation serialize APVTS's own XML state,
 * preserving .vstpreset/DAW-session compatibility. The core's JUCE-free
 * getState/setState blob is for the LPI wrapper and isn't used here.
 *
 * Traits contract (processor side):
 *   using Core = ...;                                   // DSP/include/loudio/plugin/CORE_CONTRACT.md
 *   static juce::StringArray choiceLabels(int index);   // for kParamStepped entries; empty = numeric labels
 *   static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi);  // audio thread; may be empty
 */
namespace loudio {
namespace vst {

/* An analyzer/meter frame on the DSP -> UI ring: one tag byte, then N
 * floats. pushFrame is all-or-nothing so a reader never sees a tag without
 * its payload. */
template <size_t N>
inline void pushFrame(cuif_spsc_ring_buffer& rb, unsigned char tag, const float (&values)[N]) {
    if (cuif_spsc_writable(&rb) < sizeof(tag) + sizeof(values)) return;
    cuif_spsc_write(&rb, &tag, sizeof(tag));
    cuif_spsc_write(&rb, (const unsigned char*)values, sizeof(values));
}

template <class Traits>
class LoudioProcessor : public juce::AudioProcessor {
public:
    using Core = typename Traits::Core;

    LoudioProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "Parameters", createParameterLayout()) {
        cuif_spsc_init(&dspToUiRingBuffer, dspToUiStorage, sizeof(dspToUiStorage));
        cuif_spsc_init(&uiToDspRingBuffer, uiToDspStorage, sizeof(uiToDspStorage));

        /* apvts.getRawParameterValue(id) is a string-keyed lookup that
         * builds a juce::String -- a heap allocation -- so it is resolved
         * once here and never called from processBlock() (#47/#113). */
        const loudio::ParamTable& table = Core::table();
        rawParamValues.resize((size_t)table.count, nullptr);
        for (int i = 0; i < table.count; ++i) {
            rawParamValues[(size_t)i] = apvts.getRawParameterValue(table[i].id);
        }
    }
    ~LoudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        core.prepare(sampleRate, samplesPerBlock);
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);
        if (buffer.getNumChannels() < 2) return;

        const int count = (int)rawParamValues.size();

        /* A. UI -> APVTS: apply knob-drag driven changes so the host's own
         * view of every parameter stays in sync. */
        loudio::ParamUpdate msg;
        while (cuif_spsc_readable(&uiToDspRingBuffer) >= sizeof(msg)) {
            if (cuif_spsc_read(&uiToDspRingBuffer, (unsigned char*)&msg, sizeof(msg)) != sizeof(msg)) break;
            if (msg.index >= 0 && msg.index < count) {
                *rawParamValues[(size_t)msg.index] = msg.value;
            }
        }

        /* B. APVTS -> core: every current value (host automation and the
         * UI changes above). Same thread as process(), no queue. */
        for (int i = 0; i < count; ++i) {
            core.setParameterValueDirect(i, *rawParamValues[(size_t)i]);
        }

        /* Transport (P9, #135): only for a core that asks for it, from the
         * host's playhead, same shape as lpi.transport.v1. A host with no
         * playhead, or one that reports nothing, leaves the core
         * free-running (every flag clear). */
        if constexpr (requires (Core& c, const loudio::TransportInfo& t) { c.setTransport(t); }) {
            loudio::TransportInfo t;
            t.sampleRate = getSampleRate();
            if (juce::AudioPlayHead* head = getPlayHead()) {
                if (auto pos = head->getPosition()) {
                    if (auto bpm = pos->getBpm()) { t.tempoBpm = *bpm; t.flags |= loudio::kTransportTempoValid; }
                    if (auto ppq = pos->getPpqPosition()) { t.ppqPosition = *ppq; t.flags |= loudio::kTransportPpqValid; }
                    if (pos->getIsPlaying()) t.flags |= loudio::kTransportPlaying;
                    if (pos->getIsLooping()) t.flags |= loudio::kTransportLooping;
                }
            }
            core.setTransport(t);
        }

        core.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());

        /* C. Core -> UI. */
        Traits::afterProcess(core, dspToUiRingBuffer);
    }

    juce::AudioProcessorEditor* createEditor() override; /* defined in LoudioEditor.h */
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override {
        auto state = apvts.copyState();
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        copyXmlToBinary(*xml, destData);
    }

    void setStateInformation(const void* data, int sizeInBytes) override {
        std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
        if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
        const loudio::ParamTable& table = Core::table();
        for (int i = 0; i < table.count; ++i) {
            const loudio::ParamMeta& m = table[i];
            if (m.flags & loudio::kParamBoolean) {
                params.push_back(std::make_unique<juce::AudioParameterBool>(
                    juce::ParameterID(m.id, 1), m.name, m.defaultValue >= 0.5f));
            } else if (m.flags & loudio::kParamStepped) {
                juce::StringArray labels = Traits::choiceLabels(i);
                int steps = (int)(m.maxValue - m.minValue) + 1;
                if (labels.size() != steps) {
                    labels.clear();
                    for (int s = 0; s < steps; ++s) labels.add(juce::String((int)m.minValue + s));
                }
                params.push_back(std::make_unique<juce::AudioParameterChoice>(
                    juce::ParameterID(m.id, 1), m.name, labels, (int)(m.defaultValue - m.minValue)));
            } else {
                params.push_back(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID(m.id, 1), m.name,
                    juce::NormalisableRange<float>(m.minValue, m.maxValue, m.stepValue), m.defaultValue));
            }
        }
        return { params.begin(), params.end() };
    }

    juce::AudioProcessorValueTreeState apvts;

    /* SPSC lock-free rings for DSP <-> UI. The editor is the UI side of both. */
    cuif_spsc_ring_buffer dspToUiRingBuffer;
    cuif_spsc_ring_buffer uiToDspRingBuffer;

    Core core;

private:
    std::vector<std::atomic<float>*> rawParamValues;
    unsigned char dspToUiStorage[1024];
    unsigned char uiToDspStorage[1024];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudioProcessor)
};

} // namespace vst
} // namespace loudio
