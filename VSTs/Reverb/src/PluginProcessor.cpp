#include "PluginProcessor.h"
#include "PluginEditor.h"

const float baseDelayLengths[8] = { 1013.0f, 1123.0f, 1277.0f, 1399.0f, 1511.0f, 1693.0f, 1823.0f, 1999.0f };

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

    /* 4. Prepare FDN Late Reverb & Crossovers */
    float fc1 = *apvts.getRawParameterValue("crossoverLowMid");
    float fc2 = *apvts.getRawParameterValue("crossoverMidHigh");
    for (int i = 0; i < 8; ++i) {
        int delayLen = (int)(baseDelayLengths[i] * rateScale * 1.5f);
        fdnDelayLines[i].prepare(delayLen + 2);
        fdnDampingFilters[i].prepare();
        crossovers[i].prepare();
        crossovers[i].setCrossoverFrequencies((float)sampleRate, fc1, fc2);
    }

    /* 5. Prepare Post EQ biquad chains */
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels = 1;
    postEqLeft.prepare(spec);
    postEqRight.prepare(spec);
    updatePostEQ();

    /* 6. Prepare Envelope detectors */
    duckDetector.prepare(sampleRate);
    gateDetector.prepare(sampleRate);
    gateFade = 1.0f;
    gateTimerMs = 0.0f;

    /* 7. Prepare Real-Time FFT Analyzers */
    fftAnalyzerLeft.prepare(sampleRate);
    fftAnalyzerRight.prepare(sampleRate);

    /* 8. Prepare parameter smoothing */
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

void LoudioReverbProcessor::updatePostEQ() {
    float lowGain = *apvts.getRawParameterValue("postEqLowGain");
    float midGain = *apvts.getRawParameterValue("postEqMidGain");
    float highGain = *apvts.getRawParameterValue("postEqHighGain");

    auto lowCoefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(currentSampleRate, 150.0f, 0.707f, juce::Decibels::decibelsToGain(lowGain));
    auto midCoefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(currentSampleRate, 1500.0f, 0.707f, juce::Decibels::decibelsToGain(midGain));
    auto highCoefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(currentSampleRate, 8000.0f, 0.707f, juce::Decibels::decibelsToGain(highGain));

    *postEqLeft.get<0>().coefficients = *lowCoefficients;
    *postEqRight.get<0>().coefficients = *lowCoefficients;

    *postEqLeft.get<1>().coefficients = *midCoefficients;
    *postEqRight.get<1>().coefficients = *midCoefficients;

    *postEqLeft.get<2>().coefficients = *highCoefficients;
    *postEqRight.get<2>().coefficients = *highCoefficients;
}

void LoudioReverbProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages);

    if (buffer.getNumChannels() < 2) return;

    /* A. Read UI-to-DSP Parameter Updates from SPSC Ring Buffer */
    ParamUpdateMessage paramMsg;
    while (cuif_spsc_readable(&uiToDspRingBuffer) >= sizeof(ParamUpdateMessage)) {
        size_t read_bytes = cuif_spsc_read(&uiToDspRingBuffer, (unsigned char*)&paramMsg, sizeof(ParamUpdateMessage));
        if (read_bytes == sizeof(ParamUpdateMessage)) {
            const char* paramId = nullptr;
            switch (paramMsg.index) {
                case kParamPreDelay: paramId = "preDelay"; break;
                case kParamDecayTime: paramId = "decayTime"; break;
                case kParamDamping: paramId = "damping"; break;
                case kParamWidth: paramId = "width"; break;
                case kParamDryWet: paramId = "dryWet"; break;
                case kParamDistance: paramId = "distance"; break;
                case kParamThickness: paramId = "thickness"; break;
                case kParamFreeze: paramId = "freeze"; break;
                case kParamMode: paramId = "mode"; break;
                case kParamDecayLow: paramId = "decayLow"; break;
                case kParamDecayMid: paramId = "decayMid"; break;
                case kParamDecayHigh: paramId = "decayHigh"; break;
                case kParamCrossoverLowMid: paramId = "crossoverLowMid"; break;
                case kParamCrossoverMidHigh: paramId = "crossoverMidHigh"; break;
                case kParamPostEqLowGain: paramId = "postEqLowGain"; break;
                case kParamPostEqMidGain: paramId = "postEqMidGain"; break;
                case kParamPostEqHighGain: paramId = "postEqHighGain"; break;
                case kParamDuckThreshold: paramId = "duckThreshold"; break;
                case kParamDuckAmount: paramId = "duckAmount"; break;
                case kParamDuckRelease: paramId = "duckRelease"; break;
                case kParamGateThreshold: paramId = "gateThreshold"; break;
                case kParamGateTime: paramId = "gateTime"; break;
                case kParamUiTheme: paramId = "uiTheme"; break;
                default: break;
            }
            if (paramId) {
                *apvts.getRawParameterValue(paramId) = paramMsg.value;
            }
        }
    }

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

    /* Crossover frequencies update */
    float fc1 = *apvts.getRawParameterValue("crossoverLowMid");
    float fc2 = *apvts.getRawParameterValue("crossoverMidHigh");
    for (int i = 0; i < 8; ++i) {
        crossovers[i].setCrossoverFrequencies((float)currentSampleRate, fc1, fc2);
    }

    updatePostEQ();

    /* Load Decay EQ multipliers */
    float decayLow = *apvts.getRawParameterValue("decayLow");
    float decayMid = *apvts.getRawParameterValue("decayMid");
    float decayHigh = *apvts.getRawParameterValue("decayHigh");

    /* Ducking details */
    float duckThreshold = *apvts.getRawParameterValue("duckThreshold");
    float duckAmount = *apvts.getRawParameterValue("duckAmount");
    float duckRelease = *apvts.getRawParameterValue("duckRelease");
    duckDetector.setAttackRelease(10.0f, duckRelease);

    /* Gate details */
    float gateThreshold = *apvts.getRawParameterValue("gateThreshold");
    float gateTime = *apvts.getRawParameterValue("gateTime");

    bool freeze = *apvts.getRawParameterValue("freeze") > 0.5f;
    int mode = (int)*apvts.getRawParameterValue("mode");

    float rateScale = (float)(currentSampleRate / 44100.0f);

    float modeScale = 1.0f;
    if (mode == 0)      modeScale = 0.6f;
    else if (mode == 1) modeScale = 1.0f;
    else if (mode == 2) modeScale = 0.75f;
    else if (mode == 3) modeScale = 1.5f;
    else if (mode == 4) modeScale = 0.85f;

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

        /* Pre-Delay */
        float preDelaySamples = preDelayMs * 0.001f * (float)currentSampleRate;
        preDelayLeft.write(inL);
        preDelayRight.write(inR);
        float preL = preDelayLeft.read(preDelaySamples);
        float preR = preDelayRight.read(preDelaySamples);

        /* All-Pass Diffusers */
        float diffL = diffuserLeft.process(preL);
        float diffR = diffuserRight.process(preR);

        /* Early Reflections */
        float earlyL = earlyReflectionsLeft.process(diffL);
        float earlyR = earlyReflectionsRight.process(diffR);

        /* Late FDN Reverb Loop */
        float currentDecay = decayTimeSec;
        float currentDamping = damping;
        if (freeze) {
            currentDecay = 1000.0f;
            currentDamping = 0.0f;
        }

        float loopGains[8];
        for (int i = 0; i < 8; ++i) {
            float delayInSamples = baseDelayLengths[i] * rateScale * modeScale;
            if (freeze) {
                loopGains[i] = 1.0f;
            } else {
                loopGains[i] = expf(-6.907755f * delayInSamples / ((float)currentSampleRate * currentDecay));
            }
        }

        float g_hf = currentDamping * 0.6f;

        float x_low[8], x_mid[8], x_high[8];
        for (int i = 0; i < 8; ++i) {
            float delayInSamples = baseDelayLengths[i] * rateScale * modeScale;
            float readVal = fdnDelayLines[i].read(delayInSamples);
            crossovers[i].process(readVal, x_low[i], x_mid[i], x_high[i]);
        }

        float x_damped[8];
        for (int i = 0; i < 8; ++i) {
            float g_low = loopGains[i] * decayLow;
            float g_mid = loopGains[i] * decayMid;
            float g_high = loopGains[i] * decayHigh;

            if (g_low > 0.999f && !freeze) g_low = 0.999f;
            if (g_mid > 0.999f && !freeze) g_mid = 0.999f;
            if (g_high > 0.999f && !freeze) g_high = 0.999f;

            float high_filtered = fdnDampingFilters[i].process(x_high[i], g_hf);
            x_damped[i] = x_low[i] * g_low + x_mid[i] * g_mid + high_filtered * g_high;
        }

        /* Householder Feedback Matrix */
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

        /* Post EQ stage filtering */
        wetL = postEqLeft.get<0>().processSample(wetL);
        wetL = postEqLeft.get<1>().processSample(wetL);
        wetL = postEqLeft.get<2>().processSample(wetL);

        wetR = postEqRight.get<0>().processSample(wetR);
        wetR = postEqRight.get<1>().processSample(wetR);
        wetR = postEqRight.get<2>().processSample(wetR);

        /* Early/Late balance */
        float outWetL = (1.0f - distance) * earlyL + distance * wetL;
        float outWetR = (1.0f - distance) * earlyR + distance * wetR;

        /* Stereo Width mid/side adjust */
        float mid = (outWetL + outWetR) * 0.5f;
        float side = (outWetL - outWetR) * 0.5f;
        float adjustedSide = side * width;
        outWetL = mid + adjustedSide;
        outWetR = mid - adjustedSide;

        /* Dynamic Ducking */
        float inLevelDb = duckDetector.processDb((std::abs(inL) + std::abs(inR)) * 0.5f);
        float duckDb = 0.0f;
        if (inLevelDb > duckThreshold) {
            duckDb = (inLevelDb - duckThreshold) * (duckAmount / 30.0f);
            if (duckDb > duckAmount) duckDb = duckAmount;
        }
        float duckGain = juce::Decibels::decibelsToGain(-duckDb);
        outWetL *= duckGain;
        outWetR *= duckGain;

        /* Auto-Gating */
        float wetLevelDb = gateDetector.processDb((std::abs(outWetL) + std::abs(outWetR)) * 0.5f);
        if (wetLevelDb < gateThreshold) {
            gateTimerMs += (1000.0f / (float)currentSampleRate);
            if (gateTimerMs >= gateTime) {
                gateFade = juce::jmax(0.0f, gateFade - 0.002f);
            }
        } else {
            gateTimerMs = 0.0f;
            gateFade = juce::jmin(1.0f, gateFade + 0.005f);
        }
        outWetL *= gateFade;
        outWetR *= gateFade;

        /* Global mix */
        leftChannel[sample] = (1.0f - dryWet) * inL + dryWet * outWetL;
        rightChannel[sample] = (1.0f - dryWet) * inR + dryWet * outWetR;

        /* B. Feed Output Samples to Real-Time FFT Analyzers & Push to SPSC Ring Buffer */
        if (fftAnalyzerLeft.pushSample(outWetL)) {
            std::vector<float> magnitudes(64);
            fftAnalyzerLeft.getMagnitudes(magnitudes);
            unsigned char channelId = 'L';
            if (cuif_spsc_writable(&dspToUiRingBuffer) >= sizeof(channelId) + magnitudes.size() * sizeof(float)) {
                cuif_spsc_write(&dspToUiRingBuffer, &channelId, sizeof(channelId));
                cuif_spsc_write(&dspToUiRingBuffer, (const unsigned char*)magnitudes.data(), magnitudes.size() * sizeof(float));
            }
        }

        if (fftAnalyzerRight.pushSample(outWetR)) {
            std::vector<float> magnitudes(64);
            fftAnalyzerRight.getMagnitudes(magnitudes);
            unsigned char channelId = 'R';
            if (cuif_spsc_writable(&dspToUiRingBuffer) >= sizeof(channelId) + magnitudes.size() * sizeof(float)) {
                cuif_spsc_write(&dspToUiRingBuffer, &channelId, sizeof(channelId));
                cuif_spsc_write(&dspToUiRingBuffer, (const unsigned char*)magnitudes.data(), magnitudes.size() * sizeof(float));
            }
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

    /* Base Parameters */
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
        juce::ParameterID("mode", 1), "Mode", modes, 1)); /* default Hall */

    /* Decay EQ Multipliers */
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("decayLow", 1), "Decay Low", juce::NormalisableRange<float>(0.1f, 2.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("decayMid", 1), "Decay Mid", juce::NormalisableRange<float>(0.1f, 2.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("decayHigh", 1), "Decay High", juce::NormalisableRange<float>(0.1f, 2.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("crossoverLowMid", 1), "Crossover Low-Mid", juce::NormalisableRange<float>(100.0f, 1000.0f, 1.0f), 250.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("crossoverMidHigh", 1), "Crossover Mid-High", juce::NormalisableRange<float>(1000.0f, 10000.0f, 1.0f), 4000.0f));

    /* Post EQ Gains */
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postEqLowGain", 1), "Post EQ Low Gain", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postEqMidGain", 1), "Post EQ Mid Gain", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postEqHighGain", 1), "Post EQ High Gain", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    /* Dynamic Ducking */
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("duckThreshold", 1), "Ducking Threshold", juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("duckAmount", 1), "Ducking Amount", juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("duckRelease", 1), "Ducking Release", juce::NormalisableRange<float>(10.0f, 1000.0f, 1.0f), 200.0f));

    /* Auto-Gating */
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateThreshold", 1), "Gate Threshold", juce::NormalisableRange<float>(-80.0f, -20.0f, 0.1f), -80.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateTime", 1), "Gate Time", juce::NormalisableRange<float>(10.0f, 500.0f, 1.0f), 100.0f));

    /* UI theme -- purely cosmetic, no audio effect, but persisted like any other setting via APVTS (#68). */
    juce::StringArray uiThemes = { "Default", "Hello Kitty", "Greens" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("uiTheme", 1), "UI Theme", uiThemes, 0));

    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new LoudioReverbProcessor();
}
