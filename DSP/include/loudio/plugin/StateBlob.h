#pragma once

#include "ParamTable.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

/*
 * Id-keyed parameter state blob, format v1 (P1 of the plugin platform,
 * #127; first shipped as the Reverb's "LRVB" blob in #115).
 *
 *   char magic[4]               per plugin (the Reverb uses "LRVB")
 *   u16 version = 1
 *   u16 entryCount
 *   entryCount x { u8 idLen; char id[idLen]; f32 value; }
 *
 * All integers little-endian, as written on x86/x64. Keyed on
 * ParamMeta::id, never on index -- the rule lpi.h states for every LPI
 * plugin -- so inserting or reordering parameters can't silently load a
 * preset's values into the wrong parameters. On load: unknown ids are
 * skipped (a newer build's preset loads in an older build), parameters
 * missing from the blob get their default (an older preset loads in a
 * newer build), values are clamped to the parameter's range and non-finite
 * values fall back to the default. The blob is fully validated before
 * anything is written, so a truncated/corrupt blob leaves the caller's
 * values untouched.
 *
 * Legacy: a plugin that once wrote a raw positional float array can keep
 * loading it by passing that array's length as `legacyRawCount`: a blob of
 * exactly legacyRawCount*4 bytes that doesn't start with the magic is
 * mapped positionally onto the first legacyRawCount parameters.
 */
namespace loudio {

struct StateBlob {
    static constexpr uint16_t kVersion = 1;
    static constexpr size_t kMagicLen = 4;
    static constexpr size_t kHeaderLen = kMagicLen + 2 * sizeof(uint16_t);

    static size_t requiredSize(const ParamTable& table) {
        size_t required = kHeaderLen;
        for (int i = 0; i < table.count; ++i) {
            required += 1 + std::strlen(table[i].id) + sizeof(float);
        }
        return required;
    }

    /* Two-call sizing idiom, matching lpi_plugin_api::get_state: buffer ==
     * nullptr returns the required size without writing anything; a buffer
     * too small for the whole blob returns 0 and writes nothing. */
    static size_t write(const ParamTable& table, const char magic[4], const float* values, void* buffer, size_t bufferSize) {
        size_t required = requiredSize(table);
        if (buffer == nullptr) return required;
        if (bufferSize < required) return 0;

        unsigned char* out = static_cast<unsigned char*>(buffer);
        std::memcpy(out, magic, kMagicLen); out += kMagicLen;
        uint16_t version = kVersion, count = (uint16_t)table.count;
        std::memcpy(out, &version, sizeof(version)); out += sizeof(version);
        std::memcpy(out, &count, sizeof(count)); out += sizeof(count);
        for (int i = 0; i < table.count; ++i) {
            size_t idLen = std::strlen(table[i].id);
            *out++ = (unsigned char)idLen;
            std::memcpy(out, table[i].id, idLen); out += idLen;
            std::memcpy(out, &values[i], sizeof(float)); out += sizeof(float);
        }
        return required;
    }

    /* Validates the whole blob first; only then fills `values` (every
     * parameter: defaults for the ones the blob doesn't name). Returns
     * false, leaving `values` untouched, on null/truncated/wrong-magic/
     * wrong-version input. */
    static bool read(const ParamTable& table, const char magic[4], const void* data, size_t size, float* values, int legacyRawCount = 0) {
        if (!data) return false;
        const unsigned char* in = static_cast<const unsigned char*>(data);

        if (legacyRawCount > 0 && size == sizeof(float) * (size_t)legacyRawCount
            && (size < kMagicLen || std::memcmp(in, magic, kMagicLen) != 0)) {
            table.fillDefaults(values);
            for (int i = 0; i < legacyRawCount && i < table.count; ++i) {
                float v;
                std::memcpy(&v, in + i * sizeof(float), sizeof(float));
                values[i] = table.sanitize(i, v);
            }
            return true;
        }

        if (size < kHeaderLen) return false;
        if (std::memcmp(in, magic, kMagicLen) != 0) return false;
        uint16_t version, count;
        std::memcpy(&version, in + kMagicLen, sizeof(version));
        std::memcpy(&count, in + kMagicLen + sizeof(version), sizeof(count));
        if (version != kVersion) return false; /* a future v2 needs its own reader */

        /* Pass 1: structural validation, nothing written. */
        const unsigned char* end = in + size;
        const unsigned char* p = in + kHeaderLen;
        for (uint16_t e = 0; e < count; ++e) {
            if (end - p < 1) return false;
            size_t idLen = *p++;
            if ((size_t)(end - p) < idLen + sizeof(float)) return false;
            p += idLen + sizeof(float);
        }

        /* Pass 2: apply. */
        table.fillDefaults(values);
        p = in + kHeaderLen;
        for (uint16_t e = 0; e < count; ++e) {
            size_t idLen = *p++;
            const char* id = reinterpret_cast<const char*>(p); p += idLen;
            float v;
            std::memcpy(&v, p, sizeof(float)); p += sizeof(float);
            int index = table.findById(id, idLen);
            if (index >= 0) values[index] = table.sanitize(index, v);
        }
        return true;
    }
};

} // namespace loudio
