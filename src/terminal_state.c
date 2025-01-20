// terminal_state.c
#include "draw.h"

#include "terminal_state.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

// External globals from draw.c
extern Display *global_display;
extern XftFont *xft_font;
extern XftColor xft_color;
TerminalState term_state;
TerminalCell terminal_buffer[TERMINAL_ROWS][TERMINAL_COLS];



// Initialize terminal state with default color and font
void initialize_terminal_state(TerminalState *state, XftColor default_color, XftFont *default_font) {
    state->row = 0;
    state->col = 0;
    state->current_color = default_color;
    state->current_font = default_font;

    // Initialize terminal_buffer
    for (int r = 0; r < TERMINAL_ROWS; r++) {
        memset(terminal_buffer[r], 0, sizeof(terminal_buffer[r]));
    }
}

// Reset terminal attributes to default
void reset_attributes(TerminalState *state, XftColor default_color, XftFont *default_font) {
    state->current_color = default_color;
    state->current_font = default_font;
    // Reset other attributes if added
}

void allocate_color(Display *display, TerminalState *state, int color_code) {
    XRenderColor xr;
    switch (color_code) {
        case 30: xr = (XRenderColor){0x0000, 0x0000, 0x0000, 0xFFFF}; break; // Black
        case 31: xr = (XRenderColor){0xFFFF, 0x0000, 0x0000, 0xFFFF}; break; // Red
        case 32: xr = (XRenderColor){0x0000, 0xFFFF, 0x0000, 0xFFFF}; break; // Green
        case 33: xr = (XRenderColor){0xFFFF, 0xFFFF, 0x0000, 0xFFFF}; break; // Yellow
        case 34: xr = (XRenderColor){0x0000, 0x0000, 0xFFFF, 0xFFFF}; break; // Blue
        case 35: xr = (XRenderColor){0xFFFF, 0x0000, 0xFFFF, 0xFFFF}; break; // Magenta
        case 36: xr = (XRenderColor){0x0000, 0xFFFF, 0xFFFF, 0xFFFF}; break; // Cyan
        case 37: xr = (XRenderColor){0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}; break; // White
        default: return;
    }

    if (!XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
                            DefaultColormap(display, DefaultScreen(display)), &xr, &state->current_color)) {
        fprintf(stderr, "Failed to allocate color for code %d.\n", color_code);
        // Optionally, set to a default color or handle the error as needed
    }
}

void allocate_background_color(Display *display, TerminalState *state, int color_code) {
    XRenderColor xr;
    switch (color_code) {
        case 40: xr = (XRenderColor){0x0000, 0x0000, 0x0000, 0xFFFF}; break; // Black
        case 41: xr = (XRenderColor){0xFFFF, 0x0000, 0x0000, 0xFFFF}; break; // Red
        case 42: xr = (XRenderColor){0x0000, 0xFFFF, 0x0000, 0xFFFF}; break; // Green
        case 43: xr = (XRenderColor){0xFFFF, 0xFFFF, 0x0000, 0xFFFF}; break; // Yellow
        case 44: xr = (XRenderColor){0x0000, 0x0000, 0xFFFF, 0xFFFF}; break; // Blue
        case 45: xr = (XRenderColor){0xFFFF, 0x0000, 0xFFFF, 0xFFFF}; break; // Magenta
        case 46: xr = (XRenderColor){0x0000, 0xFFFF, 0xFFFF, 0xFFFF}; break; // Cyan
        case 47: xr = (XRenderColor){0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}; break; // White
        default: return;
    }

    // Since background color should not overwrite the current foreground color,
    // you need to handle background colors differently in rendering.
    // This example focuses on foreground colors.
    // Implement background color handling as needed.
    
    // Example: Store background color in TerminalState if needed
    // For simplicity, not implemented here.
}


// Parse and handle ANSI escape sequences
void handle_ansi_sequence(const char *seq, int len, TerminalState *state, Display *display) {
    if (len < 2 || seq[0] != '\033' || seq[1] != '[') {
        return;
    }

    char cmd = seq[len - 1];  // Last character is the command
    char params[32] = {0};

    if (len - 2 < (int)sizeof(params)) {
        strncpy(params, &seq[2], len - 3);
    }

    int param_values[10] = {0};
    int param_count = 0;
    char *token = strtok(params, ";");
    
    while (token != NULL && param_count < 10) {
        param_values[param_count++] = atoi(token);
        token = strtok(NULL, ";");
    }

    switch (cmd) {
        case 'm': // SGR - Select Graphic Rendition
            for (int i = 0; i < param_count; i++) {
                int code = param_values[i];

                if (code == 0) {  // Reset attributes
                    reset_attributes(state, xft_color, xft_font);
                } else if (code == 1) {  // Bold text
                    if (state->current_font != xft_font_bold) {
                        state->current_font = xft_font_bold;
                    }
                } else if (code >= 30 && code <= 37) {  // Foreground colors
                    allocate_color(display, state, code);
                } else if (code >= 40 && code <= 47) {  // Background colors
                    allocate_background_color(display, state, code);
                }
            }
            break;

        case 'J': // Clear screen
            if (param_count == 0 || param_values[0] == 2) { // Full screen clear (ESC[2J)
                for (int r = 0; r < TERMINAL_ROWS; r++) {
                    memset(terminal_buffer[r], 0, sizeof(terminal_buffer[r]));
                }
                state->row = 0;
                state->col = 0;
                
                // Clear the X11 window
                XClearWindow(global_display, global_window);
                draw_text(global_display, global_window, DefaultGC(global_display, DefaultScreen(global_display)));
            }
            break;

        case 'H': // Move cursor to home position (0,0)
            state->row = 0;
            state->col = 0;
            break;

        default:
            // Unsupported ANSI sequences can be handled here if needed
            break;
    }
}

// Function to place a character in the terminal buffer
void put_char(char c, TerminalState *state) {
    if (c == '\b' || c == 0x7F) { // Handle backspace
        if (state->col > 0) {
            state->col--; // Move cursor back
            terminal_buffer[state->row][state->col].c = '\0'; // Erase character
        } else if (state->row > 0) { // Move to previous line if at column 0
            state->row--;
            state->col = TERMINAL_COLS - 1; // Move to end of previous line
            terminal_buffer[state->row][state->col].c = '\0';
        }
        return;
    }

    if (c == '\n') { 
        state->row++;
        state->col = 0;
        if (state->row >= TERMINAL_ROWS) {
            // Scroll up
            for (int r = 1; r < TERMINAL_ROWS; r++) {
                memcpy(terminal_buffer[r - 1], terminal_buffer[r], sizeof(TerminalCell) * TERMINAL_COLS);
            }
            // Clear the last line
            memset(terminal_buffer[TERMINAL_ROWS - 1], 0, sizeof(TerminalCell) * TERMINAL_COLS);
            state->row = TERMINAL_ROWS - 1;
        }
        return;
    }

    if (c == '\r') {
        state->col = 0;
        return;
    }

    if (isprint(c)) {
        if (state->col >= TERMINAL_COLS - 1) {
            state->row++;
            state->col = 0;
            if (state->row >= TERMINAL_ROWS) {
                // Scroll up
                for (int r = 1; r < TERMINAL_ROWS; r++) {
                    memcpy(terminal_buffer[r - 1], terminal_buffer[r], sizeof(TerminalCell) * TERMINAL_COLS);
                }
                // Clear the last line
                memset(terminal_buffer[TERMINAL_ROWS - 1], 0, sizeof(TerminalCell) * TERMINAL_COLS);
                state->row = TERMINAL_ROWS - 1;
            }
        }
        terminal_buffer[state->row][state->col].c = c;
        terminal_buffer[state->row][state->col].color = state->current_color;
        terminal_buffer[state->row][state->col].font = state->current_font;
        state->col++;
    }
}