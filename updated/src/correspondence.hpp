#pragma once
#include "fixture.hpp"
#include "gles.hpp"
namespace pf {
inline int cpu_match(GL &gl, const GL::Map &map,
                     const std::vector<Point> &input, const Hit &hit, int pixel,
                     M4 view) {
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
inline void verify_correspondences(GL &gl, const GL::Map &map,
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

} // namespace pf
