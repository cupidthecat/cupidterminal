// draw.h

#ifndef DRAW_H
#define DRAW_H

#include <X11/Xlib.h>
#include <Xft/Xft.h> // Include Xft for Unicode support

// Declare xft_color and xft_font as extern
extern XftColor xft_color;
extern XftFont *xft_font;
extern XftFont *xft_font_bold; // Declare bold font
extern XftFont *xft_font_bold; // Declare bold font globally
extern Window global_window; // Declare global window
extern Display *global_display; // Ensure display is also declared

void draw_text(Display *display, Window window, GC gc);
void append_text(const char *text);
void initialize_xft(Display *display, Window window);
void cleanup_xft();

#endif // DRAW_H
