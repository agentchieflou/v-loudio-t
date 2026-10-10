#pragma once
#include "BiquadD.h"

/*
 * 4th-order Linkwitz-Riley crossovers for the EQ-3 (#145): two cascaded
 * 2nd-order Butterworth sections (BiquadD, double precision TDF2, 5 ms
 * coefficient ramps) per edge. An LR4 low-pass and high-pass at the same
 * corner are in phase and sum to an all-pass of unity magnitude.
 *
 * ThreeBandCrossoverLR4 splits x into low / mid / high that sum to
 *   AP1 * AP2 * x
 * (flat magnitude, all-pass phase): the upper crossover (fc2) splits x
 * into LP2 and HP2; the lower (fc1) splits LP2 into low and mid; HP2 goes
 * through fc1's all-pass (LP1 + HP1) so its phase matches the other two.
 * Every pair of neighbouring bands is phase-coherent at its crossover, so
 * killing a band leaves no notch in the remaining sum.
 *
 * The Reverb's first-order ThreeBandCrossover (Crossover.h) is untouched:
 * its golden impulse responses depend on it.
 */
namespace loudio {

class LinkwitzRiley4 {
public:
    enum class Kind { LowPass, HighPass };

    void prepare(double sampleRate) {
        s1.prepare(sampleRate);
        s2.prepare(sampleRate);
    }
    void reset() { s1.reset(); s2.reset(); }

    /* Ramps toward the new corner (5 ms); snap() jumps there. */
    void set(Kind kind, double hz) {
        BiquadD::Type t = kind == Kind::LowPass ? BiquadD::Type::LowPass : BiquadD::Type::HighPass;
        s1.setTarget(t, hz, kButterworthQ, 0.0);
        s2.setTarget(t, hz, kButterworthQ, 0.0);
    }
    void snap() { s1.snapToTarget(); s2.snapToTarget(); }

    double process(double x) { return s2.process(s1.process(x)); }

    double magnitudeDb(double hz) const { return s1.magnitudeDb(hz) + s2.magnitudeDb(hz); }

    static constexpr double kButterworthQ = 0.70710678118654752;

private:
    BiquadD s1, s2;
};

class ThreeBandCrossoverLR4 {
public:
    void prepare(double sampleRate) {
        fs = sampleRate;
        lp2.prepare(fs); hp2.prepare(fs);
        lp1Low.prepare(fs); hp1Mid.prepare(fs);
        lp1High.prepare(fs); hp1High.prepare(fs);
    }
    void reset() {
        lp2.reset(); hp2.reset();
        lp1Low.reset(); hp1Mid.reset();
        lp1High.reset(); hp1High.reset();
    }

    /* fc1 < fc2; both clamped into (20 Hz, 0.49 fs) and ordered. */
    void setCrossoverFrequencies(double fc1, double fc2) {
        if (fc1 < 20.0) fc1 = 20.0;
        if (fc2 > fs * 0.49) fc2 = fs * 0.49;
        if (fc1 >= fc2) fc1 = fc2 * 0.9;
        lp2.set(LinkwitzRiley4::Kind::LowPass, fc2);
        hp2.set(LinkwitzRiley4::Kind::HighPass, fc2);
        lp1Low.set(LinkwitzRiley4::Kind::LowPass, fc1);
        hp1Mid.set(LinkwitzRiley4::Kind::HighPass, fc1);
        lp1High.set(LinkwitzRiley4::Kind::LowPass, fc1);
        hp1High.set(LinkwitzRiley4::Kind::HighPass, fc1);
    }
    void snap() { lp2.snap(); hp2.snap(); lp1Low.snap(); hp1Mid.snap(); lp1High.snap(); hp1High.snap(); }

    void process(double x, double& low, double& mid, double& high) {
        const double l2 = lp2.process(x);
        const double h2 = hp2.process(x);
        low = lp1Low.process(l2);
        mid = hp1Mid.process(l2);
        high = lp1High.process(h2) + hp1High.process(h2); /* fc1's all-pass */
    }

private:
    double fs = 48000.0;
    LinkwitzRiley4 lp2, hp2, lp1Low, hp1Mid, lp1High, hp1High;
};

} // namespace loudio
