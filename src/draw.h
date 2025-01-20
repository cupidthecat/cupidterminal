// draw.h
#ifndef DRAW_H
#define DRAW_H

#include <X11/Xlib.h>
#include <Xft/Xft.h> // Include Xft for Unicode support

void draw_text(Display *display, Window window, GC gc);
void append_text(const char *text);
void initialize_xft(Display *display, Window window, GC gc);
void cleanup_xft();

#endif // DRAW_H
