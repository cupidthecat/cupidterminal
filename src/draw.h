#ifndef DRAW_H
#define DRAW_H

/* Padding used for winsize calculation - must match draw.c */
#define DRAW_LEFT_PAD 10
#define DRAW_TOP_PAD  5

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
void draw_notify_resize(int w, int h);
void append_text(const char *text);
void initialize_xft(Display *display, Window window);
void cleanup_xft(void);
void xy_to_cell(int x, int y, int *row, int *col);
void xft_zoom(Display *display, Window window, float delta);
void xft_zoom_reset(Display *display, Window window);
void xft_set_font_change_hook(void (*hook)(Display *display, Window window));

/* XIM input method support */
extern XIC g_xic;
void xximspot(Display *display, Window window);
void xim_focus_in(void);
void xim_focus_out(void);

#endif // DRAW_H
