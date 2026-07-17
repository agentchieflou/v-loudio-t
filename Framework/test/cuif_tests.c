#include "cuif/cuif.h"
#include "cuif/widget.h"
#include "cuif/theme.h"
#include "cuif/theme_hello_kitty.h"
#include "cuif/theme_greens.h"
#include "cuif/cuif_dpi_utils.h"
#include "cuif/tessellation.h"
#include "cuif/graphics.h"
#include "cuif/font.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include <gl/gl.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool colorsEqual(cuif_color a, cuif_color b) {
    const float eps = 1e-6f;
    return fabsf(a.r - b.r) < eps && fabsf(a.g - b.g) < eps &&
           fabsf(a.b - b.b) < eps && fabsf(a.a - b.a) < eps;
}

/* Rough perceptual luminance -- good enough for a readability sanity check, not color-accurate. */
static float luminance(cuif_color c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

/*
 * Verifies a theme's text colors are readable against its own background --
 * i.e. the theme isn't internally broken (e.g. near-identical text and
 * background luminance, which would render as illegible). Shared by every
 * theme's test (default, Hello Kitty, Greens, ...) so a bad palette can't
 * ship silently.
 */
static void assertThemeIsReadable(const cuif_theme* theme, const char* theme_name) {
    float bg_lum = luminance(theme->background);
    float text_primary_lum = luminance(theme->text_primary);
    float text_secondary_lum = luminance(theme->text_secondary);

    float primary_contrast = fabsf(text_primary_lum - bg_lum);
    float secondary_contrast = fabsf(text_secondary_lum - bg_lum);

    if (primary_contrast < 0.3f) {
        printf("  FAIL: %s text_primary has insufficient contrast against background (%.3f)\n", theme_name, primary_contrast);
    }
    if (secondary_contrast < 0.15f) {
        printf("  FAIL: %s text_secondary has insufficient contrast against background (%.3f)\n", theme_name, secondary_contrast);
    }
    assert(primary_contrast >= 0.3f);
    assert(secondary_contrast >= 0.15f);
}

/*
 * Plain-C, assert()-based unit tests for the cuif framework, mirroring the
 * style of VSTs/Reverb/src/DSPTests.cpp. Grows one test function per Epic 8
 * issue as it lands, so the suite accumulates as a regression net across
 * the whole epic rather than being thrown away after each issue.
 *
 * Some tests (anything touching input dispatch) need a live cuif_window,
 * because cuif_widget_dispatch_mouse_down/up/move bail out immediately if
 * cuif_current_window is unset. We create one real (but hidden) top-level
 * window for the whole suite and reuse it -- this still requires no human
 * to look at anything, and no GPU-visible assertions are made.
 */

static cuif_window* g_test_window = NULL;

static int g_last_tab_change_index = -1;
static int g_tab_change_call_count = 0;

static void tabbarChangedCallback(cuif_widget* w, float value) {
    (void)w;
    g_last_tab_change_index = (int)value;
    g_tab_change_call_count++;
}

static void setUpTestWindow(void) {
    cuif_window_desc desc = { 0 };
    desc.title = "cuif_tests (hidden)";
    desc.width = 400;
    desc.height = 300;
    desc.parent_native_handle = NULL;

    g_test_window = cuif_window_create(&desc);
    assert(g_test_window != NULL);

    /* Establishes cuif_current_window, which dispatch functions require. */
    cuif_window_render_frame(g_test_window);
}

static void tearDownTestWindow(void) {
    if (g_test_window) {
        cuif_window_destroy(g_test_window);
        g_test_window = NULL;
    }
}

static void testTabbarCreateDefaults(void) {
    printf("Running testTabbarCreateDefaults...\n");

    static const char* labels[] = { "Main", "Advanced" };
    cuif_widget* tabbar = cuif_widget_create_tabbar(0.0f, 0.0f, 200.0f, 30.0f, labels, 2, NULL);

    assert(tabbar != NULL);
    assert(tabbar->type == CUIF_WIDGET_TABBAR);
    assert(tabbar->visible == true);
    assert(tabbar->enabled == true);
    assert(tabbar->u.tabbar.tab_count == 2);
    assert(tabbar->u.tabbar.selected_index == 0);
    assert(strcmp(tabbar->u.tabbar.tab_labels[0], "Main") == 0);
    assert(strcmp(tabbar->u.tabbar.tab_labels[1], "Advanced") == 0);

    cuif_widget_destroy(tabbar);
    printf("  Tabbar creation defaults are correct. Pass.\n");
}

static void testTabbarClickSelectsCorrectTab(void) {
    printf("Running testTabbarClickSelectsCorrectTab...\n");

    static const char* labels[] = { "Main", "Advanced", "Modulation" };
    /* 3 tabs, each 100px wide, spanning x=[0,300) at y=[0,30). */
    cuif_widget* tabbar = cuif_widget_create_tabbar(0.0f, 0.0f, 300.0f, 30.0f, labels, 3, tabbarChangedCallback);
    assert(tabbar != NULL);

    g_last_tab_change_index = -1;
    g_tab_change_call_count = 0;

    /* Click inside the 3rd tab (x in [200, 300)). */
    cuif_widget_dispatch_mouse_down(tabbar, 250.0f, 15.0f, 0);

    assert(tabbar->u.tabbar.selected_index == 2);
    assert(g_last_tab_change_index == 2);
    assert(g_tab_change_call_count == 1);

    cuif_widget_destroy(tabbar);
    printf("  Clicking a tab selects the correct index and fires on_change once. Pass.\n");
}

static void testTabbarClickingSelectedTabDoesNotRefire(void) {
    printf("Running testTabbarClickingSelectedTabDoesNotRefire...\n");

    static const char* labels[] = { "Main", "Advanced" };
    cuif_widget* tabbar = cuif_widget_create_tabbar(0.0f, 0.0f, 200.0f, 30.0f, labels, 2, tabbarChangedCallback);
    assert(tabbar != NULL);
    assert(tabbar->u.tabbar.selected_index == 0);

    g_tab_change_call_count = 0;

    /* Tab 0 is already selected by default -- clicking it again should be a no-op. */
    cuif_widget_dispatch_mouse_down(tabbar, 50.0f, 15.0f, 0);

    assert(tabbar->u.tabbar.selected_index == 0);
    assert(g_tab_change_call_count == 0);

    cuif_widget_destroy(tabbar);
    printf("  Clicking the already-selected tab does not refire on_change. Pass.\n");
}

static void testDefaultThemeIsActiveInitially(void) {
    printf("Running testDefaultThemeIsActiveInitially...\n");

    const cuif_theme* active = cuif_get_theme();
    assert(active != NULL);
    assert(colorsEqual(active->primary, CUIF_THEME_DEFAULT.primary));
    assert(colorsEqual(active->background, CUIF_THEME_DEFAULT.background));

    printf("  cuif_get_theme() returns CUIF_THEME_DEFAULT before any cuif_set_theme() call. Pass.\n");
}

static void testSetThemeChangesActiveTheme(void) {
    printf("Running testSetThemeChangesActiveTheme...\n");

    cuif_theme custom = CUIF_THEME_DEFAULT;
    custom.primary = cuif_rgb(1.0f, 0.0f, 0.0f);

    cuif_set_theme(&custom);
    const cuif_theme* active = cuif_get_theme();
    assert(colorsEqual(active->primary, cuif_rgb(1.0f, 0.0f, 0.0f)));
    assert(!colorsEqual(active->primary, CUIF_THEME_DEFAULT.primary));

    /* Restore so later tests in this suite see the default again. */
    cuif_set_theme(&CUIF_THEME_DEFAULT);
    printf("  cuif_set_theme() changes what cuif_get_theme() returns. Pass.\n");
}

static void testSetThemeNullFallsBackToDefault(void) {
    printf("Running testSetThemeNullFallsBackToDefault...\n");

    cuif_theme custom = CUIF_THEME_DEFAULT;
    custom.primary = cuif_rgb(0.0f, 1.0f, 0.0f);
    cuif_set_theme(&custom);
    assert(colorsEqual(cuif_get_theme()->primary, cuif_rgb(0.0f, 1.0f, 0.0f)));

    cuif_set_theme(NULL);
    assert(colorsEqual(cuif_get_theme()->primary, CUIF_THEME_DEFAULT.primary));

    printf("  cuif_set_theme(NULL) falls back to CUIF_THEME_DEFAULT. Pass.\n");
}

static void testDefaultThemeIsReadable(void) {
    printf("Running testDefaultThemeIsReadable...\n");
    assertThemeIsReadable(&CUIF_THEME_DEFAULT, "default");
    printf("  Default theme text has sufficient contrast against its own background. Pass.\n");
}

static void testHelloKittyThemeIsDistinctFromDefault(void) {
    printf("Running testHelloKittyThemeIsDistinctFromDefault...\n");

    assert(!colorsEqual(CUIF_THEME_HELLO_KITTY.background, CUIF_THEME_DEFAULT.background));
    assert(!colorsEqual(CUIF_THEME_HELLO_KITTY.primary, CUIF_THEME_DEFAULT.primary));
    assert(!colorsEqual(CUIF_THEME_HELLO_KITTY.panel_bg, CUIF_THEME_DEFAULT.panel_bg));

    printf("  Hello Kitty theme uses a genuinely different palette from the default. Pass.\n");
}

static void testHelloKittyThemeIsReadable(void) {
    printf("Running testHelloKittyThemeIsReadable...\n");
    assertThemeIsReadable(&CUIF_THEME_HELLO_KITTY, "Hello Kitty");
    printf("  Hello Kitty theme text has sufficient contrast against its own background. Pass.\n");
}

static void testHelloKittyThemeAppliesViaSetTheme(void) {
    printf("Running testHelloKittyThemeAppliesViaSetTheme...\n");

    cuif_set_theme(&CUIF_THEME_HELLO_KITTY);
    assert(colorsEqual(cuif_get_theme()->primary, CUIF_THEME_HELLO_KITTY.primary));

    cuif_set_theme(&CUIF_THEME_DEFAULT);
    printf("  cuif_set_theme(&CUIF_THEME_HELLO_KITTY) is reflected by cuif_get_theme(). Pass.\n");
}

static void testGreensThemeIsDistinctFromDefaultAndHelloKitty(void) {
    printf("Running testGreensThemeIsDistinctFromDefaultAndHelloKitty...\n");

    assert(!colorsEqual(CUIF_THEME_GREENS.background, CUIF_THEME_DEFAULT.background));
    assert(!colorsEqual(CUIF_THEME_GREENS.primary, CUIF_THEME_DEFAULT.primary));
    assert(!colorsEqual(CUIF_THEME_GREENS.background, CUIF_THEME_HELLO_KITTY.background));
    assert(!colorsEqual(CUIF_THEME_GREENS.primary, CUIF_THEME_HELLO_KITTY.primary));

    printf("  Greens theme uses a genuinely different palette from both other themes. Pass.\n");
}

static void testGreensThemeIsReadable(void) {
    printf("Running testGreensThemeIsReadable...\n");
    assertThemeIsReadable(&CUIF_THEME_GREENS, "Greens");
    printf("  Greens theme text has sufficient contrast against its own background. Pass.\n");
}

static void testGreensThemeAppliesViaSetTheme(void) {
    printf("Running testGreensThemeAppliesViaSetTheme...\n");

    cuif_set_theme(&CUIF_THEME_GREENS);
    assert(colorsEqual(cuif_get_theme()->primary, CUIF_THEME_GREENS.primary));

    cuif_set_theme(&CUIF_THEME_DEFAULT);
    printf("  cuif_set_theme(&CUIF_THEME_GREENS) is reflected by cuif_get_theme(). Pass.\n");
}

static void testDpiScaleIdentityAtScale1(void) {
    printf("Running testDpiScaleIdentityAtScale1...\n");

    assert(cuif_logical_to_physical_px(800, 1.0f) == 800);
    assert(cuif_logical_to_physical_px(0, 1.0f) == 0);
    assert(fabsf(cuif_physical_to_logical_px(800.0f, 1.0f) - 800.0f) < 1e-5f);

    printf("  scale=1.0 is an identity transform in both directions. Pass.\n");
}

static void testDpiScaleExactRoundingAtCommonScales(void) {
    printf("Running testDpiScaleExactRoundingAtCommonScales...\n");

    /* 1.5x (150% DPI, common on 1440p) */
    assert(cuif_logical_to_physical_px(800, 1.5f) == 1200);
    assert(cuif_logical_to_physical_px(801, 1.5f) == 1202); /* 1201.5 rounds to 1202 */

    /* 2.0x (200% DPI, typical 4K) */
    assert(cuif_logical_to_physical_px(800, 2.0f) == 1600);
    assert(cuif_logical_to_physical_px(600, 2.0f) == 1200);

    printf("  Common DPI scales (1.5x, 2.0x) produce exact expected physical pixel sizes. Pass.\n");
}

static void testDpiScaleRoundTripWithinOnePixel(void) {
    printf("Running testDpiScaleRoundTripWithinOnePixel...\n");

    const float scales[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f };
    const int logicalSizes[] = { 1, 17, 70, 110, 800, 599 };

    for (size_t s = 0; s < sizeof(scales) / sizeof(scales[0]); ++s) {
        for (size_t i = 0; i < sizeof(logicalSizes) / sizeof(logicalSizes[0]); ++i) {
            int physical = cuif_logical_to_physical_px(logicalSizes[i], scales[s]);
            float roundTripped = cuif_physical_to_logical_px((float)physical, scales[s]);
            float diff = fabsf(roundTripped - (float)logicalSizes[i]);
            if (diff >= 1.0f) {
                printf("  FAIL: logical=%d scale=%.2f -> physical=%d -> roundtrip=%.3f (diff=%.3f)\n",
                       logicalSizes[i], scales[s], physical, roundTripped, diff);
            }
            assert(diff < 1.0f);
        }
    }

    printf("  logical->physical->logical round-trips within 1px across common scales and sizes. Pass.\n");
}

static void testArcSegmentCountIsMonotonicWithRadius(void) {
    printf("Running testArcSegmentCountIsMonotonicWithRadius...\n");

    const float radii[] = { 2.0f, 5.0f, 10.0f, 22.0f, 35.0f, 60.0f, 100.0f, 300.0f };
    int prev = cuif_arc_segment_count(radii[0], (float)(1.5 * M_PI));
    for (size_t i = 1; i < sizeof(radii) / sizeof(radii[0]); ++i) {
        int cur = cuif_arc_segment_count(radii[i], (float)(1.5 * M_PI));
        assert(cur >= prev);
        prev = cur;
    }

    printf("  Segment count never decreases as radius grows. Pass.\n");
}

static void testArcSegmentCountClampsAtFloorAndCeiling(void) {
    printf("Running testArcSegmentCountClampsAtFloorAndCeiling...\n");

    /* Tiny/degenerate radius clamps to the floor, not zero or negative. */
    assert(cuif_arc_segment_count(0.01f, (float)(2.0 * M_PI)) == 8);
    assert(cuif_arc_segment_count(0.0f, (float)(2.0 * M_PI)) == 8);
    assert(cuif_arc_segment_count(-5.0f, (float)(2.0 * M_PI)) == 8);

    /* Huge radius clamps to the ceiling, doesn't blow up the vertex count unboundedly. */
    assert(cuif_arc_segment_count(5000.0f, (float)(2.0 * M_PI)) == 128);

    printf("  Segment count clamps correctly at both the small-radius floor and large-radius ceiling. Pass.\n");
}

static void testArcSegmentCountScalesWithDevicePixelDensity(void) {
    printf("Running testArcSegmentCountScalesWithDevicePixelDensity...\n");

    /* A knob's ~22px logical radius (70x70 secondary knobs, r = w*0.32) over its ~270-degree
       (1.5*pi) sweep, at logical (1x) vs. a 2x-DPI device-pixel radius -- this is the actual
       regression this issue exists to fix: the old fixed segment count (32) did not change at
       all as pixel density increased, which made faceting *more* visible on high-DPI displays,
       not less. */
    float logical_radius = 22.4f;
    float sweep = (float)(1.5 * M_PI);

    int segments_at_1x = cuif_arc_segment_count(logical_radius * 1.0f, sweep);
    int segments_at_2x = cuif_arc_segment_count(logical_radius * 2.0f, sweep);

    assert(segments_at_2x > segments_at_1x);
    /*
     * Old hardcoded value was 32 regardless of scale. Both the unscaled
     * (1x) and scaled (2x) cases must exceed it -- if only the 2x case
     * did, this issue would ship a *regression* at today's normal 1x
     * scale (the exact resolution the original faceted-knobs complaint
     * was reported at) in exchange for improving a scale nobody's on yet.
     */
    assert(segments_at_1x > 32);
    assert(segments_at_2x > 32);

    printf("  Segment count at 2x DPI scale (%d) is meaningfully higher than the old fixed 32 (1x: %d). Pass.\n",
           segments_at_2x, segments_at_1x);
}

static void testGenerateArcPointsStartsAndEndsAtCorrectAngles(void) {
    printf("Running testGenerateArcPointsStartsAndEndsAtCorrectAngles...\n");

    float points[CUIF_MAX_ARC_POINTS * 2];
    float cx = 100.0f, cy = 50.0f, r = 20.0f;
    float start_angle = 0.0f;
    float end_angle = (float)(0.5 * M_PI); /* quarter circle */

    int n = cuif_generate_arc_points(cx, cy, r, start_angle, end_angle, points, CUIF_MAX_ARC_POINTS);
    assert(n >= 2);

    float expected_start_x = cx + r * cosf(start_angle);
    float expected_start_y = cy + r * sinf(start_angle);
    float expected_end_x = cx + r * cosf(end_angle);
    float expected_end_y = cy + r * sinf(end_angle);

    assert(fabsf(points[0] - expected_start_x) < 1e-3f);
    assert(fabsf(points[1] - expected_start_y) < 1e-3f);
    assert(fabsf(points[(n - 1) * 2] - expected_end_x) < 1e-3f);
    assert(fabsf(points[(n - 1) * 2 + 1] - expected_end_y) < 1e-3f);

    /* Every generated point must actually lie on the circle (within float tolerance). */
    for (int i = 0; i < n; ++i) {
        float dx = points[i * 2] - cx;
        float dy = points[i * 2 + 1] - cy;
        float dist = sqrtf(dx * dx + dy * dy);
        assert(fabsf(dist - r) < 1e-3f);
    }

    printf("  Generated arc points start/end at the requested angles and all lie on the circle. Pass.\n");
}

static void testFontEffectiveBakePxIdentityAtScale1(void) {
    printf("Running testFontEffectiveBakePxIdentityAtScale1...\n");

    assert(fabsf(cuif_font_effective_bake_px(13.0f, 1.0f) - 13.0f) < 1e-5f);

    printf("  scale=1.0 leaves the design size unchanged. Pass.\n");
}

static void testFontEffectiveBakePxScalesWithDpi(void) {
    printf("Running testFontEffectiveBakePxScalesWithDpi...\n");

    assert(fabsf(cuif_font_effective_bake_px(13.0f, 2.0f) - 26.0f) < 1e-5f);
    assert(fabsf(cuif_font_effective_bake_px(13.0f, 1.5f) - 19.5f) < 1e-5f);

    printf("  Effective bake size scales linearly with dpi_scale. Pass.\n");
}

static void testFontEffectiveBakePxClampsAtCeiling(void) {
    printf("Running testFontEffectiveBakePxClampsAtCeiling...\n");

    /* A pathological scale factor must not request an unbounded atlas bake. */
    float effective = cuif_font_effective_bake_px(13.0f, 100.0f);
    assert(effective <= 128.0f);
    assert(effective > 0.0f);

    printf("  Effective bake size clamps at a sane ceiling (%.1fpx) for pathological scale factors. Pass.\n", effective);
}

/*
 * Regression test for #91 (closes #88): before the shared-root-GL-context
 * fix, every cuif_window got a fully independent GL object namespace with
 * no wglShareLists() anywhere, so a texture created while window A's
 * context was current did not exist in window B's context -- exactly the
 * bug that made cuif_global_font's texture render as blank/garbage in
 * every plugin instance after the first. This exercises the real driver,
 * not a mock: glIsTexture() would report GL_FALSE pre-fix.
 */
static void testGlObjectsShareAcrossTwoWindows(void) {
    printf("Running testGlObjectsShareAcrossTwoWindows...\n");

    cuif_window_desc desc = { 0 };
    desc.title = "cuif_tests share-context A";
    desc.width = 64;
    desc.height = 64;
    cuif_window* winA = cuif_window_create(&desc);
    assert(winA != NULL);

    desc.title = "cuif_tests share-context B";
    cuif_window* winB = cuif_window_create(&desc);
    assert(winB != NULL);

    /* Makes winA's context current, then create+upload a texture in it. */
    cuif_window_render_frame(winA);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 4, 4, 0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
    assert(glGetError() == GL_NO_ERROR);

    /* Switches the current context to winB's -- a different HGLRC. */
    cuif_window_render_frame(winB);
    assert(glIsTexture(tex) == GL_TRUE);

    glDeleteTextures(1, &tex);
    cuif_window_destroy(winA);
    cuif_window_destroy(winB);

    /* Restore the shared test window's context as current for subsequent tests. */
    cuif_window_render_frame(g_test_window);

    printf("  A texture created under one window's GL context is valid under another window's context. Pass.\n");
}

/*
 * Regression test for #92 (fixes reopen-in-place fuzzy/blank text).
 * Before this fix, cuif_global_font was left dangling (non-NULL, but its
 * texture_id pointed at a GL object in a now-destroyed context) after the
 * last window closed, so ensureCuifWindowCreated()'s "already loaded" guard
 * skipped reloading it on reopen. This directly exercises the invariant the
 * plugin editor's guard relies on: cuif_global_font must be NULL whenever
 * zero windows exist.
 */
static void testFontFreedWhenLastWindowCloses(void) {
    printf("Running testFontFreedWhenLastWindowCloses...\n");

    /* Temporarily close the suite's shared window so the count can reach zero. */
    tearDownTestWindow();

    cuif_window_desc desc = { 0 };
    desc.title = "cuif_tests font-lifecycle";
    desc.width = 64;
    desc.height = 64;
    cuif_window* win = cuif_window_create(&desc);
    assert(win != NULL);
    cuif_window_render_frame(win);

    cuif_global_font = cuif_font_load("C:\\Windows\\Fonts\\arial.ttf", 13.0f);
    assert(cuif_global_font != NULL);

    cuif_window_destroy(win); /* count -> 0: must free the font and reset the pointer. */
    assert(cuif_global_font == NULL);

    /* Restore the suite's shared window for any tests that run after this one. */
    setUpTestWindow();

    printf("  cuif_global_font is freed and reset to NULL once the last window closes. Pass.\n");
}

int main(void) {
    printf("==============================\n");
    printf("Starting cuif Framework Tests\n");
    printf("==============================\n");

    setUpTestWindow();

    testTabbarCreateDefaults();
    testTabbarClickSelectsCorrectTab();
    testTabbarClickingSelectedTabDoesNotRefire();

    testDefaultThemeIsActiveInitially();
    testSetThemeChangesActiveTheme();
    testSetThemeNullFallsBackToDefault();

    testDefaultThemeIsReadable();
    testHelloKittyThemeIsDistinctFromDefault();
    testHelloKittyThemeIsReadable();
    testHelloKittyThemeAppliesViaSetTheme();

    testGreensThemeIsDistinctFromDefaultAndHelloKitty();
    testGreensThemeIsReadable();
    testGreensThemeAppliesViaSetTheme();

    testDpiScaleIdentityAtScale1();
    testDpiScaleExactRoundingAtCommonScales();
    testDpiScaleRoundTripWithinOnePixel();

    testArcSegmentCountIsMonotonicWithRadius();
    testArcSegmentCountClampsAtFloorAndCeiling();
    testArcSegmentCountScalesWithDevicePixelDensity();
    testGenerateArcPointsStartsAndEndsAtCorrectAngles();

    testFontEffectiveBakePxIdentityAtScale1();
    testFontEffectiveBakePxScalesWithDpi();
    testFontEffectiveBakePxClampsAtCeiling();

    testGlObjectsShareAcrossTwoWindows();
    testFontFreedWhenLastWindowCloses();

    tearDownTestWindow();

    printf("All cuif tests passed successfully!\n");
    return 0;
}
