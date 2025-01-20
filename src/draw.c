// draw.c
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <string.h>
#include "draw.h"
#include "config.h"

#define MAX_LINES 100    // Maximum number of lines
#define MAX_CHARS 4096   // Increased to accommodate multi-byte UTF-8 characters

static char terminal_buffer[MAX_LINES][MAX_CHARS]; // Store text lines
static int line_count = 0;  // Track number of lines

// Xft-related global variables
static XftDraw *xft_draw = NULL;
static XftFont *xft_font = NULL;
static XftColor xft_color;
static Display *global_display = NULL;
static Window global_window = 0;

// Initialize Xft for Unicode and emoji support
void initialize_xft(Display *display, Window window, GC gc) {
    global_display = display;
    global_window = window;

    // Initialize XftDraw
    xft_draw = XftDrawCreate(display, window, DefaultVisual(display, DefaultScreen(display)), DefaultColormap(display, DefaultScreen(display)));
    if (!xft_draw) {
        fprintf(stderr, "Failed to create XftDraw.\n");
        exit(EXIT_FAILURE);
    }

    // Load XftFont based on configuration
    xft_font = XftFontOpenName(display, DefaultScreen(display), FONT);
    if (!xft_font) {
        fprintf(stderr, "Failed to load font: %s\n", FONT);
        exit(EXIT_FAILURE);
    }

    // Allocate color (black)
    XRenderColor render_color = {0, 0, 0, 65535};
    if (!XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
                            DefaultColormap(display, DefaultScreen(display)),
                            &render_color, &xft_color)) {
        fprintf(stderr, "Failed to allocate XftColor.\n");
        exit(EXIT_FAILURE);
    }
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

// Draw text using Xft for Unicode and emoji support
void draw_text(Display *display, Window window, GC gc) {
    if (!xft_draw || !xft_font) {
        initialize_xft(display, window, gc);
    }

    XClearWindow(display, window);

    int y_offset = xft_font->ascent + 5; // Starting y position

    for (int i = 0; i < line_count; i++) {
        // Calculate the width of the string to handle proper rendering
        XftDrawStringUtf8(xft_draw, &xft_color, xft_font, 10, y_offset, (XftChar8 *)terminal_buffer[i], strlen(terminal_buffer[i]));
        y_offset += xft_font->ascent + xft_font->descent + 2; // Move to the next line with proper spacing
    }
}

// Append text to the terminal buffer, handling UTF-8 characters
void append_text(const char *text) {
    static int current_pos = 0;  // Track cursor position

    if (line_count == 0) {
        line_count = 1; // Ensure at least one line exists
    }

    int len = strlen(text);
    for (int i = 0; i < len; i++) {
        if (text[i] == '\b' || text[i] == 0x7F) {
            // Handle backspace: Remove last UTF-8 character from buffer
            if (current_pos > 0) {
                // To handle multi-byte UTF-8 characters, backtrack correctly
                int pos = current_pos - 1;
                while (pos > 0 && ((terminal_buffer[line_count - 1][pos] & 0xC0) == 0x80)) {
                    pos--;
                }
                current_pos = pos;
                terminal_buffer[line_count - 1][current_pos] = '\0';
            }
        } else if (text[i] == '\n') {
            // Move to a new line on newline character
            if (line_count < MAX_LINES) {
                line_count++;
                current_pos = 0;
            } else {
                // Shift lines up if the buffer is full (scrolling)
                for (int j = 1; j < MAX_LINES; j++) {
                    strncpy(terminal_buffer[j - 1], terminal_buffer[j], MAX_CHARS);
                }
                terminal_buffer[MAX_LINES - 1][0] = '\0';
                current_pos = 0;
            }
        } else {
            // Append text normally
            if (current_pos < MAX_CHARS - 1) {
                terminal_buffer[line_count - 1][current_pos++] = text[i];
                terminal_buffer[line_count - 1][current_pos] = '\0';
            }
        }
    }
}
