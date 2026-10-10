#pragma once

#include "ParamTable.h"
#include "cuif/ring_buffer.h"

#include <cstddef>

/*
 * RT-safe single-producer / single-consumer parameter update queue (P1 of
 * the plugin platform, #127), extracted from ReverbCore.
 *
 * Producer: exactly one non-realtime thread over the instance's lifetime
 * (the LPI ABI's set_parameter_value contract, PluginABI/include/lpi/lpi.h).
 * Consumer: the audio thread, which calls drainInto() at the top of each
 * process() call. Never allocates, blocks or locks on either side.
 *
 * Writes are all-or-nothing per call: a single update or a whole batch is
 * enqueued completely or not at all (bad index, or not enough room for the
 * whole batch), so related edits -- the decay-EQ curve's three nodes --
 * can't be half-applied and a fixed-size record stream can never desync
 * (see PluginABI/examples/toy_gain/toy_gain.c and #107).
 *
 * CapacityBytes must be a power of two.
 */
namespace loudio {

template <size_t CapacityBytes = 1024>
class ParamQueue {
public:
    static_assert((CapacityBytes & (CapacityBytes - 1)) == 0, "CapacityBytes must be a power of two");
    static_assert(CapacityBytes >= sizeof(ParamUpdate), "CapacityBytes must hold at least one update");

    ParamQueue() { cuif_spsc_init(&ring, storage, CapacityBytes); }

    /* Producer. */
    bool push(const ParamTable& table, int index, float value) {
        if (!table.valid(index)) return false;
        ParamUpdate msg;
        msg.index = index;
        msg.value = value;
        if (cuif_spsc_writable(&ring) < sizeof(msg)) return false;
        return cuif_spsc_write(&ring, (const unsigned char*)&msg, sizeof(msg)) == sizeof(msg);
    }

    /* Producer. Enqueues all `count` updates or none. */
    bool pushAll(const ParamTable& table, const ParamUpdate* updates, size_t count) {
        if (!updates) return count == 0;
        for (size_t i = 0; i < count; ++i) {
            if (!table.valid(updates[i].index)) return false;
        }
        size_t bytes = count * sizeof(ParamUpdate);
        if (cuif_spsc_writable(&ring) < bytes) return false;
        return cuif_spsc_write(&ring, (const unsigned char*)updates, bytes) == bytes;
    }

    /* Consumer. Applies every queued update to `values` (indexed by
     * parameter index), last write wins. Returns the number applied. */
    size_t drainInto(const ParamTable& table, float* values) {
        size_t applied = 0;
        ParamUpdate msg;
        while (cuif_spsc_readable(&ring) >= sizeof(msg)) {
            if (cuif_spsc_read(&ring, (unsigned char*)&msg, sizeof(msg)) != sizeof(msg)) break;
            if (table.valid(msg.index)) {
                values[msg.index] = msg.value;
                ++applied;
            }
        }
        return applied;
    }

    /* Consumer. Discards everything queued (e.g. on deactivate). */
    void clear() {
        ParamUpdate msg;
        while (cuif_spsc_readable(&ring) >= sizeof(msg)) {
            if (cuif_spsc_read(&ring, (unsigned char*)&msg, sizeof(msg)) != sizeof(msg)) break;
        }
    }

    size_t capacityUpdates() const { return CapacityBytes / sizeof(ParamUpdate); }

private:
    cuif_spsc_ring_buffer ring;
    unsigned char storage[CapacityBytes];
};

} // namespace loudio
