#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

/* Observe real redraws without adding X events or changing their behavior. */
int XCopyArea(Display *d, Drawable src, Drawable dst, GC gc, int sx, int sy,
              unsigned int width, unsigned int height, int dx, int dy) {
    typedef int (*Copy)(Display *, Drawable, Drawable, GC, int, int,
                        unsigned int, unsigned int, int, int);
    static Copy real_copy;
    static int fd = -2;
    if (!real_copy) real_copy = (Copy)dlsym(RTLD_NEXT, "XCopyArea");
    if (!real_copy) _exit(2);
    if (fd == -2) {
        const char *path = getenv("CUPID_COPY_COUNT");
        fd = path ? open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600) : -1;
    }
    if (fd >= 0) (void)write(fd, ".", 1);
    return real_copy(d, src, dst, gc, sx, sy, width, height, dx, dy);
}
