#ifndef INPUT_H
#define INPUT_H

#include <X11/Xlib.h>

void handle_keypress(Display *display, Window window, XEvent *event);
void handle_paste_event(Display *display, Window window, XEvent *event);
void copy_to_clipboard(Display *display, Window window);  // Added declaration
void paste_from_clipboard(Display *display, Window window);  // Added declaration
const unsigned char *clipboard_get_data(size_t *len_out);

#endif // INPUT_H
