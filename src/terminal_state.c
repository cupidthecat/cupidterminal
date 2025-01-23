// terminal_state.c
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <utf8proc.h> // unicode

#include "terminal_state.h"
#include "draw.h"

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
    (void)display; // Suppress unused parameter warning
    (void)state;   // Suppress unused parameter warning
    (void)color_code; // Suppress unused parameter warning

    // TODO: Implement background color allocation
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
        case 'm': // SGR
            /* ... existing color/bold handling ... */
            break;

        case 'J': // Clear screen
            /* ... existing clear-screen handling ... */
            break;

        case 'H': // Cursor home
            /* ... existing cursor-home handling ... */
            break;

        // ADD THIS:
        case 'K': // Erase in line
        {
            int mode = (param_count > 0) ? param_values[0] : 0;
            // mode=0 => erase from cursor to end of line
            // mode=1 => erase from start of line to cursor
            // mode=2 => erase the entire line
            int r = state->row;

            switch (mode) {
                case 0: // from cursor → end
                    for (int c = state->col; c < TERMINAL_COLS; c++) {
                        memset(terminal_buffer[r][c].c, 0, sizeof(terminal_buffer[r][c].c));
                    }
                    break;
                case 1: // from start → cursor
                    for (int c = 0; c <= state->col && c < TERMINAL_COLS; c++) {
                        memset(terminal_buffer[r][c].c, 0, sizeof(terminal_buffer[r][c].c));
                    }
                    break;
                case 2: // entire line
                    memset(terminal_buffer[r], 0, sizeof(terminal_buffer[r]));
                    break;
            }
        }
        break;

        default:
            // Ignore or implement other ANSI sequences as needed
            break;
    }
}

// Function to place a character in the terminal buffer
void put_char(char c, TerminalState *state) {
    static uint8_t utf8_buf[MAX_UTF8_CHAR_SIZE + 1] = {0}; // Buffer for UTF-8 decoding
    static int utf8_len = 0;

    // Handle special control characters
    if (c == '\b' || c == 0x7F) { // Handle backspace
        if (state->col > 0) {
            state->col--;
            memset(terminal_buffer[state->row][state->col].c, 0, MAX_UTF8_CHAR_SIZE + 1); // Clear character
        }
        return;
    }

    if (c == '\n') { 
        state->col = 0;  // Move cursor to start of next line
        state->row++;

        // Scroll if at the bottom
        if (state->row >= TERMINAL_ROWS) {
            // Scroll up
            for (int r = 1; r < TERMINAL_ROWS; r++) {
                memcpy(terminal_buffer[r - 1], terminal_buffer[r], sizeof(TerminalCell) * TERMINAL_COLS);
            }
            memset(terminal_buffer[TERMINAL_ROWS - 1], 0, sizeof(TerminalCell) * TERMINAL_COLS);
            state->row = TERMINAL_ROWS - 1;
        }
        return;
    }

    if (c == '\r') {
        state->col = 0;  // Carriage return resets to beginning of line
        return;
    }

    // Handle UTF-8 decoding
    utf8_buf[utf8_len++] = (uint8_t)c;
    utf8proc_int32_t codepoint;
    ssize_t result = utf8proc_iterate(utf8_buf, utf8_len, &codepoint);

    if (result > 0) { // If valid UTF-8 sequence is detected
        utf8_buf[utf8_len] = '\0';  // Null-terminate for storage

        if (state->col >= TERMINAL_COLS) {
            state->row++;
            state->col = 0;
            if (state->row >= TERMINAL_ROWS) {
                // Scroll up
                for (int r = 1; r < TERMINAL_ROWS; r++) {
                    memcpy(terminal_buffer[r - 1], terminal_buffer[r], sizeof(TerminalCell) * TERMINAL_COLS);
                }
                memset(terminal_buffer[TERMINAL_ROWS - 1], 0, sizeof(TerminalCell) * TERMINAL_COLS);
                state->row = TERMINAL_ROWS - 1;
            }
        }

        // Store the UTF-8 character in the terminal buffer
        strncpy(terminal_buffer[state->row][state->col].c, (char *)utf8_buf, MAX_UTF8_CHAR_SIZE);
        terminal_buffer[state->row][state->col].color = state->current_color;
        terminal_buffer[state->row][state->col].font = state->current_font;
        state->col++;

        utf8_len = 0;  // Reset buffer for next character
    }
}
