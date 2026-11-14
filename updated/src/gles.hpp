#pragma once
#include "core.hpp"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <OpenGLES/ES3/gl.h>
#define PF_INTEGER_FLOAT_STORAGE 1
#else
#define PF_DESKTOP_GL 1
#include <OpenGL/gl3.h>
#endif
#else
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#endif
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace pf {
inline std::string read_text(std::string path) {
  std::ifstream f(path);
  if (!f)
    throw std::runtime_error("cannot open shader: " + path);
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
#if defined(PF_INTEGER_FLOAT_STORAGE)
  static constexpr bool integer_float_storage = true;
#else
  static constexpr bool integer_float_storage = false;
#endif
  int w, h, s;
  M4 projection;
#if !defined(__APPLE__)
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface surface = EGL_NO_SURFACE;
#endif
  bool owns_context = false, delete_objects = true;
  GL(const GL &) = delete;
  GL &operator=(const GL &) = delete;
  void abandon_context() { delete_objects = false; }
  GLuint index_program = 0, fusion_program = 0, removal_program = 0,
         index_fbo = 0, output_fbo = 0;
  GLuint ids = 0, positions = 0, normals = 0, output = 0, depth_index = 0,
         depth_output = 0;
  GLuint vao = 0, vbo = 0, ebo = 0;
  std::string renderer, version;
  bool float_color_extension = false;
  GL(int width = 192, int height = 144, int sample = 4,
     const std::string &root = SOURCE_ROOT, bool external_context = false)
      : w(width), h(height), s(sample),
        projection(perspective(float(width) / height)) {
#if !defined(__APPLE__)
    if (!external_context) {
      owns_context = true;
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
    }
#else
    require(external_context,
            "Apple frontend must provide a current GL context");
#endif
    require(glGetString(GL_RENDERER) != nullptr, "no current GL context");
    GLint max_texture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
    require(
        w > 0 && h > 0 && s > 0 && s <= 4 && w <= max_texture / s &&
            h <= max_texture / s &&
            uint64_t(w) * uint64_t(h) * uint64_t(s) * uint64_t(s) <= 1228800,
        "fusion framebuffer dimensions exceed driver or bounded-memory limits");
    renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
#if defined(PF_DESKTOP_GL)
    glEnable(GL_PROGRAM_POINT_SIZE);
#else
    if (!integer_float_storage) {
      GLint count = 0;
      glGetIntegerv(GL_NUM_EXTENSIONS, &count);

      for (GLint i = 0; i < count; i++)
        float_color_extension |=
            std::string(reinterpret_cast<const char *>(
                glGetStringi(GL_EXTENSIONS, i))) == "GL_EXT_color_buffer_float";
      if (!float_color_extension)
        std::cerr << "EXT_color_buffer_float not advertised; checking RGBA32F "
                     "framebuffer support; integer-storage build is available"
                  << std::endl;
    }
#endif
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    auto path = root;
    auto old = path + "/source/original/PointsFusion/PointsFusion/";
    auto updated = path + "/updated/shaders/";
    index_program = program(old + "indexMap.vsh", old + "indexMap.fsh");
    fusion_program = program(updated + "Fusion.vsh", updated + "Fusion.fsh", true);
    removal_program =
        program(updated + "PointRemoval.vsh", updated + "PointRemoval.fsh");
    glGenFramebuffers(1, &index_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, index_fbo);
    ids = texture(GL_R32I, GL_COLOR_ATTACHMENT0, w * s, h * s);
    positions = texture(integer_float_storage ? GL_RGBA32UI : GL_RGBA32F,
                        GL_COLOR_ATTACHMENT1, w * s, h * s);
    normals = texture(integer_float_storage ? GL_RGBA32UI : GL_RGBA32F,
                      GL_COLOR_ATTACHMENT2, w * s, h * s);
    GLenum three[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                      GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, three);
    depth_index = depth(w * s, h * s);
    check_framebuffer("index", integer_float_storage
                                   ? "R32I/RGBA32UI/RGBA32UI/depth24"
                                   : "R32I/RGBA32F/RGBA32F/depth24");
    glGenFramebuffers(1, &output_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
    output = texture(integer_float_storage ? GL_RGBA32UI : GL_RGBA32F,
                     GL_COLOR_ATTACHMENT3, w, h);
    GLenum four[] = {GL_NONE, GL_NONE, GL_NONE, GL_COLOR_ATTACHMENT3};
    glDrawBuffers(4, four);
    depth_output = depth(w, h);
    check_framebuffer("fusion", integer_float_storage ? "RGBA32UI/depth24"
                                                      : "RGBA32F/depth24");
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glcheck("setup");
  }
  static void check_framebuffer(const char *name, const char *formats) {
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      std::ostringstream s;
      s << name << " FBO status 0x" << std::hex << status << " (" << formats
        << ")";
      throw std::runtime_error(s.str());
    }
  }
  GLuint shader(GLenum type, const std::string &path) {
    GLuint sh = glCreateShader(type);
    std::string code = read_text(path);
#if defined(PF_DESKTOP_GL)
    // macOS exposes desktop GL, not ES. Retain shader bodies and change only
    // the language-version declaration; desktop GLSL accepts precision tokens.
    auto version_pos = code.find("#version 300 es");
    if (version_pos != std::string::npos)
      code.replace(version_pos, 15, "#version 330 core");
#endif
    if (integer_float_storage && type == GL_FRAGMENT_SHADER &&
        (path.find("indexMap.fsh") != std::string::npos ||
         path.find("/updated/shaders/") != std::string::npos)) {
      // ES3 integer color attachments are mandatory; RGBA32F renderability is
      // not required by ES3; the first iPhone12 framebuffer setup rejected it.
      // Store exact IEEE32 bits, without half-float rounding.
      auto replace_all = [&](const std::string &from, const std::string &to) {
        size_t pos = 0;
        while ((pos = code.find(from, pos)) != std::string::npos) {
          code.replace(pos, from.size(), to);
          pos += to.size();
        }
      };
      bool index = path.find("indexMap.fsh") != std::string::npos;
      if (index) {
        replace_all("layout (location = 1) out highp vec4 positionTex;",
                    "highp vec4 positionTex; layout(location=1) out highp "
                    "uvec4 positionBits;");
        replace_all("layout (location = 2) out highp vec4 normalTex;",
                    "highp vec4 normalTex; layout(location=2) out highp uvec4 "
                    "normalBits;");
        replace_all("void main", "void pf_main");
        code += "\nvoid "
                "main(){pf_main();positionBits=floatBitsToUint(positionTex);"
                "normalBits=floatBitsToUint(normalTex); }\n";
      } else {
        replace_all("layout (location = 3) out highp vec4 fusion;",
                    "highp vec4 fusion; layout(location=3) out highp uvec4 "
                    "fusionBits;");
        replace_all("layout(location=3) out highp vec4 fusion;",
                    "highp vec4 fusion; layout(location=3) out highp uvec4 "
                    "fusionBits;");
        replace_all("uniform highp sampler2D", "uniform highp usampler2D");
        replace_all("texelFetch(positionTexture,", "pfPosition(");
        replace_all("texelFetch(normalTexture,", "pfNormal(");
        replace_all("void main", "void pf_main");
        const std::string helpers =
            "highp vec4 pfPosition(highp ivec2 p,int l){return "
            "uintBitsToFloat(texelFetch(positionTexture,p,l));}\n"
            "highp vec4 pfNormal(highp ivec2 p,int l){return "
            "uintBitsToFloat(texelFetch(normalTexture,p,l));}\n";
        auto main_pos = code.find("void pf_main");
        require(main_pos != std::string::npos,
                "packed shader main unavailable");
        code.insert(main_pos, helpers);
        code +=
            "\nvoid main(){pf_main();fusionBits=floatBitsToUint(fusion); }\n";
      }
    }
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
  GLuint program(const std::string &v, const std::string &f,
                 bool capture_fusion_query = false) {
    auto vs = shader(GL_VERTEX_SHADER, v), fs = shader(GL_FRAGMENT_SHADER, f);
    auto p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "position");
    glBindAttribLocation(p, 1, "normal");
    glBindAttribLocation(p, 2, "radius");
    glBindAttribLocation(p, 3, "confidence");
    if (capture_fusion_query) {
      const char *outputs[] = {"newCoords", "newNormal"};
      glTransformFeedbackVaryings(p, 2, outputs, GL_INTERLEAVED_ATTRIBS);
    }
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
#if defined(PF_DESKTOP_GL)
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0,
                 format == GL_R32I
                     ? GL_RED_INTEGER
                     : (format == GL_RGBA32UI ? GL_RGBA_INTEGER : GL_RGBA),
                 format == GL_R32I
                     ? GL_INT
                     : (format == GL_RGBA32UI ? GL_UNSIGNED_INT : GL_FLOAT),
                 nullptr);
#else
    glTexStorage2D(GL_TEXTURE_2D, 1, format, width, height);
#endif
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
    M4 mvp = projection * view;
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
    // One shared FP32 cutoff avoids unspecified GLSL cosine accuracy.
    glUniform1f(glGetUniformLocation(p, "minNormalDot"),
                fusion_normal_cutoff());
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
    const size_t offsets[] = {offsetof(Point, p), offsetof(Point, n),
                              offsetof(Point, radius),
                              offsetof(Point, confidence)};
    const int sizes[] = {3, 3, 1, 1};
    for (int i = 0; i < 4; i++) {
      glEnableVertexAttribArray(i);
      glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, sizeof(Point),
                            reinterpret_cast<void *>(offsets[i]));
    }
  }
  void index(const Model &model, M4 view) {
    glBindFramebuffer(GL_FRAMEBUFFER, index_fbo);
    glViewport(0, 0, w * s, h * s);
    GLint zero[4] = {};
    float clear[4] = {};
    glClearBufferiv(GL_COLOR, 0, zero);
    if (integer_float_storage) {
      GLuint bits[4] = {};
      glClearBufferuiv(GL_COLOR, 1, bits);
      glClearBufferuiv(GL_COLOR, 2, bits);
    } else {
      glClearBufferfv(GL_COLOR, 1, clear);
      glClearBufferfv(GL_COLOR, 2, clear);
    }
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
    if (integer_float_storage) {
      GLuint bits[4] = {};
      glClearBufferuiv(GL_COLOR, 3, bits);
    } else
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
    glReadPixels(0, 0, w, h, integer_float_storage ? GL_RGBA_INTEGER : GL_RGBA,
                 integer_float_storage ? GL_UNSIGNED_INT : GL_FLOAT,
                 result.data());
    glcheck("fusion/removal readback");
    return result;
  }
  std::vector<Hit> fuse(const std::vector<Point> &points, M4 view) {
    std::vector<uint32_t> ids;
    for (uint32_t i = 1; i < points.size(); i++)
      ids.push_back(i);
    return draw(fusion_program, points, ids, view);
  }
  // Reexecute one vertex using the linked Fusion program and read its output
  // varyings. The repeat draw is separately audited against FP64; it does not
  // prove bitwise identity to intermediates in the original fragment draw.
  std::array<float, 8> fusion_query(const std::vector<Point> &points,
                                   uint32_t id, M4 view) {
    require(id > 0 && id < points.size(), "query input ID outside buffer");
    GLuint buffer = 0, query = 0;
    glGenBuffers(1, &buffer);
    glGenQueries(1, &query);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer);
    std::array<float,8> sentinel;
    sentinel.fill(std::numeric_limits<float>::quiet_NaN());
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, 8 * sizeof(float), sentinel.data(),
                 GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer);
    uniforms(fusion_program, view);
    upload(points, {});
    GLenum setup_error = glGetError();
    if (setup_error != GL_NO_ERROR) {
      glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0);
      glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,0);
      glDeleteBuffers(1,&buffer); glDeleteQueries(1,&query);
      std::ostringstream errors;
      errors << setup_error;
      for (GLenum e=glGetError();e!=GL_NO_ERROR;e=glGetError()) errors << ' ' << e;
      throw std::runtime_error("Fusion query setup GL errors "+errors.str());
    }
    glEnable(GL_RASTERIZER_DISCARD);
    glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN,query);
    glBeginTransformFeedback(GL_POINTS);
    // ES3 forbids indexed draws during transform feedback. first=id keeps
    // gl_VertexID identical to the indexed fusion draw without repacking VBO.
    glDrawArrays(GL_POINTS, GLint(id), 1);
    glEndTransformFeedback();
    glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
    glDisable(GL_RASTERIZER_DISCARD);
    GLuint written=0;
    glGetQueryObjectuiv(query,GL_QUERY_RESULT,&written);
    auto *data = static_cast<const float *>(glMapBufferRange(
        GL_TRANSFORM_FEEDBACK_BUFFER, 0, 8 * sizeof(float), GL_MAP_READ_BIT));
    std::array<float, 8> result{};
    if (data)
      std::copy(data, data + 8, result.begin());
    bool mapped = data != nullptr;
    bool unmapped = mapped && glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);
    glDeleteBuffers(1, &buffer);
    glDeleteQueries(1,&query);
    // Drain errors before logical checks so reset never inherits a stale flag.
    std::ostringstream errors;
    for (GLenum e=glGetError();e!=GL_NO_ERROR;e=glGetError()) errors << ' ' << e;
    require(errors.str().empty(),
            ("Fusion query transform feedback GL errors"+errors.str()).c_str());
    require(written == 1,"Fusion query did not capture exactly one primitive");
    require(mapped && unmapped, "Fusion query transform feedback read failed");
    return result;
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
    {
      std::vector<std::array<int, 4>> id_rgba(m.id.size());
      glReadPixels(0, 0, w * s, h * s, GL_RGBA_INTEGER, GL_INT, id_rgba.data());
      for (size_t i = 0; i < m.id.size(); i++)
        m.id[i] = id_rgba[i][0];
    }
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glReadPixels(
        0, 0, w * s, h * s, integer_float_storage ? GL_RGBA_INTEGER : GL_RGBA,
        integer_float_storage ? GL_UNSIGNED_INT : GL_FLOAT, m.position.data());
    glReadBuffer(GL_COLOR_ATTACHMENT2);
    glReadPixels(
        0, 0, w * s, h * s, integer_float_storage ? GL_RGBA_INTEGER : GL_RGBA,
        integer_float_storage ? GL_UNSIGNED_INT : GL_FLOAT, m.normal.data());
    glcheck("map readback");
    return m;
  }
  ~GL() {
    if (!delete_objects)
      return;
    {
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
#if !defined(__APPLE__)
      if (owns_context && display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
      }
#endif
    }
  }
};

} // namespace pf
