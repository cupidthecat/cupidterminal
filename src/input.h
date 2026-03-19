#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>
#include <stdint.h>
#include <X11/Xlib.h>

void input_set_pty_fd(int pty_fd);
void handle_keypress(Display *display, Window window, XEvent *event, int pty_fd);
/* Returns 1 if mouse shortcut was handled, 0 otherwise */
int handle_mouse_shortcut(XEvent *event, int pty_fd);
void handle_paste_event(Display *display, Window window, XEvent *event, int pty_fd);
void copy_to_clipboard(Display *display, Window window);
void paste_from_clipboard(Display *display, Window window);
const unsigned char *clipboard_get_data(size_t *len_out);
void clipboard_set_data(Display *display, Window window, const uint8_t *data, size_t len);

/* Mouse reporting: event_type 0=press 1=release 2=motion; button 0=left 1=mid 2=right;
   col,row are 1-based; modifiers from X event state; sgr_mode uses <b;x;y;M/m format */
void send_mouse_report(int pty_fd, int event_type, int button, int col, int row,
    unsigned int modifiers, int sgr_mode);

/* Selection: call from main on Button1 press/extend/release */
void selection_start(int col, int row, unsigned int state);
void selection_extend(int col, int row);
void selection_release(Display *display, Window window, int col, int row);
void selection_get_effective_bounds(int *sr, int *sc, int *er, int *ec);

#endif // INPUT_H
