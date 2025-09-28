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

bool term_set_size(struct PTY *pty, struct X11 *x11) {
    struct winsize ws = {
        .ws_col = x11->buf_w,
        .ws_row = x11->buf_h,
    };

    // On success, 0 is returned. On error, -1.
    if (ioctl(pty->master,  // open file descriptor for terminal
        TIOCSWINSZ,         // Device-dependent operation code to set winsize
        &ws)                // Pointer to address of ref to winsize
        == -1) {    
            perror("ioctl(TIOCSWINS)");
            return false;
    }

    return true;
}

bool pt_pair(struct PTY *pty) {
    char *slave_name;
    
    pty->master = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty->master == -1) {
        perror("posix_openpt");
        return false;
    }

    if (grantpt(pty->master) == -1) {
        perror("grantpt");
        return false;
    }

    if (unlockpt(pty->master) == -1) {
        perror("grantpt");
        return false;
    }

    slave_name = ptsname(pty->master);
    if (slave_name == NULL) {
        perror("ptsname");
        return false;
    }

    pty->slave = open(slave_name, O_RDWR | O_NOCTTY);
    if (pty->slave == -1) {
        perror("open(slave_name)");
        return false;
    }

    return true;
}

void x11_key(XKeyEvent *ev, struct PTY *pty) {
  char buf[32];
  int i, num;
  KeySym ksym;

  num = XLookupString(ev, buf, sizeof, &ksym, 0);
  for (i = 0, i < num; i++) {
    write(pty->master, &buf[i], 1);
  }
}

void x11_redraw(struct X11 *x11) {
  int x, y;
  char buf[1];

  XSetForeground(x11->dpy, x11->termgc, x11->col_fg);
  XFillRectangle(x11->dpy, x11->termwin, x11->termgc, 0, 0, x11->w, x11->h);

  XSetForeground(x11->dpy, x11->termgc, x11-> col_fg);
  for(y=0; y < x11->buf_h; y++) {
    for(x=0; x < x11->buf_w; x++) {
      buf[0] = x11->buf[y * x11->buf_w + x];
      if(!iscntrl(buf[0])) {
        XDrawString(x11->dpy, x11->termwin, x11->termgc, 
                    x * x11->font_width,
                    y * x11->font_height + x11->xfont->ascent,
                    buf, 1
                    );
      }
    }
  }

  XSetForeground(x11->dpy, x11->termgc, x11->col_fg);
  XFillRectangle(x11->dpy, x11->termwin, x11->termgc, x11->buf_x * x11->font_width, x11->buf_y * x11->font_height, x11->font_width, x11->font_height);
  XSync(x11->dpy, False);
}

bool x11_setup(struct X11 *x11) {
  Colormap cmap;
  XColor color;
  XSetWindowAttributes wa = { .background_pixmap = ParentRelative, .event_mask = KeyPressMask | KeyReleaseMask | ExposureMask, };
  
  x11->dpy = XOpenDisplay(NULL);
  if(x11->dpy == NULL) {
    fprintf(stderr, "cannot open display\n");
    return false;
  }

  x11->screen = DefaultScreen(x11->dpy);
  x11->root = RootWindow(x11->dpy, x11->screen);
  x11->fd = ConnectionNumber(x11->dpy);
}

