// src/input.c
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h> // Added for fprintf
#include "input.h"
#include "terminal_state.h"
#include "draw.h"
#include <stdlib.h> // malloc/free

// Declare terminal_buffer as extern TerminalCell array
extern TerminalCell terminal_buffer[TERMINAL_ROWS][TERMINAL_COLS];
extern int master_fd; // PTY file descriptor
extern TerminalState term_state;
extern Display *global_display;
extern Window  global_window;
static unsigned char *g_clip_data = NULL;
static size_t g_clip_len = 0;

const unsigned char *clipboard_get_data(size_t *len_out) {
    if (len_out) *len_out = g_clip_len;
    return g_clip_data;
}

void copy_to_clipboard(Display *display, Window window) {
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);

    // normalize selection bounds
    int sr=0, sc=0, er=TERMINAL_ROWS-1, ec=TERMINAL_COLS-1;
    if (term_state.sel_active) {
        int sidx = term_state.sel_anchor_row*TERMINAL_COLS + term_state.sel_anchor_col;
        int eidx = term_state.sel_row*TERMINAL_COLS       + term_state.sel_col;
        int ar = term_state.sel_anchor_row, ac = term_state.sel_anchor_col;
        int br = term_state.sel_row,        bc = term_state.sel_col;
        if (eidx < sidx) { int t; t=ar; ar=br; br=t; t=ac; ac=bc; bc=t; }
        sr=ar; sc=ac; er=br; ec=bc;
    }

    size_t max_size = TERMINAL_ROWS * TERMINAL_COLS * MAX_UTF8_CHAR_SIZE + TERMINAL_ROWS + 1;
    char *selection = malloc(max_size);
    size_t pos = 0;
    selection[0] = '\0';

    for (int r = sr; r <= er; r++) {
        int cstart = (r == sr) ? sc : 0;
        int cend   = (r == er) ? ec : (TERMINAL_COLS - 1);

        // build line
        size_t line_start = pos;
        for (int c = cstart; c <= cend; c++) {
            const char *g = terminal_buffer[r][c].c;
            const char  ch_space = ' ';
            const char *emit = (*g) ? g : &ch_space;
            size_t glen = (*g) ? strlen(g) : 1;

            if (pos + glen >= max_size - 2) break;
            if (*g) { memcpy(&selection[pos], g, glen); }
            else    { selection[pos] = ' '; }
            pos += glen;
        }
        // trim trailing spaces of that line
        while (pos > line_start && selection[pos-1] == ' ') pos--;
        if (pos < max_size - 1) selection[pos++] = '\n';
        selection[pos] = '\0';
    }

    // Become the owner of the CLIPBOARD selection
    XSetSelectionOwner(display, clipboard, window, CurrentTime);
    // Also own PRIMARY for middle-click paste
    Atom primary = XA_PRIMARY;
    XSetSelectionOwner(display, primary, window, CurrentTime);

    // Keep bytes alive until someone requests them
    free(g_clip_data);
    g_clip_data = (unsigned char*)selection;  // keep it!
    g_clip_len  = pos;
}

void paste_from_clipboard(Display *display, Window window) {
    Atom clipboard   = XInternAtom(display, "CLIPBOARD", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    Atom xsel_data   = XInternAtom(display, "XSEL_DATA", False);
    XConvertSelection(display, clipboard, utf8_string, xsel_data, window, CurrentTime);
}

void handle_paste_event(Display *display, Window window, XEvent *event) {
    if (event->type != SelectionNotify) return;

    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    Atom xsel_data   = XInternAtom(display, "XSEL_DATA", False);

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    XGetWindowProperty(display, window, xsel_data, 0, 1<<20, False, utf8_string,
                       &actual_type, &actual_format, &nitems, &bytes_after, &data);

    if (data) {
        write(master_fd, (char *)data, nitems);
        XFree(data);
    }
}

void handle_keypress(Display *display, Window window, XEvent *event) {
    char buffer[128];
    KeySym keysym;
    int len = XLookupString(&event->xkey, buffer, sizeof(buffer), &keysym, NULL);

    printf("KeyPress: %s (length: %d)\n", buffer, len); // Debugging output

    if ((event->xkey.state & ControlMask) && (event->xkey.state & ShiftMask)) {
        if (keysym == XK_C) {
            copy_to_clipboard(display, window);
            return;
        }
        if (keysym == XK_V) {
            paste_from_clipboard(display, window);
            return;
        }
    }

    // Cursor keys → ANSI CSI
    else if (keysym == XK_Up)    { write(master_fd, "\x1b[A", 3); }
    else if (keysym == XK_Down)  { write(master_fd, "\x1b[B", 3); }
    else if (keysym == XK_Left)  { write(master_fd, "\x1b[D", 3); return; }
    else if (keysym == XK_Right) { write(master_fd, "\x1b[C", 3); return; }
    
    // (nice to have)
    else if (keysym == XK_Home)  { write(master_fd, "\x1b[H", 3); }
    else if (keysym == XK_End)   { write(master_fd, "\x1b[F", 3); }
    else if (keysym == XK_Page_Up)   { write(master_fd, "\x1b[5~", 4); }
    else if (keysym == XK_Page_Down) { write(master_fd, "\x1b[6~", 4); }
    else if (keysym == XK_Insert)    { write(master_fd, "\x1b[2~", 4); }
    else if (keysym == XK_Delete)    { write(master_fd, "\x1b[3~", 4); }

    if (keysym == XK_BackSpace) {
        //write(master_fd, "\b \b", 3);  // Properly erase character
        write(master_fd, "\x7F", 1);
    } else if (keysym == XK_Return) {
        //write(master_fd, "\r\n", 2);   // Send newline (not \r)
        write(master_fd, "\n", 1);
    } else if (len > 0) {
        write(master_fd, buffer, len); // Send typed character to PTY
    }
}
