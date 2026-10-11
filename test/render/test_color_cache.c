/* Exercise the real renderer cache with a deterministic color allocator. */
#define XftColorAllocValue test_color_alloc
#define XftColorFree test_color_free
#include "../../src/xwin.c"

static int allocations, releases;
Bool test_color_alloc(Display *d, Visual *v, Colormap c,
                      const XRenderColor *value, XftColor *result) {
    (void)d; (void)v; (void)c;
    result->color = *value;
    result->pixel = (unsigned long)++allocations;
    return True;
}
void test_color_free(Display *d, Visual *v, Colormap c, XftColor *color) {
    (void)d; (void)v; (void)c; (void)color;
    releases++;
}
static void check(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "TEST FAILURE: %s\n", message); exit(1); }
}
static int equal_color(XRenderColor a, XRenderColor b) {
    return a.red == b.red && a.green == b.green && a.blue == b.blue && a.alpha == b.alpha;
}
static void check_color(Display *d, uint32_t logical, int bg, int faint) {
    XRenderColor expected = get_xrender_color(logical, bg, faint);
    XftColor *actual = get_xft_color(d, 0, logical, bg, faint);
    if (!equal_color(actual->color, expected))
        fprintf(stderr, "logical=%08x bg=%d faint=%d allocations=%d\n", logical, bg, faint, allocations);
    check(equal_color(actual->color, expected), "cache must return the current requested color");
}
int main(void) {
    _XPrivDisplay storage = calloc(1, sizeof(*storage));
    Screen screen = {0};
    Visual visual = {0};
    check(storage != NULL, "allocate display fixture");
    storage->screens = &screen;
    screen.root_visual = &visual;
    Display *d = (Display *)storage;

    for (int faint = 0; faint < 2; faint++) {
        check_color(d, 1, 0, faint);
        term.palette_overridden[1] = 1;
        term.palette_override[1] = TRUECOLOR(1, 2, 3);
        check_color(d, 1, 0, faint);
        term.palette_overridden[1] = 0;
        check_color(d, 1, 0, faint);
    }
    check_color(d, COLOR_DEFAULT_FG, 0, 0);
    check_color(d, COLOR_DEFAULT_BG, 1, 0);
    check_color(d, 256, 1, 0);
    term.osc_fg_color = TRUECOLOR(4, 5, 6);
    term.osc_bg_color = TRUECOLOR(7, 8, 9);
    term.osc_cs_color = TRUECOLOR(10, 11, 12);
    check_color(d, COLOR_DEFAULT_FG, 0, 0);
    check_color(d, COLOR_DEFAULT_BG, 1, 0);
    check_color(d, 256, 1, 0);
    term.osc_fg_color = term.osc_bg_color = term.osc_cs_color = 0;
    check_color(d, COLOR_DEFAULT_FG, 0, 0);
    check_color(d, COLOR_DEFAULT_BG, 1, 0);
    check_color(d, 256, 1, 0);

    for (int faint = 0; faint < 2; faint++) {
        for (unsigned i = 0; i < TC_HASH_SIZE * 3; i++) {
            uint32_t a = TRUECOLOR((i >> 8) & 255, i & 255, 23);
            uint32_t b = TRUECOLOR((i >> 8) & 255, i & 255, 47);
            XftColor *held = get_xft_color(d, 0, a, 0, faint);
            XRenderColor saved = held->color;
            check_color(d, b, 0, faint);
            check(equal_color(held->color, saved),
                "next lookup must not overwrite the previous color before drawing");
            check_color(d, a, 0, faint);
        }
    }
    for (unsigned variant = 0; variant < 2; variant++) {
        XftColor *held = get_xft_color(d, 0, TRUECOLOR(0, 0, 23), 0, (int)variant);
        XRenderColor saved = held->color;
        unsigned wanted = tc_last_slot[variant];
        uint32_t raw = 0x800000u;
        while (((raw * 2654435769u) >> 22) != wanted)
            raw++;
        check_color(d, TRUECOLOR((raw >> 16) & 255, (raw >> 8) & 255, raw & 255),
                    0, (int)variant);
        check(equal_color(held->color, saved),
            "colliding eviction must preserve the immediately previous lookup");
    }
    check(allocations - releases <= 2 * TC_HASH_SIZE + 2 * COLOR_CACHE_SIZE,
        "color allocations must remain bounded");
    free(storage);
    puts("PASS: render/color_cache");
    return 0;
}
