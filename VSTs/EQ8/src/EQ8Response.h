#pragma once

#include "EQ8Params.h"
#include "loudio/dsp/BiquadD.h"

/*
 * The EQ-8's frequency response as a pure function of its parameter
 * values, shared by the DSP core (which applies exactly these designs)
 * and the UI (which draws them from its own copy of the parameters, so
 * the editor never reads the audio thread's filter state).
 */
inline bool eq8ApplyBandDesign(loudio::BiquadD& f, int type, double hz, double gainDb, double q) {
    switch (type) {
        case kEQ8LowPass:   f.setTarget(loudio::BiquadD::Type::LowPass, hz, q, 0.0); return true;
        case kEQ8HighPass:  f.setTarget(loudio::BiquadD::Type::HighPass, hz, q, 0.0); return true;
        case kEQ8LowShelf:  f.setTarget(loudio::BiquadD::Type::LowShelf, hz, q, gainDb); return true;
        case kEQ8HighShelf: f.setTarget(loudio::BiquadD::Type::HighShelf, hz, q, gainDb); return true;
        case kEQ8Bell:      f.setTarget(loudio::BiquadD::Type::Bell, hz, q, gainDb); return true;
        case kEQ8Notch:     f.setTarget(loudio::BiquadD::Type::Notch, hz, q, 0.0); return true;
        default:            f.setTargetBypass(); return false;
    }
}

/* Writes `count` dB values, log-spaced from minHz to maxHz, of the whole
 * chain (all bands + output gain) for the given parameter values. */
inline void eq8ComputeResponse(const float* params, double sampleRate, float minHz, float maxHz, float* outDb, int count) {
    loudio::BiquadD bands[kEQ8Bands];
    bool active[kEQ8Bands];
    for (int b = 0; b < kEQ8Bands; ++b) {
        bands[b].prepare(sampleRate);
        active[b] = eq8ApplyBandDesign(bands[b], (int)params[eq8BandParam(b, kEQ8BandType)], params[eq8BandParam(b, kEQ8BandFreq)],
                                       params[eq8BandParam(b, kEQ8BandGain)], params[eq8BandParam(b, kEQ8BandQ)]);
    }
    const bool bypass = params[kEQ8Bypass] > 0.5f;
    const double outGain = bypass ? 0.0 : (double)params[kEQ8OutputGain];
    double lo = std::log((double)minHz), hi = std::log((double)maxHz);
    for (int i = 0; i < count; ++i) {
        double hz = std::exp(lo + (hi - lo) * (count > 1 ? (double)i / (double)(count - 1) : 0.0));
        double db = outGain;
        if (!bypass) {
            for (int b = 0; b < kEQ8Bands; ++b) if (active[b]) db += bands[b].magnitudeDb(hz);
        }
        outDb[i] = (float)db;
    }
}
