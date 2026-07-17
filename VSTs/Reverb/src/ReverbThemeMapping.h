#pragma once

#include "cuif/theme.h"
#include "cuif/theme_hello_kitty.h"
#include "cuif/theme_greens.h"

/*
 * Maps the "uiTheme" APVTS parameter's choice index (matching the
 * dropdown order: 0=Default, 1=Hello Kitty, 2=Greens) to the cuif_theme
 * to apply. Deliberately has no JUCE dependency so it's testable from
 * DSPTests.cpp without a JUCE host/AudioProcessor.
 */
inline const cuif_theme* themeForUiThemeIndex(int index) {
    switch (index) {
        case 1: return &CUIF_THEME_HELLO_KITTY;
        case 2: return &CUIF_THEME_GREENS;
        default: return &CUIF_THEME_DEFAULT;
    }
}
