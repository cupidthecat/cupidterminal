// terminal_state.h

#ifndef TERMINAL_STATE_H
#define TERMINAL_STATE_H

#include <X11/Xft/Xft.h>

#define TERMINAL_ROWS 24
#define TERMINAL_COLS 80
#define MAX_CHARS 4096

#define MAX_UTF8_CHAR_SIZE 4  // UTF-8 characters can be up to 4 bytes

typedef struct {
    char c[MAX_UTF8_CHAR_SIZE + 1]; // UTF-8 storage (plus null-terminator)
    XftColor color;        
    XftFont *font;         
} TerminalCell;

typedef struct {
    int row;
    int col;
    XftColor current_color;
    XftFont *current_font;
    int saved_row; int saved_col;

    // Selection tracking
    int sel_active;
    int sel_anchor_row, sel_anchor_col; // where drag started
    int sel_row, sel_col;               // current drag end
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
