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
#include <stdlib.h>
#include <unistd.h>

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

  num = XLookupString(ev, buf, sizeof buf, &ksym, 0);
  for (i = 0; i < num; i++) {
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

  x11->xfont = XLoadQueryFont(x11->dpy, "fixed");
  if(x11->xfont == NULL) {
    fprintf(stderr, "Could not load font\n");
    return false;
  }
  x11->font_width = XTextWidth(x11->xfont, "m", 1);
  x11->font_height = x11->xfont->ascent + x11->xfont->descent;

  cmap = DefaultColormap(x11->dpy, x11->screen);

  if(!XAllocNamedColor(x11->dpy, cmap, "#000000", &color, &color)){
    fprintf(stderr, "Could not load color\n");
    return false;
  }
  x11->col_bg = color.pixel;

  if(!XAllocNamedColor(x11->dpy, cmap, "#aaaaaa", &color, &color)){
    fprintf(stderr, "Could not load bg color\n");
    return false;
  }
  x11->col_fg = color.pixel;

  /* Terminal will have an absolute, arbitrary size. WIll need to automatically resize based on wayland/hyprland
  *  No resizing is available atm. Current size is 80x25 cells
  */
  x11->buf_w = 80;
  x11->buf_h = 25;
  x11->buf_x = 0;
  x11->buf_y = 0;
  x11->buf =calloc(x11->buf_w * x11->buf_h, 1);
  if(x11->buf == NULL) {
    perror("calloc");
    return false;
  }

  x11->w = x11->buf_w * x11->font_width;
  x11->h = x11->buf_h * x11->font_height;

  x11->termwin = XCreateWindow(x11->dpy, x11->root, 0, 0, x11->w, x11->h, 0, DefaultDepth(x11->dpy, x11->screen), CopyFromParent, DefaultVisual(x11->dpy, x11->screen), CWBackPixmap | CWEventMask, &wa);
  XStoreName(x11->dpy, x11->termwin, "kasama");
  XMapWindow(x11->dpy, x11->termwin);
  x11->termgc = XCreateGC(x11->dpy, x11->termwin, 0, NULL);
  XSync(x11->dpy, False);

  return true;
}

bool spawn(struct PTY *pty) {
    pid_t p; 
    char *env[] = {"TERM=dumb", NULL};

    p = fork();
    if (p == 0) {
        close(pty->master);
        setsid();
        if (ioctl(pty->slave, TIOCSCTTY, NULL) == -1) {
            perror("ioctl(TIOCSCTTY)");
            return false;
        }

        dup2(pty->slave, 0);
        dup2(pty->slave, 1);
        dup2(pty->slave, 2);
        close(pty->slave);

        execle(SHELL, "-" SHELL, (char *)NULL, env);
        return false;
    } else if (p > 0){
        close(pty->slave);
        return false;
    }

    perror("fork");
    return false;
}

int run(struct PTY *pty, struct X11 *x11) {
    int i, maxfd;
    fd_set readable;
    XEvent evl
    char buf[1];
    bool just_wrapped = false;

    maxfd = pty->master > x11->fd ? pty->master : x11->fd;

    for(;;) {
        FD_ZERO(&readable);
        FD_SET(pty->master, &readable);
        FD_SET(x11->fd, &readable);

        if(select(maxfd + 1, &readable, NULL, NULL, NULL) {
            perror("select");
            return 1;
        }

        if(FD_ISSET(pty->master, &readable)) {
            if (read(pty->master, buf, 1) <= 0) {
                fprintf(stderr, "Nothing to read from child: ");
                perror(NULL);
                return 1;
            }

            if(buf[0] == '\r') {
                /* Carriage returns are the simplest terminal command.
                * They make the cursor jump to the back of the first column. 
                */
                x11->buf_x = 0;
            } else {
                if (buf[0] != '\n') {
                    /* If a regular byte, store it and advance the cursor one cell "to the right".
                    *  Might potentially wrap to the next line.
                    */
                    x11->buf[x11->buf_y * x11->buf_w + x11->buf_x] = buf[0];
                    x11->buf_x++;

                    if(x11->buf_x >= x11->buf_w) { // wrap to next line
                        x11->buf_x = 0; 
                        x11->buf_y++;
                        just_wrapped = true;
                    }

                    just_wrapped = false;
                } else if (!just_wrapped) {
                    /* We read a newline and did NOT implicity wrap to the next line with the last byte we read. 
                    * This means we must NOW advance to the next line.
                    * This is basically the same behavior as every other terminal emulator. If you print a full
                    * line and then to a newline, it just ignores that \n. Best to behave this way, as considering the \n
                    * could cause the cursor to jump to a newline again.
                    */
                    x11->buf_y++;
                    just_wrapped = false;
                }

                /* We now need to check if the next line is actually outside of the buffer. 
                * If so, shift all content a line up and then stay in the last line.
                * After the memmove(), the last line still has the old content. Must clear.
                */
                if (x11->buf_y >= x11->buf_h) {
                    memmove(x11->buf, &x11->buf[x11->buf_w],
                            x11->buf_w * (x11->buf_h -1));
                    x11->buf_y = x11->buf_h -1;

                    for(i = 0; i<x11->buf_w; i++) {
                        x11->buf[x11->buf_y * x11->buf_w + i] = 0;
                    }
                }
            }
            x11_redraw(x11);
        }
        if (FD_ISSET(x11->fd, &readable)){
            while (XPending(x11->dpy)){
                XNextEvent(x11->dpy, &ev);
                switch (ev.type) {
                    case Expose:
                        x11_redraw(x11);
                        break; 
                    case KeyPress:
                        x11_key(&ev.xkey, pty);
                        break;
                }
            }
        }
    }
    return 0;
}

int main() {
    struct PTY pty;
    struct X11 x11;

    if(!x11_setup(&x11)) return 1;
    if(!pt_pair(&pty)) return 1;
    if(!term_set_size(&pty, &x11) return 1;
    if(!spawn(&pty)) return 1;

    return run(&pty, &x11);
}

