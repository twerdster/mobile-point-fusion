// New 2026 input adapter. Recorded depths and supplied poses are not tracking.
#pragma once
#include "fixture.hpp"
#include <cstring>
#include <limits>
#include <memory>

namespace pf {
inline M4 rigid_inverse(M4 pose) {
  M4 inverse = M4::identity();
  for (int row = 0; row < 3; row++)
    for (int column = 0; column < 3; column++)
      inverse.m[column * 4 + row] = pose.m[row * 4 + column];
  V3 translation{pose.m[12], pose.m[13], pose.m[14]};
  V3 offset = transform(inverse, translation, true) * -1;
  inverse.m[12] = offset.x;
  inverse.m[13] = offset.y;
  inverse.m[14] = offset.z;
  return inverse;
}
inline M4 calibrated_projection(int width, int height, float fx, float fy,
                                float cx, float cy) {
  M4 projection = perspective(float(width) / height);
  projection.m[0] = 2 * fx / width;
  projection.m[5] = 2 * fy / height;
  // Input centres have integer coordinates. GL window centres are i + 0.5.
  projection.m[8] = 1 - 2 * (cx + .5f) / width;
  projection.m[9] = 2 * (cy + .5f) / height - 1;
  return projection;
}
struct InputFrame {
  std::vector<Point> points{Point{}};
  M4 view = M4::identity();
  double timestamp = 0;
  size_t valid_depth_pixels = 0, rejected_normal_pixels = 0,
         rejected_range_pixels = 0;
};
class Recording {
  std::ifstream stream;
  std::vector<M4> poses;
  std::vector<double> timestamps;
  uint64_t frame_bytes = 0;
  uint64_t integer(unsigned bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; i++) {
      int byte = stream.get();
      require(byte != std::char_traits<char>::eof(), "truncated RGB-D pack");
      value |= uint64_t(uint8_t(byte)) << (8 * i);
    }
    return value;
  }
  float real32() {
    uint32_t bits = uint32_t(integer(4));
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    require(std::isfinite(value), "nonfinite RGB-D header or pose");
    return value;
  }
  double real64() {
    uint64_t bits = integer(8);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    require(std::isfinite(value), "nonfinite RGB-D timestamp");
    return value;
  }
  static void validate_pose(M4 pose) {
    require(std::abs(pose.m[3]) < 1e-5f && std::abs(pose.m[7]) < 1e-5f &&
                std::abs(pose.m[11]) < 1e-5f &&
                std::abs(pose.m[15] - 1) < 1e-5f,
            "RGB-D pose must have homogeneous last row 0,0,0,1");
    V3 columns[3] = {{pose.m[0], pose.m[1], pose.m[2]},
                     {pose.m[4], pose.m[5], pose.m[6]},
                     {pose.m[8], pose.m[9], pose.m[10]}};
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
        require(std::abs(dot(columns[i], columns[j]) - (i == j ? 1.f : 0.f)) <
                    1e-3f,
                "RGB-D pose rotation must be orthonormal");
    require(dot(columns[0], cross(columns[1], columns[2])) > .999f,
            "RGB-D pose rotation must preserve handedness");
  }

public:
  int width = 0, height = 0, frames = 0;
  float depth_units_per_metre = 0, fx = 0, fy = 0, cx = 0, cy = 0;
  static constexpr float normal_depth_jump_metres = .05f;
  static constexpr float min_depth_metres = .5f, max_depth_metres = 3.5f;
  explicit Recording(const fs::path &path) : stream(path, std::ios::binary) {
    static_assert(sizeof(float) == 4 && sizeof(double) == 8 &&
                      std::numeric_limits<float>::is_iec559 &&
                      std::numeric_limits<double>::is_iec559,
                  "RGB-D pack needs IEEE754 float32/64");
    require(bool(stream), "cannot open RGB-D recording");
    char magic[8];
    stream.read(magic, 8);
    require(bool(stream) && std::memcmp(magic, "PFRGBD1\0", 8) == 0,
            "invalid RGB-D pack magic/version");
    uint64_t w = integer(4), h = integer(4), count = integer(4);
    require(w >= 3 && h >= 3 && w <= 640 && h <= 480 && w * h <= 76800 &&
                count >= 1 && count <= 5000,
            "RGB-D dimensions/count outside supported limits");
    width = int(w);
    height = int(h);
    frames = int(count);
    depth_units_per_metre = real32();
    fx = real32();
    fy = real32();
    cx = real32();
    cy = real32();
    require(depth_units_per_metre > 0 && depth_units_per_metre <= 1e6f &&
                fx > 0 && fy > 0 && fx <= 1e6f && fy <= 1e6f && cx >= 0 &&
                cy >= 0 && cx < width && cy < height,
            "invalid RGB-D calibration/depth scale");
    frame_bytes = 72 + w * h * 5;
    require(fs::file_size(path) == 40 + count * frame_bytes,
            "RGB-D pack length does not match declared frames");
    poses.reserve(frames);
    timestamps.reserve(frames);
    for (int i = 0; i < frames; i++) {
      stream.seekg(std::streamoff(40 + uint64_t(i) * frame_bytes));
      double stamp = real64();
      require(i == 0 || stamp > timestamps.back(),
              "RGB-D timestamps must increase");
      timestamps.push_back(stamp);
      M4 pose;
      for (float &value : pose.m)
        value = real32();
      validate_pose(pose);
      poses.push_back(pose);
    }
  }
  double first_timestamp() const { return timestamps.front(); }
  M4 first_pose() const { return poses.front(); }
  M4 projection() const {
    return calibrated_projection(width, height, fx, fy, cx, cy);
  }
  InputFrame read(int index) {
    require(index >= 0 && index < frames, "RGB-D frame index out of range");
    stream.clear();
    stream.seekg(std::streamoff(40 + uint64_t(index) * frame_bytes + 72));
    const size_t pixels = size_t(width) * height;
    std::vector<V3> camera(pixels);
    std::vector<float> depths(pixels);
    InputFrame result;
    result.timestamp = timestamps.at(index);
    result.view = rigid_inverse(poses.at(index));
    for (int y = 0; y < height; y++)
      for (int x = 0; x < width; x++) {
        size_t pixel = size_t(y) * width + x;
        float z = float(integer(2)) / depth_units_per_metre;
        require(std::isfinite(z), "RGB-D depth overflows metric range");
        if (z > 0 && (z < min_depth_metres || z > max_depth_metres)) {
          result.rejected_range_pixels++;
          z = 0;
        }
        depths[pixel] = z;
        if (z > 0) {
          camera[pixel] = {(x - cx) * z / fx, -(y - cy) * z / fy, -z};
          require(std::isfinite(camera[pixel].x) &&
                      std::isfinite(camera[pixel].y),
                  "RGB-D unprojection overflows metric range");
          result.valid_depth_pixels++;
        }
      }
    std::vector<uint8_t> colors(pixels * 3);
    stream.read(reinterpret_cast<char *>(colors.data()), colors.size());
    require(bool(stream), "truncated RGB-D color data");
    result.points.reserve(pixels + 1);
    for (int y = 0; y < height; y++)
      for (int x = 0; x < width; x++) {
        size_t pixel = size_t(y) * width + x;
        if (depths[pixel] == 0)
          continue;
        if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
          result.rejected_normal_pixels++;
          continue;
        }
        const size_t neighbors[] = {pixel - 1, pixel + 1, pixel - width,
                                    pixel + width};
        bool valid = true;
        for (size_t neighbor : neighbors)
          valid &= depths[neighbor] > 0 &&
                   std::abs(depths[neighbor] - depths[pixel]) <=
                       normal_depth_jump_metres;
        if (!valid) {
          result.rejected_normal_pixels++;
          continue;
        }
        V3 normal = unit(cross(camera[pixel + width] - camera[pixel - width],
                               camera[pixel + 1] - camera[pixel - 1]));
        if (!std::isfinite(normal.x) || !std::isfinite(normal.y) ||
            !std::isfinite(normal.z) || length(normal) < .5f) {
          result.rejected_normal_pixels++;
          continue;
        }
        if (dot(normal, camera[pixel]) > 0)
          normal = normal * -1;
        Point point;
        point.p = transform(poses[index], camera[pixel]);
        point.n = unit(transform(poses[index], normal, true));
        require(std::isfinite(point.p.x) && std::isfinite(point.p.y) &&
                    std::isfinite(point.p.z),
                "RGB-D world transform overflows metric range");
        point.color = {colors[pixel * 3] / 255.f, colors[pixel * 3 + 1] / 255.f,
                       colors[pixel * 3 + 2] / 255.f};
        result.points.push_back(point);
      }
    return result;
  }
};
struct InputSequence {
  std::unique_ptr<Recording> recording;
  std::vector<Point> truth;
  int width = 192, height = 144, frames = 48;
  explicit InputSequence(const std::string &path = "") {
    if (path.empty())
      truth = generated();
    else {
      recording = std::make_unique<Recording>(path);
      width = recording->width;
      height = recording->height;
      frames = recording->frames;
    }
  }
  bool real() const { return bool(recording); }
  std::string description() const {
    return real() ? "recorded RGB-D pack; supplied camera poses; metres; no "
                    "pose estimation"
                  : "generated ellipsoid fixture; known analytic poses; no "
                    "camera or tracking";
  }
  InputFrame read(int index) {
    if (recording)
      return recording->read(index);
    require(index >= 0 && index < frames, "generated frame index out of range");
    InputFrame result;
    float angle = 2 * pi * index / (frames - 1);
    result.view = look({3.1f * std::sin(angle), .35f, 3.1f * std::cos(angle)});
    result.points = observation(truth, result.view, index, width, height);
    result.timestamp = index / 6.f;
    return result;
  }
};
} // namespace pf
