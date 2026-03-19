// draw.c
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <utf8proc.h> // unicode
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

#define COLOR_CACHE_SIZE (COLOR_DEFAULT_BG + 1)
static XftColor color_cache[COLOR_CACHE_SIZE];
static int color_allocated[COLOR_CACHE_SIZE] = {0};
static XftColor faint_color_cache[COLOR_CACHE_SIZE];
static int faint_color_allocated[COLOR_CACHE_SIZE] = {0};

#define TRUECOLOR_CACHE_SIZE 256
static struct {
    uint32_t key;
    XftColor color;
    int allocated;
} truecolor_cache[TRUECOLOR_CACHE_SIZE];
static struct {
    uint32_t key;
    XftColor color;
    int allocated;
} truecolor_faint_cache[TRUECOLOR_CACHE_SIZE];
static int truecolor_cache_next;
static int truecolor_faint_cache_next;

#define GLYPH_FALLBACK_CACHE_SIZE 256
typedef struct {
    utf8proc_int32_t cp;
    uint8_t style;
    XftFont *font;
} GlyphFallbackFont;

static GlyphFallbackFont glyph_fallback_cache[GLYPH_FALLBACK_CACHE_SIZE];
static int glyph_fallback_count;

static void clear_glyph_fallback_cache(Display *display) {
    if (!display) {
        glyph_fallback_count = 0;
        return;
    }

    for (int i = 0; i < glyph_fallback_count; i++) {
        if (glyph_fallback_cache[i].font) {
            XftFontClose(display, glyph_fallback_cache[i].font);
            glyph_fallback_cache[i].font = NULL;
        }
    }
    glyph_fallback_count = 0;
}

static uint8_t font_style_key(uint16_t attrs) {
    uint8_t key = 0;
    if (attrs & ATTR_BOLD) {
        key |= 1;
    }
    if (attrs & ATTR_ITALIC) {
        key |= 2;
    }
    return key;
}

static XftFont *find_glyph_fallback(utf8proc_int32_t cp, uint8_t style) {
    for (int i = 0; i < glyph_fallback_count; i++) {
        if (glyph_fallback_cache[i].cp == cp && glyph_fallback_cache[i].style == style) {
            return glyph_fallback_cache[i].font;
        }
    }
    return NULL;
}

static XftFont *load_glyph_fallback(Display *display, XftFont *base_font, utf8proc_int32_t cp) {
    FcPattern *pattern;
    FcPattern *match;
    FcCharSet *charset;
    FcResult result;
    XftFont *font = NULL;

    if (!display || !base_font || !base_font->pattern || cp < 0) {
        return NULL;
    }

    pattern = FcPatternDuplicate(base_font->pattern);
    if (!pattern) {
        return NULL;
    }

    charset = FcCharSetCreate();
    if (!charset) {
        FcPatternDestroy(pattern);
        return NULL;
    }

    FcCharSetAddChar(charset, (FcChar32)cp);
    FcPatternAddCharSet(pattern, FC_CHARSET, charset);
    FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    match = FcFontMatch(NULL, pattern, &result);
    if (match) {
        font = XftFontOpenPattern(display, match);
    }

    FcCharSetDestroy(charset);
    FcPatternDestroy(pattern);

    if (!font) {
        return NULL;
    }

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

static XRenderColor get_xrender_color(uint32_t c, int is_bg, int is_faint) {
    XRenderColor xc;

    if (COLOR_IS_TRUE_RGB(c)) {
        unsigned int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        xc = (XRenderColor){
            (unsigned short)(r * 0x101),
            (unsigned short)(g * 0x101),
            (unsigned short)(b * 0x101),
            0xFFFF
        };
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
        xc = is_bg ? (XRenderColor){0x0000, 0x0000, 0x0000, 0xFFFF}
                   : (XRenderColor){0xE5E5, 0xE5E5, 0xE5E5, 0xFFFF};
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
        int i, idx;
        XRenderColor rc;
        if (is_faint && !is_bg) {
            for (i = 0; i < TRUECOLOR_CACHE_SIZE; i++) {
                if (truecolor_faint_cache[i].allocated && truecolor_faint_cache[i].key == logical_color)
                    return &truecolor_faint_cache[i].color;
            }
            idx = truecolor_faint_cache_next++ % TRUECOLOR_CACHE_SIZE;
            rc = get_xrender_color(logical_color, is_bg, is_faint);
            if (XftColorAllocValue(d, DefaultVisual(d, DefaultScreen(d)),
                    DefaultColormap(d, DefaultScreen(d)), &rc, &truecolor_faint_cache[idx].color)) {
                truecolor_faint_cache[idx].key = logical_color;
                truecolor_faint_cache[idx].allocated = 1;
                return &truecolor_faint_cache[idx].color;
            }
        } else {
            for (i = 0; i < TRUECOLOR_CACHE_SIZE; i++) {
                if (truecolor_cache[i].allocated && truecolor_cache[i].key == logical_color)
                    return &truecolor_cache[i].color;
            }
            idx = truecolor_cache_next++ % TRUECOLOR_CACHE_SIZE;
            rc = get_xrender_color(logical_color, is_bg, is_faint);
            if (XftColorAllocValue(d, DefaultVisual(d, DefaultScreen(d)),
                    DefaultColormap(d, DefaultScreen(d)), &rc, &truecolor_cache[idx].color)) {
                truecolor_cache[idx].key = logical_color;
                truecolor_cache[idx].allocated = 1;
                return &truecolor_cache[idx].color;
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

/* Try to load font from pattern; returns font or NULL. Caller must not destroy pattern. */
static XftFont *try_load_font(Display *display, const char *pattern_str) {
    FcPattern *pattern = FcNameParse((const FcChar8 *)pattern_str);
    if (!pattern) return NULL;
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    XftFont *font = XftFontOpenPattern(display, FcFontMatch(NULL, pattern, &result));
    FcPatternDestroy(pattern);
    return font;
}

/* Extract primary font family from FONT (before first comma), trim trailing spaces. */
static void get_primary_font_family(char *out, size_t out_size) {
    const char *comma = strchr(FONT, ',');
    size_t len = comma ? (size_t)(comma - FONT) : strlen(FONT);
    if (len >= out_size) len = out_size - 1;
    if (len > 0) {
        memcpy(out, FONT, len);
        out[len] = '\0';
        while (len > 0 && out[len - 1] == ' ') { out[--len] = '\0'; }
    } else {
        out[0] = '\0';
    }
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
    g_cell_h = xft_font->ascent + xft_font->descent;
}

// Initialize Xft for Unicode and emoji support
void initialize_xft(Display *display, Window window) {
    char primary_family[128];
    char pattern_buf[256];
    FcResult result;

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

    get_primary_font_family(primary_family, sizeof(primary_family));
    snprintf(pattern_buf, sizeof(pattern_buf), "%s:size=%d", primary_family, FONT_SIZE);

    xft_font = try_load_font(display, pattern_buf);
    if (!xft_font) {
        snprintf(pattern_buf, sizeof(pattern_buf), "DejaVu Sans Mono:size=%d", FONT_SIZE);
        xft_font = try_load_font(display, pattern_buf);
    }
    if (!xft_font) {
        snprintf(pattern_buf, sizeof(pattern_buf), "Liberation Mono:size=%d", FONT_SIZE);
        xft_font = try_load_font(display, pattern_buf);
    }
    if (!xft_font) {
        snprintf(pattern_buf, sizeof(pattern_buf), "monospace:size=%d", FONT_SIZE);
        xft_font = try_load_font(display, pattern_buf);
    }
    if (!xft_font) {
        fprintf(stderr, "Failed to load normal text font.\n");
        exit(EXIT_FAILURE);
    }

    // Load emoji font
    snprintf(pattern_buf, sizeof(pattern_buf), "Noto Color Emoji:size=%d", FONT_SIZE);
    FcPattern *pattern = FcNameParse((const FcChar8 *)pattern_buf);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    xft_font_emoji = XftFontOpenPattern(display, FcFontMatch(NULL, pattern, &result));
    FcPatternDestroy(pattern);

    if (!xft_font_emoji) {
        fprintf(stderr, "Failed to load emoji font, falling back to normal font.\n");
        xft_font_emoji = xft_font;
    }

    // Load bold font
    snprintf(pattern_buf, sizeof(pattern_buf), "%s:bold:size=%d", primary_family, FONT_SIZE);
    xft_font_bold = try_load_font(display, pattern_buf);
    if (!xft_font_bold) {
        snprintf(pattern_buf, sizeof(pattern_buf), "DejaVu Sans Mono:bold:size=%d", FONT_SIZE);
        xft_font_bold = try_load_font(display, pattern_buf);
    }
    if (!xft_font_bold) {
        snprintf(pattern_buf, sizeof(pattern_buf), "monospace:bold:size=%d", FONT_SIZE);
        xft_font_bold = try_load_font(display, pattern_buf);
    }
    if (!xft_font_bold) xft_font_bold = xft_font;

    // Load italic font
    snprintf(pattern_buf, sizeof(pattern_buf), "%s:italic:size=%d", primary_family, FONT_SIZE);
    xft_font_italic = try_load_font(display, pattern_buf);
    if (!xft_font_italic) {
        snprintf(pattern_buf, sizeof(pattern_buf), "monospace:italic:size=%d", FONT_SIZE);
        xft_font_italic = try_load_font(display, pattern_buf);
    }
    if (!xft_font_italic) xft_font_italic = xft_font;

    // Load bold italic font
    snprintf(pattern_buf, sizeof(pattern_buf), "%s:bold:italic:size=%d", primary_family, FONT_SIZE);
    xft_font_bold_italic = try_load_font(display, pattern_buf);
    if (!xft_font_bold_italic) {
        snprintf(pattern_buf, sizeof(pattern_buf), "monospace:bold:italic:size=%d", FONT_SIZE);
        xft_font_bold_italic = try_load_font(display, pattern_buf);
    }
    if (!xft_font_bold_italic) xft_font_bold_italic = xft_font_bold;

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

    // IMPORTANT: initialize with the logic state
    initialize_terminal_state(&term_state);
}

static void xft_reload_fonts(Display *display) {
    char primary_family[128];
    char pattern_buf[256];
    FcResult result;
    XftFont *new_font, *new_bold, *new_italic, *new_bold_italic, *new_emoji;
    int size = (int)(usedfontsize > 0 ? usedfontsize : 12);
    if (size < 6) size = 6;
    if (size > 256) size = 256;

    get_primary_font_family(primary_family, sizeof(primary_family));
    snprintf(pattern_buf, sizeof(pattern_buf), "%s:size=%d", primary_family, size);
    new_font = try_load_font(display, pattern_buf);
    if (!new_font) {
        snprintf(pattern_buf, sizeof(pattern_buf), "DejaVu Sans Mono:size=%d", size);
        new_font = try_load_font(display, pattern_buf);
    }
    if (!new_font) {
        snprintf(pattern_buf, sizeof(pattern_buf), "monospace:size=%d", size);
        new_font = try_load_font(display, pattern_buf);
    }
    if (!new_font) return;

    snprintf(pattern_buf, sizeof(pattern_buf), "%s:bold:size=%d", primary_family, size);
    new_bold = try_load_font(display, pattern_buf);
    if (!new_bold) new_bold = new_font;

    snprintf(pattern_buf, sizeof(pattern_buf), "%s:italic:size=%d", primary_family, size);
    new_italic = try_load_font(display, pattern_buf);
    if (!new_italic) new_italic = new_font;

    snprintf(pattern_buf, sizeof(pattern_buf), "%s:bold:italic:size=%d", primary_family, size);
    new_bold_italic = try_load_font(display, pattern_buf);
    if (!new_bold_italic) new_bold_italic = new_bold;

    snprintf(pattern_buf, sizeof(pattern_buf), "Noto Color Emoji:size=%d", size);
    FcPattern *pattern = FcNameParse((const FcChar8 *)pattern_buf);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    new_emoji = XftFontOpenPattern(display, FcFontMatch(NULL, pattern, &result));
    FcPatternDestroy(pattern);
    if (!new_emoji) new_emoji = new_font;

    clear_glyph_fallback_cache(display);

    if (xft_font) XftFontClose(display, xft_font);
    if (xft_font_bold && xft_font_bold != xft_font) XftFontClose(display, xft_font_bold);
    if (xft_font_italic && xft_font_italic != xft_font) XftFontClose(display, xft_font_italic);
    if (xft_font_bold_italic && xft_font_bold_italic != xft_font_bold && xft_font_bold_italic != xft_font_italic)
        XftFontClose(display, xft_font_bold_italic);
    if (xft_font_emoji && xft_font_emoji != xft_font) XftFontClose(display, xft_font_emoji);

    xft_font = new_font;
    xft_font_bold = new_bold;
    xft_font_italic = new_italic;
    xft_font_bold_italic = new_bold_italic;
    xft_font_emoji = new_emoji;

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

// Cleanup Xft resources
void cleanup_xft() {
    clear_glyph_fallback_cache(global_display);
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
     * Prefer style font first. If it lacks the glyph, fall back to emoji font.
     * This mirrors st behavior better than hard-coded Unicode range switching.
     */
    if (cp >= 0) {
        if (font_to_use && XftCharIndex(global_display, font_to_use, (FcChar32)cp) != 0) {
            return font_to_use;
        }
        if (xft_font_emoji && xft_font_emoji != font_to_use &&
            XftCharIndex(global_display, xft_font_emoji, (FcChar32)cp) != 0) {
            return xft_font_emoji;
        }

        {
            XftFont *cached = find_glyph_fallback(cp, style);
            if (cached) {
                return cached;
            }
        }

        if (glyph_fallback_count < GLYPH_FALLBACK_CACHE_SIZE) {
            XftFont *fallback = load_glyph_fallback(global_display, font_to_use ? font_to_use : xft_font, cp);
            if (!fallback && font_to_use != xft_font) {
                fallback = load_glyph_fallback(global_display, xft_font, cp);
            }
            if (fallback) {
                glyph_fallback_cache[glyph_fallback_count].cp = cp;
                glyph_fallback_cache[glyph_fallback_count].style = style;
                glyph_fallback_cache[glyph_fallback_count].font = fallback;
                glyph_fallback_count++;
                return fallback;
            }
        }
    }

    return font_to_use;
}

static void resolve_cell_colors(uint32_t in_fg, uint32_t in_bg, uint16_t attrs, int selected,
                                uint32_t *out_fg, uint32_t *out_bg) {
    uint32_t fg = in_fg;
    uint32_t bg = in_bg;

    /* st behavior: bold brightens basic ANSI colors 0-7 when faint is not active. */
    if ((attrs & ATTR_BOLD) && !(attrs & ATTR_FAINT) && fg <= 7) {
        fg += 8;
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
    if (attrs & ATTR_BLINK) {
        fg = bg;
    }
    if (attrs & ATTR_INVISIBLE) {
        fg = bg;
    }

    *out_fg = fg;
    *out_bg = bg;
}

// Draw text using TerminalState's current attr per character
void draw_text(Display *display, Window window, GC gc) {
    if (!xft_draw) return;

    if (term_state.bell_rung) {
        XBell(display, 0);
        term_state.bell_rung = 0;
    }

    XWindowAttributes wa;
    if (!XGetWindowAttributes(display, window, &wa)) return;

    /* Resize back buffer if window size changed */
    if (back_pixmap == None || back_w != wa.width || back_h != wa.height) {
        if (xft_draw_buf) { XftDrawDestroy(xft_draw_buf); xft_draw_buf = NULL; }
        if (back_pixmap != None) { XFreePixmap(display, back_pixmap); back_pixmap = None; }
        back_w = wa.width;
        back_h = wa.height;
        if (back_w > 0 && back_h > 0) {
            back_pixmap = XCreatePixmap(display, window, back_w, back_h,
                DefaultDepth(display, DefaultScreen(display)));
            if (back_pixmap != None) {
                xft_draw_buf = XftDrawCreate(display, back_pixmap,
                    DefaultVisual(display, DefaultScreen(display)),
                    DefaultColormap(display, DefaultScreen(display)));
            }
        }
    }

    XftDraw *draw = (xft_draw_buf != NULL) ? xft_draw_buf : xft_draw;
    if (draw == NULL) return;

    /* Clear: fill with default background */
    XftDrawRect(draw, &xft_color_bg, 0, 0, back_w > 0 ? back_w : wa.width, back_h > 0 ? back_h : wa.height);

    const int left_pad = LEFT_PAD;
    const int line_gap = LINE_GAP;
    const int baseline0 = xft_font->ascent + DRAW_TOP_PAD;
    const int step_w = g_cell_w + g_cell_gap;

    for (int r = 0; r < term_rows; r++) {
        int x = LEFT_PAD;
        int y = baseline0 + r * (g_cell_h + LINE_GAP);

        for (int c = 0; c < term_cols; c++) {
            const TerminalCell *cell = &terminal_buffer[r][c];
            int cell_span = 1;
            int top = y - xft_font->ascent;
            int selected;
            int draw_w;
            uint32_t fg_val;
            uint32_t bg_val;
            XftColor *fg_color;

            if (cell->is_continuation) {
                x += step_w;
                continue;
            }

            if (cell->width == 2 && c + 1 < term_cols) {
                cell_span = 2;
            }

            selected = cell_selected(r, c) || (cell_span == 2 && cell_selected(r, c + 1));
            resolve_cell_colors(cell->fg, cell->bg, cell->attrs, selected, &fg_val, &bg_val);

            draw_w = g_cell_w * cell_span + g_cell_gap * (cell_span - 1);
            XftDrawRect(draw, get_xft_color(display, window, bg_val, 1, 0), x, top, draw_w, g_cell_h);

            fg_color = get_xft_color(display, window, fg_val, 0, (cell->attrs & ATTR_FAINT) != 0);

            if (cell->c[0] != '\0') {
                utf8proc_int32_t cp;
                ssize_t rs = utf8proc_iterate((const uint8_t *)cell->c, -1, &cp);
                if (rs > 0) {
                    XftFont *font_to_use = font_for_cell(cell->attrs, cp);
                    XftDrawStringUtf8(draw, fg_color, font_to_use, x, y,
                        (const FcChar8 *)cell->c,
                        (int)strlen(cell->c));
                }
            }

            if (cell->attrs & ATTR_UNDERLINE) {
                XftDrawRect(draw, fg_color, x, top + xft_font->ascent + 1, draw_w, 1);
            }
            if (cell->attrs & ATTR_STRUCK) {
                XftDrawRect(draw, fg_color, x, top + (2 * xft_font->ascent) / 3, draw_w, 1);
            }

            x += step_w;
        }
    }

    /* Cursor: shape from DECSCUSR (0-2 block, 3-4 underline, 5-6 bar, 7 snowman) */
    if (term_state.cursor_visible) {
        int cur_row = term_state.row;
        int cur_col = term_state.col;
        int cur_span = 1;
        int selected;
        uint32_t cursor_fg_idx;
        uint32_t cursor_bg_idx;
        const TerminalCell *cursor_cell;
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
        if (cursor_cell->width == 2 && cur_col + 1 < term_cols) {
            cur_span = 2;
        }
        selected = cell_selected(cur_row, cur_col) || (cur_span == 2 && cell_selected(cur_row, cur_col + 1));

        cursor_fg_idx = selected ? COLOR_DEFAULT_FG : COLOR_DEFAULT_BG;
        cursor_bg_idx = selected ? defaultrcs : defaultcs;

        cur_x = left_pad + cur_col * step_w;
        cur_w = g_cell_w * cur_span + g_cell_gap * (cur_span - 1);
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
                XftDrawStringUtf8(draw, cursor_fg_color, xft_font,
                                  cur_x, baseline0 + cur_row * (g_cell_h + line_gap),
                                  (const FcChar8 *)snowman, 3);
            } else if (cursor_cell->c[0] != '\0') {
                utf8proc_int32_t cp;
                ssize_t rs = utf8proc_iterate((const uint8_t *)cursor_cell->c, -1, &cp);
                if (rs > 0) {
                    XftFont *font_to_use = font_for_cell(cursor_cell->attrs, cp);
                    XftDrawStringUtf8(draw, cursor_fg_color, font_to_use,
                                      cur_x, baseline0 + cur_row * (g_cell_h + line_gap),
                                      (const FcChar8 *)cursor_cell->c,
                                      (int)strlen(cursor_cell->c));
                }
            }
        }
    }

    /* Copy back buffer to window to reduce flicker */
    if (back_pixmap != None && gc) {
        XCopyArea(display, back_pixmap, window, gc, 0, 0, back_w, back_h, 0, 0);
    }
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
