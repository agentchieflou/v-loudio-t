#include "cuif/cuif.h"
#include "cuif/widget.h"
#include "cuif/theme.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static bool colorsEqual(cuif_color a, cuif_color b) {
    const float eps = 1e-6f;
    return fabsf(a.r - b.r) < eps && fabsf(a.g - b.g) < eps &&
           fabsf(a.b - b.b) < eps && fabsf(a.a - b.a) < eps;
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

    tearDownTestWindow();

    printf("All cuif tests passed successfully!\n");
    return 0;
}
