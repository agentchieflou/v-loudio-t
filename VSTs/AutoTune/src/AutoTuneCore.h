#pragma once

#include "AutoTuneParams.h"
#include "loudio/dsp/Lookahead.h"
#include "loudio/dsp/PitchDetectorYin.h"
#include "loudio/dsp/PsolaShifter.h"
#include "loudio/dsp/SimpleSmoothedValue.h"
#include "loudio/plugin/ParamQueue.h"

#include <vector>

/*
 * AutoTune: real-time pitch correction (#146) on the platform core
 * contract, after Plugins/AutoTune/README.md (v1: YIN tracking, scale
 * quantisation, TD-PSOLA; the deep-learning hybrid is out of scope).
 * JUCE-free and LPI-free.
 *
 * Every hop (256 samples) the mono sum's last window + max lag samples go
 * through PitchDetectorYin. A voiced frame gives f0 -> MIDI (against the
 * reference A4) -> the nearest note of the key/scale -> a correction in
 * cents, scaled by (1 - humanise), smoothed in the log domain by the
 * retune speed (0 = instant), and handed to the two PsolaShifters (one
 * per channel, same period and ratio) as period = fs / f0 and ratio =
 * 2^(cents/1200). Unvoiced or quiet frames (RMS under -50 dBFS, or the
 * detector's aperiodicity over its limit) pass through the shifters
 * unchanged. Mix blends with the dry input delayed to match.
 *
 * Latency is fixed per activation: the shifter's 2048 + max period (fs /
 * 60 Hz) samples, which is the analysis window the plugin needs; the
 * dry path is delayed by the same amount.
 */
class AutoTuneCore {
public:
    static constexpr int kHop = 256;
    static constexpr int kYinWindow = 1024;
    static constexpr float kSilenceDb = -50.0f;

    AutoTuneCore();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* left, float* right, int numSamples);

    static const loudio::ParamTable& table() { return kAutoTuneParamTable; }
    int getParameterCount() const { return kAutoTuneNumParams; }
    bool getParameterInfo(int index, loudio::ParamMeta* outMeta) const;
    float getParameterValue(int index) const;

    void setParameterValueDirect(int index, float value);
    bool setParameterValue(int index, float value);
    bool setParameterValues(const loudio::ParamUpdate* updates, size_t count);

    size_t getState(void* buffer, size_t bufferSize) const;
    bool setState(const void* data, size_t size);

    int latencySamples() const { return shifterL.latencySamples(); }
    int analysisWindowSamples() const { return shifterL.latencySamples(); }

    /* Last analysis frame, for the UI and tests (torn reads are cosmetic). */
    bool detectedVoiced() const { return voiced; }
    float detectedHz() const { return detectedF0; }
    float detectedMidi() const { return detectedMidiReal; }
    int targetMidi() const { return targetNote; }
    float correctionCents() const { return appliedCents; }

    /* Nearest note of the key/scale to a (fractional) MIDI pitch. */
    static int quantise(float midiReal, int key, int scaleMask);

private:
    void applyParams();
    void analyse();

    double sampleRate = 44100.0;
    float params[kAutoTuneNumParams];
    float applied[kAutoTuneNumParams];
    loudio::ParamQueue<1024> paramQueue;

    loudio::PitchDetectorYin yin;
    loudio::PsolaShifter shifterL, shifterR;
    loudio::Lookahead dryL, dryR;
    SimpleSmoothedValue smoothedMix;

    std::vector<float> analysisRing; /* mono input history */
    std::vector<float> analysisFrame; /* linearised window for the detector */
    int ringMask = 0;
    long long written = 0;
    int sinceHop = 0;

    int scaleMask = 0xFFF, key = 0;
    float humanise = 0.0f, retuneMs = 20.0f, reference = 440.0f;
    float logRatio = 0.0f; /* smoothed correction, in cents */
    bool voiced = false, wasVoiced = false;
    float detectedF0 = 0.0f, detectedMidiReal = 0.0f, appliedCents = 0.0f;
    int targetNote = 69;
};
