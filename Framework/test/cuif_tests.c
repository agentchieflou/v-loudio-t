#include "cuif/cuif.h"
#include "cuif/widget.h"
#include "cuif/theme.h"
#include "cuif/theme_hello_kitty.h"
#include "cuif/theme_greens.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

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

    tearDownTestWindow();

    printf("All cuif tests passed successfully!\n");
    return 0;
}
