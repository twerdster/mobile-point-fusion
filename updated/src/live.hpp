// New native frontend state for the updated adapter. Generated input and known
// poses are deliberate fixtures; this is not a camera or tracking pipeline.
#pragma once
#include "correspondence.hpp"
#include <iostream>

namespace pf {
struct LiveFusion {
  GL gl;
  Model model;
  std::vector<Point> truth = generated(), input{Point{}};
  size_t compared = 0, total_input = 0;
  int frame = 0;
  static constexpr int frames = 48;
  GLuint display_program = 0, display_vao = 0, display_vbo = 0;
  std::string output_path;
  bool delete_display_objects = true;
  LiveFusion(const LiveFusion &) = delete;
  LiveFusion &operator=(const LiveFusion &) = delete;
  void abandon_context() {
    delete_display_objects = false;
    gl.abandon_context();
  }
  LiveFusion(const std::string &root, const std::string &output)
      : gl(192, 144, 4, root, true), output_path(output) {
    std::cout << "native renderer=" << gl.renderer << " version=" << gl.version
              << std::endl;
    const char *vertex =
#if defined(PF_DESKTOP_GL)
        "#version 330 core\n"
#else
        "#version 300 es\n"
#endif
        "precision highp float;\n"
        "layout(location=0) in vec3 position;\n"
        "layout(location=1) in vec3 normal;\n"
        "layout(location=2) in vec3 color;\n"
        "uniform mat4 mvp; uniform mat3 normalMatrix; uniform float "
        "pointSize;\n"
        "out vec3 lit; void main(){gl_Position=mvp*vec4(position,1.0);"
        "gl_PointSize=pointSize; vec3 n=normalize(normalMatrix*normal);"
        "lit=color*(0.45+0.55*max(0.0,dot(n,normalize(vec3(-.3,.5,1.0)))));}\n";
    const char *fragment =
#if defined(PF_DESKTOP_GL)
        "#version 330 core\n"
#else
        "#version 300 es\n"
#endif
        "precision highp float; in vec3 lit; out vec4 color;"
        "void main(){if(length(gl_PointCoord-vec2(.5))>.5)discard;"
        "color=vec4(lit,1.0);}\n";
    auto compile = [](GLenum type, const char *text) {
      GLuint s = glCreateShader(type);
      glShaderSource(s, 1, &text, nullptr);
      glCompileShader(s);
      GLint ok = 0;
      glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
      if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        throw std::runtime_error(log);
      }
      return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, vertex),
           fs = compile(GL_FRAGMENT_SHADER, fragment);
    display_program = glCreateProgram();
    glAttachShader(display_program, vs);
    glAttachShader(display_program, fs);
    glLinkProgram(display_program);
    GLint linked = 0;
    glGetProgramiv(display_program, GL_LINK_STATUS, &linked);
    require(linked, "point display program link failed");
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGenVertexArrays(1, &display_vao);
    glGenBuffers(1, &display_vbo);
    fs::create_directories(output_path);
    write_obj(fs::path(output_path) / "generated-input.obj", truth);
    write_state();
  }
  ~LiveFusion() {
    if (!delete_display_objects)
      return;
    glDeleteProgram(display_program);
    glDeleteVertexArrays(1, &display_vao);
    glDeleteBuffers(1, &display_vbo);
  }
  void reset() {
    model = Model{};
    input = {Point{}};
    compared = total_input = 0;
    frame = 0;
    write_state();
  }
  float angle() const { return 2 * pi * std::max(0, frame - 1) / (frames - 1); }
  void step() {
    if (frame >= frames)
      return;
    float a = 2 * pi * frame / (frames - 1);
    M4 view = look({3.1f * std::sin(a), .35f, 3.1f * std::cos(a)});
    input = observation(truth, view, frame, gl.w, gl.h);
    gl.index(model, view);
    auto map = gl.read_map();
    auto hits = gl.fuse(input, view);
    verify_correspondences(gl, map, input, hits, view, compared);
    auto stable = model.apply(hits, input, frame / 6.f);
    if (!stable.empty()) {
      gl.index(model, view);
      model.remove_records(
          gl.draw(gl.removal_program, model.points, stable, view));
    }
    if ((frame + 1) % 25 == 0)
      model.expire(frame / 6.f);
    total_input += input.size() - 1;
    frame++;
    write_state();
    if (frame == frames) {
      ply(fs::path(output_path) / "fused.ply", model.points, model.active);
      std::cout << "NATIVE_FUSION_COMPLETE frames=" << frame
                << " points=" << model.size() << " merged=" << model.merged
                << " compared=" << compared << std::endl;
    }
  }
  void write_state() const {
    std::ofstream f(fs::path(output_path) / "native-state.json");
    require(bool(f), "cannot open native-state.json for writing");
    f << "{\n\"frame\":" << frame
      << ",\n\"frames\":48,\n\"input\":\"generated ellipsoid fixture; known "
         "analytic poses; no camera or tracking\",\n"
      << "\"renderer\":" << std::quoted(gl.renderer)
      << ",\n\"gl_version\":" << std::quoted(gl.version) << ",\n"
      << "\"framebuffer_storage\":\""
      << (GL::integer_float_storage ? "RGBA32UI_float_bits" : "RGBA32F")
      << "\",\n"
      << "\"CPU_check_scope\":\"fusion-pass selection from GPU index map; "
         "index rasterization and removal outputs not independently checked in "
         "native runs\",\n"
      << "\"model_points\":" << model.size()
      << ",\n\"merged_total\":" << model.merged
      << ",\n\"inserted_total\":" << model.inserted
      << ",\n\"removed_total\":" << model.removed
      << ",\n\"reused_total\":" << model.reused
      << ",\n\"visible_input_points\":" << total_input
      << ",\n\"shader_CPU_correspondences_compared\":" << compared
      << ",\n\"operators\":\"indexMap shader -> adapted Fusion shader -> CPU "
         "averages -> adapted removal shader/CPU\",\n"
      << "\"pose_estimation\":false,\n\"real_RGBD\":false\n}\n";
    f.flush();
    require(bool(f), "cannot write native-state.json");
  }
  void render(GLuint framebuffer, int width, int height,
              float orbit_offset = 0) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
#if defined(PF_DESKTOP_GL)
    glEnable(GL_PROGRAM_POINT_SIZE);
#endif
    glClearColor(.025f, .045f, .08f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(display_program);
    glBindVertexArray(display_vao);
    float a = angle() + orbit_offset;
    M4 view = look({2.5f * std::sin(a), .3f, 2.5f * std::cos(a)});
    int panel_width = width / 2;
    M4 mvp = perspective(float(panel_width) / height) * view;
    float n[9] = {view.m[0], view.m[1], view.m[2], view.m[4], view.m[5],
                  view.m[6], view.m[8], view.m[9], view.m[10]};
    glUniformMatrix4fv(glGetUniformLocation(display_program, "mvp"), 1,
                       GL_FALSE, mvp.m.data());
    glUniformMatrix3fv(glGetUniformLocation(display_program, "normalMatrix"), 1,
                       GL_FALSE, n);
    glUniform1f(glGetUniformLocation(display_program, "pointSize"),
                std::max(2.f, width / 450.f));
    auto draw_points = [&](const std::vector<Point> &points,
                           const std::vector<uint32_t> &ids, int x) {
      std::vector<Point> visible;
      visible.reserve(ids.size());
      for (auto id : ids)
        if (id)
          visible.push_back(points.at(id));
      glViewport(x, 0, panel_width, height);
      glBindBuffer(GL_ARRAY_BUFFER, display_vbo);
      glBufferData(GL_ARRAY_BUFFER, visible.size() * sizeof(Point),
                   visible.data(), GL_DYNAMIC_DRAW);
      const size_t offsets[] = {offsetof(Point, p), offsetof(Point, n),
                                offsetof(Point, color)};
      for (int i = 0; i < 3; i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 3, GL_FLOAT, GL_FALSE, sizeof(Point),
                              reinterpret_cast<void *>(offsets[i]));
      }
      glDrawArrays(GL_POINTS, 0, visible.size());
    };
    draw_points(input, all_ids(input), 0);
    draw_points(model.points, model.active, panel_width);
    glViewport(0, 0, width, height);
    glcheck("native display");
  }
};

} // namespace pf
