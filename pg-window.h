#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

static const char* egl_error_string(EGLint err) {
    switch (err) {
        case EGL_SUCCESS: return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
        case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
        case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
        case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
        case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
        case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
        default: return "Unknown EGL error";
    }
}

static void egl_check(const char* where) {
    EGLint err = eglGetError();
    if (err != EGL_SUCCESS) {
        fprintf(stderr, "EGL error at %s: 0x%x (%s)\n", where, err, egl_error_string(err));
        exit(1);
    }
}

typedef struct {
  Display* x_dpy;
  Window win;
  Atom WM_DELETE_WINDOW;
  EGLDisplay egl_dpy;
  EGLContext ctx;
  EGLSurface surf;  
} PGX11Window;

static int pg_window_open_x11 (PGX11Window *pgw, int width, int height, const char *title)
{
    Display* x_dpy = XOpenDisplay(NULL);
    if (!x_dpy) {
      fprintf(stderr, "XOpenDisplay failed\n");
      goto bad;
    }

    int screen = DefaultScreen(x_dpy);
    Window root = RootWindow(x_dpy, screen);
    
    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof(swa));
    swa.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;
    
    Window win = XCreateWindow(
        x_dpy, root,
        0, 0, (unsigned)width, (unsigned)height,
        0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWEventMask, &swa
    );

    if (!win) {
      fprintf(stderr, "XCreateWindow failed\n");
      goto bad;
    }

    XStoreName(x_dpy, win, title);
    XMapWindow(x_dpy, win);

    Atom WM_DELETE_WINDOW = XInternAtom(x_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(x_dpy, win, &WM_DELETE_WINDOW, 1);

    pgw->x_dpy = x_dpy;
    pgw->win = win;
    pgw->WM_DELETE_WINDOW = WM_DELETE_WINDOW;
    return 0;
bad:
    return 1;
}

static void pg_window_unbind_context_egl (PGX11Window *pgw)
{
  eglMakeCurrent(pgw->egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(pgw->egl_dpy, pgw->ctx);
  eglDestroySurface(pgw->egl_dpy, pgw->surf);
  eglTerminate(pgw->egl_dpy); 
}

static void pg_window_close_x11 (PGX11Window *pgw)
{
  pg_window_unbind_context_egl (pgw);

  XDestroyWindow(pgw->x_dpy, pgw->win);
  XCloseDisplay(pgw->x_dpy);
}

static void pg_window_swap_buffers (PGX11Window *pgw)
{
  eglSwapBuffers(pgw->egl_dpy, pgw->surf);
}

static int pg_window_bind_context_egl (PGX11Window *pgw)
{
    // ---- EGL: get display, initialize
  EGLDisplay egl_dpy = eglGetDisplay((EGLNativeDisplayType)pgw->x_dpy);
  if (egl_dpy == EGL_NO_DISPLAY) {
    fprintf(stderr, "eglGetDisplay returned EGL_NO_DISPLAY\n");
    goto bad;
  }

  if (!eglInitialize(egl_dpy, NULL, NULL)) {
    fprintf(stderr, "eglInitialize failed\n");
    goto bad;
  }

  egl_check("eglInitialize");

  // Ensure we are using OpenGL ES (not desktop OpenGL)
  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    fprintf(stderr, "eglBindAPI(EGL_OPENGL_ES_API) failed\n");
    goto bad;
  }
  egl_check("eglBindAPI");

  // ---- Choose an EGLConfig suitable for an X11 window surface + GLES3
  const EGLint config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 24,
    EGL_STENCIL_SIZE, 8,
    EGL_NONE
  };

  EGLConfig cfg;
  EGLint num_configs = 0;
  if (!eglChooseConfig(egl_dpy, config_attribs, &cfg, 1, &num_configs) || num_configs < 1) {
    fprintf(stderr, "eglChooseConfig failed to return a matching EGLConfig\n");
    goto bad;
  }
  egl_check("eglChooseConfig");

  // ---- Create EGL window surface from the X11 Window
  EGLSurface surf = eglCreateWindowSurface(egl_dpy, cfg, (EGLNativeWindowType)pgw->win, NULL);
  if (surf == EGL_NO_SURFACE) {
    fprintf(stderr, "eglCreateWindowSurface failed\n");
    goto bad;
  }
  egl_check("eglCreateWindowSurface");

  // ---- Create GLES3 context
  const EGLint ctx_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 3,
    EGL_NONE
  };

  EGLContext ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
  if (ctx == EGL_NO_CONTEXT) {
    fprintf(stderr, "eglCreateContext failed\n");
    goto bad;
  }

  egl_check("eglCreateContext");

  // ---- Make current
  if (!eglMakeCurrent(egl_dpy, surf, surf, ctx)) {
    fprintf(stderr, "eglMakeCurrent failed\n");
    goto bad;    
  }
  egl_check("eglMakeCurrent");

  // Optional: enable vsync (1 = on, 0 = off). Not all stacks respect it.
  eglSwapInterval(egl_dpy, 1);

  pgw->egl_dpy = egl_dpy;
  pgw->ctx = ctx;
  pgw->surf = surf;
  return 0;
bad:
  return 1;
}

static int pg_window_loop (PGX11Window *pgw)
{
  int running = 1;

  while (XPending(pgw->x_dpy)) {
    XEvent ev;
    XNextEvent(pgw->x_dpy, &ev);
    
    if (ev.type == ClientMessage) {
      if ((Atom)ev.xclient.data.l[0] == pgw->WM_DELETE_WINDOW)
        running = 0;
    } else if (ev.type == ConfigureNotify) {
      // Export as something??
      glViewport(0, 0, ev.xconfigure.width, ev.xconfigure.height);
    } else if (ev.type == KeyPress) {
      running = 0;
    }
  }

  return running;
}

