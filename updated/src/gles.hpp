#pragma once
#include "core.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

inline std::string read_text(std::string path) {
  std::ifstream f(path);
  require(bool(f), "cannot open shader");
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}
inline void glcheck(const char *where) {
  GLenum e = glGetError();
  if (e != GL_NO_ERROR)
    throw std::runtime_error(std::string(where) + " GL error " +
                             std::to_string(e));
}
struct GL {
  int w, h, s;
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface surface = EGL_NO_SURFACE;
  GLuint index_program = 0, fusion_program = 0, removal_program = 0,
         index_fbo = 0, output_fbo = 0;
  GLuint ids = 0, positions = 0, normals = 0, output = 0, depth_index = 0,
         depth_output = 0;
  GLuint vao = 0, vbo = 0, ebo = 0;
  std::string renderer, version;
  GL(int width = 192, int height = 144, int sample = 4)
      : w(width), h(height), s(sample) {
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor;
    require(eglInitialize(display, &major, &minor),
            "EGL initialization failed");
    require(eglBindAPI(EGL_OPENGL_ES_API), "EGL ES bind failed");
    const EGLint attrs[] = {EGL_SURFACE_TYPE,
                            EGL_PBUFFER_BIT,
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
    require(eglChooseConfig(display, attrs, &config, 1, &count) && count == 1,
            "EGL config failed");
    const EGLint pa[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE},
                 ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    surface = eglCreatePbufferSurface(display, config, pa);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
    require(context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE,
            "EGL context failed");
    require(eglMakeCurrent(display, surface, surface, context),
            "EGL make-current failed");
    renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    auto path = std::string(SOURCE_ROOT);
    auto old = path + "/source/original/PointsFusion/PointsFusion/";
    auto updated = path + "/updated/shaders/";
    index_program = program(old + "indexMap.vsh", old + "indexMap.fsh");
    fusion_program = program(updated + "Fusion.vsh", updated + "Fusion.fsh");
    removal_program =
        program(updated + "PointRemoval.vsh", updated + "PointRemoval.fsh");
    glGenFramebuffers(1, &index_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, index_fbo);
    ids = texture(GL_R32I, GL_COLOR_ATTACHMENT0, w * s, h * s);
    positions = texture(GL_RGBA32F, GL_COLOR_ATTACHMENT1, w * s, h * s);
    normals = texture(GL_RGBA32F, GL_COLOR_ATTACHMENT2, w * s, h * s);
    GLenum three[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                      GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, three);
    depth_index = depth(w * s, h * s);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "index FBO incomplete");
    glGenFramebuffers(1, &output_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
    output = texture(GL_RGBA32F, GL_COLOR_ATTACHMENT3, w, h);
    GLenum four[] = {GL_NONE, GL_NONE, GL_NONE, GL_COLOR_ATTACHMENT3};
    glDrawBuffers(4, four);
    depth_output = depth(w, h);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "fusion FBO incomplete");
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glcheck("setup");
  }
  GLuint shader(GLenum type, const std::string &path) {
    GLuint sh = glCreateShader(type);
    std::string code = read_text(path);
    auto str = code.c_str();
    glShaderSource(sh, 1, &str, nullptr);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[4096];
      glGetShaderInfoLog(sh, 4096, nullptr, log);
      throw std::runtime_error(path + "\n" + log);
    }
    return sh;
  }
  GLuint program(const std::string &v, const std::string &f) {
    auto vs = shader(GL_VERTEX_SHADER, v), fs = shader(GL_FRAGMENT_SHADER, f);
    auto p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "position");
    glBindAttribLocation(p, 1, "normal");
    glBindAttribLocation(p, 2, "radius");
    glBindAttribLocation(p, 3, "confidence");
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[4096];
      glGetProgramInfoLog(p, 4096, nullptr, log);
      throw std::runtime_error(v + " link\n" + log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
  }
  GLuint texture(GLenum format, GLenum attachment, int width, int height) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexStorage2D(GL_TEXTURE_2D, 1, format, width, height);
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, t, 0);
    return t;
  }
  GLuint depth(int width, int height) {
    GLuint d;
    glGenRenderbuffers(1, &d);
    glBindRenderbuffer(GL_RENDERBUFFER, d);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, d);
    return d;
  }
  void uniforms(GLuint p, M4 view) {
    M4 proj = perspective(float(w) / h), mvp = proj * view;
    float n[9] = {view.m[0], view.m[1], view.m[2], view.m[4], view.m[5],
                  view.m[6], view.m[8], view.m[9], view.m[10]};
    glUseProgram(p);
    glUniformMatrix4fv(glGetUniformLocation(p, "modelViewProjectionMatrix"), 1,
                       GL_FALSE, mvp.m.data());
    glUniformMatrix4fv(glGetUniformLocation(p, "modelViewMatrix"), 1, GL_FALSE,
                       view.m.data());
    glUniformMatrix3fv(glGetUniformLocation(p, "normalMatrix"), 1, GL_FALSE, n);
    glUniform1i(glGetUniformLocation(p, "idTexture"), 0);
    glUniform1i(glGetUniformLocation(p, "positionTexture"), 1);
    glUniform1i(glGetUniformLocation(p, "normalTexture"), 2);
    glUniform1i(glGetUniformLocation(p, "superSampleFactor"), s);
    glUniform1f(glGetUniformLocation(p, "xfactor"), w);
    glUniform1f(glGetUniformLocation(p, "yfactor"), h);
    glUniform1f(glGetUniformLocation(p, "maxDegree"), 20 * 3.14159265f / 180);
    glUniform1f(glGetUniformLocation(p, "maxDistance"), .055f);
  }
  void upload(const std::vector<Point> &points,
              const std::vector<uint32_t> &indices) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(Point), points.data(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
                 indices.data(), GL_DYNAMIC_DRAW);
    const int offsets[] = {0, 3, 6, 10};
    const int sizes[] = {3, 3, 1, 1};
    for (int i = 0; i < 4; i++) {
      glEnableVertexAttribArray(i);
      glVertexAttribPointer(
          i, sizes[i], GL_FLOAT, GL_FALSE, sizeof(Point),
          reinterpret_cast<void *>(offsets[i] * sizeof(float)));
    }
  }
  void index(const Model &model, M4 view) {
    glBindFramebuffer(GL_FRAMEBUFFER, index_fbo);
    glViewport(0, 0, w * s, h * s);
    GLint zero[4] = {};
    float clear[4] = {};
    glClearBufferiv(GL_COLOR, 0, zero);
    glClearBufferfv(GL_COLOR, 1, clear);
    glClearBufferfv(GL_COLOR, 2, clear);
    glClear(GL_DEPTH_BUFFER_BIT);
    uniforms(index_program, view);
    std::vector<uint32_t> active;
    for (auto id : model.active)
      if (id)
        active.push_back(id);
    upload(model.points, active);
    glDrawElements(GL_POINTS, active.size(), GL_UNSIGNED_INT, nullptr);
    glcheck("index map");
  }
  std::vector<Hit> draw(GLuint p, const std::vector<Point> &points,
                        const std::vector<uint32_t> &active, M4 view) {
    glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
    glViewport(0, 0, w, h);
    float zero[4] = {};
    glClearBufferfv(GL_COLOR, 3, zero);
    glClear(GL_DEPTH_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ids);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, positions);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, normals);
    uniforms(p, view);
    upload(points, active);
    glDrawElements(GL_POINTS, active.size(), GL_UNSIGNED_INT, nullptr);
    std::vector<Hit> result(w * h);
    glReadBuffer(GL_COLOR_ATTACHMENT3);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, result.data());
    glcheck("fusion/removal readback");
    return result;
  }
  std::vector<Hit> fuse(const std::vector<Point> &points, M4 view) {
    std::vector<uint32_t> ids;
    for (uint32_t i = 1; i < points.size(); i++)
      ids.push_back(i);
    return draw(fusion_program, points, ids, view);
  }
  struct Map {
    std::vector<int> id;
    std::vector<std::array<float, 4>> position, normal;
  };
  Map read_map() {
    Map m;
    m.id.resize(w * h * s * s);
    m.position.resize(m.id.size());
    m.normal.resize(m.id.size());
    glBindFramebuffer(GL_FRAMEBUFFER, index_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, w * s, h * s, GL_RED_INTEGER, GL_INT, m.id.data());
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glReadPixels(0, 0, w * s, h * s, GL_RGBA, GL_FLOAT, m.position.data());
    glReadBuffer(GL_COLOR_ATTACHMENT2);
    glReadPixels(0, 0, w * s, h * s, GL_RGBA, GL_FLOAT, m.normal.data());
    glcheck("map readback");
    return m;
  }
  ~GL() {
    if (display != EGL_NO_DISPLAY) {
      GLuint programs[] = {index_program, fusion_program, removal_program};
      for (auto p : programs)
        if (p)
          glDeleteProgram(p);
      GLuint ts[] = {ids, positions, normals, output};
      glDeleteTextures(4, ts);
      GLuint fs[] = {index_fbo, output_fbo};
      glDeleteFramebuffers(2, fs);
      GLuint ds[] = {depth_index, depth_output};
      glDeleteRenderbuffers(2, ds);
      glDeleteVertexArrays(1, &vao);
      glDeleteBuffers(1, &vbo);
      glDeleteBuffers(1, &ebo);
      eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      eglDestroyContext(display, context);
      eglDestroySurface(display, surface);
      eglTerminate(display);
    }
  }
};
