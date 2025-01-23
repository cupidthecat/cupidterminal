#ifndef DRAW_H
#define DRAW_H

#include <X11/Xlib.h>
#include <Xft/Xft.h> // Include Xft for Unicode support

// Declare global fonts
extern XftColor xft_color;
extern XftFont *xft_font;       // Normal font
extern XftFont *xft_font_bold;  // Bold font
extern XftFont *xft_font_emoji; // Emoji font

extern Window global_window; // Declare global window
extern Display *global_display; // Ensure display is also declared

void draw_text(Display *display, Window window, GC gc);
void append_text(const char *text);
void initialize_xft(Display *display, Window window);
void cleanup_xft();

#endif // DRAW_H
