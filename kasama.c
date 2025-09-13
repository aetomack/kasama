#define _XOPEN_SOURCE 600
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

/* Caution while launching /bin/sh: may launch GNU Bash. 
 * Potential side-effects could be wiping ~/.bash_history
 * because it disregards $HISTSIZE from ~/.bashsrc.
 * Instead, launch /bin/dash.
*/

// Pseudoterminal struct for master, slave
struct PTY {
    int master, slave; // int because posix_openpt returns 
                       // an int of lowest unused file descriptor
};

// windowing protocol 
struct X11 {
    int fd;
    Display *dpy; 
    int screen;
    Window root;

    Window termwin;
    GC termgc;
    unsigned long col_fg, col_bg; 
    int w, h;

    XFontStruct *xfont;
    int font_width, font_height;
    char *buf;
    int buf_w, buf_h;
    int buf_x, buf_y;
};

bool
term_set_size(struct PTY *pty, struct X11 *x11) {
    struct winsize ws = {
        .ws_col = x11->buf_w,
        .ws_row = x11->buf_h,
    };

    return true;
}

