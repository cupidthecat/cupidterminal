// src/input.c

#include <X11/Xlib.h>
#include <Xatom.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h> // Added for fprintf
#include "input.h"
#include "terminal_state.h"
#include "draw.h"

// Declare terminal_buffer as extern TerminalCell array
extern TerminalCell terminal_buffer[TERMINAL_ROWS][TERMINAL_COLS];
extern int master_fd; // PTY file descriptor

void copy_to_clipboard(Display *display, Window window) {
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    
    // Calculate maximum possible size: TERMINAL_ROWS * TERMINAL_COLS * MAX_UTF8_CHAR_SIZE + TERMINAL_ROWS (newlines) + 1 (null terminator)
    size_t max_size = TERMINAL_ROWS * TERMINAL_COLS * MAX_UTF8_CHAR_SIZE + TERMINAL_ROWS + 1;
    char selection[max_size];
    size_t pos = 0;
    
    selection[0] = '\0'; // Initialize the selection string
    
    for (int i = 0; i < TERMINAL_ROWS; i++) {
        for (int j = 0; j < TERMINAL_COLS; j++) {
            const char *c = terminal_buffer[i][j].c; // Pointer to UTF-8 character
            if (*c != '\0') { // Compare first byte of UTF-8 sequence
                size_t char_len = strlen(c);
                if (pos + char_len < max_size - 2) { // Ensure space for newline & null terminator
                    strcpy(&selection[pos], c); // Copy full UTF-8 character
                    pos += char_len;
                } else {
                    fprintf(stderr, "Clipboard selection truncated.\n");
                    break;
                }
            }
        }
        if (pos < max_size - 1) {
            selection[pos++] = '\n';
            selection[pos] = '\0';
        } else {
            fprintf(stderr, "Clipboard selection truncated after newline.\n");
            break;
        }
    }
    
    // Store the selection in the property
    XSetSelectionOwner(display, clipboard, window, CurrentTime);
    XChangeProperty(display, window, clipboard, utf8_string, 8, PropModeReplace,
                   (unsigned char *)selection, pos);
}

void paste_from_clipboard(Display *display, Window window) {
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    
    XConvertSelection(display, clipboard, utf8_string, clipboard, window, CurrentTime);
}

void handle_paste_event(Display *display, Window window, XEvent *event) {
    if (event->type != SelectionNotify) return;

    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    XGetWindowProperty(display, window, clipboard, 0, 4096, False, utf8_string,
                       &actual_type, &actual_format, &nitems, &bytes_after, &data);

    if (data) {
        write(master_fd, (char *)data, nitems);  // Send pasted text to PTY
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
