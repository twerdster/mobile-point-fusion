#include "gles.hpp"
#include <filesystem>
#include <iomanip>
#include <limits>
#include <numeric>
namespace fs = std::filesystem;
constexpr float pi = 3.14159265358979323846f;
static std::vector<std::string> checks;
void checked(const std::string &name, bool result) {
  require(result, name.c_str());
  checks.push_back(name);
}
void cpu_tests() {
  Model m;
  std::vector<Point> input(2);
  input[1].p = {3, 0, 0};
  input[1].n = {0, 1, 0};
  m.apply({{-1, 1, 0, 0}}, input, 0);
  checked("new sentinel inserts without reading model[-1]",
          m.size() == 1 && m.points[1].confidence == 1);
  m.points[1].p = {0, 0, 0};
  m.points[1].n = {1, 0, 0};
  m.points[1].confidence = 2;
  m.apply({{1, 1, 0, 0}}, input, 1);
  checked("Gaussian weighted position and normal averages",
          std::abs(m.points[1].p.x - 1) < 1e-6 &&
              std::abs(m.points[1].n.x - 2.f / 3) < 1e-6 &&
              std::abs(m.points[1].confidence - 3) < 1e-6);
  bool rejected = false;
  try {
    m.apply({{9, 1, 0, 0}}, input, 2);
  } catch (const std::exception &) {
    rejected = true;
  }
  checked("invalid model ID rejected", rejected);
  rejected = false;
  try {
    m.remove_records({{4, 1, 1, 1}});
  } catch (const std::exception &) {
    rejected = true;
  }
  checked("removal count beyond three is rejected", rejected);
  m.remove_records({{3, 1, 1, 1}});
  checked("duplicate removal creates one free slot",
          m.size() == 0 && m.free.size() == 1);
  m.apply({{-1, 1, .5f, 0}}, input, 2);
  checked("free slot reused and off-axis Gaussian applied",
          m.reused == 1 && m.points.size() == 2 &&
              std::abs(m.points[1].confidence - std::exp(-.25f / .72f)) < 1e-6);
  m.expire(8);
  checked("age boundary retains point at six seconds", m.size() == 1);
  m.expire(8.01f);
  checked("old low-confidence point removed", m.size() == 0);
  Point stable;
  stable.confidence = 10;
  stable.time = 0;
  m.insert(stable);
  m.expire(20);
  checked("stable point survives age removal", m.size() == 1);
  rejected = false;
  try {
    m.apply({{-1, 2, 0, 0}}, input, 0);
  } catch (const std::exception &) {
    rejected = true;
  }
  checked("invalid input ID rejected", rejected);
  rejected = false;
  try {
    m.remove_records({{std::numeric_limits<float>::quiet_NaN(), 1, 1, 1}});
  } catch (const std::exception &) {
    rejected = true;
  }
  checked("nonfinite removal count rejected before integer conversion",
          rejected);
}
int cpu_match(GL &gl, const GL::Map &map, const std::vector<Point> &input,
              const Hit &hit, int pixel, M4 view) {
  V3 p = transform(view, input.at(int(hit.input)).p),
     n = unit(transform(view, input.at(int(hit.input)).n, true));
  V3 ray = unit(p);
  float confidence = -1, dist = 100;
  int best = -1, x = pixel % gl.w, y = pixel / gl.w;
  for (int i = 0; i < gl.s; i++)
    for (int j = 0; j < gl.s; j++) {
      int xx = x * gl.s + i, yy = y * gl.s + j;
      if (xx >= gl.w * gl.s || yy >= gl.h * gl.s)
        continue;
      int k = yy * gl.w * gl.s + xx, id = map.id[k];
      if (id <= 0)
        continue;
      auto pos = map.position[k], norm = map.normal[k];
      V3 a{pos[0], pos[1], pos[2]}, b{norm[0], norm[1], norm[2]};
      float d = length(cross(ray, a));
      if (length(p - a) <= .055f && dot(n, b) > std::cos(20 * pi / 180) &&
          (pos[3] > confidence || (pos[3] == confidence && d < dist))) {
        confidence = pos[3];
        dist = d;
        best = id;
      }
    }
  return best;
}
void verify_correspondences(GL &gl, const GL::Map &map,
                            const std::vector<Point> &input,
                            const std::vector<Hit> &hits, M4 view,
                            size_t &compared) {
  for (size_t i = 0; i < hits.size(); i++)
    if (hits[i].model != 0) {
      require(int(hits[i].model) ==
                  cpu_match(gl, map, input, hits[i], int(i), view),
              "ES3/CPU correspondence disagreement");
      compared++;
    }
}
void gpu_tests(GL &gl) {
  auto old =
      std::string(SOURCE_ROOT) + "/source/original/PointsFusion/PointsFusion/";
  for (auto pair :
       std::vector<std::string>{"indexMap", "Fusion", "PointRemoval"}) {
    auto p = gl.program(old + pair + ".vsh", old + pair + ".fsh");
    glDeleteProgram(p);
    checked("original " + pair + " ES3 pair compiles and links", true);
  }
  Model m;
  Point p;
  p.p = {.01f, .01f, 0};
  p.n = {0, 0, 2};
  p.confidence = 2;
  m.insert(p);
  p.p.z = -.1f;
  p.confidence = 20;
  m.insert(p);
  M4 view = look({0, 0, 3});
  gl.index(m, view);
  auto map = gl.read_map();
  checked("index depth buffer retains nearest overlapping point",
          std::count(map.id.begin(), map.id.end(), 1) == 1 &&
              std::count(map.id.begin(), map.id.end(), 2) == 0);
  bool exact = true;
  for (size_t i = 0; i < map.id.size(); i++)
    if (map.id[i]) {
      V3 expected = transform(view, m.points[map.id[i]].p);
      auto a = map.position[i];
      exact &= length(expected - V3{a[0], a[1], a[2]}) < 1e-6f &&
               std::abs(a[3] - m.points[map.id[i]].confidence) < 1e-6f;
    }
  checked("index positions and confidence equal CPU transform", exact);
  bool normalized = true;
  for (size_t i = 0; i < map.id.size(); i++)
    if (map.id[i])
      normalized &= std::abs(map.normal[i][2] - 1) < 1e-6;
  checked("index shader normalizes stored weighted normals", normalized);
  p.p = {.035f, .01f, 0};
  p.confidence = 5;
  m.insert(p);
  gl.index(m, view);
  map = gl.read_map();
  std::vector<Point> input(2);
  input[1] = p;
  input[1].p = {.02f, .01f, 0};
  auto hits = gl.fuse(input, view);
  size_t compared = 0;
  verify_correspondences(gl, map, input, hits, view, compared);
  checked("ES3 candidates match CPU confidence/ray-distance selection",
          compared == 1 && std::count_if(hits.begin(), hits.end(), [](Hit h) {
                             return h.model == 3;
                           }) == 1);
  input[1].n = {0, 0, -1};
  hits = gl.fuse(input, view);
  checked("normal-angle rejection inserts a new point",
          std::count_if(hits.begin(), hits.end(),
                        [](Hit h) { return h.model == -1; }) == 1);
  input[1].n = {0, 0, 1};
  input[1].p.z = .15f;
  hits = gl.fuse(input, view);
  checked("Euclidean gate rejects another surface on same ray",
          std::count_if(hits.begin(), hits.end(),
                        [](Hit h) { return h.model == -1; }) == 1);
  Model neighbors;
  for (float y : {.01f, .035f})
    for (float x : {.01f, .035f}) {
      p.p = {x, y, -1};
      p.radius = .04f;
      neighbors.insert(p);
    }
  p.p = {.012f, .012f, -1};
  p.confidence = 6;
  uint32_t query = neighbors.insert(p);
  neighbors.active[query] = 0;
  gl.index(neighbors, view);
  auto removal = gl.draw(gl.removal_program, neighbors.points, {query}, view);
  auto it = std::find_if(removal.begin(), removal.end(),
                         [](Hit h) { return h.model > 0; });
  checked("overlap removal emits at most three IDs",
          it != removal.end() && it->model == 3);
  auto overlap_map = gl.read_map();
  std::vector<int> expected;
  int qpixel = int(std::distance(removal.begin(), it));
  int bx = (qpixel % gl.w) * gl.s, by = (qpixel / gl.w) * gl.s;
  for (int i = 0; i < gl.s; i++)
    for (int j = 0; j < gl.s; j++) {
      int k = (by + j) * gl.w * gl.s + bx + i;
      int id = overlap_map.id[k];
      if (id <= 0 || id == int(query))
        continue;
      auto a = overlap_map.position[k], n = overlap_map.normal[k];
      V3 cam = transform(view, neighbors.points[query].p);
      if (length(cam - V3{a[0], a[1], a[2]}) <
              neighbors.points[query].radius + n[3] &&
          std::find(expected.begin(), expected.end(), id) == expected.end() &&
          expected.size() < 3)
        expected.push_back(id);
    }
  checked("bounded overlap IDs match CPU neighborhood reference",
          expected.size() == 3 && it->input == expected[0] &&
              it->u == expected[1] && it->v == expected[2]);
  auto original =
      gl.program(old + "PointRemoval.vsh", old + "PointRemoval.fsh");
  auto broken = gl.draw(original, neighbors.points, {query}, view);
  glDeleteProgram(original);
  checked("original normalized-coordinate overlap fixture misses neighbors",
          std::all_of(broken.begin(), broken.end(),
                      [](Hit h) { return h.model == 0; }));
  size_t before = neighbors.size();
  neighbors.remove_records(removal);
  checked("bounded CPU removal consumes shader IDs once",
          neighbors.size() == before - 3);
  Model pair;
  p.p = {.01f, .01f, -1};
  p.confidence = 2;
  pair.insert(p);
  p.p = {.035f, .01f, -1};
  p.confidence = 1;
  pair.insert(p);
  auto removal_ids = [](const std::vector<Hit> &records, int id) {
    return std::any_of(records.begin(), records.end(),
                       [id](Hit h) { return h.model == 1 && h.input == id; });
  };
  gl.index(pair, view);
  auto strong = gl.draw(gl.removal_program, pair.points, {1}, view);
  auto weak = gl.draw(gl.removal_program, pair.points, {2}, view);
  bool ordered = removal_ids(strong, 2) && !removal_ids(weak, 1);
  pair.points[2].confidence = 2;
  gl.index(pair, view);
  strong = gl.draw(gl.removal_program, pair.points, {1}, view);
  weak = gl.draw(gl.removal_program, pair.points, {2}, view);
  checked("confidence then lower ID prevents mutual deletion",
          ordered && removal_ids(strong, 2) && !removal_ids(weak, 1));
  glcheck("all GPU tests");
}
std::vector<Point> generated() {
  std::vector<Point> points(1);
  auto ellipsoid = [&](V3 center, V3 radius, V3 color) {
    for (int j = 1; j < 32; j++)
      for (int i = 0; i < 64; i++) {
        float v = pi * j / 32, u = 2 * pi * i / 64;
        V3 q{std::sin(v) * std::cos(u), std::cos(v), std::sin(v) * std::sin(u)};
        Point p;
        p.p = center + V3{radius.x * q.x, radius.y * q.y, radius.z * q.z};
        p.n = unit({q.x / radius.x, q.y / radius.y, q.z / radius.z});
        p.color = color;
        points.push_back(p);
      }
  };
  ellipsoid({0, -.22f, 0}, {.32f, .43f, .25f}, {.1f, .65f, .9f});
  ellipsoid({0, .36f, 0}, {.43f, .39f, .32f}, {.2f, .8f, .95f});
  for (float sign : {-1.f, 1.f}) {
    ellipsoid({sign * .22f, .4f, .28f}, {.15f, .17f, .11f}, {.95f, .65f, .12f});
    ellipsoid({sign * .36f, -.18f, 0}, {.13f, .32f, .19f}, {.15f, .4f, .8f});
    ellipsoid({sign * .18f, -.61f, .08f}, {.16f, .08f, .19f},
              {.95f, .65f, .12f});
    ellipsoid({sign * .3f, .68f, -.02f}, {.095f, .18f, .095f}, {.3f, .7f, .9f});
  }
  ellipsoid({0, .25f, .35f}, {.07f, .11f, .12f}, {1, .4f, .1f});
  return points;
}
void write_obj(const fs::path &path, const std::vector<Point> &pts) {
  std::ofstream f(path);
  f << "# Updated offline OBJ fixture; provenance in metrics.json; arbitrary "
       "model units.\n"
    << std::setprecision(9);
  for (size_t i = 1; i < pts.size(); i++)
    f << "v " << pts[i].p.x << ' ' << pts[i].p.y << ' ' << pts[i].p.z << ' '
      << pts[i].color.x << ' ' << pts[i].color.y << ' ' << pts[i].color.z
      << '\n';
  for (size_t i = 1; i < pts.size(); i++)
    f << "vn " << pts[i].n.x << ' ' << pts[i].n.y << ' ' << pts[i].n.z << '\n';
}
std::vector<Point> read_obj(const fs::path &path) {
  std::ifstream f(path);
  require(bool(f), "cannot open OBJ");
  std::vector<Point> p(1);
  std::vector<V3> normals(1);
  std::string line, kind;
  while (std::getline(f, line)) {
    std::istringstream s(line);
    s >> kind;
    if (kind == "v") {
      Point q;
      require(bool(s >> q.p.x >> q.p.y >> q.p.z), "malformed OBJ vertex");
      V3 optional_color;
      if (s >> optional_color.x >> optional_color.y >> optional_color.z)
        q.color = optional_color;
      p.push_back(q);
    } else if (kind == "vn") {
      V3 n;
      require(bool(s >> n.x >> n.y >> n.z) && length(n) > 0,
              "malformed OBJ normal");
      normals.push_back(unit(n));
    }
  }
  require(p.size() > 1 && p.size() == normals.size(),
          "offline OBJ adapter requires one ordered vn per v");
  for (size_t i = 1; i < p.size(); i++)
    p[i].n = normals[i];
  return p;
}
void ply(const fs::path &path, const std::vector<Point> &pts,
         const std::vector<uint32_t> &active) {
  std::ofstream f(path);
  size_t n = std::count_if(active.begin(), active.end(),
                           [](uint32_t x) { return x != 0; });
  f << "ply\nformat ascii 1.0\ncomment Actual updated point-fusion output; "
       "generated input, known poses, not a mesh\nelement vertex "
    << n
    << "\nproperty float x\nproperty float y\nproperty float z\nproperty float "
       "nx\nproperty float ny\nproperty float nz\nproperty uchar red\nproperty "
       "uchar green\nproperty uchar blue\nproperty float "
       "confidence\nend_header\n"
    << std::setprecision(7);
  for (auto id : active)
    if (id) {
      auto p = pts.at(id);
      f << p.p.x << ' ' << p.p.y << ' ' << p.p.z << ' ' << p.n.x << ' ' << p.n.y
        << ' ' << p.n.z << ' ' << int(p.color.x * 255) << ' '
        << int(p.color.y * 255) << ' ' << int(p.color.z * 255) << ' '
        << p.confidence << '\n';
    }
}
std::vector<uint32_t> all_ids(const std::vector<Point> &p) {
  std::vector<uint32_t> ids(p.size() - 1);
  std::iota(ids.begin(), ids.end(), 1);
  return ids;
}
std::vector<Point> observation(const std::vector<Point> &truth, M4 view,
                               int frame, int w, int h) {
  auto proj = perspective(float(w) / h);
  std::vector<float> depth(w * h, 1e9f);
  std::vector<int> selected(w * h, 0);
  for (size_t i = 1; i < truth.size(); i++) {
    auto p = transform(view, truth[i].p), n = transform(view, truth[i].n, true);
    if (p.z >= -.1f || dot(p, n) >= 0)
      continue;
    int x = int(std::floor((proj.m[0] * p.x / -p.z + 1) * w / 2)),
        y = int(std::floor((proj.m[5] * p.y / -p.z + 1) * h / 2));
    if (x < 0 || x >= w || y < 0 || y >= h)
      continue;
    int k = y * w + x;
    if (-p.z < depth[k]) {
      depth[k] = -p.z;
      selected[k] = i;
    }
  }
  std::vector<Point> input(1);
  for (auto i : selected)
    if (i) {
      Point p = truth[i];
      float noise = .003f * std::sin(float(i) * 12.9898f + frame * 7.233f);
      p.p = p.p + p.n * noise;
      input.push_back(p);
    }
  return input;
}
void demo(GL &gl, const fs::path &out, int frames, const fs::path &obj) {
  fs::create_directories(out / "frames");
  auto truth = obj.empty() ? generated() : read_obj(obj);
  auto fixture_path =
      out / (obj.empty() ? "generated-input.obj" : "input-model.obj");
  write_obj(fixture_path, truth);
  truth = read_obj(fixture_path);
  ply(out / "reference.ply", truth, all_ids(truth));
  Model model;
  size_t references = 0, total_input = 0, total_removal = 0;
  std::ofstream json(out / "metrics.json");
  json << std::setprecision(9)
       << "{\n\"status\":\"executed\",\n\"generated_model\":"
       << (obj.empty() ? "true" : "false") << ",\n\"input\":\""
       << (obj.empty() ? "generated ellipsoid OBJ"
                       : "user-supplied ordered OBJ")
       << " points, visibility-selected multi-view observations with bounded "
          "0.003 model-unit normal noise\",\n\"poses\":\"supplied analytic "
          "orbit, not estimated\",\n\"renderer\":\""
       << gl.renderer << "\",\n\"gl_version\":\"" << gl.version
       << "\",\n\"operators\":\"original indexMap ES3 -> updated Fusion ES3 -> "
          "CPU Gaussian weighted averaging -> updated bounded overlap removal "
          "ES3/CPU -> CPU age expiry\",\n\"image_width\":"
       << gl.w << ",\n\"image_height\":" << gl.h
       << ",\n\"supersample_factor\":" << gl.s
       << ",\n\"reference_points\":" << truth.size() - 1 << ",\n\"frames\":[\n";
  for (int frame = 0; frame < frames; frame++) {
    float angle = 2 * pi * frame / (frames - 1);
    M4 view = look({3.1f * std::sin(angle), .35f, 3.1f * std::cos(angle)});
    auto input = observation(truth, view, frame, gl.w, gl.h);
    gl.index(model, view);
    auto map = gl.read_map();
    auto hits = gl.fuse(input, view);
    verify_correspondences(gl, map, input, hits, view, references);
    auto stable = model.apply(hits, input, frame / 6.f);
    size_t removed_before = model.removed;
    if (!stable.empty()) {
      gl.index(model, view);
      auto removal = gl.draw(gl.removal_program, model.points, stable, view);
      model.remove_records(removal);
    }
    if ((frame + 1) % 25 == 0)
      model.expire(frame / 6.f);
    total_removal += model.removed - removed_before;
    total_input += input.size() - 1;
    std::ostringstream prefix;
    prefix << std::setw(3) << std::setfill('0') << frame;
    ply(out / "frames" / (prefix.str() + "-input.ply"), input, all_ids(input));
    ply(out / "frames" / (prefix.str() + "-model.ply"), model.points,
        model.active);
    if (frame) {
      json << ",\n";
    }
    json << "{\"index\":" << frame << ",\"angle_degrees\":" << angle * 180 / pi
         << ",\"input_points\":" << input.size() - 1
         << ",\"model_points\":" << model.size()
         << ",\"inserted_total\":" << model.inserted
         << ",\"merged_total\":" << model.merged
         << ",\"removed_total\":" << model.removed
         << ",\"reused_total\":" << model.reused
         << ",\"shader_cpu_compared_total\":" << references << "}";
    std::cout << "frame=" << frame << " input=" << input.size() - 1
              << " model=" << model.size() << " merged=" << model.merged
              << " removed=" << model.removed << std::endl;
  }
  ply(out / "fused.ply", model.points, model.active);
  json
      << "\n],\n\"total_input_points\":" << total_input
      << ",\n\"final_model_points\":" << model.size()
      << ",\n\"inserted_total\":" << model.inserted
      << ",\n\"merged_total\":" << model.merged
      << ",\n\"removed_total\":" << total_removal
      << ",\n\"reused_total\":" << model.reused
      << ",\n\"shader_cpu_correspondences_compared\":" << references
      << ",\n\"surface_accuracy_pass_threshold\":null,\n\"limitations\":"
         "\"generated point-model observations, no real RGB-D or camera, known "
         "poses, no tracking, no iOS runtime, no performance claim\"\n}\n";
  require(model.merged > 0 && model.inserted > 0 && references > 0 &&
              model.size() > 100,
          "demo did not exercise insertion and fusion");
}
int main(int argc, char **argv) {
  try {
    bool cpu = false, gpu = false, run = false;
    int frames = 48;
    fs::path out = "mobile-fusion-output", obj;
    for (int i = 1; i < argc; i++) {
      std::string a = argv[i];
      if (a == "--cpu-test")
        cpu = true;
      else if (a == "--gpu-test")
        gpu = true;
      else if (a == "--demo")
        run = true;
      else if (a == "--output" && i + 1 < argc)
        out = argv[++i];
      else if (a == "--frames" && i + 1 < argc)
        frames = std::stoi(argv[++i]);
      else if (a == "--obj" && i + 1 < argc)
        obj = argv[++i];
      else
        throw std::runtime_error(
            "usage: points_fusion --cpu-test | --gpu-test | --demo [--frames "
            "N] [--obj ordered-v-vn.obj] [--output DIR]");
    }
    require(frames >= 2 && frames <= 300, "frames must be 2..300");
    if (cpu)
      cpu_tests();
    if (gpu || run) {
      GL gl(gpu ? 64 : 192, gpu ? 48 : 144);
      std::cout << "renderer=" << gl.renderer << "\nversion=" << gl.version
                << std::endl;
      if (gpu)
        gpu_tests(gl);
      if (run)
        demo(gl, out, frames, obj);
    }
    if (!cpu && !gpu && !run)
      throw std::runtime_error("no action requested");
    for (auto &c : checks)
      std::cout << "PASS " << c << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
