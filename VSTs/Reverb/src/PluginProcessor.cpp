#include "PluginProcessor.h"
#include "PluginEditor.h"

const float baseDelayLengths[8] = { 1013.0f, 1123.0f, 1277.0f, 1399.0f, 1511.0f, 1693.0f, 1823.0f, 1999.0f };

LoudioReverbProcessor::LoudioReverbProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {
}

void LoudioReverbProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;

    float rateScale = (float)(sampleRate / 44100.0);

    /* 1. Prepare Pre-Delay (max 150ms) */
    int maxPreDelaySamples = (int)(0.150f * sampleRate) + 2;
    preDelayLeft.prepare(maxPreDelaySamples);
    preDelayRight.prepare(maxPreDelaySamples);

    /* 2. Prepare Schroeder Diffusers */
    std::vector<int> leftDiffLengths = {
        (int)(151 * rateScale),
        (int)(307 * rateScale),
        (int)(439 * rateScale),
        (int)(571 * rateScale)
    };
    diffuserLeft.prepare(leftDiffLengths, 0.6f);

    std::vector<int> rightDiffLengths = {
        (int)(163 * rateScale),
        (int)(313 * rateScale),
        (int)(443 * rateScale),
        (int)(577 * rateScale)
    };
    diffuserRight.prepare(rightDiffLengths, 0.6f);

    /* 3. Prepare Early Reflections */
    std::vector<ReverbTap> leftTaps = {
        { (int)(353 * rateScale), 0.7f },
        { (int)(563 * rateScale), -0.5f },
        { (int)(827 * rateScale), 0.4f },
        { (int)(1093 * rateScale), -0.3f }
    };
    earlyReflectionsLeft.prepare((int)(1200 * rateScale), leftTaps);

    std::vector<ReverbTap> rightTaps = {
        { (int)(389 * rateScale), -0.7f },
        { (int)(599 * rateScale), 0.5f },
        { (int)(859 * rateScale), -0.4f },
        { (int)(1117 * rateScale), 0.3f }
    };
    earlyReflectionsRight.prepare((int)(1200 * rateScale), rightTaps);

    /* 4. Prepare FDN Late Reverb */
    for (int i = 0; i < 8; ++i) {
        int delayLen = (int)(baseDelayLengths[i] * rateScale);
        fdnDelayLines[i].prepare(delayLen + 2);
        fdnDampingFilters[i].prepare();
    }

    /* 5. Prepare parameter smoothing */
    smoothedPreDelayMs.reset(sampleRate, 0.02);
    smoothedDecayTimeSec.reset(sampleRate, 0.02);
    smoothedDamping.reset(sampleRate, 0.02);
    smoothedWidth.reset(sampleRate, 0.02);
    smoothedDryWet.reset(sampleRate, 0.02);
    smoothedDistance.reset(sampleRate, 0.02);
    smoothedThickness.reset(sampleRate, 0.02);

    /* Initialize smoothed values */
    smoothedPreDelayMs.setCurrentAndTargetValue(*apvts.getRawParameterValue("preDelay"));
    smoothedDecayTimeSec.setCurrentAndTargetValue(*apvts.getRawParameterValue("decayTime"));
    smoothedDamping.setCurrentAndTargetValue(*apvts.getRawParameterValue("damping"));
    smoothedWidth.setCurrentAndTargetValue(*apvts.getRawParameterValue("width"));
    smoothedDryWet.setCurrentAndTargetValue(*apvts.getRawParameterValue("dryWet"));
    smoothedDistance.setCurrentAndTargetValue(*apvts.getRawParameterValue("distance"));
    smoothedThickness.setCurrentAndTargetValue(*apvts.getRawParameterValue("thickness"));
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

    int numSamples = buffer.getNumSamples();
    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getWritePointer(1);

    /* Update smoothed target values */
    smoothedPreDelayMs.setTargetValue(*apvts.getRawParameterValue("preDelay"));
    smoothedDecayTimeSec.setTargetValue(*apvts.getRawParameterValue("decayTime"));
    smoothedDamping.setTargetValue(*apvts.getRawParameterValue("damping"));
    smoothedWidth.setTargetValue(*apvts.getRawParameterValue("width"));
    smoothedDryWet.setTargetValue(*apvts.getRawParameterValue("dryWet"));
    smoothedDistance.setTargetValue(*apvts.getRawParameterValue("distance"));
    smoothedThickness.setTargetValue(*apvts.getRawParameterValue("thickness"));

    bool freeze = *apvts.getRawParameterValue("freeze") > 0.5f;

    float rateScale = (float)(currentSampleRate / 44100.0f);

    for (int sample = 0; sample < numSamples; ++sample) {
        float preDelayMs = smoothedPreDelayMs.getNextValue();
        float decayTimeSec = smoothedDecayTimeSec.getNextValue();
        float damping = smoothedDamping.getNextValue();
        float width = smoothedWidth.getNextValue();
        float dryWet = smoothedDryWet.getNextValue();
        float distance = smoothedDistance.getNextValue();
        float thickness = smoothedThickness.getNextValue();

        float inL = leftChannel[sample];
        float inR = rightChannel[sample];

        /* 1. Pre-Delay */
        float preDelaySamples = preDelayMs * 0.001f * (float)currentSampleRate;
        preDelayLeft.write(inL);
        preDelayRight.write(inR);
        float preL = preDelayLeft.read(preDelaySamples);
        float preR = preDelayRight.read(preDelaySamples);

        /* 2. All-Pass Diffusers */
        float diffL = diffuserLeft.process(preL);
        float diffR = diffuserRight.process(preR);

        /* 3. Early Reflections */
        float earlyL = earlyReflectionsLeft.process(diffL);
        float earlyR = earlyReflectionsRight.process(diffR);

        /* 4. Late FDN Reverb Loop */
        float currentDecay = decayTimeSec;
        float currentDamping = damping;
        if (freeze) {
            currentDecay = 1000.0f;
            currentDamping = 0.0f;
        }

        float loopGains[8];
        for (int i = 0; i < 8; ++i) {
            float delayInSamples = baseDelayLengths[i] * rateScale;
            if (freeze) {
                loopGains[i] = 1.0f;
            } else {
                loopGains[i] = expf(-6.907755f * delayInSamples / ((float)currentSampleRate * currentDecay));
            }
        }

        float g_hf = currentDamping * 0.6f;

        float x_delay[8];
        for (int i = 0; i < 8; ++i) {
            float delayInSamples = baseDelayLengths[i] * rateScale;
            x_delay[i] = fdnDelayLines[i].read(delayInSamples);
        }

        float x_damped[8];
        for (int i = 0; i < 8; ++i) {
            x_damped[i] = fdnDampingFilters[i].process(x_delay[i], g_hf);
        }

        /* Householder Matrix loop multiplication */
        float sum = 0.0f;
        for (int i = 0; i < 8; ++i) {
            sum += x_damped[i];
        }

        float factor = 0.25f;
        float x_feedback[8];
        for (int i = 0; i < 8; ++i) {
            x_feedback[i] = x_damped[i] - factor * sum;
        }

        /* Input distribution */
        float fdnInput[8];
        fdnInput[0] = diffL;
        fdnInput[1] = -diffL;
        fdnInput[2] = diffL;
        fdnInput[3] = -diffL;
        fdnInput[4] = diffR;
        fdnInput[5] = -diffR;
        fdnInput[6] = diffR;
        fdnInput[7] = -diffR;

        float thicknessScale = 1.0f + 0.3f * thickness;
        for (int i = 0; i < 8; ++i) {
            float writeVal = fdnInput[i] * thicknessScale + x_feedback[i] * loopGains[i];
            fdnDelayLines[i].write(writeVal);
        }

        float wetL = (x_damped[0] - x_damped[1] + x_damped[2] - x_damped[3]) * 0.5f;
        float wetR = (x_damped[4] - x_damped[5] + x_damped[6] - x_damped[7]) * 0.5f;

        /* Early/Late blend */
        float outWetL = (1.0f - distance) * earlyL + distance * wetL;
        float outWetR = (1.0f - distance) * earlyR + distance * wetR;

        /* Stereo Width mid/side adjust */
        float mid = (outWetL + outWetR) * 0.5f;
        float side = (outWetL - outWetR) * 0.5f;
        float adjustedSide = side * width;
        outWetL = mid + adjustedSide;
        outWetR = mid - adjustedSide;

        /* Global mix */
        leftChannel[sample] = (1.0f - dryWet) * inL + dryWet * outWetL;
        rightChannel[sample] = (1.0f - dryWet) * inR + dryWet * outWetR;
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

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("preDelay", 1), "Pre-Delay", juce::NormalisableRange<float>(0.0f, 150.0f, 0.1f), 20.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("decayTime", 1), "Decay Time", juce::NormalisableRange<float>(0.4f, 15.0f, 0.1f), 2.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("damping", 1), "Damping", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("width", 1), "Width", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("dryWet", 1), "Dry/Wet", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.4f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("distance", 1), "Distance", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("thickness", 1), "Thickness", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("freeze", 1), "Freeze", false));

    juce::StringArray modes = { "Room", "Hall", "Plate", "Cathedral", "Spring" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("mode", 1), "Mode", modes, 0));

    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new LoudioReverbProcessor();
}
