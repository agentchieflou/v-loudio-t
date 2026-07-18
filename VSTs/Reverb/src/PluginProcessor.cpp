#include "PluginProcessor.h"
#include "PluginEditor.h"

LoudioReverbProcessor::LoudioReverbProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {

    /* Initialize lock-free ring buffers with static pre-allocated storage */
    cuif_spsc_init(&dspToUiRingBuffer, dspToUiStorage, 1024);
    cuif_spsc_init(&uiToDspRingBuffer, uiToDspStorage, 1024);
}

void LoudioReverbProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    core.prepare(sampleRate, samplesPerBlock);
}

void LoudioReverbProcessor::releaseResources() {
}

bool LoudioReverbProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void LoudioReverbProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages);

    if (buffer.getNumChannels() < 2) return;

    /* A. Read UI-to-DSP Parameter Updates from SPSC Ring Buffer, apply to
     * APVTS -- unchanged from before extraction, keeps the host's own
     * automation-lane/generic-UI view of every parameter in sync with UI
     * (knob drag) driven changes. */
    ParamUpdateMessage paramMsg;
    while (cuif_spsc_readable(&uiToDspRingBuffer) >= sizeof(ParamUpdateMessage)) {
        size_t read_bytes = cuif_spsc_read(&uiToDspRingBuffer, (unsigned char*)&paramMsg, sizeof(ParamUpdateMessage));
        if (read_bytes == sizeof(ParamUpdateMessage)) {
            if (paramMsg.index >= 0 && paramMsg.index < kNumParams) {
                *apvts.getRawParameterValue(kReverbParams[paramMsg.index].id) = paramMsg.value;
            }
        }
    }

    /* B. Push every current APVTS value (reflects both host automation and
     * the ring-buffer-applied UI change above) into the DSP core.
     * Same-thread, no queue -- see ReverbCore::setParameterValueDirect()'s
     * doc comment for why this is safe here specifically. */
    for (int i = 0; i < kNumParams; ++i) {
        core.setParameterValueDirect(i, *apvts.getRawParameterValue(kReverbParams[i].id));
    }

    int numSamples = buffer.getNumSamples();
    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getWritePointer(1);

    core.process(leftChannel, rightChannel, numSamples);

    /* C. Pull fresh analyzer data out of the core and push it to the UI
     * exactly like before extraction did. Note: if a host ever uses a block
     * size large enough for the analyzer's 1024-sample FFT window to
     * complete more than once within a single processBlock() call (block
     * size > 1024 -- not a realistic real-time size, but possible for
     * offline/bounce rendering), only the LAST completion within that block
     * is pushed here; earlier ones are silently superseded. Before
     * extraction, every completion got its own ring-buffer write. This is a
     * deliberate, low-risk simplification: it only affects the cosmetic
     * spectrum-analyzer display (never the audio signal path) and only at
     * block sizes far outside normal real-time use. */
    if (core.isLeftAnalyzerFrameDirty()) {
        float magnitudes[64];
        core.getLeftAnalyzerMagnitudes(magnitudes);
        unsigned char channelId = 'L';
        if (cuif_spsc_writable(&dspToUiRingBuffer) >= sizeof(channelId) + sizeof(magnitudes)) {
            cuif_spsc_write(&dspToUiRingBuffer, &channelId, sizeof(channelId));
            cuif_spsc_write(&dspToUiRingBuffer, (const unsigned char*)magnitudes, sizeof(magnitudes));
        }
    }

    if (core.isRightAnalyzerFrameDirty()) {
        float magnitudes[64];
        core.getRightAnalyzerMagnitudes(magnitudes);
        unsigned char channelId = 'R';
        if (cuif_spsc_writable(&dspToUiRingBuffer) >= sizeof(channelId) + sizeof(magnitudes)) {
            cuif_spsc_write(&dspToUiRingBuffer, &channelId, sizeof(channelId));
            cuif_spsc_write(&dspToUiRingBuffer, (const unsigned char*)magnitudes, sizeof(magnitudes));
        }
    }
}

juce::AudioProcessorEditor* LoudioReverbProcessor::createEditor() {
    return new LoudioReverbEditor(*this);
}

void LoudioReverbProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void LoudioReverbProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr) {
        if (xmlState->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout LoudioReverbProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto floatParam = [](const ReverbParamMeta& meta) {
        return std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(meta.id, 1), meta.name,
            juce::NormalisableRange<float>(meta.minValue, meta.maxValue, meta.stepValue), meta.defaultValue);
    };

    /* Base Parameters */
    params.push_back(floatParam(kReverbParams[kParamPreDelay]));
    params.push_back(floatParam(kReverbParams[kParamDecayTime]));
    params.push_back(floatParam(kReverbParams[kParamDamping]));
    params.push_back(floatParam(kReverbParams[kParamWidth]));
    params.push_back(floatParam(kReverbParams[kParamDryWet]));
    params.push_back(floatParam(kReverbParams[kParamDistance]));
    params.push_back(floatParam(kReverbParams[kParamThickness]));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(kReverbParams[kParamFreeze].id, 1), kReverbParams[kParamFreeze].name, false));

    juce::StringArray modes = { "Room", "Hall", "Plate", "Cathedral", "Spring" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(kReverbParams[kParamMode].id, 1), kReverbParams[kParamMode].name, modes, (int)kReverbParams[kParamMode].defaultValue)); /* default Hall */

    /* Decay EQ Multipliers */
    params.push_back(floatParam(kReverbParams[kParamDecayLow]));
    params.push_back(floatParam(kReverbParams[kParamDecayMid]));
    params.push_back(floatParam(kReverbParams[kParamDecayHigh]));
    params.push_back(floatParam(kReverbParams[kParamCrossoverLowMid]));
    params.push_back(floatParam(kReverbParams[kParamCrossoverMidHigh]));

    /* Post EQ Gains */
    params.push_back(floatParam(kReverbParams[kParamPostEqLowGain]));
    params.push_back(floatParam(kReverbParams[kParamPostEqMidGain]));
    params.push_back(floatParam(kReverbParams[kParamPostEqHighGain]));

    /* Dynamic Ducking */
    params.push_back(floatParam(kReverbParams[kParamDuckThreshold]));
    params.push_back(floatParam(kReverbParams[kParamDuckAmount]));
    params.push_back(floatParam(kReverbParams[kParamDuckRelease]));

    /* Auto-Gating */
    params.push_back(floatParam(kReverbParams[kParamGateThreshold]));
    params.push_back(floatParam(kReverbParams[kParamGateTime]));

    /* UI theme -- purely cosmetic, no audio effect, but persisted like any other setting via APVTS (#68). */
    juce::StringArray uiThemes = { "Default", "Hello Kitty", "Greens" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(kReverbParams[kParamUiTheme].id, 1), kReverbParams[kParamUiTheme].name, uiThemes, (int)kReverbParams[kParamUiTheme].defaultValue));

    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new LoudioReverbProcessor();
}
