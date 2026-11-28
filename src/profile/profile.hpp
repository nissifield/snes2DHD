#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace snes2dhd::profile {

inline constexpr std::uint32_t ProfileSchemaVersion = 1;
inline constexpr std::size_t MaxProfileBytes = 1024 * 1024;

enum class RenderMode { Stock2D, Flat, Voxel };

struct LayerRule {
  int layer = 0;  // 0..3 for BG1..BG4
  RenderMode mode = RenderMode::Stock2D;
  float depth = 0.0f;
  float tilt_deg = 0.0f;

  bool operator==(const LayerRule&) const = default;
};

struct SpriteRule {
  std::string tile_hash;
  RenderMode mode = RenderMode::Flat;
  float depth_bias = 0.0f;
  float height_scale = 1.0f;

  bool operator==(const SpriteRule&) const = default;
};

struct CameraPreset {
  float fov_deg = 35.0f;
  float pitch_deg = 25.0f;
  float distance = 10.0f;

  bool operator==(const CameraPreset&) const = default;
};

struct LightPreset {
  float ambient = 0.65f;
  float dir_x = 0.3f, dir_y = -0.8f, dir_z = 0.5f;
  float bloom = 0.0f;
  float dof = 0.0f;

  bool operator==(const LightPreset&) const = default;
};

struct SceneRule {
  std::string id;
  std::uint32_t wram_addr = 0;
  std::uint8_t wram_value = 0;
  bool has_selector = false;
  CameraPreset camera;
  LightPreset light;
  std::vector<LayerRule> layers;
  std::vector<SpriteRule> sprites;

  bool operator==(const SceneRule&) const = default;
};

struct Profile {
  std::uint32_t schema_version = ProfileSchemaVersion;
  std::string game_id;
  std::vector<std::string> rom_sha256;
  std::vector<SceneRule> scenes;

  bool matches_rom(std::string_view sha256) const;
  bool operator==(const Profile&) const = default;
};

struct LoadResult {
  std::optional<Profile> profile;
  std::vector<std::string> warnings;
  std::string error;
};

LoadResult parse_profile_json(std::string_view json_text);
LoadResult load_profile_file(const std::string& path);

}  // namespace snes2dhd::profile
