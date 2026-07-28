/* See LICENSE for license details. */
/*
 * xwin.h: PURE CALLBACK CONTRACT — functions called from terminal logic (cupid.c)
 * into the X11 layer (xwin.c). cupid.c never includes <X11/...>.
 *
 * Entry points (xinit, run, parse_geometry_str) live in xentry.h.
 */
#ifndef XWIN_H
#define XWIN_H

#include "cupid.h"

/* Mode flags for xsetmode(set, flags). Values follow st conventions. */
enum win_mode {
    MODE_VISIBLE     = 1 << 0,
    MODE_FOCUSED     = 1 << 1,
    MODE_APPKEYPAD   = 1 << 2,
    MODE_MOUSEBTN    = 1 << 3,
    MODE_MOUSEMOTION = 1 << 4,
    MODE_REVERSE     = 1 << 5,
    MODE_KBDLOCK     = 1 << 6,
    MODE_HIDE        = 1 << 7,
    MODE_APPCURSOR   = 1 << 8,
    MODE_MOUSESGR    = 1 << 9,
    MODE_8BIT        = 1 << 10,
    MODE_BLINK       = 1 << 11,
    MODE_FBLINK      = 1 << 12,
    MODE_FOCUS       = 1 << 13,
    MODE_MOUSEX10    = 1 << 14,
    MODE_MOUSEMANY   = 1 << 15,
    MODE_BRCKTPASTE  = 1 << 16,
    MODE_NUMLOCK     = 1 << 17,
};

/* Upstream st-compatible callbacks */
void xbell(void);
void xclipcopy(void);
void xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og);
void xdrawline(Line line, int x1, int y1, int x2);
void xfinishdraw(void);
void xstartdraw(void);
void xloadcols(void);
int  xsetcolorname(int x, const char *name);
int  xparsecolor(const char *name, uint32_t *color);
int  xgetcolor(int x, uint8_t *r, uint8_t *g, uint8_t *b);
void xseticontitle(char *p);
void xsettitle(char *p);
void xsetsel(char *str);
int  xsetcursor(int cursor);
void xsetmode(int set, unsigned int flags);
void xsetpointermotion(int set);
void xximspot(int x, int y);
void xresize(int col, int row);

/* cupidterminal extensions */
void xzoom(float delta);
void xzoomreset(void);

#endif /* XWIN_H */
