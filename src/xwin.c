/* See LICENSE for license details. */
/* xwin.c: X11/Xft layer, event loop, keymap dispatch, clipboard, mouse-to-selection. */

#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <utf8proc.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <wchar.h>
#include <sys/select.h>
#include "xconfig.h"
#include "cupid.h"
#include "xwin.h"
#include "xentry.h"
#include "pty.h"

/* ======================================================================== */
/* Macros / constants from draw.c                                           */
/* ======================================================================== */

/* Padding constants (previously in draw.h — kept here as renderer-private) */
#define DRAW_LEFT_PAD 10
#define DRAW_TOP_PAD  5

#define MAX_LINES 100
#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

int line_count = 0;

static const int LEFT_PAD = DRAW_LEFT_PAD;
static const int LINE_GAP = 0;
/* Forward declarations for input functions used in draw section */
void paste_from_clipboard(Display *display, Window window);

/* ======================================================================== */
/* Globals from draw.c                                                      */
/* ======================================================================== */

XftDraw *xft_draw = NULL;
XftFont *xft_font = NULL;
XftFont *xft_font_bold = NULL;
XftFont *xft_font_italic = NULL;
XftFont *xft_font_bold_italic = NULL;
XftFont *xft_font_emoji = NULL;
XftColor xft_color;
XftColor xft_color_fg;
XftColor xft_color_bg;

Display *global_display = NULL;
Window global_window = 0;
int g_cell_w = 0;
int g_cell_h = 0;
int g_cell_gap = 0;

static Pixmap back_pixmap = None;
static XftDraw *xft_draw_buf = NULL;
static int back_w = 0, back_h = 0;
static PtySession *g_x_session = NULL;
static int window_focused = 0;

static XIM g_xim = NULL;
XIC g_xic = NULL;
static XPoint g_xim_spot = {0, 0};

static void ximopen(Display *dpy, XPointer unused1, XPointer unused2);

static void xicdestroy(XIM xim, XPointer client, XPointer call) {
    (void)xim; (void)call;
    Display *dpy = (Display *)client;
    g_xic = NULL;
    XRegisterIMInstantiateCallback(dpy, NULL, NULL, NULL,
                                   ximopen, (XPointer)dpy);
}

static void ximopen(Display *dpy, XPointer unused1, XPointer unused2) {
    (void)unused1; (void)unused2;
    XIMCallback cb;
    XVaNestedList spot_list;

    if (g_xim)
        return;

    g_xim = XOpenIM(dpy, NULL, NULL, NULL);
    if (!g_xim)
        return;

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

    XUnregisterIMInstantiateCallback(dpy, NULL, NULL, NULL,
                                     ximopen, (XPointer)dpy);
}

static void ximdestroy(XIM xim, XPointer client, XPointer call) {
    (void)xim; (void)call;
    Display *dpy = (Display *)client;
    g_xim = NULL;
    if (g_xic) {
        XDestroyIC(g_xic);
        g_xic = NULL;
    }
    XRegisterIMInstantiateCallback(dpy, NULL, NULL, NULL,
                                   ximopen, (XPointer)dpy);
}

void xximspot_xy(Display *display, Window window) {
    static int prev_col = -1, prev_row = -1;
    XVaNestedList spot_list;
    const int baseline0 = xft_font ? (xft_font->ascent + DRAW_TOP_PAD) : DRAW_TOP_PAD;
    const int step_w    = g_cell_w + g_cell_gap;
    const int step_h    = g_cell_h + LINE_GAP;
    int cur_col, cur_row;

    (void)window;
    if (!g_xic || !display) return;

    cur_col = term.col;
    cur_row = term.row;

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

static int cached_win_w = 0;
static int cached_win_h = 0;

void draw_notify_resize(int w, int h) {
    cached_win_w = w;
    cached_win_h = h;
}

#define COLOR_CACHE_SIZE (COLOR_DEFAULT_BG + 1)
static XftColor color_cache[COLOR_CACHE_SIZE];
static int color_allocated[COLOR_CACHE_SIZE] = {0};
static XftColor faint_color_cache[COLOR_CACHE_SIZE];
static int faint_color_allocated[COLOR_CACHE_SIZE] = {0};

#define TC_HASH_SIZE 1024
#define TC_HASH_MASK (TC_HASH_SIZE - 1)
typedef struct { uint32_t key; XftColor color; } TcEntry;
static TcEntry tc_hash[TC_HASH_SIZE];
static TcEntry tc_faint_hash[TC_HASH_SIZE];

static int blink_hidden = 0;
static int blink_initialized = 0;
static struct timespec blink_last_toggle = {0, 0};

static void mark_all_rows_dirty_local(void) {
    if (!term_lines || term_rows <= 0)
        return;
    for (int r = 0; r < term_rows; r++)
        term_lines[r].dirty = 1;
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

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return;

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
            if (ticks % 2LL != 0LL)
                blink_hidden = !blink_hidden;

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

#define GF_HASH_SIZE 2048
#define GF_HASH_MASK (GF_HASH_SIZE - 1)
typedef struct {
    utf8proc_int32_t cp;
    uint8_t          style;
    uint8_t          occupied;
    XftFont         *font;
} GfEntry;
static GfEntry gf_hash[GF_HASH_SIZE];

static FcPattern *g_fc_pat[4];
static FcFontSet *g_fc_set[4];

static void fc_fallback_teardown(void) {
    for (int i = 0; i < 4; i++) {
        if (g_fc_set[i]) { FcFontSetDestroy(g_fc_set[i]); g_fc_set[i] = NULL; }
        if (g_fc_pat[i]) { FcPatternDestroy(g_fc_pat[i]); g_fc_pat[i] = NULL; }
    }
}

static FcPattern *pattern_for_glyph_fallback(uint8_t style) {
    if (style < 4 && g_fc_pat[style]) return g_fc_pat[style];
    if (style == 3) {
        if (g_fc_pat[3]) return g_fc_pat[3];
        if (g_fc_pat[1]) return g_fc_pat[1];
        if (g_fc_pat[2]) return g_fc_pat[2];
        return g_fc_pat[0];
    }
    if (style == 2 && !g_fc_pat[2]) return g_fc_pat[0];
    if (style == 1 && !g_fc_pat[1]) return g_fc_pat[0];
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

static uint8_t font_style_key(uint16_t mode) {
    uint8_t key = 0;
    if (mode & ATTR_BOLD)   key |= 1;
    if (mode & ATTR_ITALIC) key |= 2;
    return key;
}

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
}

static XftFont *load_glyph_fallback(Display *display, uint8_t style, utf8proc_int32_t cp) {
    FcPattern *base_pat;
    FcPattern *fcpattern;
    FcPattern *fontpattern;
    FcCharSet *fccharset;
    FcFontSet *fcsets[1];
    FcResult fcres;
    XftFont *font = NULL;

    if (!display || cp < 0) return NULL;
    base_pat = pattern_for_glyph_fallback(style);
    if (!base_pat) return NULL;

    if (!g_fc_set[style]) {
        g_fc_set[style] = FcFontSort(NULL, base_pat, FcTrue, NULL, &fcres);
        if (!g_fc_set[style] && base_pat != g_fc_pat[0] && g_fc_pat[0])
            g_fc_set[style] = FcFontSort(NULL, g_fc_pat[0], FcTrue, NULL, &fcres);
    }
    if (!g_fc_set[style]) return NULL;

    fcpattern = FcPatternDuplicate(base_pat);
    if (!fcpattern) return NULL;

    fccharset = FcCharSetCreate();
    if (!fccharset) { FcPatternDestroy(fcpattern); return NULL; }

    FcCharSetAddChar(fccharset, (FcChar32)cp);
    FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
    FcPatternAddBool(fcpattern, FC_SCALABLE, FcTrue);
    FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
    FcDefaultSubstitute(fcpattern);

    fcsets[0] = g_fc_set[style];
    fontpattern = FcFontSetMatch(NULL, fcsets, 1, fcpattern, &fcres);
    FcPatternDestroy(fcpattern);
    FcCharSetDestroy(fccharset);

    if (!fontpattern) return NULL;

    font = XftFontOpenPattern(display, fontpattern);
    if (!font) return NULL;

    if (XftCharIndex(display, font, (FcChar32)cp) == 0) {
        XftFontClose(display, font);
        return NULL;
    }
    return font;
}

static unsigned short u8_to_u16(unsigned int x) {
    return (unsigned short)(x * 0x101u);
}

static unsigned short sixd_to_16bit(int x) {
    return (unsigned short)(x == 0 ? 0 : (0x3737 + 0x2828 * x));
}

static int parse_config_color(uint32_t idx, XRenderColor *out) {
    XColor xc;
    if (!out || !global_display) return 0;
    if ((size_t)idx >= LEN(colorname) || colorname[idx] == NULL || colorname[idx][0] == '\0')
        return 0;
    if (!XParseColor(global_display,
            DefaultColormap(global_display, DefaultScreen(global_display)),
            colorname[idx], &xc))
        return 0;
    out->red = xc.red;
    out->green = xc.green;
    out->blue = xc.blue;
    out->alpha = 0xFFFF;
    return 1;
}

static XRenderColor get_xrender_color(uint32_t c, int is_bg, int is_faint) {
    XRenderColor xc;

    if (c == COLOR_DEFAULT_FG && term.osc_fg_color)
        c = term.osc_fg_color;
    else if (c == COLOR_DEFAULT_BG && term.osc_bg_color)
        c = term.osc_bg_color;
    else if (c == 256 && term.osc_cs_color)
        c = term.osc_cs_color;
    else if (c < 256 && term.palette_overridden[c])
        c = term.palette_override[c];

    if (IS_TRUECOL(c)) {
        unsigned int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        xc = (XRenderColor){
            (unsigned short)(r * 0x101),
            (unsigned short)(g * 0x101),
            (unsigned short)(b * 0x101),
            0xFFFF
        };
    } else if (parse_config_color(c, &xc)) {
        /* nop */
    } else if (c < 16) {
        static const unsigned char ansi[16][3] = {
            {0x00,0x00,0x00},{0xCD,0x00,0x00},{0x00,0xCD,0x00},{0xCD,0xCD,0x00},
            {0x00,0x00,0xEE},{0xCD,0x00,0xCD},{0x00,0xCD,0xCD},{0xE5,0xE5,0xE5},
            {0x7F,0x7F,0x7F},{0xFF,0x00,0x00},{0x00,0xFF,0x00},{0xFF,0xFF,0x00},
            {0x5C,0x5C,0xFF},{0xFF,0x00,0xFF},{0x00,0xFF,0xFF},{0xFF,0xFF,0xFF},
        };
        xc = (XRenderColor){u8_to_u16(ansi[c][0]),u8_to_u16(ansi[c][1]),u8_to_u16(ansi[c][2]),0xFFFF};
    } else if (c >= 16 && c <= 231) {
        int idx = c - 16;
        int r = idx / 36, g = (idx / 6) % 6, b = idx % 6;
        xc = (XRenderColor){sixd_to_16bit(r),sixd_to_16bit(g),sixd_to_16bit(b),0xFFFF};
    } else if (c >= 232 && c <= 255) {
        unsigned short v = (unsigned short)(0x0808 + (c - 232) * 0x0A0A);
        xc = (XRenderColor){v, v, v, 0xFFFF};
    } else if (c == 256) {
        xc = (XRenderColor){0xCCCC,0xCCCC,0xCCCC,0xFFFF};
    } else if (c == 257) {
        xc = (XRenderColor){0x5555,0x5555,0x5555,0xFFFF};
    } else if (c == 258) {
        xc = (XRenderColor){0xE5E5,0xE5E5,0xE5E5,0xFFFF};
    } else if (c == 259) {
        xc = (XRenderColor){0x0000,0x0000,0x0000,0xFFFF};
    } else {
        uint32_t fallback = is_bg ? defaultbg : defaultfg;
        if (fallback != c && fallback <= COLOR_DEFAULT_BG) {
            xc = get_xrender_color(fallback, is_bg, 0);
        } else {
            xc = is_bg ? (XRenderColor){0x0000,0x0000,0x0000,0xFFFF}
                       : (XRenderColor){0xE5E5,0xE5E5,0xE5E5,0xFFFF};
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

    if (logical_color > COLOR_DEFAULT_BG && !IS_TRUECOL(logical_color))
        return is_bg ? &xft_color_bg : &xft_color_fg;

    if (IS_TRUECOL(logical_color)) {
        TcEntry *table = (is_faint && !is_bg) ? tc_faint_hash : tc_hash;
        unsigned slot = ((logical_color & 0xFFFFFFu) * 2654435769u) >> 22;
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

    if (logical_color > COLOR_DEFAULT_BG)
        return is_bg ? &xft_color_bg : &xft_color_fg;

    idx = (size_t)logical_color;

    if (is_faint && !is_bg) {
        if (!faint_color_allocated[idx]) {
            XRenderColor rc = get_xrender_color(logical_color, is_bg, is_faint);
            if (!XftColorAllocValue(d, DefaultVisual(d, DefaultScreen(d)),
                DefaultColormap(d, DefaultScreen(d)), &rc, &faint_color_cache[idx]))
                return &xft_color_fg;
            faint_color_allocated[idx] = 1;
        }
        return &faint_color_cache[idx];
    } else {
        if (!color_allocated[idx]) {
            XRenderColor rc = get_xrender_color(logical_color, is_bg, 0);
            if (!XftColorAllocValue(d, DefaultVisual(d, DefaultScreen(d)),
                DefaultColormap(d, DefaultScreen(d)), &rc, &color_cache[idx]))
                return is_bg ? &xft_color_bg : &xft_color_fg;
            color_allocated[idx] = 1;
        }
        return &color_cache[idx];
    }
}

static int open_xft_font(Display *display, FcPattern *pattern, XftFont **out_font,
                         FcPattern **out_configured_keep) {
    FcPattern *configured;
    FcPattern *match;
    FcResult result;

    *out_font = NULL;
    if (out_configured_keep) *out_configured_keep = NULL;

    configured = FcPatternDuplicate(pattern);
    if (!configured) return -1;

    FcConfigSubstitute(NULL, configured, FcMatchPattern);
    XftDefaultSubstitute(display, DefaultScreen(display), configured);

    match = FcFontMatch(NULL, configured, &result);
    if (!match) { FcPatternDestroy(configured); return -1; }

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

    if (!fontstr || !fontstr[0]) fontstr = "monospace:pixelsize=12";

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
            if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval) == FcResultMatch)
                usedfontsize = fontval;
            else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontval) == FcResultMatch)
                usedfontsize = -1.0;
            else {
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
                if (fontsize_override <= 1.0) defaultfontsize = fontval;
            }
        }

        FcPatternDel(pattern, FC_SLANT);
        FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
        if (open_xft_font(display, pattern, &ni, &pat_italic) != 0) { ni = nf; pat_italic = NULL; }

        FcPatternDel(pattern, FC_WEIGHT);
        FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
        if (open_xft_font(display, pattern, &nbi, &pat_bi) != 0) { nbi = (ni != nf) ? ni : nf; pat_bi = NULL; }

        FcPatternDel(pattern, FC_SLANT);
        FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
        if (open_xft_font(display, pattern, &nb, &pat_bold) != 0) { nb = nf; pat_bold = NULL; }

        FcPatternDestroy(pattern);
        pattern = NULL;

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
        if (!ne) ne = nf;
    }

    *out_reg = nf; *out_bold = nb; *out_italic = ni; *out_bold_italic = nbi; *out_emoji = ne;
    return 0;
}

static void free_font_set(Display *display, XftFont *nf, XftFont *nb, XftFont *ni,
                          XftFont *nbi, XftFont *ne) {
    if (ne && ne != nf && ne != nb && ne != ni && ne != nbi) XftFontClose(display, ne);
    if (nbi && nbi != nf && nbi != nb && nbi != ni) XftFontClose(display, nbi);
    if (ni && ni != nf) XftFontClose(display, ni);
    if (nb && nb != nf) XftFontClose(display, nb);
    if (nf) XftFontClose(display, nf);
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
        XftTextExtentsUtf8(display, xft_font, (const FcChar8 *)ascii_printable,
                           (int)ascii_len, &ext_ascii);
        if (ext_ascii.xOff > 0)
            w_ascii = (int)((ext_ascii.xOff + (int)ascii_len - 1) / (int)ascii_len);
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

void initialize_xft(Display *display, Window window) {
    XftFont *nf, *nb, *ni, *nbi, *ne;

    global_display = display;
    global_window = window;
    clear_glyph_fallback_cache(display);

    xft_draw = XftDrawCreate(display, window,
        DefaultVisual(display, DefaultScreen(display)),
        DefaultColormap(display, DefaultScreen(display)));
    if (!xft_draw) { fprintf(stderr, "Failed to create XftDraw.\n"); exit(EXIT_FAILURE); }

    if (load_font_set(display, FONT, 0.0, &nf, &nb, &ni, &nbi, &ne) != 0) {
        fprintf(stderr, "cupidterminal: can't open font \"%s\", trying DejaVu Sans Mono\n", FONT);
        if (load_font_set(display, "DejaVu Sans Mono:pixelsize=12:antialias=true", 12.0,
                          &nf, &nb, &ni, &nbi, &ne) != 0) {
            fprintf(stderr, "cupidterminal: failed to load fallback font.\n");
            exit(EXIT_FAILURE);
        }
        defaultfontsize = usedfontsize;
    }

    xft_font = nf; xft_font_bold = nb; xft_font_italic = ni;
    xft_font_bold_italic = nbi; xft_font_emoji = ne;

    XRenderColor rc_white = get_xrender_color(COLOR_DEFAULT_FG, 0, 0);
    if (!XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
            DefaultColormap(display, DefaultScreen(display)), &rc_white, &xft_color_fg)) {
        fprintf(stderr, "Failed to allocate white color.\n");
        exit(EXIT_FAILURE);
    }

    XRenderColor rc_black = get_xrender_color(COLOR_DEFAULT_BG, 1, 0);
    if (!XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
            DefaultColormap(display, DefaultScreen(display)), &rc_black, &xft_color_bg)) {
        fprintf(stderr, "Failed to allocate black color.\n");
        exit(EXIT_FAILURE);
    }

    color_cache[COLOR_DEFAULT_FG] = xft_color_fg; color_allocated[COLOR_DEFAULT_FG] = 1;
    color_cache[COLOR_DEFAULT_BG] = xft_color_bg; color_allocated[COLOR_DEFAULT_BG] = 1;

    recompute_cell_metrics(display);
    tnew(&term);

    if (!XSupportsLocale()) {
        fprintf(stderr, "warning: X does not support locale\n");
    } else {
        XIMCallback cb;
        ximopen(display, NULL, NULL);
        if (!g_xim) {
            XRegisterIMInstantiateCallback(display, NULL, NULL, NULL,
                                           ximopen, (XPointer)display);
        } else {
            cb.client_data = (XPointer)display;
            cb.callback    = ximdestroy;
            XSetIMValues(g_xim, XNDestroyCallback, &cb, NULL);
        }
    }
}

static void xft_reload_fonts(Display *display) {
    double sz = usedfontsize;
    XftFont *nf, *nb, *ni, *nbi, *ne;
    XftFont *of = xft_font, *ob = xft_font_bold, *oi = xft_font_italic,
            *obi = xft_font_bold_italic, *oe = xft_font_emoji;

    if (sz < 6.0) sz = 6.0;
    if (sz > 256.0) sz = 256.0;

    clear_glyph_fallback_cache(display);
    if (load_font_set(display, FONT, sz, &nf, &nb, &ni, &nbi, &ne) != 0) return;

    xft_font = nf; xft_font_bold = nb; xft_font_italic = ni;
    xft_font_bold_italic = nbi; xft_font_emoji = ne;
    free_font_set(display, of, ob, oi, obi, oe);
    recompute_cell_metrics(display);
}

void xft_zoom(Display *display, Window window, float delta) {
    (void)window;
    usedfontsize += delta;
    if (usedfontsize < 6) usedfontsize = 6;
    if (usedfontsize > 256) usedfontsize = 256;
    xft_reload_fonts(display);
}

void xft_zoom_reset(Display *display, Window window) {
    (void)window;
    usedfontsize = defaultfontsize;
    if (usedfontsize < 6) usedfontsize = 6;
    xft_reload_fonts(display);
}

void cleanup_xft(void) {
    if (g_xic) { XDestroyIC(g_xic); g_xic = NULL; }
    if (g_xim) { XCloseIM(g_xim);   g_xim = NULL; }
    clear_glyph_fallback_cache(global_display);
    fc_fallback_teardown();
    if (xft_draw_buf) { XftDrawDestroy(xft_draw_buf); xft_draw_buf = NULL; }
    if (back_pixmap != None && global_display) {
        XFreePixmap(global_display, back_pixmap); back_pixmap = None;
    }
    if (xft_draw) XftDrawDestroy(xft_draw);
    if (xft_font) XftFontClose(global_display, xft_font);
    if (xft_font_bold && xft_font_bold != xft_font)
        XftFontClose(global_display, xft_font_bold);
    if (xft_font_italic && xft_font_italic != xft_font)
        XftFontClose(global_display, xft_font_italic);
    if (xft_font_bold_italic && xft_font_bold_italic != xft_font_bold && xft_font_bold_italic != xft_font_italic)
        XftFontClose(global_display, xft_font_bold_italic);
    if (xft_font_emoji && xft_font_emoji != xft_font)
        XftFontClose(global_display, xft_font_emoji);
}

static XftFont *font_for_cell(uint16_t mode, utf8proc_int32_t cp) {
    XftFont *font_to_use = xft_font;
    uint8_t style = font_style_key(mode);

    if ((mode & ATTR_BOLD) && (mode & ATTR_ITALIC))
        font_to_use = xft_font_bold_italic ? xft_font_bold_italic : xft_font;
    else if (mode & ATTR_BOLD)
        font_to_use = xft_font_bold ? xft_font_bold : xft_font;
    else if (mode & ATTR_ITALIC)
        font_to_use = xft_font_italic ? xft_font_italic : xft_font;

    if (cp >= 0) {
        if (cp >= 0x20 && cp <= 0x7E) return font_to_use;

        if (font_to_use && XftCharIndex(global_display, font_to_use, (FcChar32)cp) != 0)
            return font_to_use;
        if (xft_font_emoji && xft_font_emoji != font_to_use &&
            XftCharIndex(global_display, xft_font_emoji, (FcChar32)cp) != 0)
            return xft_font_emoji;

        {
            XftFont *cached;
            if (find_glyph_fallback(cp, style, &cached))
                return cached ? cached : font_to_use;
        }
        {
            XftFont *fallback = load_glyph_fallback(global_display, style, cp);
            insert_glyph_fallback(cp, style, fallback);
            if (fallback) return fallback;
        }
    }
    return font_to_use;
}

static void resolve_cell_colors(uint32_t in_fg, uint32_t in_bg, uint16_t mode, int selected,
                                int hide_blink, uint32_t *out_fg, uint32_t *out_bg) {
    uint32_t fg = in_fg, bg = in_bg;

    if ((mode & ATTR_BOLD) && !(mode & ATTR_FAINT) && fg <= 7) fg += 8;

    if (term.screen_reverse) {
        if (fg == COLOR_DEFAULT_FG) {
            fg = COLOR_DEFAULT_BG;
        } else {
            XRenderColor rc = get_xrender_color(fg, 0, 0);
            uint32_t rgb = ((uint32_t)(rc.red>>8)<<16)|((uint32_t)(rc.green>>8)<<8)|(uint32_t)(rc.blue>>8);
            fg = TRUECOLOR((~rgb >> 16) & 0xFF, (~rgb >> 8) & 0xFF, ~rgb & 0xFF);
        }
        if (bg == COLOR_DEFAULT_BG) {
            bg = COLOR_DEFAULT_FG;
        } else {
            XRenderColor rc = get_xrender_color(bg, 1, 0);
            uint32_t rgb = ((uint32_t)(rc.red>>8)<<16)|((uint32_t)(rc.green>>8)<<8)|(uint32_t)(rc.blue>>8);
            bg = TRUECOLOR((~rgb >> 16) & 0xFF, (~rgb >> 8) & 0xFF, ~rgb & 0xFF);
        }
    }

    if (mode & ATTR_REVERSE)   { uint32_t t=fg; fg=bg; bg=t; }
    if (selected)               { uint32_t t=fg; fg=bg; bg=t; }
    if ((mode & ATTR_BLINK) && hide_blink) fg = bg;
    if (mode & ATTR_INVISIBLE)  fg = bg;

    *out_fg = fg; *out_bg = bg;
}

static int draw_full_refresh = 1;
static int prev_cursor_row = -1;

/* Encode the cluster shown at a visual row, including scrollback marks. */
static size_t encode_render_cluster(int visual_row, int col, const Glyph *cell,
                                    char *out, size_t cap) {
    (void)cell;
    return term_render_visual_cluster(visual_row, col, out, cap);
}

void draw_text(Display *display, Window window, GC gc) {
    if (!xft_draw) return;

    update_blink_state();

    int win_w = cached_win_w, win_h = cached_win_h;
    if (win_w <= 0 || win_h <= 0) {
        XWindowAttributes wa;
        if (!XGetWindowAttributes(display, window, &wa)) return;
        win_w = wa.width; win_h = wa.height;
        cached_win_w = win_w; cached_win_h = win_h;
    }

    if (back_pixmap == None || back_w != win_w || back_h != win_h) {
        if (xft_draw_buf) { XftDrawDestroy(xft_draw_buf); xft_draw_buf = NULL; }
        if (back_pixmap != None) { XFreePixmap(display, back_pixmap); back_pixmap = None; }
        back_w = win_w; back_h = win_h;
        if (back_w > 0 && back_h > 0) {
            back_pixmap = XCreatePixmap(display, window, back_w, back_h,
                DefaultDepth(display, DefaultScreen(display)));
            if (back_pixmap != None)
                xft_draw_buf = XftDrawCreate(display, back_pixmap,
                    DefaultVisual(display, DefaultScreen(display)),
                    DefaultColormap(display, DefaultScreen(display)));
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
    const int scrollback_offset = tgetscrolloffset();
    const int show_cursor = (scrollback_offset == 0);

    int cursor_row = term.row;
    if (cursor_row < 0) cursor_row = 0;
    if (cursor_row >= term_rows) cursor_row = term_rows - 1;
    if (term_lines) {
        if (prev_cursor_row >= 0 && prev_cursor_row < term_rows)
            term_lines[prev_cursor_row].dirty = 1;
        if (show_cursor)
            term_lines[cursor_row].dirty = 1;
    }

    int any_dirty = draw_full_refresh;
    if (!any_dirty && term_lines) {
        for (int r = 0; r < term_rows; r++)
            if (term_lines[r].dirty) { any_dirty = 1; break; }
    } else if (!term_lines) {
        any_dirty = 1;
    }
    if (!any_dirty) return;

    int full = draw_full_refresh;
    draw_full_refresh = 0;
    if (full) XftDrawRect(draw, &xft_color_bg, 0, 0, buf_w, buf_h);

    for (int r = 0; r < term_rows; r++) {
        const Glyph *row_cells = tgetline(r);
        if (!full && term_lines && !term_lines[r].dirty) continue;
        if (!row_cells) continue;

        int x = LEFT_PAD;
        int y = baseline0 + r * (g_cell_h + line_gap);
        int row_top = y - xft_font->ascent;

        if (!full) XftDrawRect(draw, &xft_color_bg, 0, row_top, buf_w, g_cell_h);

        /* Pass 1: background run batching */
        {
            int cur_px = LEFT_PAD, run_px = LEFT_PAD;
            XftColor *run_bg_color = NULL;
            for (int c = 0; c <= term_cols; c++) {
                XftColor *bg_color = NULL;
                int cell_w_px = step_w;
                if (c < term_cols) {
                    const Glyph *cell = &row_cells[c];
                    if (cell->mode & ATTR_WDUMMY) { cur_px += step_w; continue; }
                    int cell_span = ((cell->mode & ATTR_WIDE) && c+1 < term_cols) ? 2 : 1;
                    int is_sel = selected(c, r) ||
                                 (cell_span == 2 && selected(c + 1, r));
                    uint32_t fg_val, bg_val;
                    resolve_cell_colors(cell->fg, cell->bg, cell->mode, is_sel, blink_hidden, &fg_val, &bg_val);
                    bg_color = get_xft_color(display, window, bg_val, 1, 0);
                    cell_w_px = g_cell_w * cell_span + g_cell_gap * (cell_span - 1);
                }
                if (run_bg_color != NULL && bg_color != run_bg_color) {
                    XftDrawRect(draw, run_bg_color, run_px, row_top, cur_px - run_px, g_cell_h);
                    run_px = cur_px; run_bg_color = NULL;
                }
                if (c < term_cols) {
                    if (run_bg_color == NULL) { run_px = cur_px; run_bg_color = bg_color; }
                    cur_px += cell_w_px;
                } else {
                    if (run_bg_color != NULL && cur_px > run_px)
                        XftDrawRect(draw, run_bg_color, run_px, row_top, cur_px - run_px, g_cell_h);
                }
            }
        }

        /* Pass 2: foreground text */
        {
            x = LEFT_PAD;
            for (int c = 0; c < term_cols; c++) {
                const Glyph *cell = &row_cells[c];
                int cell_span = 1, top = row_top, is_sel, draw_w;
                uint32_t fg_val, bg_val;
                XftColor *fg_color;

                if (cell->mode & ATTR_WDUMMY) { x += step_w; continue; }
                if ((cell->mode & ATTR_WIDE) && c+1 < term_cols) cell_span = 2;

                is_sel = selected(c, r) ||
                         (cell_span == 2 && selected(c + 1, r));
                resolve_cell_colors(cell->fg, cell->bg, cell->mode, is_sel, blink_hidden, &fg_val, &bg_val);
                draw_w = g_cell_w * cell_span;
                fg_color = get_xft_color(display, window, fg_val, 0, (cell->mode & ATTR_FAINT) != 0);

                {
                    char cluster[MAX_CLUSTER_UTF8];
                    size_t cluster_len = encode_render_cluster(r, c, cell, cluster, sizeof cluster);
                    if (cluster_len > 0) {
                        utf8proc_int32_t cp;
                        ssize_t rs = utf8proc_iterate((const uint8_t *)cluster, (utf8proc_ssize_t)cluster_len, &cp);
                        if (rs > 0) {
                            XRectangle clip_rect;
                            XftFont *font_to_use = font_for_cell(cell->mode, cp);
                            clip_rect.x=0; clip_rect.y=0;
                            clip_rect.width=(unsigned short)draw_w;
                            clip_rect.height=(unsigned short)g_cell_h;
                            XftDrawSetClipRectangles(draw, x, top, &clip_rect, 1);
                            XftDrawStringUtf8(draw, fg_color, font_to_use, x, y,
                                              (const FcChar8 *)cluster, (int)cluster_len);
                            XftDrawSetClip(draw, NULL);
                        }
                    }
                }
                if (cell->mode & ATTR_UNDERLINE)
                    XftDrawRect(draw, fg_color, x, top + xft_font->ascent + 1, draw_w, 1);
                if (cell->mode & ATTR_STRUCK)
                    XftDrawRect(draw, fg_color, x, top + (2 * xft_font->ascent) / 3, draw_w, 1);
                x += step_w;
            }
        }
    }

    /* Cursor */
    if (term.cursor_visible && show_cursor && term_lines &&
        term_rows > 0 && term_cols > 0) {
        int cur_row = term.row, cur_col = term.col, cur_span = 1;
        int is_sel;
        uint32_t cursor_fg_idx, cursor_bg_idx;
        const Glyph *cursor_cell;
        uint16_t cursor_attrs;
        int cur_x, cur_w, cur_h = g_cell_h, cy_top;
        int shape = (term.cursorshape >= 0 && term.cursorshape <= 7) ? term.cursorshape : 2;
        XftColor *cursor_bg_color;

        if (cur_row < 0) cur_row = 0;
        if (cur_row >= term_rows) cur_row = term_rows - 1;
        if (cur_col < 0) cur_col = 0;
        if (cur_col >= term_cols) cur_col = term_cols - 1;
        if ((term_lines[cur_row].line[cur_col].mode & ATTR_WDUMMY) && cur_col > 0) cur_col--;

        cursor_cell = &term_lines[cur_row].line[cur_col];
        cursor_attrs = cursor_cell->mode & (ATTR_BOLD | ATTR_ITALIC | ATTR_UNDERLINE | ATTR_STRUCK);
        if ((cursor_cell->mode & ATTR_WIDE) && cur_col+1 < term_cols) cur_span = 2;
        is_sel = selected(cur_col, cur_row) ||
                 (cur_span == 2 && selected(cur_col + 1, cur_row));

        if (term.screen_reverse) {
            cursor_bg_idx = is_sel ? defaultcs : defaultrcs;
            cursor_fg_idx = is_sel ? defaultrcs : defaultcs;
        } else {
            if (is_sel) { cursor_fg_idx = COLOR_DEFAULT_FG; cursor_bg_idx = defaultrcs; }
            else        { cursor_fg_idx = COLOR_DEFAULT_BG; cursor_bg_idx = defaultcs;  }
        }

        cur_x = left_pad + cur_col * step_w;
        cur_w = g_cell_w * cur_span;
        cy_top = baseline0 + cur_row * (g_cell_h + line_gap) - xft_font->ascent;
        cursor_bg_color = get_xft_color(display, window, cursor_bg_idx, 1, 0);

        if (!window_focused) {
            XftDrawRect(draw, cursor_bg_color, cur_x, cy_top, (unsigned int)cur_w, 1);
            XftDrawRect(draw, cursor_bg_color, cur_x, cy_top, 1, (unsigned int)cur_h);
            XftDrawRect(draw, cursor_bg_color, cur_x + cur_w - 1, cy_top,
                        1, (unsigned int)cur_h);
            XftDrawRect(draw, cursor_bg_color, cur_x, cy_top + cur_h - 1,
                        (unsigned int)cur_w, 1);
        } else if (shape >= 3 && shape <= 4) {
            int uh = (int)cursorthickness; if (uh < 1) uh = 1;
            XftDrawRect(draw, cursor_bg_color, cur_x, cy_top + g_cell_h - uh, cur_w, uh);
        } else if (shape >= 5 && shape <= 6) {
            int bw = (int)cursorthickness; if (bw < 1) bw = 1;
            XftDrawRect(draw, cursor_bg_color, cur_x, cy_top, bw, g_cell_h);
        } else {
            XftColor *cursor_fg_color = get_xft_color(display, window, cursor_fg_idx, 0, 0);
            XftDrawRect(draw, cursor_bg_color, cur_x, cy_top, cur_w, cur_h);
            if (shape == 7) {
                const char snowman[] = "\xE2\x98\x83";
                XftFont *cursor_font = font_for_cell(cursor_attrs, 0x2603);
                XRectangle clip_rect;
                clip_rect.x=0; clip_rect.y=0;
                clip_rect.width=(unsigned short)cur_w;
                clip_rect.height=(unsigned short)g_cell_h;
                XftDrawSetClipRectangles(draw, cur_x, cy_top, &clip_rect, 1);
                XftDrawStringUtf8(draw, cursor_fg_color, cursor_font,
                                  cur_x, baseline0 + cur_row*(g_cell_h+line_gap),
                                  (const FcChar8 *)snowman, 3);
                XftDrawSetClip(draw, NULL);
            } else {
                char cursor_cluster[MAX_CLUSTER_UTF8];
                size_t cursor_cluster_len = term_render_cluster(cur_row, cur_col, cursor_cluster, sizeof cursor_cluster);
                if (cursor_cluster_len > 0) {
                    utf8proc_int32_t cp;
                    ssize_t rs = utf8proc_iterate((const uint8_t *)cursor_cluster, (utf8proc_ssize_t)cursor_cluster_len, &cp);
                    if (rs > 0) {
                        XftFont *font_to_use = font_for_cell(cursor_attrs, cp);
                        XRectangle clip_rect;
                        clip_rect.x=0; clip_rect.y=0;
                        clip_rect.width=(unsigned short)cur_w;
                        clip_rect.height=(unsigned short)g_cell_h;
                        XftDrawSetClipRectangles(draw, cur_x, cy_top, &clip_rect, 1);
                        XftDrawStringUtf8(draw, cursor_fg_color, font_to_use,
                                          cur_x, baseline0 + cur_row*(g_cell_h+line_gap),
                                          (const FcChar8 *)cursor_cluster, (int)cursor_cluster_len);
                        XftDrawSetClip(draw, NULL);
                    }
                }
            }
        }
    }

    if (back_pixmap != None && gc)
        XCopyArea(display, back_pixmap, window, gc, 0, 0, back_w, back_h, 0, 0);

    prev_cursor_row = show_cursor ? cursor_row : -1;
    if (term_lines) for (int r = 0; r < term_rows; r++) term_lines[r].dirty = 0;
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

/* ======================================================================== */
/* win.h callbacks (Phase 2 wrappers)                                       */
/* ======================================================================== */

void xbell(void) {
    XWMHints *hints;

    if (!global_display || !global_window)
        return;
    if (!window_focused) {
        hints = XGetWMHints(global_display, global_window);
        if (!hints)
            hints = XAllocWMHints();
        if (hints) {
            hints->flags |= XUrgencyHint;
            XSetWMHints(global_display, global_window, hints);
            XFree(hints);
        }
    }
    if (bellvolume)
        XkbBell(global_display, global_window, bellvolume, None);
}

static void xseturgency(int urgent) {
    XWMHints *hints;

    if (!global_display || !global_window)
        return;
    hints = XGetWMHints(global_display, global_window);
    if (!hints)
        hints = XAllocWMHints();
    if (!hints)
        return;
    if (urgent)
        hints->flags |= XUrgencyHint;
    else
        hints->flags &= ~XUrgencyHint;
    XSetWMHints(global_display, global_window, hints);
    XFree(hints);
}

static void set_utf8_window_property(char *p, int icon) {
    XTextProperty prop;
    char *list[] = { p };
    Atom net_property;

    if (!p || !global_display || !global_window)
        return;

    if (Xutf8TextListToTextProperty(global_display, list, 1,
                                    XUTF8StringStyle, &prop) == Success) {
        if (icon)
            XSetWMIconName(global_display, global_window, &prop);
        else
            XSetWMName(global_display, global_window, &prop);
        net_property = XInternAtom(global_display,
            icon ? "_NET_WM_ICON_NAME" : "_NET_WM_NAME", False);
        XSetTextProperty(global_display, global_window, &prop, net_property);
        XFree(prop.value);
    } else if (icon) {
        XSetIconName(global_display, global_window, p);
    } else {
        XStoreName(global_display, global_window, p);
    }
}

void xsettitle(char *p) {
    extern char *opt_title;
    if (!p)
        p = (opt_title && opt_title[0]) ? opt_title : "cupidterminal";
    set_utf8_window_property(p, 0);
}

void xseticontitle(char *p) {
    extern char *opt_title;
    if (!p)
        p = (opt_title && opt_title[0]) ? opt_title : "cupidterminal";
    set_utf8_window_property(p, 1);
}

void xstartdraw(void) {
    /* DEFERRED: draw_text() integrates start+per-row+cursor+flush in one
       call. Extraction into a per-row xdrawline path is future cleanup;
       current event loop calls draw_text() directly, not these callbacks. */
}

void xfinishdraw(void) { if (global_display) XFlush(global_display); }

void xdrawline(Line line, int x1, int y1, int x2) {
    (void)line; (void)x1; (void)y1; (void)x2;
    /* DEFERRED: per-row render extraction from draw_text is a future task.
       draw_text() walks rows internally; individual row rendering is not yet
       factored out into this callback. */
}

void xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og) {
    (void)cx; (void)cy; (void)g; (void)ox; (void)oy; (void)og;
    /* DEFERRED: cursor render extraction from draw_text is a future task.
       Cursor drawing is currently embedded inside draw_text(); this callback
       is not invoked by the current event loop. */
}

void xresize(int col, int row) {
    (void)col; (void)row;
    /* DEFERRED: cupidterminal has no programmatic-resize path. The window
       size is driven by X11's ConfigureNotify event which is handled in
       run() and propagates to the terminal via tresize() + ttyresize(). */
}

void xloadcols(void) {
    /* UNUSED: st.c has no caller. cupidterminal allocates colors lazily
       inside draw_text via xft_alloc_color(). */
}

int xsetcolorname(int x, const char *name) {
    (void)x; (void)name;
    /* UNUSED: st.c has no caller. Color name overrides are not yet wired;
       colors are resolved at draw time by draw_text(). */
    return 0;
}

int xparsecolor(const char *name, uint32_t *color) {
    XColor parsed;

    if (!global_display || !name || !color ||
        !XParseColor(global_display,
                     DefaultColormap(global_display, DefaultScreen(global_display)),
                     name, &parsed))
        return -1;
    *color = TRUECOLOR(parsed.red >> 8, parsed.green >> 8, parsed.blue >> 8);
    return 0;
}

int xgetcolor(int x, uint8_t *r, uint8_t *g, uint8_t *b) {
    XRenderColor color;

    if (!r || !g || !b || x < 0)
        return -1;
    color = get_xrender_color((uint32_t)x, x == (int)defaultbg, 0);
    *r = (uint8_t)(color.red >> 8);
    *g = (uint8_t)(color.green >> 8);
    *b = (uint8_t)(color.blue >> 8);
    return 0;
}

int xsetcursor(int cursor) {
    (void)cursor;
    /* UNUSED: st.c has no caller. Cursor shape selection is not yet wired;
       the cursor style is a fixed compile-time default in draw_text(). */
    return 0;
}

void xsetmode(int set, unsigned int flags) {
    (void)set; (void)flags;
    /* UNUSED: st.c has no caller. Window-mode flags (e.g. reverse video,
       hide cursor) are tracked in term.mode and read directly by draw_text(). */
}

void xsetpointermotion(int set) {
    (void)set;
    /* UNUSED: st.c has no caller. Pointer-motion tracking toggle is not yet
       wired; the X event mask is set once at window creation in x.c. */
}

void xzoom(float delta) {
    if (global_display && global_window)
        xft_zoom(global_display, global_window, delta);
}

void xzoomreset(void) {
    if (global_display && global_window)
        xft_zoom_reset(global_display, global_window);
}

void xximspot(int x, int y) {
    (void)x; (void)y;
    if (global_display && global_window)
        xximspot_xy(global_display, global_window);
}

/* ======================================================================== */
/* input.c: keymap dispatch, clipboard, mouse, selection                    */
/* ======================================================================== */

/* Forward declarations for X11 clipboard helpers defined later in this file */
void clipboard_set_data(Display *display, Window window, const uint8_t *data, size_t len);
void copy_to_clipboard(Display *display, Window window);

#define TIMEDIFF_MS(t1, t2) \
    (((t1).tv_sec - (t2).tv_sec) * 1000 + ((t1).tv_nsec - (t2).tv_nsec) / 1000000)

static unsigned char *g_primary_data = NULL;
static unsigned char *g_clip_data = NULL;
static struct timespec g_tclick1;
static struct timespec g_tclick2;
static size_t g_primary_len = 0;
static size_t g_clip_len = 0;
static int g_pty_fd = -1;
static int g_numlock = 1;
static int g_paste_incr = 0;
static int g_paste_started = 0;
static Atom g_paste_property = None;

void input_set_pty_fd(int fd) { g_pty_fd = fd; }

static int match(unsigned int mask, unsigned int state) {
    return mask == XK_ANY_MOD || mask == (state & ~ignoremod);
}

static const char *kmap(KeySym k, unsigned int state) {
    const Key *kp;
    int i;
    for (i = 0; i < (int)LEN(mappedkeys); i++)
        if (mappedkeys[i] == k) break;
    if (i == (int)LEN(mappedkeys) && (k & 0xFFFF) < 0xFD00) return NULL;
    for (kp = key; kp < key + LEN(key); kp++) {
        if (kp->k != k) continue;
        if (!match(kp->mask, state)) continue;
        if (term.application_keypad ? kp->appkey < 0 : kp->appkey > 0) continue;
        if (kp->appkey && g_numlock && kp->appkey == 2) continue;
        if (kp->appcursor && (term.application_cursor_keys ? kp->appcursor < 0 : kp->appcursor > 0)) continue;
        return kp->s;
    }
    return NULL;
}

void clipcopy(const Arg *arg) {
    (void)arg;
    if (global_display && global_window) copy_to_clipboard(global_display, global_window);
}
void clippaste(const Arg *arg) {
    (void)arg;
    if (global_display && global_window) paste_from_clipboard(global_display, global_window);
}
void selpaste(const Arg *arg) {
    (void)arg;
    if (global_display && global_window) {
        Atom utf8 = XInternAtom(global_display, "UTF8_STRING", False);
        XDeleteProperty(global_display, global_window, XA_PRIMARY);
        XConvertSelection(global_display, XA_PRIMARY, utf8, XA_PRIMARY,
                          global_window, CurrentTime);
    }
}
void zoom(const Arg *arg) {
    if (global_display && global_window)
        xft_zoom(global_display, global_window, arg->f);
}
void zoomreset(const Arg *arg) {
    (void)arg;
    if (global_display && global_window)
        xft_zoom_reset(global_display, global_window);
}
void sendbreak(const Arg *arg) {
    (void)arg;
    if (g_pty_fd >= 0) tcsendbreak(g_pty_fd, 0);
}
void numlock(const Arg *arg) { (void)arg; g_numlock ^= 1; }
void toggleprinter(const Arg *arg) { (void)arg; tprinter_toggle(); }
void printscreen(const Arg *arg) { (void)arg; tprinter_screen(); }
void printsel(const Arg *arg) { (void)arg; tprinter_selection(); }

/* tty_write_all: raw fd write for protocol responses (mouse, focus).
   No echo, no lnm expansion — used only for terminal → host direction. */
static void tty_write_all(int fd, const uint8_t *data, size_t len) {
    if (fd < 0 || !data || len == 0) return;
    if (g_x_session && g_x_session->master_fd == fd &&
        pty_session_write(g_x_session, data, len) < 0)
        perror("write error on tty");
}

void ttysend(const Arg *arg) {
    if (arg->s)
        ttywrite(arg->s, strlen(arg->s), 1);
}

/* X11-side selection helpers — delegate all state to st.c selection API */

void selection_start(int col, int row, unsigned int state) {
    int snap = 0;
    struct timespec now;
    unsigned int s = state & ~(Button1Mask | Button2Mask | Button3Mask);
    int stype;
    for (stype = 1; stype < (int)LEN(selmasks); stype++) {
        if (match(selmasks[stype], s)) {
            selstart(col, row, 0, SEL_RECTANGULAR);
            return;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (TIMEDIFF_MS(now, g_tclick2) <= (int)tripleclicktimeout)      snap = SNAP_LINE;
    else if (TIMEDIFF_MS(now, g_tclick1) <= (int)doubleclicktimeout) snap = SNAP_WORD;
    g_tclick2 = g_tclick1; g_tclick1 = now;
    selstart(col, row, snap, SEL_REGULAR);
}

void selection_extend(int col, int row) {
    selextend(col, row);
}

void selection_release(Display *display, Window window, int col, int row) {
    char *text;
    selrelease(col, row);
    text = getsel();
    if (text) {
        size_t len = strlen(text);
        unsigned char *copy = malloc(len + 1);
        if (copy) {
            memcpy(copy, text, len + 1);
            free(g_primary_data);
            g_primary_data = copy;
            g_primary_len = len;
            XSetSelectionOwner(display, XA_PRIMARY, window, CurrentTime);
            if (XGetSelectionOwner(display, XA_PRIMARY) != window)
                selclear();
        }
        free(text);
    }
}

const unsigned char *clipboard_get_data(size_t *len_out) {
    if (len_out) *len_out = g_clip_len;
    return g_clip_data;
}

void clipboard_set_data(Display *display, Window window, const uint8_t *data, size_t len) {
    Atom clipboard;
    unsigned char *copy;

    free(g_clip_data); g_clip_data = NULL; g_clip_len = 0;
    if (!display || !window || !data) return;

    copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, data, len);
    copy[len] = '\0';
    g_clip_data = copy; g_clip_len = len;

    clipboard = XInternAtom(display, "CLIPBOARD", False);
    XSetSelectionOwner(display, clipboard, window, CurrentTime);
}

void copy_to_clipboard(Display *display, Window window) {
    if (g_primary_data)
        clipboard_set_data(display, window, g_primary_data, g_primary_len);
}

void paste_from_clipboard(Display *display, Window window) {
    Atom clipboard   = XInternAtom(display, "CLIPBOARD", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    XDeleteProperty(display, window, clipboard);
    XConvertSelection(display, clipboard, utf8_string, clipboard, window, CurrentTime);
}

static void paste_chunk(unsigned char *data, size_t len) {
    unsigned char *newline;
    unsigned char *end;

    if (!data || len == 0)
        return;
    newline = data;
    end = data + len;
    while ((newline = memchr(newline, '\n', (size_t)(end - newline))) != NULL)
        *newline++ = '\r';
    if (term.bracketed_paste_mode && !g_paste_started) {
        ttywrite("\033[200~", 6, 0);
        g_paste_started = 1;
    }
    ttywrite((const char *)data, len, 1);
}

static void finish_paste(void) {
    if (term.bracketed_paste_mode && g_paste_started)
        ttywrite("\033[201~", 6, 0);
    g_paste_started = 0;
}

void handle_paste_event(Display *display, Window window, XEvent *event, int pty_fd) {
    Atom incr = XInternAtom(display, "INCR", False);
    Atom property = None;
    unsigned long offset = 0;
    unsigned long nitems;
    unsigned long bytes_after;
    Atom actual_type;
    int actual_format;
    unsigned char *data = NULL;

    (void)pty_fd;
    if (event->type == SelectionNotify) {
        property = event->xselection.property;
        g_paste_started = 0;
    } else if (event->type == PropertyNotify &&
               event->xproperty.state == PropertyNewValue &&
               g_paste_incr && event->xproperty.atom == g_paste_property) {
        property = event->xproperty.atom;
    } else {
        return;
    }
    if (property == None)
        return;

    do {
        if (XGetWindowProperty(display, window, property, (long)offset,
                               BUFSIZ / 4, False, AnyPropertyType,
                               &actual_type, &actual_format, &nitems,
                               &bytes_after, &data) != Success) {
            fprintf(stderr, "cupidterminal: clipboard property read failed\n");
            finish_paste();
            g_paste_incr = 0;
            return;
        }

        if (actual_type == incr) {
            XFree(data);
            g_paste_incr = 1;
            g_paste_property = property;
            XDeleteProperty(display, window, property);
            XFlush(display);
            return;
        }

        if (actual_format != 0 && actual_format != 8) {
            XFree(data);
            finish_paste();
            g_paste_incr = 0;
            XDeleteProperty(display, window, property);
            return;
        }

        if (nitems > 0)
            paste_chunk(data, (size_t)nitems * (size_t)actual_format / 8);
        XFree(data);
        data = NULL;

        if (actual_format > 0)
            offset += nitems * (unsigned long)actual_format / 32;
    } while (bytes_after > 0);

    if (g_paste_incr) {
        if (nitems == 0) {
            finish_paste();
            g_paste_incr = 0;
            g_paste_property = None;
        }
        XDeleteProperty(display, window, property);
        XFlush(display);
    } else {
        finish_paste();
        XDeleteProperty(display, window, property);
    }
}

void send_mouse_report(int pty_fd, int event_type, int button, int col, int row,
    unsigned int modifiers, int sgr_mode) {
    if (pty_fd < 0 || col < 1 || row < 1) return;
    int code = (event_type == 2) ? 32 : 0;

    if ((!sgr_mode && event_type == 1) || button == 12) code += 3;
    else if (button >= 8) code += 128 + button - 8;
    else if (button >= 4) code += 64 + button - 4;
    else code += button - 1;

    if (modifiers & ShiftMask)   code += 4;
    if (modifiers & Mod1Mask)    code += 8;
    if (modifiers & ControlMask) code += 16;

    if (sgr_mode) {
        char buf[32];
        char final = (event_type == 1) ? 'm' : 'M';
        int n = snprintf(buf, sizeof(buf), "\033[<%d;%d;%d%c", code, col, row, final);
        if (n > 0 && (size_t)n < sizeof(buf))
            tty_write_all(pty_fd, (const uint8_t *)buf, (size_t)n);
    } else if (col <= 223 && row <= 223) {
        unsigned char buf[6];
        buf[0]='\033'; buf[1]='['; buf[2]='M';
        buf[3]=(unsigned char)(32+code);
        buf[4]=(unsigned char)(32+col);
        buf[5]=(unsigned char)(32+row);
        tty_write_all(pty_fd, buf, 6);
    }
}

static void tty_write(int fd, const char *data, size_t len) {
    (void)fd;  /* fd unused: user input routes through ttywrite/g_pty_session */
    ttywrite(data, len, 1);
}

static int mouseaction(XEvent *e, unsigned int release) {
    const MouseShortcut *ms;
    unsigned int state = e->xbutton.state & ~(Button1Mask | Button2Mask | Button3Mask);
    for (ms = mshortcuts; ms < mshortcuts + LEN(mshortcuts); ms++) {
        if (ms->release == release && ms->button == e->xbutton.button &&
            (match(ms->mod, state) || match(ms->mod, state & ~forcemousemod))) {
            if (ms->func) ms->func(&ms->arg);
            return 1;
        }
    }
    return 0;
}

int handle_mouse_shortcut(XEvent *event, int pty_fd) {
    g_pty_fd = pty_fd;
    if (event->type == ButtonPress)   return mouseaction(event, 0);
    if (event->type == ButtonRelease) return mouseaction(event, 1);
    return 0;
}

void handle_keypress(Display *display, Window window, XEvent *event, int pty_fd) {
    char buffer[128];
    KeySym keysym;
    int len;
    unsigned int state;
    Status xim_status;

    (void)display; (void)window;

    if (term.keyboard_lock) {
        return;
    }

    if (g_xic) {
        keysym = NoSymbol;
        len = XmbLookupString(g_xic, &event->xkey, buffer, sizeof(buffer)-1, &keysym, &xim_status);
        if (len < 0) len = 0;
        buffer[len] = '\0';
        switch (xim_status) {
        case XLookupNone:    return;
        case XBufferOverflow: len = 0; keysym = XLookupKeysym(&event->xkey, 0); break;
        case XLookupChars:   keysym = XLookupKeysym(&event->xkey, 0); break;
        case XLookupKeySym:
        case XLookupBoth:    break;
        default:             keysym = XLookupKeysym(&event->xkey, 0); break;
        }
    } else {
        len = XLookupString(&event->xkey, buffer, sizeof(buffer), &keysym, NULL);
    }
    state = event->xkey.state;
    g_pty_fd = pty_fd;

    {
        const Shortcut *bp;
        for (bp = shortcuts; bp < shortcuts + LEN(shortcuts); bp++) {
            if (keysym == bp->keysym && match(bp->mod, state)) {
                if (bp->func) bp->func(&bp->arg);
                return;
            }
        }
    }

    if (!term.alt_screen_active && (state & ShiftMask) && keysym == XK_Page_Up) {
        kscrollup_n((term_rows > 1) ? (term_rows - 1) : 1); return;
    }
    if (!term.alt_screen_active && (state & ShiftMask) && keysym == XK_Page_Down) {
        kscrolldown_n((term_rows > 1) ? (term_rows - 1) : 1); return;
    }

    {
        const char *customkey;
        if ((customkey = kmap(keysym, state))) {
            ttywrite(customkey, strlen(customkey), 1);
            return;
        }
    }

    if (keysym == XK_Up || keysym == XK_Down || keysym == XK_Left || keysym == XK_Right) {
        unsigned int st = event->xkey.state;
        int mod = 0;
        if (st & ShiftMask)   mod |= 1;
        if (st & Mod1Mask)    mod |= 2;
        if (st & ControlMask) mod |= 4;
        if (mod > 0) mod += 1;

        char buf[16];
        const char *seq;
        size_t seqlen;
        if (term.application_cursor_keys) {
            static const char *app[] = {"\x1bOA","\x1bOB","\x1bOC","\x1bOD"};
            static const char app_char[] = {'A','B','C','D'};
            int idx = (keysym==XK_Up)?0:(keysym==XK_Down)?1:(keysym==XK_Left)?3:2;
            if (mod > 0) { seqlen=(size_t)snprintf(buf,sizeof(buf),"\x1b[1;%d%c",mod,app_char[idx]); seq=buf; }
            else { seq=app[idx]; seqlen=3; }
        } else {
            if (mod > 0) {
                char ch=(keysym==XK_Up)?'A':(keysym==XK_Down)?'B':(keysym==XK_Left)?'D':'C';
                seqlen=(size_t)snprintf(buf,sizeof(buf),"\x1b[1;%d%c",mod,ch); seq=buf;
            } else {
                static const char *norm[] = {"\x1b[A","\x1b[B","\x1b[C","\x1b[D"};
                int idx=(keysym==XK_Up)?0:(keysym==XK_Down)?1:(keysym==XK_Left)?3:2;
                seq=norm[idx]; seqlen=3;
            }
        }
        tty_write(pty_fd, seq, seqlen);
        return;
    }

    else if (keysym == XK_Home)      { tty_write(pty_fd, "\x1b[H",  3); }
    else if (keysym == XK_End)       { tty_write(pty_fd, "\x1b[F",  3); }
    else if (keysym == XK_Page_Up)   { tty_write(pty_fd, "\x1b[5~", 4); }
    else if (keysym == XK_Page_Down) { tty_write(pty_fd, "\x1b[6~", 4); }
    else if (keysym == XK_Insert)    { tty_write(pty_fd, "\x1b[2~", 4); }
    else if (keysym == XK_Delete)    { tty_write(pty_fd, "\x1b[3~", 4); }
    else if (keysym == XK_F1)        { tty_write(pty_fd, "\x1bOP",  4); }
    else if (keysym == XK_F2)        { tty_write(pty_fd, "\x1bOQ",  4); }
    else if (keysym == XK_F3)        { tty_write(pty_fd, "\x1bOR",  4); }
    else if (keysym == XK_F4)        { tty_write(pty_fd, "\x1bOS",  4); }
    else if (keysym == XK_F5)        { tty_write(pty_fd, "\x1b[15~",5); }
    else if (keysym == XK_F6)        { tty_write(pty_fd, "\x1b[17~",5); }
    else if (keysym == XK_F7)        { tty_write(pty_fd, "\x1b[18~",5); }
    else if (keysym == XK_F8)        { tty_write(pty_fd, "\x1b[19~",5); }
    else if (keysym == XK_F9)        { tty_write(pty_fd, "\x1b[20~",5); }
    else if (keysym == XK_F10)       { tty_write(pty_fd, "\x1b[21~",5); }
    else if (keysym == XK_F11)       { tty_write(pty_fd, "\x1b[23~",5); }
    else if (keysym == XK_F12)       { tty_write(pty_fd, "\x1b[24~",5); }

    if (keysym == XK_BackSpace) {
        tty_write(pty_fd, "\x7F", 1);
    } else if (keysym == XK_Return) {
        tty_write(pty_fd, "\r", 1);
    } else if ((event->xkey.state & Mod1Mask) && len > 0) {
        if (term.meta_eight_bit && len == 1 &&
            (unsigned char)buffer[0] < 0x7f) {
            utf8proc_uint8_t encoded[4];
            utf8proc_ssize_t encoded_len =
                utf8proc_encode_char((utf8proc_int32_t)
                    ((unsigned char)buffer[0] | 0x80), encoded);
            if (encoded_len > 0) {
                tty_write(pty_fd, (const char *)encoded, (size_t)encoded_len);
            }
        } else {
            tty_write(pty_fd, "\x1b", 1);
            tty_write(pty_fd, buffer, (size_t)len);
        }
    } else if (len > 0) {
        tty_write(pty_fd, buffer, len);
    }
}

/* win.h clipboard callbacks */
void xsetsel(char *str) {
    if (!str || !global_display || !global_window) return;
    size_t len = strlen(str);
    unsigned char *copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, str, len + 1);
    free(g_primary_data);
    g_primary_data = copy;
    g_primary_len = len;
    XSetSelectionOwner(global_display, XA_PRIMARY, global_window, CurrentTime);
    if (XGetSelectionOwner(global_display, XA_PRIMARY) != global_window) {
        free(g_primary_data);
        g_primary_data = NULL;
        g_primary_len = 0;
        selclear();
    }
}

void xclipcopy(void) {
    if (!global_display || !global_window) return;
    /* Re-uses existing selection extraction in copy_to_clipboard which
       walks the current selection and pushes to X CLIPBOARD. */
    copy_to_clipboard(global_display, global_window);
}

/* ======================================================================== */
/* PTY callback and output handler (moved from main.c)                      */
/* ======================================================================== */

static void pty_response_cb(const uint8_t *bytes, size_t len, void *ctx) {
    PtySession *session = (PtySession *)ctx;
    if (session && session->master_fd >= 0 && len > 0)
        (void)pty_session_write(session, bytes, len);
}

int handle_pty_output(Display *display, Window window, GC gc, PtySession *session, Term *state) {
    (void)display; (void)window; (void)gc;
    char buf[65536];
    int got_data = 0;

    for (;;) {
        ssize_t num_read = pty_session_read(session, buf, sizeof(buf) - 1);
        if (num_read > 0) {
            got_data = 1;
            twrite((const uint8_t *)buf, (size_t)num_read, state, pty_response_cb, session);
            if (state->title_dirty) {
                /* xsettitle() is already called from oschandle in st.c when the
                   title is set; just clear the flag here. */
                state->title_dirty = 0;
            }
            if (state->osc52_pending) {
                /* Route OSC-52 through the win.h contract (xsetsel) instead of
                   calling clipboard_set_data directly. */
                size_t n = (state->osc52_len < sizeof(state->osc52_buf) - 1)
                           ? state->osc52_len : sizeof(state->osc52_buf) - 2;
                state->osc52_buf[n] = '\0';
                xsetsel((char *)state->osc52_buf);
                xclipcopy();
                state->osc52_pending = 0; state->osc52_len = 0;
            }
        } else if (num_read == 0) {
            return 0;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }
    return got_data ? 1 : 0;
}

/* ======================================================================== */
/* sync_pty_winsize_from_window (X11 territory)                            */
/* ======================================================================== */

void sync_pty_winsize_from_window(Display *display, Window window, PtySession *session) {
    XWindowAttributes wa;
    int cw, ch;
    unsigned short ws_col, ws_row;

    if (!display || !session) return;
    if (!XGetWindowAttributes(display, window, &wa)) return;

    cw = (g_cell_w > 0) ? (g_cell_w + g_cell_gap) : xft_font->max_advance_width;
    ch = (g_cell_h > 0) ? g_cell_h : (xft_font->ascent + xft_font->descent + 2);

    {
        int usable_w = wa.width  - DRAW_LEFT_PAD;
        int usable_h = wa.height - DRAW_TOP_PAD;
        if (usable_w < 1) usable_w = 1;
        if (usable_h < 1) usable_h = 1;
        ws_col = (unsigned short)(usable_w / (cw ? cw : 8));
        ws_row = (unsigned short)(usable_h / (ch ? ch : 16));
    }
    if (ws_col == 0) ws_col = 1;
    if (ws_row == 0) ws_row = 1;

    pty_session_set_winsize(session, ws_row, ws_col);
    tresize(ws_row, ws_col);
}

void xsync_pty_winsize(PtySession *session) {
    sync_pty_winsize_from_window(global_display, global_window, session);
}

/* ======================================================================== */
/* xinit() + run() — X11 init and event loop (moved from main.c)           */
/* ======================================================================== */

static int x_geometry_mask;
static int x_geometry_x;
static int x_geometry_y;

/* Parse an st-style character geometry and retain its placement hints. */
void parse_geometry_str(const char *geom, unsigned int *cols, unsigned int *rows) {
    unsigned int w = 0, h = 0;
    if (!geom || !cols || !rows) return;
    x_geometry_mask = XParseGeometry(geom, &x_geometry_x, &x_geometry_y, &w, &h);
    if (x_geometry_mask & WidthValue)  *cols = w;
    if (x_geometry_mask & HeightValue) *rows = h;
}

static Display *x_display = NULL;
static Window   x_window  = 0;
static GC       x_gc      = 0;

void xinit(int cols, int rows, PtySession *session) {
    XClassHint class_hint;
    XSizeHints size_hint;
    XWMHints wm_hint;
    Window parent;
    Window root;
    int screen;
    int win_x = x_geometry_x;
    int win_y = x_geometry_y;
    int win_w;
    int win_h;
    int step_w;
    int step_h;
    char window_id[32];
    const char *title;

    extern char *opt_class;
    extern char *opt_embed;
    extern char *opt_name;
    extern char *opt_title;
    extern int opt_fixed;

    g_x_session = session;

    XSetLocaleModifiers("");

    x_display = XOpenDisplay(NULL);
    if (!x_display) {
        fprintf(stderr, "Failed to open X display\n");
        exit(EXIT_FAILURE);
    }

    screen = DefaultScreen(x_display);
    root = RootWindow(x_display, screen);
    parent = root;
    if (opt_embed && opt_embed[0]) {
        char *end = NULL;
        unsigned long candidate = strtoul(opt_embed, &end, 0);
        if (!end || *end != '\0' || candidate == 0) {
            fprintf(stderr, "cupidterminal: invalid window id: %s\n", opt_embed);
            exit(EXIT_FAILURE);
        }
        parent = (Window)candidate;
    }

    x_window = XCreateSimpleWindow(x_display, root,
                                   0, 0, 1, 1, 0,
                                   BlackPixel(x_display, screen),
                                   BlackPixel(x_display, screen));
    if (!x_window) {
        fprintf(stderr, "cupidterminal: could not create window\n");
        exit(EXIT_FAILURE);
    }
    if (parent != root)
        XReparentWindow(x_display, x_window, parent, 0, 0);

    XSetWindowBackground(x_display, x_window, BlackPixel(x_display, screen));
    title = (opt_title && opt_title[0]) ? opt_title : "cupidterminal";
    XStoreName(x_display, x_window, title);
    XSetIconName(x_display, x_window, title);

    {
        Atom wm_delete = XInternAtom(x_display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(x_display, x_window, &wm_delete, 1);
    }

    XSelectInput(x_display, x_window,
        ExposureMask | KeyPressMask | KeyReleaseMask | PropertyChangeMask |
        StructureNotifyMask | FocusChangeMask |
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
    x_gc = XCreateGC(x_display, x_window, 0, NULL);

    initialize_xft(x_display, x_window);
    xsettitle((char *)title);
    xseticontitle((char *)title);

    step_w = g_cell_w + g_cell_gap;
    step_h = g_cell_h + LINE_GAP;
    if (step_w < 1) step_w = 1;
    if (step_h < 1) step_h = 1;
    win_w = DRAW_LEFT_PAD + cols * step_w;
    win_h = DRAW_TOP_PAD + rows * step_h;
    if (x_geometry_mask & XNegative)
        win_x += DisplayWidth(x_display, screen) - win_w;
    if (x_geometry_mask & YNegative)
        win_y += DisplayHeight(x_display, screen) - win_h;

    memset(&size_hint, 0, sizeof(size_hint));
    size_hint.flags = PSize | PResizeInc | PBaseSize | PMinSize;
    size_hint.width = win_w;
    size_hint.height = win_h;
    size_hint.width_inc = step_w;
    size_hint.height_inc = step_h;
    size_hint.base_width = DRAW_LEFT_PAD;
    size_hint.base_height = DRAW_TOP_PAD;
    size_hint.min_width = DRAW_LEFT_PAD + step_w;
    size_hint.min_height = DRAW_TOP_PAD + step_h;
    if (opt_fixed) {
        size_hint.flags |= PMaxSize;
        size_hint.min_width = size_hint.max_width = win_w;
        size_hint.min_height = size_hint.max_height = win_h;
    }
    if (x_geometry_mask & (XValue | YValue)) {
        size_hint.flags |= USPosition | PWinGravity;
        size_hint.x = win_x;
        size_hint.y = win_y;
        size_hint.win_gravity =
            (x_geometry_mask & XNegative)
                ? ((x_geometry_mask & YNegative) ? SouthEastGravity : NorthEastGravity)
                : ((x_geometry_mask & YNegative) ? SouthWestGravity : NorthWestGravity);
    }

    memset(&wm_hint, 0, sizeof(wm_hint));
    wm_hint.flags = InputHint;
    wm_hint.input = True;
    class_hint.res_name = (opt_name && opt_name[0]) ? opt_name : termname;
    class_hint.res_class = (opt_class && opt_class[0]) ? opt_class : termname;
    XSetWMProperties(x_display, x_window, NULL, NULL, NULL, 0,
                     &size_hint, &wm_hint, &class_hint);

    {
        Atom net_wm_pid = XInternAtom(x_display, "_NET_WM_PID", False);
        long pid = (long)getpid();
        XChangeProperty(x_display, x_window, net_wm_pid, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)&pid, 1);
    }

    XMoveResizeWindow(x_display, x_window, win_x, win_y,
                      (unsigned int)win_w, (unsigned int)win_h);
    XMapWindow(x_display, x_window);
    XSync(x_display, False);

    snprintf(window_id, sizeof(window_id), "%lu", (unsigned long)x_window);
    if (setenv("WINDOWID", window_id, 1) == -1) {
        perror("setenv WINDOWID");
        exit(EXIT_FAILURE);
    }

    {
        XWindowAttributes wa_init;
        if (XGetWindowAttributes(x_display, x_window, &wa_init))
            draw_notify_resize(wa_init.width, wa_init.height);
    }

    if (session && session->master_fd >= 0)
        sync_pty_winsize_from_window(x_display, x_window, session);
}

void run(void) {
    XEvent event;
    fd_set fds, wfds;
    Atom XA_UTF8    = XInternAtom(x_display, "UTF8_STRING", False);
    Atom XA_TEXT    = XInternAtom(x_display, "TEXT", False);
    Atom XA_TARGETS = XInternAtom(x_display, "TARGETS", False);
    Atom XA_XEMBED  = XInternAtom(x_display, "_XEMBED", False);

    int drawing = 0;
    struct timespec trigger = {0, 0};
    struct timespec now;

    while (1) {
        int x11_fd, nfds, ready;
        struct timeval *tv_ptr = NULL, tv;
        double timeout_ms = -1, elapsed;

        pty_reap(g_x_session);
        if (!pty_session_child_alive(g_x_session)) break;

        FD_ZERO(&fds);
        FD_ZERO(&wfds);
        x11_fd = ConnectionNumber(x_display);
        FD_SET(x11_fd, &fds);
        nfds = x11_fd + 1;

        if (g_x_session->master_fd >= 0) {
            FD_SET(g_x_session->master_fd, &fds);
            if (pty_session_wants_write(g_x_session))
                FD_SET(g_x_session->master_fd, &wfds);
            if (g_x_session->master_fd >= nfds) nfds = g_x_session->master_fd + 1;
        }

        if (XPending(x_display)) {
            timeout_ms = 0;
        } else if (drawing && minlatency > 0 && maxlatency > 0) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            elapsed = (now.tv_sec - trigger.tv_sec)*1000.0 + (now.tv_nsec - trigger.tv_nsec)/1e6;
            timeout_ms = (maxlatency - elapsed) / maxlatency * minlatency;
            if (timeout_ms <= 0) timeout_ms = 0;
        }

        if (timeout_ms >= 0) {
            tv.tv_sec  = (long)(timeout_ms / 1000);
            tv.tv_usec = (long)((timeout_ms - tv.tv_sec*1000.0) * 1000);
            if (tv.tv_sec < 0) tv.tv_sec = 0;
            if (tv.tv_usec < 0) tv.tv_usec = 0;
            tv_ptr = &tv;
        }

        ready = select(nfds, &fds, &wfds, NULL, tv_ptr);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select failed");
            break;
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        int had_input = 0;

        if (ready > 0 && g_x_session->master_fd >= 0 &&
            FD_ISSET(g_x_session->master_fd, &wfds)) {
            if (pty_session_flush(g_x_session) < 0) {
                perror("write error on tty");
                break;
            }
        }

        if (ready > 0 && g_x_session->master_fd >= 0 && FD_ISSET(g_x_session->master_fd, &fds)) {
            had_input = 1;
            if (!handle_pty_output(x_display, x_window, x_gc, g_x_session, &term)) {
                pty_reap(g_x_session);
                break;
            }
        }

        while (XPending(x_display)) {
            had_input = 1;
            XNextEvent(x_display, &event);
            if (XFilterEvent(&event, None)) continue;

            if (event.type == KeyPress) {
                handle_keypress(x_display, x_window, &event, g_x_session->master_fd);
            } else if (event.type == Expose) {
                /* draw deferred */
            } else if (event.type == SelectionNotify) {
                handle_paste_event(x_display, x_window, &event, g_x_session->master_fd);
            } else if (event.type == PropertyNotify) {
                handle_paste_event(x_display, x_window, &event, g_x_session->master_fd);
            } else if (event.type == SelectionClear) {
                Atom clipboard = XInternAtom(x_display, "CLIPBOARD", False);
                if (event.xselectionclear.selection == XA_PRIMARY) {
                    free(g_primary_data);
                    g_primary_data = NULL;
                    g_primary_len = 0;
                    selclear();
                } else if (event.xselectionclear.selection == clipboard) {
                    free(g_clip_data);
                    g_clip_data = NULL;
                    g_clip_len = 0;
                }
            } else if (event.type == ConfigureNotify) {
                draw_notify_resize(event.xconfigure.width, event.xconfigure.height);
                sync_pty_winsize_from_window(x_display, x_window, g_x_session);
            } else if (event.type == ButtonPress || event.type == ButtonRelease ||
                       event.type == MotionNotify) {
                unsigned int event_state = event.type == MotionNotify
                    ? event.xmotion.state : event.xbutton.state;
                int mouse_active = term.mouse_reporting_x10 ||
                    term.mouse_reporting_basic ||
                    term.mouse_reporting_button || term.mouse_reporting_any;
                int app_mouse_event = mouse_active &&
                    g_x_session->master_fd >= 0 &&
                    !(event_state & forcemousemod);
                int cupid_scrollback_wheel =
                    event.type == ButtonPress && !term.alt_screen_active &&
                    !mouse_active && !(event_state & forcemousemod) &&
                    (event.xbutton.button == Button4 ||
                     event.xbutton.button == Button5);

                if (!app_mouse_event && !cupid_scrollback_wheel &&
                    handle_mouse_shortcut(&event, g_x_session->master_fd)) {
                    /* handled */
                } else {
                    if (app_mouse_event) {
                        int r=0, c=0, btn=0, evt=-1;
                        unsigned int mods=0;
                        if (event.type == ButtonPress) {
                            xy_to_cell(event.xbutton.x, event.xbutton.y, &r, &c);
                            btn=event.xbutton.button; mods=event.xbutton.state; evt=0;
                        } else if (event.type == ButtonRelease) {
                            if (term.mouse_reporting_x10) {
                                evt = -1;
                                btn = 0;
                            } else {
                            xy_to_cell(event.xbutton.x, event.xbutton.y, &r, &c);
                            btn=event.xbutton.button; mods=event.xbutton.state;
                            if (btn==4||btn==5) evt=-1; else evt=1;
                            }
                        } else {
                            if (term.mouse_reporting_x10) {
                                evt = -1;
                                btn = 0;
                            } else {
                            xy_to_cell(event.xmotion.x, event.xmotion.y, &r, &c);
                            mods=event.xmotion.state;
                            if (term.mouse_reporting_any) { evt=2; btn=12; }
                            else if (term.mouse_reporting_button &&
                                    (mods&(Button1Mask|Button2Mask|Button3Mask))) {
                                evt=2;
                                btn=(mods&Button1Mask)?1:(mods&Button2Mask)?2:3;
                            }
                            }
                        }
                        if (term.mouse_reporting_x10)
                            mods = 0;
                        if (evt>=0 && btn>=1 && btn<=12)
                            send_mouse_report(g_x_session->master_fd, evt, btn,
                                c+1, r+1, mods, term.mouse_sgr_mode);
                    } else if (event.type == ButtonPress && !term.alt_screen_active &&
                               (event.xbutton.button==Button4||event.xbutton.button==Button5)) {
                        if (event.xbutton.button == Button4) kscrollup_n(1);
                        else kscrolldown_n(1);
                    } else if (event.type == ButtonPress && event.xbutton.button == Button1) {
                        int r, c;
                        xy_to_cell(event.xbutton.x, event.xbutton.y, &r, &c);
                        selection_start(c, r, event.xbutton.state);
                    } else if (event.type == MotionNotify && selisactive() &&
                               (event.xmotion.state & Button1Mask)) {
                        int r, c;
                        xy_to_cell(event.xmotion.x, event.xmotion.y, &r, &c);
                        selection_extend(c, r);
                    } else if (event.type == ButtonRelease && event.xbutton.button == Button1) {
                        int r, c;
                        xy_to_cell(event.xbutton.x, event.xbutton.y, &r, &c);
                        selection_release(x_display, x_window, c, r);
                    }
                }
            } else if (event.type == FocusIn) {
                if (event.xfocus.mode == NotifyGrab)
                    continue;
                window_focused = 1;
                xseturgency(0);
                xim_focus_in();
                if (term.focus_mode && g_x_session->master_fd >= 0)
                    (void)pty_session_write(g_x_session, "\033[I", 3);
            } else if (event.type == FocusOut) {
                if (event.xfocus.mode == NotifyGrab)
                    continue;
                window_focused = 0;
                xim_focus_out();
                if (term.focus_mode && g_x_session->master_fd >= 0)
                    (void)pty_session_write(g_x_session, "\033[O", 3);
            } else if (event.type == ClientMessage) {
                Atom wm_protocols = XInternAtom(x_display, "WM_PROTOCOLS", False);
                Atom wm_delete    = XInternAtom(x_display, "WM_DELETE_WINDOW", False);
                if (event.xclient.message_type == XA_XEMBED &&
                    event.xclient.format == 32 &&
                    event.xclient.data.l[1] == XEMBED_FOCUS_IN) {
                    window_focused = 1;
                    xseturgency(0);
                } else if (event.xclient.message_type == XA_XEMBED &&
                           event.xclient.format == 32 &&
                           event.xclient.data.l[1] == XEMBED_FOCUS_OUT) {
                    window_focused = 0;
                } else if (event.xclient.message_type == wm_protocols &&
                    (Atom)event.xclient.data.l[0] == wm_delete) {
                    pty_session_close(g_x_session);
                    cleanup_xft();
                    XCloseDisplay(x_display);
                    exit(0);
                }
            } else if (event.type == SelectionRequest) {
                XSelectionRequestEvent *req = &event.xselectionrequest;
                Atom property = (req->property == None) ? req->target : req->property;
                XSelectionEvent ev = {
                    .type=SelectionNotify, .display=req->display,
                    .requestor=req->requestor, .selection=req->selection,
                    .target=req->target, .property=None, .time=req->time
                };
                size_t len = 0;
                const unsigned char *data = NULL;
                Atom clipboard = XInternAtom(x_display, "CLIPBOARD", False);
                if (req->selection == XA_PRIMARY) {
                    data = g_primary_data;
                    len = g_primary_len;
                } else if (req->selection == clipboard) {
                    data = clipboard_get_data(&len);
                }
                if (!data) {
                    XSendEvent(x_display, req->requestor, True, 0, (XEvent*)&ev);
                    XFlush(x_display);
                    continue;
                }
                if (req->target == XA_TARGETS) {
                    Atom targets[4] = {XA_UTF8, XA_TEXT, XA_STRING, XA_TARGETS};
                    XChangeProperty(x_display, req->requestor, property, XA_ATOM, 32,
                                    PropModeReplace, (unsigned char*)targets, 4);
                    ev.property = property;
                } else if (req->target==XA_UTF8 || req->target==XA_TEXT || req->target==XA_STRING) {
                    Atom type = (req->target==XA_STRING) ? XA_STRING : XA_UTF8;
                    XChangeProperty(x_display, req->requestor, property, type, 8,
                                    PropModeReplace, (unsigned char*)data, (int)len);
                    ev.property = property;
                }
                XSendEvent(x_display, req->requestor, True, 0, (XEvent*)&ev);
                XFlush(x_display);
            }
        }

        if (g_x_session->write_failed)
            break;

        /* Draw latency */
        if (had_input) {
            if (!drawing) { trigger = now; drawing = 1; }
            if (minlatency > 0 && maxlatency > 0) {
                elapsed = (now.tv_sec - trigger.tv_sec)*1000.0 + (now.tv_nsec - trigger.tv_nsec)/1e6;
                timeout_ms = (maxlatency - elapsed) / maxlatency * minlatency;
                if (timeout_ms > 0) continue;
            }
        }

        draw_text(x_display, x_window, x_gc);
        xximspot_xy(x_display, x_window);
        XFlush(x_display);
        drawing = 0;
    }

    pty_session_close(g_x_session);
    cleanup_xft();
    XCloseDisplay(x_display);
}
