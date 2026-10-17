#ifndef CUIF_RING_BUFFER_H
#define CUIF_RING_BUFFER_H

#include <stddef.h>

/*
 * Index type + full memory fence per platform. MSVC: volatile + MemoryBarrier()
 * (see the ordering note below). Everything else (the Linux DSP test job,
 * a future dlopen host): the GCC/Clang atomic builtin fence.
 */
#if defined(_WIN32)
#include <windows.h>
typedef LONG64 cuif_spsc_index;
#define CUIF_SPSC_FENCE() MemoryBarrier()
#else
#include <stdint.h>
typedef int64_t cuif_spsc_index;
#define CUIF_SPSC_FENCE() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lock-free single-producer/single-consumer ring buffer.
 *
 * Intended for audio-thread <-> UI-thread data exchange (parameter changes,
 * analyzer/FFT data) without the audio thread ever allocating, blocking, or
 * taking a lock. This header has a plain C ABI so it can be included from
 * both the C++ DSP side and the cuif C side without a language bridge.
 *
 * Ordering uses `volatile` indices plus MemoryBarrier() rather than C11
 * <stdatomic.h> -- the MSVC toolchain used to build this project does not
 * reliably support C11 atomics without extra experimental flags, whereas
 * this pattern (correct on x86/x64's strongly-ordered memory model) needs
 * nothing beyond <windows.h>. Off Windows the same shape uses the compiler
 * fence builtin (CUIF_SPSC_FENCE above).
 *
 * `capacity` must be a power of two. One instance is single-producer,
 * single-consumer only -- do not share a producer or consumer role across
 * more than one thread.
 */
typedef struct {
    unsigned char* data;
    size_t capacity; /* power of two */
    size_t mask;
    volatile cuif_spsc_index write_index; /* written only by the producer */
    volatile cuif_spsc_index read_index;  /* written only by the consumer */
} cuif_spsc_ring_buffer;

static inline void cuif_spsc_init(cuif_spsc_ring_buffer* rb, unsigned char* storage, size_t capacity_pow2) {
    rb->data = storage;
    rb->capacity = capacity_pow2;
    rb->mask = capacity_pow2 - 1;
    rb->write_index = 0;
    rb->read_index = 0;
}

static inline size_t cuif_spsc_writable(cuif_spsc_ring_buffer* rb) {
    cuif_spsc_index w = rb->write_index;
    CUIF_SPSC_FENCE();
    cuif_spsc_index r = rb->read_index;
    return rb->capacity - (size_t)(w - r);
}

static inline size_t cuif_spsc_readable(cuif_spsc_ring_buffer* rb) {
    cuif_spsc_index w = rb->write_index;
    CUIF_SPSC_FENCE();
    cuif_spsc_index r = rb->read_index;
    return (size_t)(w - r);
}

/* Producer-only. Returns bytes actually written (less than len if the buffer is full). Never blocks or allocates. */
static inline size_t cuif_spsc_write(cuif_spsc_ring_buffer* rb, const unsigned char* src, size_t len) {
    size_t avail = cuif_spsc_writable(rb);
    if (len > avail) len = avail;
    cuif_spsc_index w = rb->write_index;
    for (size_t i = 0; i < len; ++i) {
        rb->data[((size_t)w + i) & rb->mask] = src[i];
    }
    CUIF_SPSC_FENCE();
    rb->write_index = w + (cuif_spsc_index)len;
    return len;
}

/* Consumer-only. Returns bytes actually read. */
static inline size_t cuif_spsc_read(cuif_spsc_ring_buffer* rb, unsigned char* dst, size_t len) {
    size_t avail = cuif_spsc_readable(rb);
    if (len > avail) len = avail;
    cuif_spsc_index r = rb->read_index;
    for (size_t i = 0; i < len; ++i) {
        dst[i] = rb->data[((size_t)r + i) & rb->mask];
    }
    CUIF_SPSC_FENCE();
    rb->read_index = r + (cuif_spsc_index)len;
    return len;
}

#ifdef __cplusplus
}
#endif

#endif /* CUIF_RING_BUFFER_H */
