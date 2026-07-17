#include "cuif/theme.h"

const cuif_theme CUIF_THEME_DEFAULT = {
    /* background */         { 0.12f, 0.13f, 0.15f, 1.0f },
    /* panel_bg */            { 0.18f, 0.19f, 0.22f, 1.0f },
    /* border */               { 0.28f, 0.30f, 0.34f, 1.0f },
    /* primary */               { 0.35f, 0.65f, 0.95f, 1.0f },
    /* text_primary */           { 0.95f, 0.95f, 0.95f, 1.0f },
    /* text_secondary */          { 0.6f, 0.62f, 0.66f, 1.0f },
    /* grid_line */                { 0.2f, 0.22f, 0.25f, 0.4f },
    /* dropdown_overlay_bg */       { 0.18f, 0.19f, 0.22f, 0.95f },
    /* dropdown_overlay_border */    { 0.3f, 0.32f, 0.35f, 1.0f },
    /* dropdown_hover_bg */           { 0.28f, 0.32f, 0.38f, 1.0f },
};

static const cuif_theme* g_current_theme = &CUIF_THEME_DEFAULT;

void cuif_set_theme(const cuif_theme* theme) {
    g_current_theme = theme ? theme : &CUIF_THEME_DEFAULT;
}

const cuif_theme* cuif_get_theme(void) {
    return g_current_theme;
}
