#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <fontconfig/fontconfig.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <pty.h>
#include "config.h"

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

XRenderColor hex_to_xrender_color(const char *hex) {
  XRenderColor xrcolor = {0, 0, 0, 0xffff};

  if (hex[0] == '#') {
    hex++;
  }

  unsigned int r, g, b;
  sscanf(hex, "%2x%2x%2x", &r, &g, &b); 

  xrcolor.red = r * 257;
  xrcolor.green = g * 257;
  xrcolor.blue = b * 257;

  return xrcolor;
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
      BlackPixel(term.dpy, term.scr), strtol(background + 1, NULL, 16));
  XStoreName(term.dpy, term.win, "pebble");
  XSelectInput(term.dpy, term.win, ExposureMask | KeyPressMask);
  XMapWindow(term.dpy, term.win);

  // init xft
  term.draw = XftDrawCreate(term.dpy, term.win, term.visual, term.colormap);
  term.font = XftFontOpenName(term.dpy, term.scr, font_name);
  XRenderColor xrcolor = hex_to_xrender_color(foreground);
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

  int master_fd;
  int pid = forkpty(&master_fd, NULL, NULL, NULL);
  if (pid == 0) {
    char *shell = getenv("SHELL");
    if (shell == NULL) {
      shell = "/bin/sh";
    }
    execlp(shell, shell, NULL);
    perror("execlp");
    _exit(1);
  }

  char buf[1024] = "";
  int buflen = 0;

  XEvent ev;
  while (running) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(master_fd, &fds);
    int x11_fd = ConnectionNumber(term.dpy);
    FD_SET(x11_fd, &fds);
    int maxfd = master_fd > x11_fd ? master_fd : x11_fd;

    struct timeval tv = {0, 10000}; // 10ms
    select(maxfd + 1, &fds, NULL, NULL, &tv);

    while (XPending(term.dpy)) {
      XNextEvent(term.dpy, &ev);
      switch (ev.type) {
        case Expose:
          XClearWindow(term.dpy, term.win);
          XftDrawStringUtf8(term.draw, &term.color, term.font, 50, 50, (FcChar8 *)buf, strlen(buf));
          break;

        case KeyPress:
          char keybuf[32];
          KeySym keysym;
          int len = XLookupString(&ev.xkey, keybuf, sizeof(keybuf), &keysym, NULL);
          keybuf[len] = '\0';
          if (len > 0) {
            write(master_fd, keybuf, len);
          }
          break;

          // wm ask to close pebble
        case ClientMessage:
          if (ev.xclient.data.l[0] == wm_delete) {
            running = 0;
          }
          break;
      }
    }

    if (FD_ISSET(master_fd, &fds)) {
      int n = read(master_fd, buf + buflen, sizeof(buf) - buflen - 1);
      if (n > 0) {
        buflen += n;
        buf[buflen] = '\0';

        XClearWindow(term.dpy, term.win);
        XftDrawStringUtf8(term.draw, &term.color, term.font,
            50, 50, (FcChar8 *)buf, buflen);
        continue;
      }

      if (n == 0) {
        // Check if child is still alive
        if (waitpid(pid, NULL, WNOHANG) != 0) {
          running = 0;
        }
        continue;
      }

      perror("read");
    }
  }

  kill(pid, SIGTERM);
  clean_up(&term);

  return 0;
}
