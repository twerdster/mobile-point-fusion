// Updated 2026 portable adapter for the recovered 2015 Daniel & Gal experiment.
// CPU weights/averages follow ViewController.m. Original notices remain in
// source/.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace pf {
struct V3 {
  float x = 0, y = 0, z = 0;
  V3 operator+(V3 b) const { return {x + b.x, y + b.y, z + b.z}; }
  V3 operator-(V3 b) const { return {x - b.x, y - b.y, z - b.z}; }
  V3 operator*(float s) const { return {x * s, y * s, z * s}; }
  V3 operator/(float s) const { return *this * (1 / s); }
};
// Shared FP32 value: strict 20-degree normal gate, evaluated once per process.
// GLSL cosine precision is unspecified, so Fusion receives this as a uniform.
inline float fusion_normal_cutoff() {
  static const float value = std::cos(20 * 3.14159265f / 180);
  return value;
}
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 unit(V3 a) {
  float n = length(a);
  return n > 0 ? a / n : V3{};
}
struct M4 {
  std::array<float, 16> m{};
  static M4 identity() {
    M4 a;
    for (int i = 0; i < 4; i++)
      a.m[i * 5] = 1;
    return a;
  }
};
inline M4 operator*(M4 a, M4 b) {
  M4 c;
  for (int j = 0; j < 4; j++)
    for (int i = 0; i < 4; i++)
      for (int k = 0; k < 4; k++)
        c.m[j * 4 + i] += a.m[k * 4 + i] * b.m[j * 4 + k];
  return c;
}
inline V3 transform(M4 a, V3 p, bool normal = false) {
  return {a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + (normal ? 0 : a.m[12]),
          a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + (normal ? 0 : a.m[13]),
          a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + (normal ? 0 : a.m[14])};
}
inline M4 perspective(float aspect) {
  M4 p;
  float f = 1 / std::tan(50.f * 3.14159265f / 360);
  p.m[0] = f / aspect;
  p.m[5] = f;
  p.m[10] = -(20 + .1f) / (20 - .1f);
  p.m[11] = -1;
  p.m[14] = -2 * 20 * .1f / (20 - .1f);
  return p;
}
inline M4 look(V3 eye, V3 target = {}) {
  V3 z = unit(eye - target), x = unit(cross({0, 1, 0}, z)), y = cross(z, x);
  M4 m = M4::identity();
  m.m[0] = x.x;
  m.m[4] = x.y;
  m.m[8] = x.z;
  m.m[1] = y.x;
  m.m[5] = y.y;
  m.m[9] = y.z;
  m.m[2] = z.x;
  m.m[6] = z.y;
  m.m[10] = z.z;
  m.m[12] = -dot(x, eye);
  m.m[13] = -dot(y, eye);
  m.m[14] = -dot(z, eye);
  return m;
}
struct Point {
  V3 p, n;
  float radius = .012f;
  V3 color{.2f, .7f, .9f};
  float confidence = 0, time = 0;
};
static_assert(sizeof(Point) == 12 * sizeof(float),
              "historical 12-float record layout");
struct Hit {
  float model = 0, input = 0, u = 0, v = 0;
};
static_assert(sizeof(Hit) == 16 && std::is_trivially_copyable_v<Hit>,
              "RGBA readback layout");
inline void require(bool yes, const char *message) {
  if (!yes)
    throw std::runtime_error(message);
}
struct Model {
  std::vector<Point> points{Point{}};
  std::vector<uint32_t> active{0}, free;
  size_t inserted = 0, merged = 0, removed = 0, reused = 0;
  uint32_t insert(Point p) {
    require(points.size() < 16777216, "float ID storage capacity exceeded");
    uint32_t id;
    if (free.empty()) {
      id = points.size();
      points.push_back(p);
      active.push_back(id);
    } else {
      id = free.back();
      free.pop_back();
      require(!active.at(id), "free slot already active");
      points[id] = p;
      active[id] = id;
      reused++;
    }
    inserted++;
    return id;
  }
  bool erase(uint32_t id) {
    if (id == 0 || id >= active.size() || !active[id])
      return false;
    active[id] = 0;
    free.push_back(id);
    removed++;
    return true;
  }
  size_t size() const {
    return std::count_if(active.begin(), active.end(),
                         [](uint32_t x) { return x != 0; });
  }
  std::vector<uint32_t> apply(const std::vector<Hit> &hits,
                              const std::vector<Point> &input, float time) {
    std::vector<uint32_t> stable;
    for (auto h : hits) {
      if (h.model == 0)
        continue;
      require(std::isfinite(h.model) && std::isfinite(h.input),
              "nonfinite shader ID");
      require(h.model >= -1 && h.model < 16777216 && h.input > 0 &&
                  h.input < 16777216,
              "shader ID outside representable range");
      int id = int(h.model), in = int(h.input);
      require(float(id) == h.model && float(in) == h.input,
              "noninteger shader ID");
      require(in > 0 && size_t(in) < input.size(), "input ID outside buffer");
      float alpha = std::exp(-(h.u * h.u + h.v * h.v) / (2 * .6f * .6f));
      require(std::isfinite(alpha) && alpha > 0, "invalid confidence weight");
      if (id == -1) {
        Point p = input[in];
        p.confidence = alpha;
        p.time = time;
        insert(p);
      } else {
        require(id > 0 && size_t(id) < points.size() && active[id],
                "model ID outside active buffer");
        Point &p = points[id];
        float old = p.confidence, weight = old + alpha;
        require(weight > 0, "nonpositive fusion weight");
        p.p = (p.p * old + input[in].p * alpha) / weight;
        p.n = (p.n * old + input[in].n * alpha) / weight;
        p.confidence = weight;
        p.time = time;
        merged++;
        if (old > 10)
          stable.push_back(id);
      }
    }
    std::sort(stable.begin(), stable.end());
    stable.erase(std::unique(stable.begin(), stable.end()), stable.end());
    return stable;
  }
  void remove_records(const std::vector<Hit> &records) {
    for (auto h : records) {
      require(std::isfinite(h.model) && h.model >= 0 && h.model <= 3 &&
                  h.model == std::floor(h.model),
              "removal count exceeds RGBA payload");
      int n = int(h.model);
      require(n >= 0 && n <= 3, "removal count exceeds RGBA payload");
      float ids[3] = {h.input, h.u, h.v};
      for (int k = 0; k < n; k++) {
        require(ids[k] > 0 && std::isfinite(ids[k]) && ids[k] < points.size() &&
                    ids[k] == std::floor(ids[k]),
                "invalid removal ID");
        erase(uint32_t(ids[k]));
      }
    }
  }
  void expire(float time) {
    for (uint32_t i = 1; i < points.size(); i++)
      if (active[i] && time - points[i].time > 6 && points[i].confidence < 10)
        erase(i);
  }
};

} // namespace pf
