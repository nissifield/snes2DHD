#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace snes2dhd::scene {

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  bool operator==(const Vec3&) const = default;
};

struct Plane {
  int layer = 0;
  float depth = 0.0f;
  float tilt_deg = 0.0f;
  bool stock_2d = false;

  bool operator==(const Plane&) const = default;
};

struct Billboard {
  std::uint8_t oam_index = 0;
  float x = 0.0f;
  float y = 0.0f;
  float depth = 0.0f;
  float width = 8.0f;
  float height = 8.0f;
  bool voxel = false;

  bool operator==(const Billboard&) const = default;
};

struct Camera {
  float fov_deg = 0.0f;
  float pitch_deg = 0.0f;
  float distance = 1.0f;
  bool orthographic = true;

  bool operator==(const Camera&) const = default;
};

struct Lighting {
  float ambient = 1.0f;
  Vec3 direction{0.0f, 0.0f, -1.0f};
  float bloom = 0.0f;
  float dof = 0.0f;

  bool operator==(const Lighting&) const = default;
};

struct Scene {
  bool is_stock_2d = true;
  std::uint16_t width = 256;
  std::uint16_t height = 224;
  std::string active_scene_id;
  Camera camera;
  Lighting lighting;
  std::vector<Plane> planes;
  std::vector<Billboard> billboards;

  bool operator==(const Scene&) const = default;
};

}  // namespace snes2dhd::scene
