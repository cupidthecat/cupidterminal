// draw.c
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
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
XftFont *xft_font_bold = NULL; // Define bold font here
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
    // Load regular font
    xft_font = XftFontOpenName(display, DefaultScreen(display), FONT);
    if (!xft_font) {
        fprintf(stderr, "Failed to load font: %s\n", FONT);
        exit(EXIT_FAILURE);
    }

    // Load bold font
    xft_font_bold = XftFontOpenName(display, DefaultScreen(display), "monospace:bold");
    if (!xft_font_bold) {
        fprintf(stderr, "Failed to load bold font.\n");
        // Optionally, fallback to regular font
        xft_font_bold = xft_font;
    }
    // Allocate color (black)
    XRenderColor render_color = {0, 0, 0, 65535};
    if (!XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
                            DefaultColormap(display, DefaultScreen(display)),
                            &render_color, &xft_color)) {
        fprintf(stderr, "Failed to allocate XftColor.\n");
        exit(EXIT_FAILURE);
    }

    // Initialize terminal state
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
    (void)gc; // Suppress unused parameter warning
    if (!xft_draw || !xft_font) {
        initialize_xft(display, window);
    }

    XClearWindow(display, window);

    int y_offset = xft_font->ascent + 5;
    int x_offset = 10;

    for (int r = 0; r < TERMINAL_ROWS; r++) {
        x_offset = 10; // Reset x_offset at the start of each line
        for (int c = 0; c < TERMINAL_COLS; c++) {
            TerminalCell cell = terminal_buffer[r][c];
            if (cell.c != '\0') {
                XftDrawStringUtf8(xft_draw, &cell.color, cell.font,
                                  x_offset, y_offset, (XftChar8 *)&cell.c, 1);
                x_offset += cell.font->max_advance_width;
            } else {
                x_offset += xft_font->max_advance_width;
            }
        }
        y_offset += xft_font->ascent + xft_font->descent + 2;
    }
}
