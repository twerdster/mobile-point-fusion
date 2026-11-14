#pragma once
#include "core.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
namespace pf {
namespace fs = std::filesystem;
inline constexpr float pi = 3.14159265358979323846f;
inline std::vector<Point> generated() {
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
inline void write_obj(const fs::path &path, const std::vector<Point> &pts) {
  std::ofstream f(path);
  require(bool(f), "cannot open point output for writing");
  f << "# Updated offline OBJ fixture; provenance in metrics.json; arbitrary "
       "model units.\n"
    << std::setprecision(9);
  for (size_t i = 1; i < pts.size(); i++)
    f << "v " << pts[i].p.x << ' ' << pts[i].p.y << ' ' << pts[i].p.z << ' '
      << pts[i].color.x << ' ' << pts[i].color.y << ' ' << pts[i].color.z
      << '\n';
  for (size_t i = 1; i < pts.size(); i++)
    f << "vn " << pts[i].n.x << ' ' << pts[i].n.y << ' ' << pts[i].n.z << '\n';
  f.flush();
  require(bool(f), "cannot write OBJ output");
}
inline std::vector<Point> read_obj(const fs::path &path) {
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
inline void ply(const fs::path &path, const std::vector<Point> &pts,
                const std::vector<uint32_t> &active,
                const std::string &description =
                    "generated input, known poses, not a mesh") {
  std::ofstream f(path);
  require(bool(f), "cannot open point output for writing");
  size_t n = std::count_if(active.begin(), active.end(),
                           [](uint32_t x) { return x != 0; });
  f << "ply\nformat ascii 1.0\ncomment Actual updated point-fusion output; "
    << description << "\nelement vertex " << n
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
  f.flush();
  require(bool(f), "cannot write PLY output");
}
inline std::vector<uint32_t> all_ids(const std::vector<Point> &p) {
  std::vector<uint32_t> ids(p.size() - 1);
  std::iota(ids.begin(), ids.end(), 1);
  return ids;
}
inline std::vector<Point> observation(const std::vector<Point> &truth, M4 view,
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

} // namespace pf
