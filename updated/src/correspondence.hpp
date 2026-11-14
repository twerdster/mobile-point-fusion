#pragma once
#include "fixture.hpp"
#include "gles.hpp"
#include "precision.hpp"
namespace pf {
struct CorrespondenceMismatch : std::runtime_error {
  std::string detail;
  CorrespondenceMismatch(const std::string &summary,
                         const std::string &diagnostic)
      : std::runtime_error(summary), detail(diagnostic) {}
};
inline int cpu_match_query(GL &gl, const GL::Map &map, V3 p, V3 n, int pixel) {
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
      if (length(p - a) <= .055f && dot(n, b) > fusion_normal_cutoff() &&
          (pos[3] > confidence || (pos[3] == confidence && d < dist))) {
        confidence = pos[3];
        dist = d;
        best = id;
      }
    }
  return best;
}
inline int cpu_match(GL &gl, const GL::Map &map,
                     const std::vector<Point> &input, const Hit &hit, int pixel,
                     M4 view) {
  return cpu_match_query(gl, map, transform(view,input.at(int(hit.input)).p),
                        unit(transform(view,input.at(int(hit.input)).n,true)),pixel);
}
inline void verify_correspondences(GL &gl, const GL::Map &map,
                                   const std::vector<Point> &input,
                                   const std::vector<Hit> &hits, M4 view,
                                   size_t &compared, int frame = -1,
                                   CorrespondenceAudit *audit = nullptr,
                                   std::ostream *boundary_log = nullptr) {
  for (size_t i = 0; i < hits.size(); i++)
    if (hits[i].model != 0) {
      require(std::isfinite(hits[i].model) && std::isfinite(hits[i].input) &&
                  hits[i].model >= -1 && hits[i].model < 16777216 &&
                  hits[i].input > 0 && hits[i].input < input.size() &&
                  float(int(hits[i].input)) == hits[i].input &&
                  float(int(hits[i].model)) == hits[i].model,
              "invalid correspondence readback IDs");
      int expected = cpu_match(gl, map, input, hits[i], int(i), view);
      if (int(hits[i].model) != expected) {
        std::ostringstream summary, diagnostic;
        summary << "GPU/CPU correspondence mismatch: frame " << frame
                << ", pixel " << i << ", GPU " << int(hits[i].model) << ", CPU "
                << expected;
        V3 point = transform(view, input.at(int(hits[i].input)).p);
        V3 normal = unit(transform(view, input.at(int(hits[i].input)).n, true));
        auto xyz = [](std::ostream &s, V3 v) {
          s << v.x << ',' << v.y << ',' << v.z;
        };
        diagnostic << std::setprecision(17) << summary.str() << '\n'
                   << "input_id=" << hits[i].input << " image_xy=" << i % gl.w
                   << ',' << i / gl.w << " camera_point=";
        xyz(diagnostic, point);
        diagnostic << " camera_normal=";
        xyz(diagnostic, normal);
        diagnostic << " max_distance=" << .055f
                   << " min_normal_dot=" << fusion_normal_cutoff() << '\n';
        for (int dx = 0; dx < gl.s; dx++)
          for (int dy = 0; dy < gl.s; dy++) {
            int x = int(i % gl.w) * gl.s + dx, y = int(i / gl.w) * gl.s + dy;
            int k = y * gl.w * gl.s + x;
            if (x >= gl.w * gl.s || y >= gl.h * gl.s || map.id[k] <= 0)
              continue;
            auto p = map.position[k], n = map.normal[k];
            V3 candidate{p[0], p[1], p[2]}, candidate_normal{n[0], n[1], n[2]};
            diagnostic << "candidate_id=" << map.id[k] << " index_xy=" << x
                       << ',' << y << " confidence=" << p[3] << " position=";
            xyz(diagnostic, candidate);
            diagnostic << " normal=";
            xyz(diagnostic, candidate_normal);
            diagnostic << " euclidean_distance=" << length(point - candidate)
                       << " normal_dot=" << dot(normal, candidate_normal)
                       << " ray_distance="
                       << length(cross(unit(point), candidate)) << " cpu_gate="
                       << (length(point - candidate) <= .055f &&
                           dot(normal, candidate_normal) >
                               fusion_normal_cutoff())
                       << '\n';
          }
        QueryAudit query;
        try {
          auto measured = gl.fusion_query(input, uint32_t(hits[i].input), view);
          query = audit_fusion_query(input.at(int(hits[i].input)),view,measured);
        } catch (const std::exception &failure) {
          diagnostic << "GPU query readback failed: " << failure.what() << '\n';
          std::cerr << diagnostic.str() << std::flush;
          throw CorrespondenceMismatch(summary.str(),diagnostic.str());
        }
        diagnostic << "GPU query valid=" << query.valid
                   << " point_reference_max_delta=" << query.point_delta
                   << " point_reference_bound=" << query.point_bound
                   << " normal_reference_L2_delta=" << query.normal_delta
                   << " normal_reference_L2_bound=" << query.normal_bound
                   << " reason=" << query.reason << '\n';
        bool accepted = false;
        if (query.valid) {
          int from_query = cpu_match_query(gl,map,query.point,query.normal,int(i));
          diagnostic << "measured_GPU_camera_point=";
          xyz(diagnostic,query.point);
          diagnostic << " measured_GPU_camera_normal=";
          xyz(diagnostic,query.normal);
          diagnostic << " measured_query_CPU_ID=" << from_query << '\n';
          accepted = from_query == int(hits[i].model);
          if (!accepted)
            diagnostic << "Exact measured-query CPU winner still disagrees; failing\n";
        }
        if (accepted) {
          if (audit) {
            ++audit->query_rechecks;
            audit->max_point_delta=std::max(audit->max_point_delta,query.point_delta);
            audit->max_normal_delta=std::max(audit->max_normal_delta,query.normal_delta);
          }
          if (boundary_log) {
            *boundary_log << diagnostic.str() << "ACCEPTED_BOUNDED_GPU_QUERY_EXACT_WINNER\n";
            boundary_log->flush();
            require(bool(*boundary_log),"cannot write correspondence boundary log");
          } else std::cerr << diagnostic.str() << "ACCEPTED_BOUNDED_GPU_QUERY_EXACT_WINNER\n";
        } else {
          std::cerr << diagnostic.str() << std::flush;
          throw CorrespondenceMismatch(summary.str(), diagnostic.str());
        }
      }
      compared++;
    }
}

} // namespace pf
