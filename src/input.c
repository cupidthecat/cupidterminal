#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <unistd.h>
#include "input.h"
#include "draw.h" // Add this line

extern int master_fd; // Declare external PTY file descriptor

void handle_keypress(Display *display, Window window, XEvent *event) {
    static int current_pos = 0;  // Track cursor position in the line

    char buffer[128];
    KeySym keysym;
    int len = XLookupString(&event->xkey, buffer, sizeof(buffer), &keysym, NULL);

    if (keysym == XK_BackSpace) {
        if (current_pos > 0) {
            char backspace = 0x7F;  // DEL character (standard backspace in PTY)
            write(master_fd, &backspace, 1);
            current_pos--;  // Move cursor back
        }
    } else if (len > 0) {
        write(master_fd, buffer, len); // Send typed text to PTY
        current_pos += len;
    }

    if (keysym == XK_q) {
        XCloseDisplay(display);
        exit(0);
    }
}
