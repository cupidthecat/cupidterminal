#ifndef INPUT_H
#define INPUT_H

#include <X11/Xlib.h>

void handle_keypress(Display *display, Window window, XEvent *event);

#endif // INPUT_H
