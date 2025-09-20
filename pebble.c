#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pty.h>

typedef struct {
  Display *dpy;
  int scr;
  Visual *visual;
  Colormap colormap;
  Window win;
  XftDraw *draw;
  XftFont *font;
  XftColor color;
} Terminal;

int running = 1;

void handle_sigint(int sig) {
  running = 0;
}

Terminal create_terminal() {
  // init window
  Terminal term;
  term.dpy = XOpenDisplay(NULL);
  if (term.dpy == NULL) {
    perror("dpy");
    exit(1);
  }
  term.scr = DefaultScreen(term.dpy);
  term.visual = DefaultVisual(term.dpy, term.scr);
  term.colormap = DefaultColormap(term.dpy, term.scr);
  term.win = XCreateSimpleWindow(term.dpy, DefaultRootWindow(term.dpy),
            0, 0, 800, 450, 0,
            BlackPixel(term.dpy, term.scr), BlackPixel(term.dpy, term.scr));
  XStoreName(term.dpy, term.win, "pebble");
  XSelectInput(term.dpy, term.win, ExposureMask | KeyPressMask);
  XMapWindow(term.dpy, term.win);

  // init xft
  term.draw = XftDrawCreate(term.dpy, term.win, term.visual, term.colormap);
  term.font = XftFontOpenName(term.dpy, term.scr, "CaskaydiaMono Nerd Font:size=20");
  XRenderColor xrcolor = { 0xffff, 0xffff, 0xffff, 0xffff };
  XftColorAllocValue(term.dpy, term.visual, term.colormap, &xrcolor, &term.color);

  return term;
}

void clean_up(Terminal *term) {
  XftDrawDestroy(term->draw);
  XftFontClose(term->dpy, term->font);
  XCloseDisplay(term->dpy);
}

int main() {
  // handle signals
  signal(SIGINT, handle_sigint);
  signal(SIGTERM, handle_sigint);

  Terminal term = create_terminal();
  Atom wm_delete = XInternAtom(term.dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(term.dpy, term.win, &wm_delete, 1);

  char buf[1024] = "";
  int buflen = 0;
  XEvent ev;
  while (running) {
    XNextEvent(term.dpy, &ev);
    switch (ev.type) {
      case Expose:
        XClearWindow(term.dpy, term.win);
        XftDrawStringUtf8(term.draw, &term.color, term.font, 50, 50, (FcChar8 *)buf, strlen(buf));
        break;

      case KeyPress:
        char keybuf[32];
        KeySym keysym;
        int len = XLookupString(&ev.xkey, keybuf, sizeof(keybuf) - 1, &keysym, NULL);
        keybuf[len] = '\0';

        // handle backspace
        if (keysym == XK_BackSpace) {
          if (buflen >= 0) {
            buf[buflen] = '\0';
            buflen--;
            XClearWindow(term.dpy, term.win);
            XftDrawStringUtf8(term.draw, &term.color, term.font, 50, 50, (FcChar8 *)buf, strlen(buf));
          }
          break;
        }

        // avoid buffer overflow
        if (buflen + len < sizeof(buf)) {
          strcat(buf, keybuf);
          buflen += len;
        }

        XClearWindow(term.dpy, term.win);
        XftDrawStringUtf8(term.draw, &term.color, term.font, 50, 50, (FcChar8 *)buf, strlen(buf));
        break;

      // wm ask to close pebble
      case ClientMessage:
        if (ev.xclient.data.l[0] == wm_delete) {
          running = 0;
        }
        break;
    }
  }

  clean_up(&term);

  return 0;
}
