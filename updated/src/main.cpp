#include "correspondence.hpp"
#include "fixture.hpp"
#include "gles.hpp"
#include <filesystem>
#include <iomanip>
#include <limits>
#include <numeric>
using namespace pf;
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
