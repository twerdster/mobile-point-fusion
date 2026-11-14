// New X11/EGL frontend. All cloud changes come from live shader/CPU execution.
#include "../../src/live.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <chrono>
#include <iostream>
#include <thread>
using namespace pf;
int main(int argc, char **argv) {
  try {
    std::string root = SOURCE_ROOT, output = "mobile-fusion-native-output",
                recording;
    bool auto_exit = false;
    for (int i = 1; i < argc; i++) {
      std::string a = argv[i];
      if (a == "--output" && i + 1 < argc)
        output = argv[++i];
      else if (a == "--recording" && i + 1 < argc)
        recording = argv[++i];
      else if (a == "--auto-exit")
        auto_exit = true;
      else
        throw std::runtime_error("usage: points_fusion_ui [--output DIR] "
                                 "[--recording PACK] [--auto-exit]");
    }
    Display *xd = XOpenDisplay(nullptr);
    require(xd, "X11 display unavailable");
    EGLDisplay ed = eglGetDisplay((EGLNativeDisplayType)xd);
    EGLint ma, mi;
    require(eglInitialize(ed, &ma, &mi), "EGL X11 initialization failed");
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint attrs[] = {EGL_SURFACE_TYPE,
                      EGL_WINDOW_BIT,
                      EGL_RENDERABLE_TYPE,
                      EGL_OPENGL_ES3_BIT,
                      EGL_RED_SIZE,
                      8,
                      EGL_GREEN_SIZE,
                      8,
                      EGL_BLUE_SIZE,
                      8,
                      EGL_DEPTH_SIZE,
                      24,
                      EGL_NONE};
    EGLConfig config;
    EGLint count;
    require(eglChooseConfig(ed, attrs, &config, 1, &count) && count == 1,
            "EGL window configuration unavailable");
    EGLint visual;
    eglGetConfigAttrib(ed, config, EGL_NATIVE_VISUAL_ID, &visual);
    XVisualInfo query{};
    query.visualid = visual;
    int n;
    XVisualInfo *vi = XGetVisualInfo(xd, VisualIDMask, &query, &n);
    require(vi, "EGL X11 visual unavailable");
    int width = 1180, height = 800, screen = DefaultScreen(xd);
    Window win = XCreateSimpleWindow(xd, RootWindow(xd, screen), 0, 0, width,
                                     height, 0, 0, 0x071022);
    XStoreName(xd, win, "Mobile Point Fusion — native shader + CPU execution");
    XSelectInput(xd, win,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                     StructureNotifyMask);
    Atom close = XInternAtom(xd, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(xd, win, &close, 1);
    Colormap cmap = XCreateColormap(xd, win, vi->visual, AllocNone);
    XSetWindowAttributes wa{};
    wa.colormap = cmap;
    wa.event_mask = ExposureMask | ButtonPressMask | Button1MotionMask;
    Window canvas =
        XCreateWindow(xd, win, 24, 128, width - 48, height - 285, 0, vi->depth,
                      InputOutput, vi->visual, CWColormap | CWEventMask, &wa);
    XMapWindow(xd, canvas);
    XMapWindow(xd, win);
    XFlush(xd);
    EGLSurface surface = eglCreateWindowSurface(
        ed, config, (EGLNativeWindowType)canvas, nullptr);
    EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(ed, config, EGL_NO_CONTEXT, ca);
    require(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT &&
                eglMakeCurrent(ed, surface, surface, context),
            "EGL X11 context failed");
    GC gc = XCreateGC(xd, win, 0, nullptr);
    XSetForeground(xd, gc, 0xffffff);
    auto text = [&](int x, int y, const std::string &s) {
      XDrawString(xd, win, gc, x, y, s.c_str(), s.size());
    };
    bool running = true, quit = false;
    float orbit = 0;
    int previous_x = 0;
    auto last = std::chrono::steady_clock::now();
    {
      LiveFusion fusion(root, output, recording, true, false);
      while (!quit) {
        while (XPending(xd)) {
          XEvent e;
          XNextEvent(xd, &e);
          if (e.type == ClientMessage && Atom(e.xclient.data.l[0]) == close)
            quit = true;
          if (e.type == KeyPress) {
            auto k = XLookupKeysym(&e.xkey, 0);
            if (k == XK_Escape)
              quit = true;
            if (k == XK_space)
              running = !running;
            if (k == XK_r) {
              fusion.reset();
              running = true;
              orbit = 0;
            }
          }
          if (e.type == ButtonPress) {
            previous_x = e.xbutton.x;
            if (e.xbutton.window == win && e.xbutton.y >= height - 90 &&
                e.xbutton.y <= height - 62 && e.xbutton.x >= 24 &&
                e.xbutton.x <= 480) {
              if (e.xbutton.x <= 250)
                running = !running;
              else if (e.xbutton.x >= 280) {
                fusion.reset();
                running = true;
                orbit = 0;
              }
            }
          }
          if (e.type == MotionNotify) {
            orbit += (e.xmotion.x - previous_x) / 180.f;
            previous_x = e.xmotion.x;
          }
          if (e.type == ConfigureNotify && e.xconfigure.window == win) {
            width = e.xconfigure.width;
            height = e.xconfigure.height;
            XMoveResizeWindow(xd, canvas, 24, 128, std::max(100, width - 48),
                              std::max(100, height - 285));
            XClearWindow(xd, win);
          }
        }
        auto now = std::chrono::steady_clock::now();
        if (running &&
            std::chrono::duration<double>(now - last).count() > .15) {
          fusion.step();
          last = now;
          XClearWindow(xd, win);
        }
        XClearArea(xd, win, 0, height - 150, width, 150, False);
        text(24, 32, "MOBILE POINT FUSION");
        text(24, 60,
             fusion.sequence.real()
                 ? "Native ES3 shader + CPU execution | Recorded RGB-D | "
                   "Supplied camera poses"
                 : "Native ES3 shader + CPU execution | Generated observations "
                   "| Supplied poses");
        text(24, 84,
             fusion.sequence.real()
                 ? "Recorded depth + RGB, metres. No pose estimation. Drag to "
                   "orbit."
                 : "No real RGB-D, camera input or tracking. Drag to orbit.");
        text(30, 116, "CURRENT OBSERVATION");
        text(width / 2 + 16, 116, "ACCUMULATED MODEL");
        std::string status =
            "View " + std::to_string(fusion.frame) + " / " +
            std::to_string(fusion.frames) + " | " +
            std::to_string(fusion.model.size()) + " points | " +
            std::to_string(fusion.model.merged) + " merges | " +
            std::to_string(fusion.model.removed) + " removals";
        text(24, height - 128, status);
        text(24, height - 106,
             std::to_string(fusion.compared) +
                 " fusion correspondences checked against CPU");
        text(24, height - 74, running ? "[Pause / Space]" : "[Resume / Space]");
        text(280, height - 74, "[Reset and run / R]");
        text(24, height - 42,
             "2015: D. Ben-Hoda, G. Kamar, A. Wetzler, Y. Honen, R. Kimmel");
        text(24, height - 22,
             "2026 adapter | Daniel & Gal notices preserved | Esc closes");
        fusion.render(0, std::max(100, width - 48), std::max(100, height - 285),
                      orbit);
        require(eglSwapBuffers(ed, surface), "EGL swap failed");
        XFlush(xd);
        if (auto_exit && fusion.frame == fusion.frames)
          quit = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
      }
    }
    eglMakeCurrent(ed, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(ed, context);
    eglDestroySurface(ed, surface);
    eglTerminate(ed);
    XFreeGC(xd, gc);
    XDestroyWindow(xd, win);
    XFree(vi);
    XCloseDisplay(xd);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "NATIVE_FUSION_FAILURE " << e.what() << std::endl;
    return 1;
  }
}
