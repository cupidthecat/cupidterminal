// draw.c
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <utf8proc.h> // unicode
#include <string.h>
#include "draw.h"
#include "config.h"
#include "terminal_state.h"

#define MAX_LINES 100    // Maximum number of lines
#define MAX_CHARS 4096   // Increased to accommodate multi-byte UTF-8 characters

// Removed static declarations
// char terminal_buffer[MAX_LINES][MAX_CHARS]; // Now defined in terminal_state.c
int line_count = 0;  // Track number of lines

// Global variables
XftDraw *xft_draw = NULL;
XftFont *xft_font = NULL;
XftFont *xft_font_bold = NULL;
XftFont *xft_font_emoji = NULL;  // <-- Add this line
XftColor xft_color;
Display *global_display = NULL;
Window global_window = 0;


// Initialize Xft for Unicode and emoji support
void initialize_xft(Display *display, Window window) {
    global_display = display;
    global_window = window;

    // Initialize XftDraw
    xft_draw = XftDrawCreate(display, window, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)));
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

    // Allocate default text color (black)
    XRenderColor render_color = {0, 0, 0, 65535};
    if (!XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
                            DefaultColormap(display, DefaultScreen(display)),
                            &render_color, &xft_color)) {
        fprintf(stderr, "Failed to allocate XftColor.\n");
        exit(EXIT_FAILURE);
    }

    // Initialize terminal state with normal text font
    initialize_terminal_state(&term_state, xft_color, xft_font);
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
    XClearWindow(display, window);

    int y_offset = xft_font->ascent + 5;
    for (int r = 0; r < TERMINAL_ROWS; r++) {
        int x_offset = 10;
        for (int c = 0; c < TERMINAL_COLS; c++) {
            if (terminal_buffer[r][c].c[0] != '\0') {
                utf8proc_int32_t codepoint;
                ssize_t char_size = utf8proc_iterate((uint8_t *)terminal_buffer[r][c].c, -1, &codepoint);

                // Use emoji font if it's an emoji
                XftFont *font_to_use = xft_font;
                // Use emoji font for a wider range of emoji codepoints
                if ((codepoint >= 0x1F300 && codepoint <= 0x1F5FF) ||  // Miscellaneous Symbols and Pictographs
                    (codepoint >= 0x1F600 && codepoint <= 0x1F64F) ||  // Emoticons
                    (codepoint >= 0x1F680 && codepoint <= 0x1F6FF) ||  // Transport & Map
                    (codepoint >= 0x1F900 && codepoint <= 0x1FAFF) ||  // Supplemental Symbols and Pictographs
                    (codepoint >= 0x2600 && codepoint <= 0x26FF) ||    // Miscellaneous Symbols (some emojis here)
                    (codepoint >= 0x2700 && codepoint <= 0x27BF) ||    // Dingbats (checkmarks, crosses, etc.)
                    (codepoint >= 0xFE00 && codepoint <= 0xFE0F) ||    // Variation Selectors (for emoji styling)
                    (codepoint >= 0x1F1E6 && codepoint <= 0x1F1FF)) {  // Regional Indicator Symbols (Flags)
                    
                    font_to_use = xft_font_emoji;
                }

                XftDrawStringUtf8(
                    xft_draw,
                    &terminal_buffer[r][c].color,
                    font_to_use,
                    x_offset,
                    y_offset,
                    (const FcChar8 *)terminal_buffer[r][c].c,
                    strlen(terminal_buffer[r][c].c)
                );

                XGlyphInfo extents;
                XftTextExtentsUtf8(display, font_to_use, (const FcChar8 *)terminal_buffer[r][c].c, 
                                   strlen(terminal_buffer[r][c].c), &extents);
                x_offset += extents.xOff;
            }
        }
        y_offset += xft_font->ascent + xft_font->descent + 2;
    }
}
