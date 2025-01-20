// terminal_state.h

#ifndef TERMINAL_STATE_H
#define TERMINAL_STATE_H

#include <X11/Xft/Xft.h>

#define TERMINAL_ROWS 24
#define TERMINAL_COLS 80
#define MAX_CHARS 4096

typedef struct {
    char c;                // The character
    XftColor color;        // Foreground color
    XftFont *font;         // Font (for bold, etc.)
} TerminalCell;

typedef struct {
    int row;
    int col;
    XftColor current_color;
    XftFont *current_font;
} TerminalState;

// Declare the global TerminalState variable
extern TerminalState term_state;

// Declare the terminal buffer with attributes
extern TerminalCell terminal_buffer[TERMINAL_ROWS][TERMINAL_COLS];

// Function prototypes
void initialize_terminal_state(TerminalState *state, XftColor default_color, XftFont *default_font);
void reset_attributes(TerminalState *state, XftColor default_color, XftFont *default_font);
void handle_ansi_sequence(const char *seq, int len, TerminalState *state, Display *display);
void put_char(char c, TerminalState *state);  // Ensure this is declared
void allocate_background_color(Display *display, TerminalState *state, int color_code);

#endif // TERMINAL_STATE_H
