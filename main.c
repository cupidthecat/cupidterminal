// bare bones C x11 terminal for linux systems
#include <X11/Xlib.h>
#include <stdio.h>
#include <X11/Xutil.h>
#include <string.h>
#include <stdlib.h>

#define MAX_CMD_LENGTH 256

int next_line_y = 50; // starting y position for output lines

int main() {
    Display *display;
    Window window;
    XEvent event;
    char cmd[MAX_CMD_LENGTH] = {0};
    int screen;

    // open display
    display = XOpenDisplay(NULL);
    if(display == NULL) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    screen = DefaultScreen(display);

    // create window
    window = XCreateSimpleWindow(display, RootWindow(display, screen), 10, 10, 500, 300, 1,
                                 BlackPixel(display, screen), WhitePixel(display, screen));

    // select kind of events we are interested in
    XSelectInput(display, window, ExposureMask | KeyPressMask);

    // map (show) the window
    XMapWindow(display, window);

    // Create a new GC for the background color
    XGCValues values;
    values.foreground = WhitePixel(display, screen);
    GC background_gc = XCreateGC(display, window, GCForeground, &values);

    // event loop
    while(1) {
        XNextEvent(display, &event);

        /* draw or redraw the window */
        if(event.type == Expose) {
            XFillRectangle(display, window, DefaultGC(display, screen), 20, 20, 10, 10);
            XDrawString(display, window, DefaultGC(display, screen), 50, next_line_y, cmd, strlen(cmd));
        }
        // handle key press
        if(event.type == KeyPress) {
            char buf[128] = {0};
            KeySym keysym;
            XComposeStatus composeStatus;
            FILE *fp; // Declare fp here
            char output[1024] = {0}; // Declare output here
            int len = XLookupString(&event.xkey, buf, sizeof buf, &keysym, &composeStatus);
            if(len > 0) {
                switch (keysym) {
                    case XK_Return: // Enter key
                        // execute command and get output
                        fp = popen(cmd, "r");
                        if (fp == NULL) {
                            printf("Failed to run command\n" );
                            return 1;
                        }
                        next_line_y += 15; // increment y position for next line before executing the command
                        while (fgets(output, sizeof(output)-1, fp) != NULL) {
                            char *line = strtok(output, "\n");
                            while(line != NULL) {
                                XDrawString(display, window, DefaultGC(display, screen), 50, next_line_y, line, strlen(line)); // draw the output line
                                XFlush(display); // force the X server to perform all queued actions
                                next_line_y += 15; // increment y position for next line
                                line = strtok(NULL, "\n");
                            }
                        }
                        pclose(fp);
                        memset(cmd, 0, sizeof cmd); // clear command
                        break;
                    case XK_BackSpace: // Backspace key
                        if(strlen(cmd) > 0) {
                            cmd[strlen(cmd) - 1] = '\0'; // remove last character
                            XFillRectangle(display, window, background_gc, 50, next_line_y - 15, 500, 15); // clear the current line
                            XDrawString(display, window, DefaultGC(display, screen), 50, next_line_y, cmd, strlen(cmd)); // redraw the string
                            XFlush(display); // force the X server to perform all queued actions
                        }
                        break;
                    default: // Other keys
                        if(strlen(cmd) < MAX_CMD_LENGTH - 1) {
                            strncat(cmd, buf, len);
                        }
                        XDrawString(display, window, DefaultGC(display, screen), 50, next_line_y, cmd, strlen(cmd)); // redraw the string
                        XFlush(display); // force the X server to perform all queued actions
                        break;
                }
            }
        }
    }


    /* close connection to server */
    XCloseDisplay(display);

    return 0;
}