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
extern XftColor xft_color_fg;
extern XftColor xft_color_bg;

TerminalState term_state;
TerminalCell terminal_buffer[TERMINAL_ROWS][TERMINAL_COLS];

static XRenderColor map_ansi_color(int code) {
    // Maps a 30-37, 40-47, 90-97, 100-107 code to an XRenderColor
    int index = 0;
    if (code >= 30 && code <= 37) index = code - 30;       // Standard FG
    if (code >= 40 && code <= 47) index = code - 40;       // Standard BG
    if (code >= 90 && code <= 97) index = code - 90 + 8;   // Bright FG
    if (code >= 100 && code <= 107) index = code - 100 + 8; // Bright BG

    static const unsigned short ansi[16][3] = {
        {0x0000, 0x0000, 0x0000}, // 0: Black
        {0xCCCC, 0x0000, 0x0000}, // 1: Red
        {0x0000, 0xCCCC, 0x0000}, // 2: Green
        {0xCCCC, 0xCCCC, 0x0000}, // 3: Yellow
        {0x0000, 0x0000, 0xCCCC}, // 4: Blue
        {0xCCCC, 0x0000, 0xCCCC}, // 5: Magenta
        {0x0000, 0xCCCC, 0xCCCC}, // 6: Cyan
        {0xCCCC, 0xCCCC, 0xCCCC}, // 7: White
        {0x8080, 0x8080, 0x8080}, // 8: Bright Black (Gray)
        {0xFFFF, 0x0000, 0x0000}, // 9: Bright Red
        {0x0000, 0xFFFF, 0x0000}, // 10: Bright Green
        {0xFFFF, 0xFFFF, 0x0000}, // 11: Bright Yellow
        {0x0000, 0x0000, 0xFFFF}, // 12: Bright Blue
        {0xFFFF, 0x0000, 0xFFFF}, // 13: Bright Magenta
        {0x0000, 0xFFFF, 0xFFFF}, // 14: Bright Cyan
        {0xFFFF, 0xFFFF, 0xFFFF}, // 15: Bright White
    };
    
    return (XRenderColor){ansi[index][0], ansi[index][1], ansi[index][2], 0xFFFF};
}

// Initialize terminal state with default color and font
void initialize_terminal_state(TerminalState *state, XftColor default_fg, XftColor default_bg, XftFont *default_font) {
    state->row = 0;
    state->col = 0;
    state->current_color = default_fg;
    state->current_bg_color = default_bg;
    state->current_font = default_font;
    
    state->sel_active = 0;
    state->sel_anchor_row = 0;
    state->sel_anchor_col = 0;
    state->sel_row = 0;
    state->sel_col = 0;

    for (int r = 0; r < TERMINAL_ROWS; r++) {
        for (int c = 0; c < TERMINAL_COLS; c++) {
            memset(terminal_buffer[r][c].c, 0, MAX_UTF8_CHAR_SIZE + 1);
            terminal_buffer[r][c].fg_color = default_fg;
            terminal_buffer[r][c].bg_color = default_bg;
            terminal_buffer[r][c].font = default_font;
        }
    }
}

// Reset terminal attributes to default
void reset_attributes(TerminalState *s, XftColor default_color, XftFont *default_font) {
    s->current_color = default_color;
    s->current_font  = default_font;
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

static XRenderColor map_xterm256(int n) {
    // 0–15: ANSI; 16–231: 6x6x6 cube; 232–255: grayscale
    XRenderColor xr = {0,0,0,0xFFFF};
    if (n < 16) {
        // reuse your allocate_color equivalents for 30–37/90–97
        static const unsigned short ansi[16][3] = {
            {0,0,0},{0x8000,0,0},{0,0x8000,0},{0x8000,0x8000,0},
            {0,0,0x8000},{0x8000,0,0x8000},{0,0x8000,0x8000},{0xC000,0xC000,0xC000},
            {0x4000,0x4000,0x4000},{0xFFFF,0,0},{0,0xFFFF,0},{0xFFFF,0xFFFF,0},
            {0,0,0xFFFF},{0xFFFF,0,0xFFFF},{0,0xFFFF,0xFFFF},{0xFFFF,0xFFFF,0xFFFF}
        };
        xr.red = ansi[n][0]; xr.green = ansi[n][1]; xr.blue = ansi[n][2];
        return xr;
    } else if (n >= 16 && n <= 231) {
        int idx = n - 16;
        int r = idx / 36, g = (idx / 6) % 6, b = idx % 6;
        unsigned short step[6] = {0, 0x3333, 0x6666, 0x9999, 0xCCCC, 0xFFFF};
        xr.red = step[r]; xr.green = step[g]; xr.blue = step[b];
        return xr;
    } else {
        unsigned short v = 0x0800 + (n - 232) * 0x0A8A; // approx 8..238
        xr.red = xr.green = xr.blue = v;
        return xr;
    }
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
        case 'H':    // CUP: ESC[<row>;<col>H
        case 'f': {  // HVP: same as CUP
            int r = (param_count >= 1 && param_values[0] > 0) ? param_values[0] : 1;
            int c = (param_count >= 2 && param_values[1] > 0) ? param_values[1] : 1;
            r--; c--; // 1-based -> 0-based
            if (r < 0) r = 0; if (r >= TERMINAL_ROWS) r = TERMINAL_ROWS - 1;
            if (c < 0) c = 0; if (c >= TERMINAL_COLS) c = TERMINAL_COLS - 1;
            state->row = r; state->col = c;
        } break;

        case 'J': {  // ED: erase display
            int n = (param_count ? param_values[0] : 0);
            if (n == 2 || n == 3) {               // whole screen (3 also clears scrollback; we have none)
                for (int r = 0; r < TERMINAL_ROWS; r++)
                    memset(terminal_buffer[r], 0, sizeof(terminal_buffer[r]));
                state->row = 0; state->col = 0;   // home cursor
            } else if (n == 0) {                  // cursor -> end of screen
                // clear from cursor to end of line
                for (int c = state->col; c < TERMINAL_COLS; c++)
                    memset(terminal_buffer[state->row][c].c, 0, MAX_UTF8_CHAR_SIZE + 1);
                // clear all following lines
                for (int r = state->row + 1; r < TERMINAL_ROWS; r++)
                    memset(terminal_buffer[r], 0, sizeof(terminal_buffer[r]));
            } else if (n == 1) {                  // start -> cursor
                for (int r = 0; r < state->row; r++)
                    memset(terminal_buffer[r], 0, sizeof(terminal_buffer[r]));
                for (int c = 0; c <= state->col && c < TERMINAL_COLS; c++)
                    memset(terminal_buffer[state->row][c].c, 0, MAX_UTF8_CHAR_SIZE + 1);
            }
        } break;

        case 'K': {  // EL: erase line
            int n = (param_count ? param_values[0] : 0);
            if (n == 2) {                          // entire line
                memset(terminal_buffer[state->row], 0, sizeof(terminal_buffer[state->row]));
            } else if (n == 0) {                   // cursor -> end of line
                for (int c = state->col; c < TERMINAL_COLS; c++)
                    memset(terminal_buffer[state->row][c].c, 0, MAX_UTF8_CHAR_SIZE + 1);
            } else if (n == 1) {                   // start -> cursor
                for (int c = 0; c <= state->col && c < TERMINAL_COLS; c++)
                    memset(terminal_buffer[state->row][c].c, 0, MAX_UTF8_CHAR_SIZE + 1);
            }
        } break;

        // Cursor movement
        case 'A': { // CUU: move up N
            int n = (param_count ? param_values[0] : 1);
            state->row -= n; if (state->row < 0) state->row = 0;
        } break;
        case 'B': { // CUD: move down N
            int n = (param_count ? param_values[0] : 1);
            state->row += n; if (state->row >= TERMINAL_ROWS) state->row = TERMINAL_ROWS - 1;
        } break;
        case 'C': { // CUF
            int n = (param_count ? param_values[0] : 1);
            state->col += n;
            if (state->col >= TERMINAL_COLS) state->col = TERMINAL_COLS - 1;
        } break;
        case 'D': { // CUB
            int n = (param_count ? param_values[0] : 1);
            state->col -= n;
            if (state->col < 0) state->col = 0;
        } break;        
        case 's': { // save cursor
            state->saved_row = state->row;
            state->saved_col = state->col;
        } break;
        case 'u': { // restore cursor
            if (state->saved_row >= 0) state->row = state->saved_row;
            if (state->saved_col >= 0) state->col = state->saved_col;
        } break;
        
        case 'm': { // SGR (colors, bold, reset)
            if (param_count == 0) { // ESC[m is equivalent to ESC[0m
                param_values[0] = 0;
                param_count = 1;
            }
            for (int i = 0; i < param_count; i++) {
                int p = param_values[i];
                if (p == 0) { // Reset all attributes
                    state->current_color = xft_color_fg;
                    state->current_bg_color = xft_color_bg; // <-- FIX: Reset background
                    state->current_font  = xft_font;
                } else if (p == 1) { // Bold
                    state->current_font = xft_font_bold ? xft_font_bold : xft_font;
                } else if (p == 22) { // Normal intensity
                    state->current_font = xft_font;
                } else if ((p >= 30 && p <= 37) || (p >= 90 && p <= 97)) { // FG Color
                    XRenderColor xr = map_ansi_color(p);
                    XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
                                       DefaultColormap(display, DefaultScreen(display)), &xr, &state->current_color);
                } else if (p == 39) { // Default FG
                    state->current_color = xft_color_fg;
                } else if ((p >= 40 && p <= 47) || (p >= 100 && p <= 107)) { // <-- FIX: BG Color
                    XRenderColor xr = map_ansi_color(p);
                    XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
                                       DefaultColormap(display, DefaultScreen(display)), &xr, &state->current_bg_color);
                } else if (p == 49) { // Default BG
                    state->current_bg_color = xft_color_bg; // <-- FIX: Handle default BG
                } else if (p == 38 || p == 48) { // 256-color or 24-bit color
                    if (i + 2 < param_count && param_values[i+1] == 5) { // 256-color mode
                        int n = param_values[i+2];
                        XRenderColor xr = map_xterm256(n);
                        XftColor *target_color = (p == 38) ? &state->current_color : &state->current_bg_color;
                        XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
                                           DefaultColormap(display, DefaultScreen(display)), &xr, target_color);
                        i += 2; // Consume the next two parameters
                    }
                    // You can add logic for 24-bit color (param_values[i+1] == 2) here later
                }
            }
        } break;

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

    // Handle bell (and other non-printing controls)
    if (c == '\a') {                    // BEL (0x07)
        XBell(global_display, 0);       // optional audible/visual bell
        return;                         // do not draw a glyph
    }
    
    // Handle newline
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
        memcpy(terminal_buffer[state->row][state->col].c, utf8_buf, (size_t)utf8_len + 1);
        terminal_buffer[state->row][state->col].fg_color = state->current_color;
        terminal_buffer[state->row][state->col].bg_color = state->current_bg_color;
        terminal_buffer[state->row][state->col].font = state->current_font;
        state->col++;

        utf8_len = 0;  // Reset buffer for next character
    }
}
