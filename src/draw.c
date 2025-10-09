// draw.c
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <utf8proc.h> // unicode
#include <string.h>
#include <stdio.h>
#include "draw.h"
#include "config.h"
#include "terminal_state.h"

#define MAX_LINES 100    // Maximum number of lines
#define MAX_CHARS 4096   // Increased to accommodate multi-byte UTF-8 characters

// Removed static declarations
// char terminal_buffer[MAX_LINES][MAX_CHARS]; // Now defined in terminal_state.c
int line_count = 0;  // Track number of lines

// Static constants
static const int LEFT_PAD = 10;
static const int LINE_GAP = 2;
static void sel_norm_bounds(int *sr, int *sc, int *er, int *ec);
static int cell_selected(int r, int c);

// Global variables
XftDraw *xft_draw = NULL;
XftFont *xft_font = NULL;
XftFont *xft_font_bold = NULL;
XftFont *xft_font_emoji = NULL;  // <-- Add this line
XftColor xft_color;
XftColor xft_color_fg;   // white text
XftColor xft_color_bg;   // black background

Display *global_display = NULL;
Window global_window = 0;
int g_cell_w = 0;
int g_cell_h = 0;
int g_cell_gap = 1;

// Initialize Xft for Unicode and emoji support
void initialize_xft(Display *display, Window window) {
    global_display = display;
    global_window = window;

    // Initialize XftDraw
    xft_draw = XftDrawCreate(display, window, 
        DefaultVisual(display, DefaultScreen(display)), 
        DefaultColormap(display, DefaultScreen(display)));
    if (!xft_draw) {
        fprintf(stderr, "Failed to create XftDraw.\n");
        exit(EXIT_FAILURE);
    }

    // Load normal font using XftFontMatch
    FcPattern *pattern = FcNameParse((const FcChar8 *)"Noto Sans Mono CJK-12");
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    xft_font = XftFontOpenPattern(display, FcFontMatch(NULL, pattern, &result));
    FcPatternDestroy(pattern);

    if (!xft_font) {
        fprintf(stderr, "Failed to load normal text font.\n");
        exit(EXIT_FAILURE);
    }

    // Load emoji font using XftFontMatch
    pattern = FcNameParse((const FcChar8 *)"Noto Color Emoji-12");
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    xft_font_emoji = XftFontOpenPattern(display, FcFontMatch(NULL, pattern, &result));
    FcPatternDestroy(pattern);

    if (!xft_font_emoji) {
        fprintf(stderr, "Failed to load emoji font, falling back to normal font.\n");
        xft_font_emoji = xft_font;
    }

    // Load bold font
    pattern = FcNameParse((const FcChar8 *)"Noto Sans Mono CJK:bold-12");
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    xft_font_bold = XftFontOpenPattern(display, FcFontMatch(NULL, pattern, &result));
    FcPatternDestroy(pattern);

    if (!xft_font_bold) {
        fprintf(stderr, "Failed to load bold font.\n");
        xft_font_bold = xft_font;
    }

    // Allocate WHITE (foreground)
    XRenderColor rc_white = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    if (!XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
            DefaultColormap(display, DefaultScreen(display)),
            &rc_white, &xft_color_fg)) {
        fprintf(stderr, "Failed to allocate white color.\n");
        exit(EXIT_FAILURE);
    }

    // Allocate BLACK (background)
    XRenderColor rc_black = {0x0000, 0x0000, 0x0000, 0xFFFF};
    if (!XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
            DefaultColormap(display, DefaultScreen(display)),
            &rc_black, &xft_color_bg)) {
        fprintf(stderr, "Failed to allocate black color.\n");
        exit(EXIT_FAILURE);
    }

    // Compute cell size (keep your M-measure)
    XGlyphInfo extM;
    XftTextExtentsUtf8(display, xft_font, (const FcChar8*)"M", 1, &extM);
    g_cell_w = extM.xOff > 0 ? extM.xOff : xft_font->max_advance_width;
    g_cell_h = xft_font->ascent + xft_font->descent;

    // IMPORTANT: initialize with the REAL foreground (white)
    initialize_terminal_state(&term_state, xft_color_fg, xft_color_bg, xft_font);
}

// Cleanup Xft resources
void cleanup_xft() {
    if (xft_draw) {
        XftDrawDestroy(xft_draw);
    }
    if (xft_font) {
        XftFontClose(global_display, xft_font);
    }
    // XftColor does not need to be freed explicitly
}

// Draw text using TerminalState's current color per character
void draw_text(Display *display, Window window, GC gc) {
    (void)gc;
    XClearWindow(display, window);

    const int left_pad = LEFT_PAD;
    const int line_gap = LINE_GAP;
    const int baseline0 = xft_font->ascent + 5;
    const int step_w = g_cell_w + g_cell_gap;   // <- cell width + gap

    for (int r = 0; r < TERMINAL_ROWS; r++) {
        int x = LEFT_PAD;
        int y = baseline0 + r * (g_cell_h + LINE_GAP);
    
        for (int c = 0; c < TERMINAL_COLS; c++) {
            int selected = cell_selected(r, c);
            // Draw background (even if not selected)
            int top = y - xft_font->ascent;
            if (!selected) {
                // Use the cell's background color
                XftDrawRect(xft_draw, &terminal_buffer[r][c].bg_color, 
                        x, top, g_cell_w, g_cell_h);
            } else {
                // Selection uses white background
                XftDrawRect(xft_draw, &xft_color_fg, x, top, g_cell_w, g_cell_h);
            }
    
            if (terminal_buffer[r][c].c[0] != '\0') {
                utf8proc_int32_t cp;
                ssize_t rs = utf8proc_iterate((uint8_t*)terminal_buffer[r][c].c, -1, &cp);
                if (rs > 0) {
                    XftFont *font_to_use = xft_font;
                    // (emoji coverage kept as-is)
                    if ((cp >= 0x1F300 && cp <= 0x1F5FF) ||
                        (cp >= 0x1F600 && cp <= 0x1F64F) ||
                        (cp >= 0x1F680 && cp <= 0x1F6FF) ||
                        (cp >= 0x1F900 && cp <= 0x1FAFF) ||
                        (cp >= 0x2600  && cp <= 0x26FF)  ||
                        (cp >= 0x2700  && cp <= 0x27BF)  ||
                        (cp >= 0xFE00  && cp <= 0xFE0F)  ||
                        (cp >= 0x1F1E6 && cp <= 0x1F1FF)) {
                        font_to_use = xft_font_emoji;
                    }
    
                    // Invert fg when selected: black on white
                    XftColor *fg = selected ? &xft_color_bg : &terminal_buffer[r][c].fg_color;
                    XftDrawStringUtf8(xft_draw, fg, font_to_use, x, y,
                        (const FcChar8*)terminal_buffer[r][c].c,
                        strlen(terminal_buffer[r][c].c));
                }
            }
            x += step_w;
        }
    }

    // Caret in the GAP between columns (never over a glyph)
    {
        int gap_x = left_pad + term_state.col * step_w - g_cell_gap; // gap BEFORE current cell
        if (term_state.col == 0) gap_x = left_pad;                    // at left margin

        int cy_top = baseline0 + term_state.row * (g_cell_h + line_gap) - xft_font->ascent;

        XSetForeground(display, DefaultGC(display, DefaultScreen(display)),
                       WhitePixel(display, DefaultScreen(display)));
        XFillRectangle(display, window, DefaultGC(display, DefaultScreen(display)),
                       gap_x, cy_top, g_cell_gap, g_cell_h);
    }
}

void xy_to_cell(int x, int y, int *row, int *col) {
    const int baseline0 = xft_font->ascent + 5;
    const int step_w    = g_cell_w + g_cell_gap;
    const int step_h    = g_cell_h + LINE_GAP;

    int relx = x - LEFT_PAD;
    if (relx < 0) relx = 0;
    int c = relx / (step_w ? step_w : 1);

    int top0 = baseline0 - xft_font->ascent;
    int rely = y - top0;
    int r = rely / (step_h ? step_h : 1);

    if (r < 0) r = 0;
    if (r >= TERMINAL_ROWS) r = TERMINAL_ROWS - 1;
    if (c < 0) c = 0;
    if (c >= TERMINAL_COLS) c = TERMINAL_COLS - 1;

    *row = r; *col = c;
}

static void sel_norm_bounds(int *sr,int *sc,int *er,int *ec) {
    int sidx = term_state.sel_anchor_row * TERMINAL_COLS + term_state.sel_anchor_col;
    int eidx = term_state.sel_row       * TERMINAL_COLS + term_state.sel_col;
    int ar = term_state.sel_anchor_row, ac = term_state.sel_anchor_col;
    int br = term_state.sel_row,        bc = term_state.sel_col;
    if (eidx < sidx) { int t; t=ar; ar=br; br=t; t=ac; ac=bc; bc=t; }
    *sr = ar; *sc = ac; *er = br; *ec = bc;
}

static int cell_selected(int r,int c) {
    if (!term_state.sel_active) return 0;
    int sr,sc,er,ec; sel_norm_bounds(&sr,&sc,&er,&ec);
    if (r < sr || r > er) return 0;
    if (sr == er) return (c >= sc && c <= ec);
    if (r == sr)  return (c >= sc);
    if (r == er)  return (c <= ec);
    return 1;
}
