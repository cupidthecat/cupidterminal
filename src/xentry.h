/* xentry.h — X11 layer entry points called from main(). NOT callbacks. */
#ifndef XENTRY_H
#define XENTRY_H

#include "pty.h"

void xinit(int cols, int rows, PtySession *session);
void run(void);
void parse_geometry_str(const char *geom, unsigned int *cols, unsigned int *rows);

#endif /* XENTRY_H */
