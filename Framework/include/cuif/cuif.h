#ifndef CUIF_H
#define CUIF_H

/*
 * cuif: custom C UI rendering framework.
 *
 * Shared foundation for all plugins in this repo (Reverb, EQ-3, EQ-8,
 * Compressor, Glue, AutoTune) -- not Reverb-specific. See
 * Plugins/Reverb/ARCHITECTURE_DECISIONS.md for why this exists instead of
 * JUCE's own GUI system or a third-party toolkit.
 *
 * This header just aggregates the public API. See the individual headers
 * for documentation.
 */

#include "window.h"
#include "ring_buffer.h"
#include "graphics.h"
#include "font.h"
#include "widget.h"

#endif /* CUIF_H */
