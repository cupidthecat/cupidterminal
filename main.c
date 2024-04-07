// bare bones C x11 terminal for linux systems
#include <X11/Xlib.h>
#include <stdio.h>
#include <X11/Xutil.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

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

    // set window title
    XStoreName(display, window, "cupidterm");

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

        // draw or redraw the window
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
                        printf("%s\n", cmd); // log the command to the IDE terminal
                        next_line_y += 15; // increment y position for next line before executing the command
                        // check if the command starts with 'cd '
                        if(strncmp(cmd, "cd ", 3) == 0) {
                            char *dir = cmd + 3;
                            char full_dir[MAX_CMD_LENGTH] = {0};
                            if(dir[0] == '~') {
                                char *home_dir = getenv("HOME");
                                if(home_dir != NULL) {
                                    snprintf(full_dir, sizeof(full_dir), "%s%s", home_dir, dir + 1);
                                    dir = full_dir;
                                }
                            }
                            if(chdir(dir) != 0) {
                                perror("Failed to change directory");
                            }
                        } else if(strcmp(cmd, "clear") == 0) {
                            XClearWindow(display, window); // clear the window
                            next_line_y = 50; // reset y position for output lines
                        } else {
                            // execute command and get output
                            char cmd_with_stderr[MAX_CMD_LENGTH + 5]; // +5 for " 2>&1" and null terminator
                            snprintf(cmd_with_stderr, sizeof(cmd_with_stderr), "%s 2>&1", cmd);
                            fp = popen(cmd_with_stderr, "r");
                            if (fp == NULL) {
                                char *error_message = "Failed to run command";
                                XDrawString(display, window, DefaultGC(display, screen), 50, next_line_y, error_message, strlen(error_message)); // draw the error message to the X11 terminal
                                next_line_y += 15; // increment y position for next line even if command fails
                            } else {
                                while (fgets(output, sizeof(output)-1, fp) != NULL) {
                                    char *line = strtok(output, "\n");
                                    while(line != NULL) {
                                        XDrawString(display, window, DefaultGC(display, screen), 50, next_line_y, line, strlen(line)); // draw the output line
                                        XFlush(display); // force the X server to perform all queued actions
                                        printf("%s\n", line); // log the output to the IDE terminal
                                        next_line_y += 15; // increment y position for next line
                                        line = strtok(NULL, "\n");
                                    }
                                }
                                pclose(fp);
                            }
                        }
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

    // close display
    XCloseDisplay(display);

    return 0;
}