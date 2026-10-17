#pragma once
#include <cstdint>
#include <cmath>

/*
 * Host transport as a plugin core sees it (P9, #135). Mirrors
 * lpi_transport_info / LPI_TRANSPORT_* in PluginABI/include/lpi/lpi.h bit
 * for bit (the LPI wrapper static_asserts this) without making the DSP
 * library depend on lpi.h; the VST3 shell fills it from the host's
 * ProcessContext.
 *
 * A core that wants transport adds
 *     void setTransport(const loudio::TransportInfo& t);
 * and both wrappers detect it (CORE_CONTRACT.md). It is called on the
 * audio thread once per block before process(). The host may call it with
 * kTransportPlaying clear and a stale ppq, or stop calling it: keep a
 * free-running fallback and resync only when PLAYING and PPQ_VALID are set.
 */
namespace loudio {

enum TransportFlags : uint32_t {
    kTransportPlaying    = 1u << 0,
    kTransportLooping    = 1u << 1,
    kTransportTempoValid = 1u << 2,
    kTransportPpqValid   = 1u << 3
};

struct TransportInfo {
    double tempoBpm = 120.0;
    double ppqPosition = 0.0;
    double sampleRate = 0.0;
    uint32_t flags = 0;

    bool playing() const { return (flags & kTransportPlaying) != 0; }
    bool tempoValid() const { return (flags & kTransportTempoValid) != 0; }
    bool ppqValid() const { return (flags & kTransportPpqValid) != 0; }
};

/* Frequency in Hz of a cycle lasting `beatsPerCycle` quarter notes at
 * `tempoBpm` (1.0 = a quarter note, 4.0 = a bar of 4/4, 0.5 = an eighth). */
inline float tempoDivisionHz(double tempoBpm, double beatsPerCycle) {
    if (tempoBpm <= 0.0 || beatsPerCycle <= 0.0) return 0.0f;
    return (float)(tempoBpm / 60.0 / beatsPerCycle);
}

/* Length in seconds of `beatsPerCycle` quarter notes (a synced delay time). */
inline float tempoDivisionSeconds(double tempoBpm, double beatsPerCycle) {
    if (tempoBpm <= 0.0) return 0.0f;
    return (float)(beatsPerCycle * 60.0 / tempoBpm);
}

/* Phase in cycles [0, 1) of a `beatsPerCycle`-long cycle at musical
 * position `ppq`, for resyncing an Lfo (Lfo::resetPhase). */
inline float tempoPhaseAtPpq(double ppq, double beatsPerCycle) {
    if (beatsPerCycle <= 0.0) return 0.0f;
    double cycles = ppq / beatsPerCycle;
    double phase = cycles - std::floor(cycles);
    return phase >= 1.0 ? 0.0f : (float)phase;
}

} // namespace loudio
