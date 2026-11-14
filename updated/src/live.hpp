// New native frontend state. Recorded input uses supplied poses, not tracking.
#pragma once
#include "correspondence.hpp"
#include "input_sequence.hpp"
#include <iostream>

namespace pf {
struct LiveFusion {
  InputSequence sequence;
  GL gl;
  Model model;
  std::vector<Point> input{Point{}};
  size_t compared = 0, total_input = 0, valid_depth = 0, rejected_normals = 0,
         rejected_range = 0;
  CorrespondenceAudit correspondence_audit;
  int frame = 0;
  int frames = 48;
  double source_timestamp = 0, elapsed_source_seconds = 0;
  V3 display_target{}, display_up{0, 1, 0}, display_forward{0, 0, -1};
  float display_radius = 1;
  bool display_bounds_ready = false;
  GLuint display_program = 0, display_vao = 0, display_vbo = 0;
  std::string output_path;
  bool delete_display_objects = true;
  LiveFusion(const LiveFusion &) = delete;
  LiveFusion &operator=(const LiveFusion &) = delete;
  void abandon_context() {
    delete_display_objects = false;
    gl.abandon_context();
  }
  static std::string recording_path(const std::string &root,
                                    const std::string &explicit_path,
                                    bool use_bundled = true) {
    if (!explicit_path.empty())
      return explicit_path;
    if (!use_bundled)
      return "";
    auto bundled = fs::path(root) / "recording.pfrgbd";
    return fs::exists(bundled) ? bundled.string() : "";
  }
  LiveFusion(const std::string &root, const std::string &output,
             const std::string &recording = "", bool external_context = true,
             bool use_bundled_recording = true)
      : sequence(recording_path(root, recording, use_bundled_recording)),
        gl(sequence.width, sequence.height, 4, root, external_context),
        frames(sequence.frames), output_path(output) {
    if (sequence.real()) {
      gl.projection = sequence.recording->projection();
      M4 first_pose = sequence.recording->first_pose();
      display_up = unit(transform(first_pose, {0, 1, 0}, true));
      display_forward = unit(transform(first_pose, {0, 0, -1}, true));
    }
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
    if (!sequence.real())
      write_obj(fs::path(output_path) / "generated-input.obj", sequence.truth);
    clear_diagnostics();
    write_state();
  }
  ~LiveFusion() {
    if (!delete_display_objects)
      return;
    glDeleteProgram(display_program);
    glDeleteVertexArrays(1, &display_vao);
    glDeleteBuffers(1, &display_vbo);
  }
  void clear_diagnostics() const {
    fs::remove(fs::path(output_path) / "correspondence-failure.txt");
    std::ofstream log(fs::path(output_path) / "correspondence-boundaries.txt");
    require(bool(log), "cannot reset correspondence boundary log");
  }
  void reset() {
    model = Model{};
    input = {Point{}};
    compared = total_input = valid_depth = rejected_normals = rejected_range =
        0;
    correspondence_audit = {};
    clear_diagnostics();
    frame = 0;
    source_timestamp = elapsed_source_seconds = 0;
    display_bounds_ready = false;
    write_state();
  }
  float angle() const {
    return frames > 1 ? 2 * pi * std::max(0, frame - 1) / (frames - 1) : 0;
  }
  void step() {
    if (frame >= frames)
      return;
    auto sample = sequence.read(frame);
    M4 view = sample.view;
    input = std::move(sample.points);
    source_timestamp = sample.timestamp;
    elapsed_source_seconds =
        sequence.real()
            ? source_timestamp - sequence.recording->first_timestamp()
            : source_timestamp;
    valid_depth += sample.valid_depth_pixels;
    rejected_normals += sample.rejected_normal_pixels;
    rejected_range += sample.rejected_range_pixels;
    if (sequence.real() && !display_bounds_ready && input.size() > 1) {
      V3 low = input[1].p, high = low;
      for (size_t i = 1; i < input.size(); i++) {
        const auto p = input[i].p;
        low = {std::min(low.x, p.x), std::min(low.y, p.y),
               std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y),
                std::max(high.z, p.z)};
      }
      display_target = (low + high) * .5f;
      display_radius = std::max(.1f, length(high - low) * .5f);
      display_bounds_ready = true;
    }
    gl.index(model, view);
    auto map = gl.read_map();
    auto hits = gl.fuse(input, view);
    try {
      std::ofstream boundary_log(fs::path(output_path) /
                                 "correspondence-boundaries.txt",std::ios::app);
      require(bool(boundary_log),"cannot open correspondence boundary log");
      verify_correspondences(gl, map, input, hits, view, compared, frame + 1,
                             &correspondence_audit, &boundary_log);
    } catch (const CorrespondenceMismatch &failure) {
      std::ofstream diagnostic(fs::path(output_path) /
                               "correspondence-failure.txt");
      diagnostic << std::setprecision(17)
                 << "source_timestamp=" << source_timestamp
                 << " elapsed_source_seconds=" << elapsed_source_seconds << '\n'
                 << failure.detail;
      diagnostic.flush();
      throw;
    }
    auto stable = model.apply(hits, input, float(elapsed_source_seconds));
    if (!stable.empty()) {
      gl.index(model, view);
      model.remove_records(
          gl.draw(gl.removal_program, model.points, stable, view));
    }
    if ((frame + 1) % 25 == 0)
      model.expire(float(elapsed_source_seconds));
    total_input += input.size() - 1;
    frame++;
    write_state();
    if (frame == frames) {
      ply(fs::path(output_path) / "fused.ply", model.points, model.active,
          sequence.real() ? "recorded RGB-D; supplied camera "
                            "poses; metres; no pose estimation; not a mesh"
                          : "generated input, known poses, not a mesh");
      std::cout << "NATIVE_FUSION_COMPLETE frames=" << frame
                << " points=" << model.size() << " merged=" << model.merged
                << " compared=" << compared << std::endl;
    }
  }
  void write_state() const {
    std::ofstream f(fs::path(output_path) / "native-state.json");
    require(bool(f), "cannot open native-state.json for writing");
    f << std::setprecision(17) << "{\n\"frame\":" << frame
      << ",\n\"frames\":" << frames
      << ",\n\"input\":" << std::quoted(sequence.description()) << ",\n"
      << "\"renderer\":" << std::quoted(gl.renderer)
      << ",\n\"gl_version\":" << std::quoted(gl.version) << ",\n"
      << "\"framebuffer_storage\":\""
      << (GL::integer_float_storage ? "RGBA32UI_float_bits" : "RGBA32F")
      << "\",\n"
      << "\"CPU_check_scope\":\"fusion-pass selection from GPU index map; "
         "rare mismatches require a bounded GPU-query repeat and exact CPU winner; "
         "index rasterization and removal outputs not independently checked in "
         "native runs\",\n"
      << "\"model_points\":" << model.size()
      << ",\n\"merged_total\":" << model.merged
      << ",\n\"inserted_total\":" << model.inserted
      << ",\n\"removed_total\":" << model.removed
      << ",\n\"reused_total\":" << model.reused
      << ",\n\"visible_input_points\":" << total_input
      << ",\n\"shader_CPU_correspondences_compared\":" << compared
      << ",\n\"strict_CPU_transform_matches\":" << compared-correspondence_audit.query_rechecks
      << ",\n\"GPU_query_rechecks_accepted\":" << correspondence_audit.query_rechecks
      << ",\n\"max_GPU_query_point_reference_delta\":" << correspondence_audit.max_point_delta
      << ",\n\"max_GPU_query_normal_reference_delta\":" << correspondence_audit.max_normal_delta
      << ",\n\"normal_cutoff_float32\":" << fusion_normal_cutoff()
      << ",\n\"operators\":\"indexMap shader -> adapted Fusion shader -> CPU "
         "averages -> adapted removal shader/CPU\",\n"
      << "\"pose_estimation\":false,\n\"real_RGBD\":"
      << (sequence.real() ? "true" : "false")
      << ",\n\"source_depth_timestamp\":" << source_timestamp
      << ",\n\"elapsed_source_seconds\":" << elapsed_source_seconds
      << ",\n\"valid_depth_pixels_total\":" << valid_depth
      << ",\n\"rejected_normal_pixels_total\":" << rejected_normals
      << ",\n\"rejected_range_pixels_total\":" << rejected_range
      << ",\n\"processing_width\":" << gl.w
      << ",\n\"processing_height\":" << gl.h
      << ",\n\"radius_metres\":" << (sequence.real() ? "0.012" : "null")
      << ",\n\"correspondence_gate_metres\":"
      << (sequence.real() ? "0.055" : "null")
      << ",\n\"normal_depth_jump_metres\":"
      << (sequence.real() ? "0.05" : "null")
      << ",\n\"depth_min_metres\":" << (sequence.real() ? "0.5" : "null")
      << ",\n\"depth_max_metres\":" << (sequence.real() ? "3.5" : "null")
      << "\n}\n";
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
    if (sequence.real() && display_bounds_ready) {
      // Display-only fitted orbit. Acquisition still uses calibrated K/poses.
      float aspect = float(panel_width) / height;
      float distance = display_radius / (.40f * std::min(1.f, aspect));
      V3 forward = display_forward * std::cos(orbit_offset) +
                   cross(display_up, display_forward) * std::sin(orbit_offset);
      V3 eye = display_target - forward * distance;
      V3 z = unit(eye - display_target), x = unit(cross(display_up, z)),
         y = cross(z, x);
      view = M4::identity();
      view.m[0] = x.x;
      view.m[4] = x.y;
      view.m[8] = x.z;
      view.m[1] = y.x;
      view.m[5] = y.y;
      view.m[9] = y.z;
      view.m[2] = z.x;
      view.m[6] = z.y;
      view.m[10] = z.z;
      view.m[12] = -dot(x, eye);
      view.m[13] = -dot(y, eye);
      view.m[14] = -dot(z, eye);
      M4 projection = perspective(aspect);
      float far = std::max(20.f, distance + display_radius * 3);
      projection.m[10] = -(far + .1f) / (far - .1f);
      projection.m[14] = -2 * far * .1f / (far - .1f);
      mvp = projection * view;
    }
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
