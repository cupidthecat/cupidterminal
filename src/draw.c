// draw.c
#define _POSIX_C_SOURCE 199309L
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <utf8proc.h> // unicode
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "draw.h"
#include "config.h"
#include "terminal_state.h"
#include "input.h"

#define MAX_LINES 100    // Maximum number of lines
#define MAX_CHARS 4096   // Increased to accommodate multi-byte UTF-8 characters

int line_count = 0;  // Track number of lines

// Static constants (LEFT_PAD/TOP_PAD must match DRAW_LEFT_PAD/DRAW_TOP_PAD in draw.h)
static const int LEFT_PAD = DRAW_LEFT_PAD;
static const int LINE_GAP = 0;  /* 0 for btop graph alignment; Braille/block need contiguous rows */
static int cell_selected(int r, int c);

// Global variables
XftDraw *xft_draw = NULL;
XftFont *xft_font = NULL;
XftFont *xft_font_bold = NULL;
XftFont *xft_font_italic = NULL;
XftFont *xft_font_bold_italic = NULL;
XftFont *xft_font_emoji = NULL;
XftColor xft_color;
XftColor xft_color_fg;   // white text
XftColor xft_color_bg;   // black background

Display *global_display = NULL;
Window global_window = 0;
int g_cell_w = 0;
int g_cell_h = 0;
int g_cell_gap = 0; // Remove artificial spacing

/* Double buffer to reduce flicker (btop and other TUIs) */
static Pixmap back_pixmap = None;
static XftDraw *xft_draw_buf = NULL;
static int back_w = 0, back_h = 0;

/* XIM (X Input Method) state – mirrors st's ximopen/ximinstantiate design */
static XIM g_xim = NULL;
XIC g_xic = NULL;
static XPoint g_xim_spot = {0, 0};

/*
 * Forward declaration: ximopen is registered as an XIDProc (instantiate callback)
 * and also called from ximdestroy, so declare it before those.
 */
static void ximopen(Display *dpy, XPointer unused1, XPointer unused2);

/*
 * IC destroy callback – called by XIM when the input context is destroyed.
 * XIM calls this as XIMProc(XIM, XPointer client, XPointer call).
 */
static void xicdestroy(XIM xim, XPointer client, XPointer call) {
    (void)xim; (void)call;
    Display *dpy = (Display *)client;
    g_xic = NULL;
    /* Re-register instantiate callback so we recover when the IM restarts */
    XRegisterIMInstantiateCallback(dpy, NULL, NULL, NULL,
                                   ximopen, (XPointer)dpy);
}

/*
 * IM instantiate callback – called by X when an IM becomes available.
 * Must match XIDProc = void(*)(Display*, XPointer, XPointer).
 */
static void ximopen(Display *dpy, XPointer unused1, XPointer unused2) {
    (void)unused1; (void)unused2;
    XIMCallback cb;
    XVaNestedList spot_list;

    if (g_xim)
        return; /* already open */

    g_xim = XOpenIM(dpy, NULL, NULL, NULL);
    if (!g_xim) {
        /* IM unavailable – leave g_xic NULL; fall back to XLookupString */
        return;
    }

    /* Register IC destroy callback so we can recreate it if IM is destroyed */
    cb.client_data = (XPointer)dpy;
    cb.callback    = xicdestroy;
    XSetIMValues(g_xim, XNDestroyCallback, &cb, NULL);

    spot_list = XVaCreateNestedList(0, XNSpotLocation, &g_xim_spot, NULL);
    g_xic = XCreateIC(g_xim,
                      XNInputStyle,        XIMPreeditNothing | XIMStatusNothing,
                      XNClientWindow,      global_window,
                      XNFocusWindow,       global_window,
                      XNPreeditAttributes, spot_list,
                      NULL);
    XFree(spot_list);

    if (!g_xic) {
        XCloseIM(g_xim);
        g_xim = NULL;
        return;
    }

    /* Successfully opened: unregister the instantiate callback */
    XUnregisterIMInstantiateCallback(dpy, NULL, NULL, NULL,
                                     ximopen, (XPointer)dpy);
}

/*
 * IM destroy callback – called when the IM server dies.
 * Must match XIMProc = void(*)(XIM, XPointer client, XPointer call).
 */
static void ximdestroy(XIM xim, XPointer client, XPointer call) {
    (void)xim; (void)call;
    Display *dpy = (Display *)client;
    g_xim = NULL;
    if (g_xic) {
        XDestroyIC(g_xic);
        g_xic = NULL;
    }
    /* Re-register instantiate callback to recover when IM restarts */
    XRegisterIMInstantiateCallback(dpy, NULL, NULL, NULL,
                                   ximopen, (XPointer)dpy);
}

/*
 * xximspot – update the XIC preedit spot to the current cursor position so
 * IME popup windows appear in the right place. Called after each draw_text().
 *
 * XSetICValues is an X11 round-trip; caching the last-sent position avoids
 * calling it on every frame when the cursor hasn't moved (e.g. during btop
 * redraws that don't move the cursor).
 */
void xximspot(Display *display, Window window) {
    static int prev_col = -1, prev_row = -1;
    XVaNestedList spot_list;
    const int baseline0 = xft_font ? (xft_font->ascent + DRAW_TOP_PAD) : DRAW_TOP_PAD;
    const int step_w    = g_cell_w + g_cell_gap;
    const int step_h    = g_cell_h + LINE_GAP;
    int cur_col, cur_row;

    (void)window;
    if (!g_xic || !display) return;

    cur_col = term_state.col;
    cur_row = term_state.row;

    /* Skip the round-trip if the cursor hasn't moved */
    if (cur_col == prev_col && cur_row == prev_row)
        return;
    prev_col = cur_col;
    prev_row = cur_row;

    g_xim_spot.x = (short)(LEFT_PAD + cur_col * step_w);
    g_xim_spot.y = (short)(baseline0 + cur_row * step_h);

    spot_list = XVaCreateNestedList(0, XNSpotLocation, &g_xim_spot, NULL);
    XSetICValues(g_xic, XNPreeditAttributes, spot_list, NULL);
    XFree(spot_list);
}

void xim_focus_in(void) {
    if (g_xic)
        XSetICFocus(g_xic);
}

void xim_focus_out(void) {
    if (g_xic)
        XUnsetICFocus(g_xic);
}

/* Cached window dimensions updated via draw_notify_resize() on ConfigureNotify.
   Avoids an XGetWindowAttributes() round-trip on every frame. */
static int cached_win_w = 0;
static int cached_win_h = 0;
static void (*font_change_hook)(Display *, Window) = NULL;

void draw_notify_resize(int w, int h) {
    cached_win_w = w;
    cached_win_h = h;
}

void xft_set_font_change_hook(void (*hook)(Display *display, Window window)) {
    font_change_hook = hook;
}

#define COLOR_CACHE_SIZE (COLOR_DEFAULT_BG + 1)
static XftColor color_cache[COLOR_CACHE_SIZE];
static int color_allocated[COLOR_CACHE_SIZE] = {0};
static XftColor faint_color_cache[COLOR_CACHE_SIZE];
static int faint_color_allocated[COLOR_CACHE_SIZE] = {0};
static int draw_full_refresh;

/* Open-addressing hash table for true-RGB XftColor allocation.
 * key == 0 is the empty-slot sentinel; no valid true-RGB key is 0 because
 * COLOR_TRUE_RGB_BASE = 0x01000000.  1024 slots → ~50% load for btop's
 * ~500 unique gradient colors, O(2) average probes vs the old O(256) scan. */
#define TC_HASH_SIZE 1024
#define TC_HASH_MASK (TC_HASH_SIZE - 1)
typedef struct { uint32_t key; XftColor color; } TcEntry;
static TcEntry tc_hash[TC_HASH_SIZE];
static TcEntry tc_faint_hash[TC_HASH_SIZE];

static int blink_hidden = 0;
static int blink_initialized = 0;
static struct timespec blink_last_toggle = {0, 0};

static void mark_all_rows_dirty_local(void) {
    if (!dirty_rows || term_rows <= 0) {
        return;
    }
    memset(dirty_rows, 1, (size_t)term_rows);
}

static void update_blink_state(void) {
    struct timespec now;
    unsigned int interval = blinktimeout;

    if (interval == 0) {
        if (blink_hidden) {
            blink_hidden = 0;
            mark_all_rows_dirty_local();
        }
        blink_initialized = 0;
        return;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return;
    }

    if (!blink_initialized) {
        blink_last_toggle = now;
        blink_initialized = 1;
        return;
    }

    {
        long long elapsed_ms = (now.tv_sec - blink_last_toggle.tv_sec) * 1000LL +
            (now.tv_nsec - blink_last_toggle.tv_nsec) / 1000000LL;
        if (elapsed_ms >= (long long)interval) {
            long long ticks = elapsed_ms / (long long)interval;
            if (ticks % 2LL != 0LL) {
                blink_hidden = !blink_hidden;
            }

            blink_last_toggle.tv_sec += (time_t)((ticks * interval) / 1000LL);
            blink_last_toggle.tv_nsec += (long)(((ticks * interval) % 1000LL) * 1000000LL);
            if (blink_last_toggle.tv_nsec >= 1000000000L) {
                blink_last_toggle.tv_sec += 1;
                blink_last_toggle.tv_nsec -= 1000000000L;
            }

            mark_all_rows_dirty_local();
        }
    }
}

/* Open-addressing hash table for glyph fallback font lookup.
 * occupied == 0 is the empty-slot sentinel (zero-initialized by static storage).
 * font == NULL with occupied == 1 means "tried, no font found" – the miss is
 * cached to avoid repeated FcFontMatch calls for unsupported codepoints.
 * 2048 slots handles 256 braille + 128 box-drawing + block elements with room. */
#define GF_HASH_SIZE 2048
#define GF_HASH_MASK (GF_HASH_SIZE - 1)
typedef struct {
    utf8proc_int32_t cp;
    uint8_t          style;
    uint8_t          occupied;
    XftFont         *font;
} GfEntry;
static GfEntry gf_hash[GF_HASH_SIZE];

/*
 * st keeps Font.pattern = configured (pre-match query pattern) and uses
 * FcFontSort + FcFontSetMatch for missing glyphs (st/x.c xmakeglyphfontspecs).
 * XftFont->pattern alone is only the matched face; FcFontMatch(charset) on that
 * cannot walk the same fallback list — causes tofu boxes.  g_fc_pat[] mirrors
 * st's per-style patterns; g_fc_set[] is FcFontSort(...) lazily.
 */
static FcPattern *g_fc_pat[4];
static FcFontSet *g_fc_set[4];

static void fc_fallback_teardown(void) {
    for (int i = 0; i < 4; i++) {
        if (g_fc_set[i]) {
            FcFontSetDestroy(g_fc_set[i]);
            g_fc_set[i] = NULL;
        }
        if (g_fc_pat[i]) {
            FcPatternDestroy(g_fc_pat[i]);
            g_fc_pat[i] = NULL;
        }
    }
}

static FcPattern *pattern_for_glyph_fallback(uint8_t style) {
    if (style < 4 && g_fc_pat[style])
        return g_fc_pat[style];
    if (style == 3) {
        if (g_fc_pat[3]) return g_fc_pat[3];
        if (g_fc_pat[1]) return g_fc_pat[1];
        if (g_fc_pat[2]) return g_fc_pat[2];
        return g_fc_pat[0];
    }
    if (style == 2 && !g_fc_pat[2])
        return g_fc_pat[0];
    if (style == 1 && !g_fc_pat[1])
        return g_fc_pat[0];
    return g_fc_pat[0];
}

static void clear_glyph_fallback_cache(Display *display) {
    for (int i = 0; i < GF_HASH_SIZE; i++) {
        if (gf_hash[i].occupied) {
            if (display && gf_hash[i].font)
                XftFontClose(display, gf_hash[i].font);
            gf_hash[i].occupied = 0;
            gf_hash[i].font = NULL;
        }
    }
}

static uint8_t font_style_key(uint16_t attrs) {
    uint8_t key = 0;
    if (attrs & ATTR_BOLD)   key |= 1;
    if (attrs & ATTR_ITALIC) key |= 2;
    return key;
}

/* Returns 1 if (cp,style) is in the hash table (font_out set, may be NULL for
 * a cached miss), 0 if not yet tried. */
static int find_glyph_fallback(utf8proc_int32_t cp, uint8_t style, XftFont **font_out) {
    uint32_t k = ((uint32_t)cp ^ ((uint32_t)cp >> 8)) * 2654435769u ^ (uint32_t)style;
    unsigned slot = (k >> 21) & GF_HASH_MASK;
    unsigned probe;
    for (probe = 0; probe < GF_HASH_SIZE; probe++) {
        unsigned s = (slot + probe) & GF_HASH_MASK;
        if (!gf_hash[s].occupied) { *font_out = NULL; return 0; }
        if (gf_hash[s].cp == cp && gf_hash[s].style == style) {
            *font_out = gf_hash[s].font;
            return 1;
        }
    }
    *font_out = NULL;
    return 0;
}

static void insert_glyph_fallback(utf8proc_int32_t cp, uint8_t style, XftFont *font) {
    uint32_t k = ((uint32_t)cp ^ ((uint32_t)cp >> 8)) * 2654435769u ^ (uint32_t)style;
    unsigned slot = (k >> 21) & GF_HASH_MASK;
    unsigned probe;
    for (probe = 0; probe < GF_HASH_SIZE; probe++) {
        unsigned s = (slot + probe) & GF_HASH_MASK;
        if (!gf_hash[s].occupied || (gf_hash[s].cp == cp && gf_hash[s].style == style)) {
            gf_hash[s].cp       = cp;
            gf_hash[s].style    = style;
            gf_hash[s].font     = font;
            gf_hash[s].occupied = 1;
            return;
        }
    }
    /*
     * Table full: skip caching.
     * Do not close FONT here because caller may still draw with it.
     * (st uses a growable fallback cache; this fixed-size table must
     * preserve correctness over aggressive reclamation.)
     */
}

static XftFont *load_glyph_fallback(Display *display, uint8_t style, utf8proc_int32_t cp) {
    FcPattern *base_pat;
    FcPattern *fcpattern;
    FcPattern *fontpattern;
    FcCharSet *fccharset;
    FcFontSet *fcsets[1];
    FcResult fcres;
    XftFont *font = NULL;

    if (!display || cp < 0)
        return NULL;

    base_pat = pattern_for_glyph_fallback(style);
    if (!base_pat)
        return NULL;

    if (!g_fc_set[style]) {
        g_fc_set[style] = FcFontSort(NULL, base_pat, FcTrue, NULL, &fcres);
        if (!g_fc_set[style] && base_pat != g_fc_pat[0] && g_fc_pat[0])
            g_fc_set[style] = FcFontSort(NULL, g_fc_pat[0], FcTrue, NULL, &fcres);
    }
    if (!g_fc_set[style])
        return NULL;

    fcpattern = FcPatternDuplicate(base_pat);
    if (!fcpattern)
        return NULL;

    fccharset = FcCharSetCreate();
    if (!fccharset) {
        FcPatternDestroy(fcpattern);
        return NULL;
    }

    FcCharSetAddChar(fccharset, (FcChar32)cp);
    FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
    FcPatternAddBool(fcpattern, FC_SCALABLE, FcTrue);
    FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
    FcDefaultSubstitute(fcpattern);

    fcsets[0] = g_fc_set[style];
    fontpattern = FcFontSetMatch(NULL, fcsets, 1, fcpattern, &fcres);

    FcPatternDestroy(fcpattern);
    FcCharSetDestroy(fccharset);

    if (!fontpattern)
        return NULL;

    font = XftFontOpenPattern(display, fontpattern);
    if (!font)
        return NULL;

    if (XftCharIndex(display, font, (FcChar32)cp) == 0) {
        XftFontClose(display, font);
        return NULL;
    }

    return font;
}

static unsigned short
u8_to_u16(unsigned int x)
{
    return (unsigned short)(x * 0x101u);
}

static unsigned short
sixd_to_16bit(int x)
{
    return (unsigned short)(x == 0 ? 0 : (0x3737 + 0x2828 * x));
}

static int parse_config_color(uint32_t idx, XRenderColor *out)
{
    XColor xc;

    if (!out || !global_display) {
        return 0;
    }
    if ((size_t)idx >= LEN(colorname) || colorname[idx] == NULL || colorname[idx][0] == '\0') {
        return 0;
    }

    if (!XParseColor(global_display,
            DefaultColormap(global_display, DefaultScreen(global_display)),
            colorname[idx], &xc)) {
        return 0;
    }

    out->red = xc.red;
    out->green = xc.green;
    out->blue = xc.blue;
    out->alpha = 0xFFFF;
    return 1;
}

static XRenderColor get_xrender_color(uint32_t c, int is_bg, int is_faint) {
    XRenderColor xc;

    /* Apply OSC dynamic-color overrides */
    if (c == COLOR_DEFAULT_FG && term_state.osc_fg_color)
        c = term_state.osc_fg_color;
    else if (c == COLOR_DEFAULT_BG && term_state.osc_bg_color)
        c = term_state.osc_bg_color;
    else if (c == 256 && term_state.osc_cs_color) /* cursor */
        c = term_state.osc_cs_color;
    /* Apply OSC 4 per-index palette overrides */
    else if (c < 256 && term_state.palette_overridden[c])
        c = term_state.palette_override[c];

    if (COLOR_IS_TRUE_RGB(c)) {
        unsigned int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        xc = (XRenderColor){
            (unsigned short)(r * 0x101),
            (unsigned short)(g * 0x101),
            (unsigned short)(b * 0x101),
            0xFFFF
        };
    } else if (parse_config_color(c, &xc)) {
        /* st-compatible: prefer explicit colorname[] entries when present. */
    } else if (c < 16) {
        static const unsigned char ansi[16][3] = {
            {0x00, 0x00, 0x00}, /* black */
            {0xCD, 0x00, 0x00}, /* red3 */
            {0x00, 0xCD, 0x00}, /* green3 */
            {0xCD, 0xCD, 0x00}, /* yellow3 */
            {0x00, 0x00, 0xEE}, /* blue2 */
            {0xCD, 0x00, 0xCD}, /* magenta3 */
            {0x00, 0xCD, 0xCD}, /* cyan3 */
            {0xE5, 0xE5, 0xE5}, /* gray90 */
            {0x7F, 0x7F, 0x7F}, /* gray50 */
            {0xFF, 0x00, 0x00}, /* red */
            {0x00, 0xFF, 0x00}, /* green */
            {0xFF, 0xFF, 0x00}, /* yellow */
            {0x5C, 0x5C, 0xFF}, /* #5c5cff */
            {0xFF, 0x00, 0xFF}, /* magenta */
            {0x00, 0xFF, 0xFF}, /* cyan */
            {0xFF, 0xFF, 0xFF}, /* white */
        };
        xc = (XRenderColor){
            u8_to_u16(ansi[c][0]),
            u8_to_u16(ansi[c][1]),
            u8_to_u16(ansi[c][2]),
            0xFFFF
        };
    } else if (c >= 16 && c <= 231) {
        int idx = c - 16;
        int r = idx / 36, g = (idx / 6) % 6, b = idx % 6;
        xc = (XRenderColor){
            sixd_to_16bit(r),
            sixd_to_16bit(g),
            sixd_to_16bit(b),
            0xFFFF
        };
    } else if (c >= 232 && c <= 255) {
        unsigned short v = (unsigned short)(0x0808 + (c - 232) * 0x0A0A);
        xc = (XRenderColor){v, v, v, 0xFFFF};
    } else if (c == 256) {
        xc = (XRenderColor){0xCCCC, 0xCCCC, 0xCCCC, 0xFFFF}; /* cursor color */
    } else if (c == 257) {
        xc = (XRenderColor){0x5555, 0x5555, 0x5555, 0xFFFF}; /* reverse cursor */
    } else if (c == 258) {
        xc = (XRenderColor){0xE5E5, 0xE5E5, 0xE5E5, 0xFFFF}; /* default fg */
    } else if (c == 259) {
        xc = (XRenderColor){0x0000, 0x0000, 0x0000, 0xFFFF}; /* default bg */
    } else {
        uint32_t fallback = is_bg ? defaultbg : defaultfg;
        if (fallback != c && fallback <= COLOR_DEFAULT_BG) {
            xc = get_xrender_color(fallback, is_bg, 0);
        } else {
            xc = is_bg ? (XRenderColor){0x0000, 0x0000, 0x0000, 0xFFFF}
                       : (XRenderColor){0xE5E5, 0xE5E5, 0xE5E5, 0xFFFF};
        }
    }

    if (is_faint && !is_bg) {
        xc.red /= 2; xc.green /= 2; xc.blue /= 2;
    }
    return xc;
}

static XftColor *get_xft_color(Display *d, Window w, uint32_t logical_color, int is_bg, int is_faint) {
    size_t idx;

    (void)w;
    if (logical_color > COLOR_DEFAULT_BG && !COLOR_IS_TRUE_RGB(logical_color)) {
        return is_bg ? &xft_color_bg : &xft_color_fg;
    }

    if (COLOR_IS_TRUE_RGB(logical_color)) {
        /* O(1) open-addressing hash lookup (Fibonacci hashing on the 24-bit RGB). */
        TcEntry *table = (is_faint && !is_bg) ? tc_faint_hash : tc_hash;
        unsigned slot = ((logical_color & 0xFFFFFFu) * 2654435769u) >> 22; /* top 10 bits */
        unsigned probe;
        for (probe = 0; probe < TC_HASH_SIZE; probe++) {
            unsigned s = (slot + probe) & TC_HASH_MASK;
            if (table[s].key == logical_color) return &table[s].color;
            if (table[s].key == 0) {
                XRenderColor rc = get_xrender_color(logical_color, is_bg, is_faint);
                if (XftColorAllocValue(d, DefaultVisual(d, DefaultScreen(d)),
                        DefaultColormap(d, DefaultScreen(d)), &rc, &table[s].color)) {
                    table[s].key = logical_color;
                    return &table[s].color;
                }
                break;
            }
        }
        return is_bg ? &xft_color_bg : &xft_color_fg;
    }

    if (logical_color > COLOR_DEFAULT_BG) {
        return is_bg ? &xft_color_bg : &xft_color_fg;
    }

    idx = (size_t)logical_color;

    if (is_faint && !is_bg) {
        if (!faint_color_allocated[idx]) {
            XRenderColor rc = get_xrender_color(logical_color, is_bg, is_faint);
            if (!XftColorAllocValue(d, DefaultVisual(d, DefaultScreen(d)),
                DefaultColormap(d, DefaultScreen(d)), &rc, &faint_color_cache[idx])) {
                return &xft_color_fg;
            }
            faint_color_allocated[idx] = 1;
        }
        return &faint_color_cache[idx];
    } else {
        if (!color_allocated[idx]) {
            XRenderColor rc = get_xrender_color(logical_color, is_bg, 0);
            if (!XftColorAllocValue(d, DefaultVisual(d, DefaultScreen(d)),
                DefaultColormap(d, DefaultScreen(d)), &rc, &color_cache[idx])) {
                return is_bg ? &xft_color_bg : &xft_color_fg;
            }
            color_allocated[idx] = 1;
        }
        return &color_cache[idx];
    }
}

/*
 * Like st's xloadfont: keep the configured (pre-match) pattern for FcFontSort /
 * FcFontSetMatch.  If out_configured_keep is NULL, the configured pattern is freed.
 */
static int open_xft_font(Display *display, FcPattern *pattern, XftFont **out_font,
                         FcPattern **out_configured_keep) {
    FcPattern *configured;
    FcPattern *match;
    FcResult result;

    *out_font = NULL;
    if (out_configured_keep)
        *out_configured_keep = NULL;

    configured = FcPatternDuplicate(pattern);
    if (!configured)
        return -1;

    FcConfigSubstitute(NULL, configured, FcMatchPattern);
    XftDefaultSubstitute(display, DefaultScreen(display), configured);

    match = FcFontMatch(NULL, configured, &result);
    if (!match) {
        FcPatternDestroy(configured);
        return -1;
    }

    *out_font = XftFontOpenPattern(display, match);
    if (!*out_font) {
        FcPatternDestroy(configured);
        FcPatternDestroy(match);
        return -1;
    }

    if (out_configured_keep)
        *out_configured_keep = configured;
    else
        FcPatternDestroy(configured);
    return 0;
}

/*
 * Load primary + bold/italic/bold-italic + emoji from a full Fontconfig name string,
 * mirroring st's xloadfonts / xloadfont (st/x.c). fontsize_override > 1 forces FC_PIXEL_SIZE.
 * Returns 0 on success; on failure frees any opened fonts and returns -1.
 */
static int load_font_set(Display *display, const char *fontstr, double fontsize_override,
                         XftFont **out_reg, XftFont **out_bold, XftFont **out_italic,
                         XftFont **out_bold_italic, XftFont **out_emoji) {
    FcPattern *pattern = NULL;
    double fontval;
    XftFont *nf = NULL, *nb = NULL, *ni = NULL, *nbi = NULL, *ne = NULL;
    char emoji_buf[160];
    FcPattern *epat;
    FcResult eres;

    *out_reg = *out_bold = *out_italic = *out_bold_italic = *out_emoji = NULL;

    if (!fontstr || !fontstr[0])
        fontstr = "monospace:pixelsize=12";

    if (fontstr[0] == '-')
        pattern = XftXlfdParse(fontstr, False, False);
    else
        pattern = FcNameParse((const FcChar8 *)fontstr);

    if (!pattern) {
        fprintf(stderr, "cupidterminal: can't parse font \"%s\"\n", fontstr);
        return -1;
    }

    {
        FcPattern *pat_reg = NULL, *pat_bold = NULL, *pat_italic = NULL, *pat_bi = NULL;

        if (fontsize_override > 1.0) {
            FcPatternDel(pattern, FC_PIXEL_SIZE);
            FcPatternDel(pattern, FC_SIZE);
            FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontsize_override);
            usedfontsize = fontsize_override;
        } else {
            if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval) == FcResultMatch) {
                usedfontsize = fontval;
            } else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontval) == FcResultMatch) {
                usedfontsize = -1.0;
            } else {
                FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12.0);
                usedfontsize = 12.0;
            }
            defaultfontsize = usedfontsize;
        }

        if (open_xft_font(display, pattern, &nf, &pat_reg) != 0) {
            FcPatternDestroy(pattern);
            return -1;
        }

        if (usedfontsize < 0.0) {
            if (FcPatternGetDouble(nf->pattern, FC_PIXEL_SIZE, 0, &fontval) == FcResultMatch) {
                usedfontsize = fontval;
                if (fontsize_override <= 1.0)
                    defaultfontsize = fontval;
            }
        }

        /* Italic — same mutation order as st xloadfonts */
        FcPatternDel(pattern, FC_SLANT);
        FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
        if (open_xft_font(display, pattern, &ni, &pat_italic) != 0) {
            ni = nf;
            pat_italic = NULL;
        }

        FcPatternDel(pattern, FC_WEIGHT);
        FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
        if (open_xft_font(display, pattern, &nbi, &pat_bi) != 0) {
            nbi = (ni != nf) ? ni : nf;
            pat_bi = NULL;
        }

        FcPatternDel(pattern, FC_SLANT);
        FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
        if (open_xft_font(display, pattern, &nb, &pat_bold) != 0) {
            nb = nf;
            pat_bold = NULL;
        }

        FcPatternDestroy(pattern);
        pattern = NULL;

        /* Only after new faces load: old patterns would break FcFontSetMatch if reload fails mid-way. */
        fc_fallback_teardown();
        g_fc_pat[0] = pat_reg;
        g_fc_pat[1] = pat_bold;
        g_fc_pat[2] = pat_italic;
        g_fc_pat[3] = pat_bi;
    }

    {
        int psz = (int)(usedfontsize > 0.0 ? usedfontsize + 0.5 : 12);
        if (psz < 6) psz = 6;
        if (psz > 256) psz = 256;
        snprintf(emoji_buf, sizeof(emoji_buf), "Noto Color Emoji:pixelsize=%d", psz);
        epat = FcNameParse((const FcChar8 *)emoji_buf);
        if (epat) {
            FcConfigSubstitute(NULL, epat, FcMatchPattern);
            XftDefaultSubstitute(display, DefaultScreen(display), epat);
            ne = XftFontOpenPattern(display, FcFontMatch(NULL, epat, &eres));
            FcPatternDestroy(epat);
        }
        if (!ne)
            ne = nf;
    }

    *out_reg = nf;
    *out_bold = nb;
    *out_italic = ni;
    *out_bold_italic = nbi;
    *out_emoji = ne;
    return 0;
}

static void free_font_set(Display *display, XftFont *nf, XftFont *nb, XftFont *ni,
                          XftFont *nbi, XftFont *ne) {
    if (ne && ne != nf && ne != nb && ne != ni && ne != nbi)
        XftFontClose(display, ne);
    if (nbi && nbi != nf && nbi != nb && nbi != ni)
        XftFontClose(display, nbi);
    if (ni && ni != nf)
        XftFontClose(display, ni);
    if (nb && nb != nf)
        XftFontClose(display, nb);
    if (nf)
        XftFontClose(display, nf);
}

static void recompute_cell_metrics(Display *display) {
    static const char ascii_printable[] =
        " !\"#$%&'()*+,-./0123456789:;<=>?"
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
        "`abcdefghijklmnopqrstuvwxyz{|}~";
    XGlyphInfo ext_ascii;
    int w_ascii = 0;
    size_t ascii_len = strlen(ascii_printable);

    if (ascii_len > 0) {
        XftTextExtentsUtf8(display, xft_font,
                           (const FcChar8 *)ascii_printable,
                           (int)ascii_len, &ext_ascii);
        if (ext_ascii.xOff > 0) {
            w_ascii = (int)((ext_ascii.xOff + (int)ascii_len - 1) / (int)ascii_len);
        }
    }

    g_cell_w = w_ascii;
    if (g_cell_w <= 0) {
        XGlyphInfo ext;
        XftTextExtentsUtf8(display, xft_font, (const FcChar8 *)"0", 1, &ext);
        g_cell_w = ext.xOff;
    }

    if (g_cell_w <= 0) g_cell_w = xft_font->max_advance_width;
    if (g_cell_w <= 0) g_cell_w = 8;

    {
        int base_w = g_cell_w;
        int base_h = xft_font->ascent + xft_font->descent;
        int scaled_w = (int)(base_w * cwscale + 0.999f);
        int scaled_h = (int)(base_h * chscale + 0.999f);
        g_cell_w = (scaled_w > 0) ? scaled_w : 1;
        g_cell_h = (scaled_h > 0) ? scaled_h : 1;
    }
}

// Initialize Xft for Unicode and emoji support
void initialize_xft(Display *display, Window window) {
    XftFont *nf, *nb, *ni, *nbi, *ne;

    global_display = display;
    global_window = window;
    clear_glyph_fallback_cache(display);

    // Initialize XftDraw
    xft_draw = XftDrawCreate(display, window,
        DefaultVisual(display, DefaultScreen(display)),
        DefaultColormap(display, DefaultScreen(display)));
    if (!xft_draw) {
        fprintf(stderr, "Failed to create XftDraw.\n");
        exit(EXIT_FAILURE);
    }

    /* Full Fontconfig string + size from pattern (st xloadfonts with fontsize 0). */
    if (load_font_set(display, FONT, 0.0, &nf, &nb, &ni, &nbi, &ne) != 0) {
        fprintf(stderr, "cupidterminal: can't open font \"%s\", trying DejaVu Sans Mono\n", FONT);
        if (load_font_set(display, "DejaVu Sans Mono:pixelsize=12:antialias=true", 12.0,
                          &nf, &nb, &ni, &nbi, &ne) != 0) {
            fprintf(stderr, "cupidterminal: failed to load fallback font.\n");
            exit(EXIT_FAILURE);
        }
        /* Forced pixel size path does not set defaultfontsize — needed for zoom reset. */
        defaultfontsize = usedfontsize;
    }

    xft_font = nf;
    xft_font_bold = nb;
    xft_font_italic = ni;
    xft_font_bold_italic = nbi;
    xft_font_emoji = ne;

    // Allocate default foreground/background with st-compatible indices.
    XRenderColor rc_white = get_xrender_color(COLOR_DEFAULT_FG, 0, 0);
    if (!XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
            DefaultColormap(display, DefaultScreen(display)),
            &rc_white, &xft_color_fg)) {
        fprintf(stderr, "Failed to allocate white color.\n");
        exit(EXIT_FAILURE);
    }

    XRenderColor rc_black = get_xrender_color(COLOR_DEFAULT_BG, 1, 0);
    if (!XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
            DefaultColormap(display, DefaultScreen(display)),
            &rc_black, &xft_color_bg)) {
        fprintf(stderr, "Failed to allocate black color.\n");
        exit(EXIT_FAILURE);
    }

    // Prepopulate defaults into cache
    color_cache[COLOR_DEFAULT_FG] = xft_color_fg;
    color_allocated[COLOR_DEFAULT_FG] = 1;
    color_cache[COLOR_DEFAULT_BG] = xft_color_bg;
    color_allocated[COLOR_DEFAULT_BG] = 1;

    recompute_cell_metrics(display);

    /* IMPORTANT: initialize with the logic state */
    initialize_terminal_state(&term_state);

    /* Open XIM (or register a callback for when IM becomes available) */
    if (!XSupportsLocale()) {
        fprintf(stderr, "warning: X does not support locale\n");
    } else {
        XIMCallback cb;
        ximopen(display, NULL, NULL);
        if (!g_xim) {
            /* IM not yet available; get notified when it starts */
            XRegisterIMInstantiateCallback(display, NULL, NULL, NULL,
                                           ximopen, (XPointer)display);
        } else {
            /* Register destroy callback so we re-open if IM server dies */
            cb.client_data = (XPointer)display;
            cb.callback    = ximdestroy;
            XSetIMValues(g_xim, XNDestroyCallback, &cb, NULL);
        }
    }
}

static int xft_reload_fonts(Display *display) {
    double sz = usedfontsize;
    XftFont *nf, *nb, *ni, *nbi, *ne;
    XftFont *of = xft_font, *ob = xft_font_bold, *oi = xft_font_italic,
            *obi = xft_font_bold_italic, *oe = xft_font_emoji;

    if (sz < minfontsize) sz = minfontsize;
    if (sz > maxfontsize) sz = maxfontsize;

    clear_glyph_fallback_cache(display);

    /* st xloadfonts: fontsize > 1 forces FC_PIXEL_SIZE */
    if (load_font_set(display, FONT, sz, &nf, &nb, &ni, &nbi, &ne) != 0)
        return -1;

    xft_font = nf;
    xft_font_bold = nb;
    xft_font_italic = ni;
    xft_font_bold_italic = nbi;
    xft_font_emoji = ne;

    free_font_set(display, of, ob, oi, obi, oe);

    recompute_cell_metrics(display);
    draw_full_refresh = 1;
    mark_all_rows_dirty_local();
    return 0;
}

void xft_zoom(Display *display, Window window, float delta) {
    usedfontsize += delta;
    if (usedfontsize < minfontsize) usedfontsize = minfontsize;
    if (usedfontsize > maxfontsize) usedfontsize = maxfontsize;
    if (xft_reload_fonts(display) == 0 && font_change_hook)
        font_change_hook(display, window);
}

void xft_zoom_reset(Display *display, Window window) {
    usedfontsize = (defaultfontsize > 0.0) ? defaultfontsize : usedfontsize;
    if (usedfontsize < minfontsize) usedfontsize = minfontsize;
    if (usedfontsize > maxfontsize) usedfontsize = maxfontsize;
    if (xft_reload_fonts(display) == 0 && font_change_hook)
        font_change_hook(display, window);
}

// Cleanup Xft resources
void cleanup_xft(void) {
    if (g_xic) { XDestroyIC(g_xic); g_xic = NULL; }
    if (g_xim) { XCloseIM(g_xim);   g_xim = NULL; }
    clear_glyph_fallback_cache(global_display);
    fc_fallback_teardown();
    if (xft_draw_buf) { XftDrawDestroy(xft_draw_buf); xft_draw_buf = NULL; }
    if (back_pixmap != None && global_display) {
        XFreePixmap(global_display, back_pixmap);
        back_pixmap = None;
    }
    if (xft_draw) {
        XftDrawDestroy(xft_draw);
    }
    if (xft_font) {
        XftFontClose(global_display, xft_font);
    }
    if (xft_font_bold && xft_font_bold != xft_font) {
        XftFontClose(global_display, xft_font_bold);
    }
    if (xft_font_italic && xft_font_italic != xft_font) {
        XftFontClose(global_display, xft_font_italic);
    }
    if (xft_font_bold_italic && xft_font_bold_italic != xft_font_bold && xft_font_bold_italic != xft_font_italic) {
        XftFontClose(global_display, xft_font_bold_italic);
    }
    if (xft_font_emoji && xft_font_emoji != xft_font) {
        XftFontClose(global_display, xft_font_emoji);
    }
}

static XftFont *font_for_cell(uint16_t attrs, utf8proc_int32_t cp) {
    XftFont *font_to_use = xft_font;
    uint8_t style = font_style_key(attrs);

    if ((attrs & ATTR_BOLD) && (attrs & ATTR_ITALIC)) {
        font_to_use = xft_font_bold_italic ? xft_font_bold_italic : xft_font;
    } else if (attrs & ATTR_BOLD) {
        font_to_use = xft_font_bold ? xft_font_bold : xft_font;
    } else if (attrs & ATTR_ITALIC) {
        font_to_use = xft_font_italic ? xft_font_italic : xft_font;
    }

    /*
     * Prefer style font first. If it lacks the glyph, fall back to emoji font,
     * then to a fontconfig-matched fallback (cached to avoid repeated FcFontMatch).
     */
    if (cp >= 0) {
        /* ASCII fast path: printable ASCII is always in the configured monospace font.
         * Skips XftCharIndex for ~80% of terminal cells with zero overhead. */
        if (cp >= 0x20 && cp <= 0x7E) return font_to_use;

        if (font_to_use && XftCharIndex(global_display, font_to_use, (FcChar32)cp) != 0) {
            return font_to_use;
        }
        if (xft_font_emoji && xft_font_emoji != font_to_use &&
            XftCharIndex(global_display, xft_font_emoji, (FcChar32)cp) != 0) {
            return xft_font_emoji;
        }

        {
            XftFont *cached;
            int in_cache = find_glyph_fallback(cp, style, &cached);
            if (in_cache) {
                return cached ? cached : font_to_use; /* cached miss → use default */
            }
        }

        /* Not cached yet: FcFontSetMatch like st (not FcFontMatch on the matched face only). */
        {
            XftFont *fallback = load_glyph_fallback(global_display, style, cp);
            insert_glyph_fallback(cp, style, fallback); /* caches both hits and misses */
            if (fallback) return fallback;
        }
    }

    return font_to_use;
}

static void resolve_cell_colors(uint32_t in_fg, uint32_t in_bg, uint16_t attrs, int selected,
                                int hide_blink,
                                uint32_t *out_fg, uint32_t *out_bg) {
    uint32_t fg = in_fg;
    uint32_t bg = in_bg;

    /* st behavior: bold brightens basic ANSI colors 0-7 when faint is not active. */
    if ((attrs & ATTR_BOLD) && !(attrs & ATTR_FAINT) && fg <= 7) {
        fg += 8;
    }

    /* DECSCNM: mirror st behavior.
       - swap defaults
       - invert resolved RGB value for all non-default colors */
    if (term_state.screen_reverse) {
        if (fg == COLOR_DEFAULT_FG) {
            fg = COLOR_DEFAULT_BG;
        } else {
            XRenderColor rc = get_xrender_color(fg, 0, 0);
            uint32_t rgb = ((uint32_t)(rc.red >> 8) << 16) |
                           ((uint32_t)(rc.green >> 8) << 8) |
                           (uint32_t)(rc.blue >> 8);
            fg = COLOR_TRUE_RGB_BASE | ((~rgb) & 0x00FFFFFFu);
        }

        if (bg == COLOR_DEFAULT_BG) {
            bg = COLOR_DEFAULT_FG;
        } else {
            XRenderColor rc = get_xrender_color(bg, 1, 0);
            uint32_t rgb = ((uint32_t)(rc.red >> 8) << 16) |
                           ((uint32_t)(rc.green >> 8) << 8) |
                           (uint32_t)(rc.blue >> 8);
            bg = COLOR_TRUE_RGB_BASE | ((~rgb) & 0x00FFFFFFu);
        }
    }

    if (attrs & ATTR_REVERSE) {
        uint32_t tmp = fg;
        fg = bg;
        bg = tmp;
    }
    if (selected) {
        uint32_t tmp = fg;
        fg = bg;
        bg = tmp;
    }
    if ((attrs & ATTR_BLINK) && hide_blink) {
        fg = bg;
    }
    if (attrs & ATTR_INVISIBLE) {
        fg = bg;
    }

    *out_fg = fg;
    *out_bg = bg;
}

/* Triggers a full clear+redraw on next draw_text() call (set after resize). */
static int draw_full_refresh = 1;
/* Tracks previous cursor row to dirty it when cursor moves between rows. */
static int prev_cursor_row = -1;

// Draw text using TerminalState's current attr per character
void draw_text(Display *display, Window window, GC gc) {
    if (!xft_draw) return;

    update_blink_state();

    if (term_state.bell_rung) {
        XBell(display, 0);
        term_state.bell_rung = 0;
    }

    int win_w = cached_win_w;
    int win_h = cached_win_h;

    /* Fall back to XGetWindowAttributes only if dimensions not yet known. */
    if (win_w <= 0 || win_h <= 0) {
        XWindowAttributes wa;
        if (!XGetWindowAttributes(display, window, &wa)) return;
        win_w = wa.width;
        win_h = wa.height;
        cached_win_w = win_w;
        cached_win_h = win_h;
    }

    /* Resize back buffer if window size changed */
    if (back_pixmap == None || back_w != win_w || back_h != win_h) {
        if (xft_draw_buf) { XftDrawDestroy(xft_draw_buf); xft_draw_buf = NULL; }
        if (back_pixmap != None) { XFreePixmap(display, back_pixmap); back_pixmap = None; }
        back_w = win_w;
        back_h = win_h;
        if (back_w > 0 && back_h > 0) {
            back_pixmap = XCreatePixmap(display, window, back_w, back_h,
                DefaultDepth(display, DefaultScreen(display)));
            if (back_pixmap != None) {
                xft_draw_buf = XftDrawCreate(display, back_pixmap,
                    DefaultVisual(display, DefaultScreen(display)),
                    DefaultColormap(display, DefaultScreen(display)));
            }
        }
        draw_full_refresh = 1;
    }

    XftDraw *draw = (xft_draw_buf != NULL) ? xft_draw_buf : xft_draw;
    if (draw == NULL) return;

    const int left_pad = LEFT_PAD;
    const int line_gap = LINE_GAP;
    const int baseline0 = xft_font->ascent + DRAW_TOP_PAD;
    const int step_w = g_cell_w + g_cell_gap;
    const int buf_w = back_w > 0 ? back_w : win_w;
    const int buf_h = back_h > 0 ? back_h : win_h;
    const int scrollback_offset = terminal_get_scrollback_offset();
    const int show_cursor = (scrollback_offset == 0);

    /* Dirty the rows occupied by the cursor (old position + new position) so
       the cursor shape is always erased/redrawn even when cell content is unchanged. */
    int cursor_row = term_state.row;
    if (cursor_row < 0) cursor_row = 0;
    if (cursor_row >= term_rows) cursor_row = term_rows - 1;
    if (dirty_rows) {
        if (prev_cursor_row >= 0 && prev_cursor_row < term_rows)
            dirty_rows[prev_cursor_row] = 1;
        if (show_cursor)
            dirty_rows[cursor_row] = 1;
    }

    /* Check whether anything actually needs rendering. */
    int any_dirty = draw_full_refresh;
    if (!any_dirty && dirty_rows) {
        for (int r = 0; r < term_rows; r++) {
            if (dirty_rows[r]) { any_dirty = 1; break; }
        }
    } else if (!dirty_rows) {
        any_dirty = 1;
    }

    if (!any_dirty) {
        /* Nothing changed — skip the entire render. */
        return;
    }

    /* On a full refresh, clear the entire back pixmap once (covers padding areas too). */
    int full = draw_full_refresh;
    draw_full_refresh = 0;
    if (full) {
        XftDrawRect(draw, &xft_color_bg, 0, 0, buf_w, buf_h);
    }

    for (int r = 0; r < term_rows; r++) {
        const TerminalCell *row_cells = terminal_get_visible_row(r);

        /* Skip rows that haven't changed (incremental update only). */
        if (!full && dirty_rows && !dirty_rows[r])
            continue;

        if (!row_cells) {
            continue;
        }

        int x = LEFT_PAD;
        int y = baseline0 + r * (g_cell_h + line_gap);
        int row_top = y - xft_font->ascent;

        /* On incremental updates, clear just this row before redrawing it. */
        if (!full) {
            XftDrawRect(draw, &xft_color_bg, 0, row_top, buf_w, g_cell_h);
        }

        /*
         * Pass 1: background run batching.
         * Walk cells left→right, tracking current pixel x.  Consecutive cells
         * that resolve to the same XftColor* pointer are merged into one rect.
         * Continuation cells are skipped (their lead cell covers them).
         */
        {
            int cur_px = LEFT_PAD;
            int run_px = LEFT_PAD;
            XftColor *run_bg_color = NULL;

            for (int c = 0; c <= term_cols; c++) {
                XftColor *bg_color = NULL;
                int cell_w_px = step_w;

                if (c < term_cols) {
                    const TerminalCell *cell = &row_cells[c];
                    if (cell->is_continuation) {
                        cur_px += step_w;
                        continue;
                    }
                    int cell_span = (cell->width == 2 && c + 1 < term_cols) ? 2 : 1;
                    int selected = cell_selected(r, c) || (cell_span == 2 && cell_selected(r, c + 1));
                    uint32_t fg_val, bg_val;
                    resolve_cell_colors(cell->fg, cell->bg, cell->attrs, selected, blink_hidden, &fg_val, &bg_val);
                    bg_color = get_xft_color(display, window, bg_val, 1, 0);
                    cell_w_px = g_cell_w * cell_span + g_cell_gap * (cell_span - 1);
                }

                /* Flush the current run when the color changes or we're past the last cell. */
                if (run_bg_color != NULL && bg_color != run_bg_color) {
                    XftDrawRect(draw, run_bg_color, run_px, row_top, cur_px - run_px, g_cell_h);
                    run_px = cur_px;
                    run_bg_color = NULL;
                }

                if (c < term_cols) {
                    if (run_bg_color == NULL) {
                        run_px = cur_px;
                        run_bg_color = bg_color;
                    }
                    cur_px += cell_w_px;
                } else {
                    /* End of row: flush final run. */
                    if (run_bg_color != NULL && cur_px > run_px)
                        XftDrawRect(draw, run_bg_color, run_px, row_top, cur_px - run_px, g_cell_h);
                }
            }
        }

        /*
         * Pass 2: foreground text run batching.
         * Accumulate consecutive non-continuation cells sharing the same font
         * and fg XftColor pointer into a UTF-8 string, draw one call per run.
         * Decorations (underline, strikethrough) are drawn per cell since they
         * are cheap (single pixel rect) and rarely span many cells.
         */
        {
            x = LEFT_PAD;
            for (int c = 0; c < term_cols; c++) {
                const TerminalCell *cell = &row_cells[c];
                int cell_span = 1;
                int top = row_top;
                int selected;
                int draw_w;
                uint32_t fg_val;
                uint32_t bg_val;
                XftColor *fg_color;

                if (cell->is_continuation) {
                    x += step_w;
                    continue;
                }

                if (cell->width == 2 && c + 1 < term_cols)
                    cell_span = 2;

                selected = cell_selected(r, c) || (cell_span == 2 && cell_selected(r, c + 1));
                resolve_cell_colors(cell->fg, cell->bg, cell->attrs, selected, blink_hidden, &fg_val, &bg_val);
                draw_w = g_cell_w * cell_span;
                fg_color = get_xft_color(display, window, fg_val, 0, (cell->attrs & ATTR_FAINT) != 0);

                if (cell->c[0] != '\0') {
                    utf8proc_int32_t cp;
                    ssize_t rs = utf8proc_iterate((const uint8_t *)cell->c, -1, &cp);
                    if (rs > 0) {
                        XRectangle clip_rect;
                        XftFont *font_to_use = font_for_cell(cell->attrs, cp);
                        int glyph_len = (int)strlen(cell->c);
                        /* Draw one cell at a time to preserve terminal cell boundaries
                           and avoid cross-cell ligature/shaping effects. */

                        clip_rect.x = 0;
                        clip_rect.y = 0;
                        clip_rect.width = (unsigned short)draw_w;
                        clip_rect.height = (unsigned short)g_cell_h;
                        XftDrawSetClipRectangles(draw, x, top, &clip_rect, 1);

                        XftDrawStringUtf8(draw, fg_color, font_to_use, x, y,
                                          (const FcChar8 *)cell->c, glyph_len);

                        XftDrawSetClip(draw, NULL);
                    }
                }

                /* Decorations are cheap; draw per-cell. */
                if (cell->attrs & ATTR_UNDERLINE)
                    XftDrawRect(draw, fg_color, x, top + xft_font->ascent + 1, draw_w, 1);
                if (cell->attrs & ATTR_STRUCK)
                    XftDrawRect(draw, fg_color, x, top + (2 * xft_font->ascent) / 3, draw_w, 1);

                x += step_w;
            }
        }
    }

    /* Cursor: shape from DECSCUSR (0-2 block, 3-4 underline, 5-6 bar, 7 snowman) */
    if (term_state.cursor_visible && show_cursor) {
        int cur_row = term_state.row;
        int cur_col = term_state.col;
        int cur_span = 1;
        int selected;
        uint32_t cursor_fg_idx;
        uint32_t cursor_bg_idx;
        const TerminalCell *cursor_cell;
        uint16_t cursor_attrs;
        int cur_x;
        int cur_w;
        int cur_h = g_cell_h;
        int cy_top;
        int shape = (term_state.cursorshape >= 0 && term_state.cursorshape <= 7) ? term_state.cursorshape : 2;
        XftColor *cursor_bg_color;

        if (cur_row < 0) cur_row = 0;
        if (cur_row >= term_rows) cur_row = term_rows - 1;
        if (cur_col < 0) cur_col = 0;
        if (cur_col >= term_cols) cur_col = term_cols - 1;

        if (terminal_buffer[cur_row][cur_col].is_continuation && cur_col > 0) {
            cur_col--;
        }

        cursor_cell = &terminal_buffer[cur_row][cur_col];
        cursor_attrs = cursor_cell->attrs & (ATTR_BOLD | ATTR_ITALIC | ATTR_UNDERLINE | ATTR_STRUCK);
        if (cursor_cell->width == 2 && cur_col + 1 < term_cols) {
            cur_span = 2;
        }
        selected = cell_selected(cur_row, cur_col) || (cur_span == 2 && cell_selected(cur_row, cur_col + 1));

        if (term_state.screen_reverse) {
            cursor_bg_idx = selected ? defaultcs : defaultrcs;
            cursor_fg_idx = selected ? defaultrcs : defaultcs;
        } else {
            if (selected) {
                cursor_fg_idx = COLOR_DEFAULT_FG;
                cursor_bg_idx = defaultrcs;
            } else {
                cursor_fg_idx = COLOR_DEFAULT_BG;
                cursor_bg_idx = defaultcs;
            }
        }

        cur_x = left_pad + cur_col * step_w;
        cur_w = g_cell_w * cur_span;
        cy_top = baseline0 + cur_row * (g_cell_h + line_gap) - xft_font->ascent;
        cursor_bg_color = get_xft_color(display, window, cursor_bg_idx, 1, 0);

        if (shape >= 3 && shape <= 4) {
            int uh = (int)cursorthickness;
            if (uh < 1) uh = 1;
            XftDrawRect(draw, cursor_bg_color, cur_x, cy_top + g_cell_h - uh, cur_w, uh);
        } else if (shape >= 5 && shape <= 6) {
            int bw = (int)cursorthickness;
            if (bw < 1) bw = 1;
            XftDrawRect(draw, cursor_bg_color, cur_x, cy_top, bw, g_cell_h);
        } else {
            XftColor *cursor_fg_color = get_xft_color(display, window, cursor_fg_idx, 0, 0);
            XftDrawRect(draw, cursor_bg_color, cur_x, cy_top, cur_w, cur_h);

            if (shape == 7) {
                const char snowman[] = "\xE2\x98\x83";
                XftFont *cursor_font = font_for_cell(cursor_attrs, 0x2603);
                XRectangle clip_rect;

                clip_rect.x = 0;
                clip_rect.y = 0;
                clip_rect.width = (unsigned short)cur_w;
                clip_rect.height = (unsigned short)g_cell_h;
                XftDrawSetClipRectangles(draw, cur_x, cy_top, &clip_rect, 1);
                XftDrawStringUtf8(draw, cursor_fg_color, cursor_font,
                                  cur_x, baseline0 + cur_row * (g_cell_h + line_gap),
                                  (const FcChar8 *)snowman, 3);
                XftDrawSetClip(draw, NULL);
            } else if (cursor_cell->c[0] != '\0') {
                utf8proc_int32_t cp;
                ssize_t rs = utf8proc_iterate((const uint8_t *)cursor_cell->c, -1, &cp);
                if (rs > 0) {
                    XftFont *font_to_use = font_for_cell(cursor_attrs, cp);
                    XRectangle clip_rect;

                    clip_rect.x = 0;
                    clip_rect.y = 0;
                    clip_rect.width = (unsigned short)cur_w;
                    clip_rect.height = (unsigned short)g_cell_h;
                    XftDrawSetClipRectangles(draw, cur_x, cy_top, &clip_rect, 1);
                    XftDrawStringUtf8(draw, cursor_fg_color, font_to_use,
                                      cur_x, baseline0 + cur_row * (g_cell_h + line_gap),
                                      (const FcChar8 *)cursor_cell->c,
                                      (int)strlen(cursor_cell->c));
                    XftDrawSetClip(draw, NULL);
                }
            }
        }
    }

    /* Copy back buffer to window to reduce flicker */
    if (back_pixmap != None && gc) {
        XCopyArea(display, back_pixmap, window, gc, 0, 0, back_w, back_h, 0, 0);
    }

    /* Update cursor tracking and clear dirty flags for next frame. */
    prev_cursor_row = show_cursor ? cursor_row : -1;
    if (dirty_rows)
        memset(dirty_rows, 0, (size_t)term_rows);
}

void xy_to_cell(int x, int y, int *row, int *col) {
    const int baseline0 = xft_font->ascent + DRAW_TOP_PAD;
    const int step_w    = g_cell_w + g_cell_gap;
    const int step_h    = g_cell_h + LINE_GAP;

    int relx = x - LEFT_PAD;
    if (relx < 0) relx = 0;
    int c = relx / (step_w ? step_w : 1);

    int top0 = baseline0 - xft_font->ascent;
    int rely = y - top0;
    int r = rely / (step_h ? step_h : 1);

    if (r < 0) r = 0;
    if (r >= term_rows) r = term_rows - 1;
    if (c < 0) c = 0;
    if (c >= term_cols) c = term_cols - 1;

    *row = r; *col = c;
}

static int cell_selected(int r, int c) {
    int sr, sc, er, ec;

    if (!term_state.sel_active)
        return 0;
    selection_get_effective_bounds(&sr, &sc, &er, &ec);
    if (r < sr || r > er)
        return 0;
    if (term_state.sel_type == SEL_RECTANGULAR)
        return (c >= sc && c <= ec);
    if (sr == er)
        return (c >= sc && c <= ec);
    if (r == sr)
        return (c >= sc);
    if (r == er)
        return (c <= ec);
    return 1;
}
