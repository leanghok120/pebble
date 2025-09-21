#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <fontconfig/fontconfig.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <unistd.h>
#include <pty.h>
#include <vterm.h>
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
int rows = 24;
int cols = 80;
int char_width, char_height;

static VTerm *vt;
static VTermScreen *vts;

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

  char_height = term.font->ascent + term.font->descent;
  char_width = term.font->max_advance_width;

  return term;
}

void clean_up(Terminal *term) {
  XftColorFree(term->dpy, term->visual, term->colormap, &term->color);
  XftDrawDestroy(term->draw);
  XftFontClose(term->dpy, term->font);
  XCloseDisplay(term->dpy);
}

void draw_screen(Terminal *term) {
  XClearWindow(term->dpy, term->win);

  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      VTermScreenCell cell;
      VTermPos pos = { r, c };
      if (vterm_screen_get_cell(vts, pos, &cell)) {
        if (cell.chars[0]) {
          char utf8[8];
          int len = wctomb(utf8, cell.chars[0]);
          if (len > 0) {
            XftDrawStringUtf8(term->draw, &term->color, term->font,
                c * char_width,
                (r + 1) * char_height,
                (FcChar8 *)utf8, len);
          }
        }
      }
    }
  }
  XFlush(term->dpy);
}

int main() {
  // handle signals
  signal(SIGINT, handle_sigint);
  signal(SIGTERM, handle_sigint);

  Terminal term = create_terminal();
  Atom wm_delete = XInternAtom(term.dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(term.dpy, term.win, &wm_delete, 1);

  vt = vterm_new(rows, cols);
  vts = vterm_obtain_screen(vt);
  vterm_screen_reset(vts, 1);

  int master_fd;
  pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
  if (pid == -1) {
    perror("forkpty");
    exit(1);
  }
  if (pid == 0) {
    char *shell = getenv("SHELL");
    if (!shell) shell = "/bin/sh";
    execlp(shell, shell, (char *)NULL);
    perror("execlp");
    _exit(1);
  }

  XEvent ev;
  while (running) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(master_fd, &fds);
    int x11_fd = ConnectionNumber(term.dpy);
    FD_SET(x11_fd, &fds);
    int maxfd = master_fd > x11_fd ? master_fd : x11_fd;

    if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0) break;

    // handle x11 events
    while (XPending(term.dpy)) {
      XNextEvent(term.dpy, &ev);
      switch (ev.type) {
        case Expose:
          draw_screen(&term);
          break;
        case KeyPress: {
                         char keybuf[32];
                         KeySym keysym;
                         int len = XLookupString(&ev.xkey, keybuf, sizeof(keybuf), &keysym, NULL);
                         if (len > 0) {
                           /* TODO: proper keyboard mapping via vterm_keyboard_unichar */
                           write(master_fd, keybuf, len);
                         }
                         break;
                       }
        case ClientMessage:
                       if ((Atom)ev.xclient.data.l[0] == wm_delete) running = 0;
                       break;
      }
    }

    // handle reading the pty
    if (FD_ISSET(master_fd, &fds)) {
      char buf[4096];
      ssize_t n = read(master_fd, buf, sizeof(buf));
      if (n > 0) {
        vterm_input_write(vt, buf, n);
        draw_screen(&term);
      } else if (n == 0) {
        if (waitpid(pid, NULL, WNOHANG) != 0) running = 0;
      } else {
        if (errno == EIO) running = 0;
        else perror("read");
      }
    }
  }

  kill(pid, SIGTERM);
  clean_up(&term);
  return 0;
}
