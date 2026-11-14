// New 2026 audit of a rare GPU-transformed correspondence query.
// No tolerance is added to the shader or CPU distance/normal predicates.
#pragma once
#include "core.hpp"
#include <limits>
#include <string>
namespace pf {
// GLSL ES3.00 §4.5.1: basic highp operations are correctly rounded with
// unspecified rounding mode (<=1 ULP); inversesqrt <=2 ULP, division <=2.5 ULP.
// Use epsilon=2^-23, rather than round-to-nearest unit roundoff 2^-24.
// gamma(n) bounds n rounded operations, including allowed multiply-add fusion.
inline double fp32_gamma(unsigned n) {
  constexpr double epsilon = 0x1p-23;
  return n * epsilon / (1 - n * epsilon);
}
struct QueryAudit {
  bool valid = false;
  V3 point{}, normal{};
  double point_delta = 0, normal_delta = 0;
  double point_bound = 0, normal_bound = 0;
  std::string reason;
};
inline QueryAudit audit_fusion_query(const Point &input, M4 view,
                                    const std::array<float, 8> &gpu) {
  QueryAudit result;
  const float values[] = {input.p.x,input.p.y,input.p.z,input.n.x,input.n.y,input.n.z};
  for (float f : values) if (!std::isfinite(f)) {
    result.reason = "nonfinite CPU query input"; return result;
  }
  for (float f : view.m) if (!std::isfinite(f)) {
    result.reason = "nonfinite CPU view matrix"; return result;
  }
  for (float f : gpu)
    if (!std::isfinite(f)) {
      result.reason = "nonfinite GPU query";
      return result;
    }
  if (gpu[3] != 1 || gpu[7] != 1) {
    result.reason = "unexpected GPU homogeneous query component";
    return result;
  }
  double ref[3]{}, normal[3]{}, error[3]{};
  const double p[] = {input.p.x,input.p.y,input.p.z,1};
  const double n[] = {input.n.x,input.n.y,input.n.z};
  double normal_error_squared = 0, normal_length_squared = 0;
  for (int row = 0; row < 3; ++row) {
    double l1 = 0, normal_l1 = 0;
    for (int col = 0; col < 4; ++col) {
      double term = double(view.m[col*4+row]) * p[col];
      ref[row] += term; l1 += std::abs(term);
      if (col < 3) {
        term = double(view.m[col*4+row]) * n[col];
        normal[row] += term; normal_l1 += std::abs(term);
      }
    }
    // Four products and three additions, plus allowance for subnormal flush.
    error[row] = fp32_gamma(7) * l1 + 8 * double(std::numeric_limits<float>::min());
    result.point_bound = std::max(result.point_bound, error[row]);
    result.point_delta = std::max(result.point_delta, std::abs(double(gpu[row])-ref[row]));
    if (!std::isfinite(ref[row]) || std::abs(double(gpu[row])-ref[row]) > error[row]) {
      result.reason = "GPU camera point outside FP32 matrix-product bound";
      return result;
    }
    const double e = fp32_gamma(5) * normal_l1 + 8 * double(std::numeric_limits<float>::min());
    normal_error_squared += e*e;
    normal_length_squared += normal[row]*normal[row];
  }
  const double norm = std::sqrt(normal_length_squared), error_norm = std::sqrt(normal_error_squared);
  if (!std::isfinite(norm) || norm <= error_norm || norm < 1e-12) {
    result.reason = "degenerate double-reference normal";
    return result;
  }
  // Normalizing a perturbed vector changes it by <=2*||error||/(||n||-||error||).
  // gamma(32) additionally covers 3 squares+2 sums, sqrt via inversesqrt/divide
  // (2 and 2.5 ULP), and component division/multiplication, conservatively.
  result.normal_bound = 2 * error_norm / (norm-error_norm) + fp32_gamma(32);
  double measured_length_squared = 0;
  for (int row = 0; row < 3; ++row) {
    const double delta = double(gpu[row+4])-normal[row]/norm;
    result.normal_delta += delta*delta;
    measured_length_squared += double(gpu[row+4])*gpu[row+4];
  }
  result.normal_delta = std::sqrt(result.normal_delta);
  if (result.normal_delta > result.normal_bound ||
      std::abs(std::sqrt(measured_length_squared)-1) > result.normal_bound) {
    result.reason = "GPU normal outside FP32 matrix/normalization bound";
    return result;
  }
  result.point = {gpu[0],gpu[1],gpu[2]};
  result.normal = {gpu[4],gpu[5],gpu[6]};
  result.valid = true;
  return result;
}
struct CorrespondenceAudit {
  size_t query_rechecks = 0;
  double max_point_delta = 0, max_normal_delta = 0;
};
} // namespace pf
