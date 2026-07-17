#ifndef CUIF_RING_BUFFER_H
#define CUIF_RING_BUFFER_H

#include <stddef.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lock-free single-producer/single-consumer ring buffer.
 *
 * Intended for audio-thread <-> UI-thread data exchange (parameter changes,
 * analyzer/FFT data) without the audio thread ever allocating, blocking, or
 * taking a lock. This header has a plain C ABI so it can be included from
 * both the JUCE/C++ DSP side and the cuif C side without a language bridge.
 *
 * Ordering uses `volatile` indices plus MemoryBarrier() rather than C11
 * <stdatomic.h> -- the MSVC toolchain used to build this project does not
 * reliably support C11 atomics without extra experimental flags, whereas
 * this pattern (correct on x86/x64's strongly-ordered memory model) needs
 * nothing beyond <windows.h>.
 *
 * `capacity` must be a power of two. One instance is single-producer,
 * single-consumer only -- do not share a producer or consumer role across
 * more than one thread.
 */
typedef struct {
    unsigned char* data;
    size_t capacity; /* power of two */
    size_t mask;
    volatile LONG64 write_index; /* written only by the producer */
    volatile LONG64 read_index;  /* written only by the consumer */
} cuif_spsc_ring_buffer;

static inline void cuif_spsc_init(cuif_spsc_ring_buffer* rb, unsigned char* storage, size_t capacity_pow2) {
    rb->data = storage;
    rb->capacity = capacity_pow2;
    rb->mask = capacity_pow2 - 1;
    rb->write_index = 0;
    rb->read_index = 0;
}

static inline size_t cuif_spsc_writable(cuif_spsc_ring_buffer* rb) {
    LONG64 w = rb->write_index;
    MemoryBarrier();
    LONG64 r = rb->read_index;
    return rb->capacity - (size_t)(w - r);
}

static inline size_t cuif_spsc_readable(cuif_spsc_ring_buffer* rb) {
    LONG64 w = rb->write_index;
    MemoryBarrier();
    LONG64 r = rb->read_index;
    return (size_t)(w - r);
}

/* Producer-only. Returns bytes actually written (less than len if the buffer is full). Never blocks or allocates. */
static inline size_t cuif_spsc_write(cuif_spsc_ring_buffer* rb, const unsigned char* src, size_t len) {
    size_t avail = cuif_spsc_writable(rb);
    if (len > avail) len = avail;
    LONG64 w = rb->write_index;
    for (size_t i = 0; i < len; ++i) {
        rb->data[((size_t)w + i) & rb->mask] = src[i];
    }
    MemoryBarrier();
    rb->write_index = w + (LONG64)len;
    return len;
}

/* Consumer-only. Returns bytes actually read. */
static inline size_t cuif_spsc_read(cuif_spsc_ring_buffer* rb, unsigned char* dst, size_t len) {
    size_t avail = cuif_spsc_readable(rb);
    if (len > avail) len = avail;
    LONG64 r = rb->read_index;
    for (size_t i = 0; i < len; ++i) {
        dst[i] = rb->data[((size_t)r + i) & rb->mask];
    }
    MemoryBarrier();
    rb->read_index = r + (LONG64)len;
    return len;
}

#ifdef __cplusplus
}
#endif

#endif /* CUIF_RING_BUFFER_H */
