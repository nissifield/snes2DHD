// Synthetic HD-2D profile loader, schema validator, and ROM matching tests.

#include <profile/profile.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int failures = 0;

auto check(const char* label, bool cond) -> void {
  if(cond) return;
  std::fprintf(stderr, "profile: %s failed\n", label);
  failures++;
}

auto approx(float a, float b) -> bool {
  return std::fabs(a - b) < 1e-4f;
}

const char* kHashLower = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char* kHashUpper = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
const char* kHashAlt   = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";

}  // namespace

auto main() -> int {
  using namespace snes2dhd::profile;

  // 1. Valid multi-scene profile parsing with escapes, unicode, defaults, and SHA-256 normalization.
  {
    const std::string json = R"({
      "schema_version": 1,
      "game_id": "synthetic-demo\u002d01",
      "rom_sha256": [
        ")" + std::string(kHashUpper) + R"(",
        ")" + std::string(kHashAlt) + R"("
      ],
      "scenes": [
        {
          "id": "overworld",
          "wram_addr": 4660,
          "wram_value": 7,
          "camera": { "fov_deg": 42.5, "pitch_deg": 30.0, "distance": 14.5 },
          "light": { "ambient": 0.45, "dir_x": -0.2, "dir_y": -0.9, "dir_z": 0.4, "bloom": 0.35, "dof": 0.25 },
          "layers": [
            { "layer": 0, "mode": "flat", "depth": 0.0, "tilt_deg": 15.0 },
            { "layer": 2, "mode": "voxel", "depth": -3.5, "tilt_deg": 0.0 }
          ],
          "sprites": [
            { "tile_hash": "a1b2c3d4e5f60708", "mode": "voxel", "depth_bias": 0.25, "height_scale": 1.75 }
          ]
        },
        {
          "id": "fallback-default",
          "layers": [ { "layer": 1 } ],
          "sprites": [ { "tile_hash": "0011223344556677" } ]
        }
      ]
    })";

    const auto res = parse_profile_json(json);
    check("valid parse ok", res.profile.has_value());
    check("valid no error", res.error.empty());
    check("valid no warnings", res.warnings.empty());
    if(res.profile) {
      const Profile& p = *res.profile;
      check("schema_version == 1", p.schema_version == 1);
      check("game_id unicode escape", p.game_id == "synthetic-demo-01");
      check("rom_sha256 count", p.rom_sha256.size() == 2);
      check("rom_sha256 normalized lowercase", p.rom_sha256[0] == kHashLower);
      check("matches_rom lowercase", p.matches_rom(kHashLower));
      check("matches_rom uppercase", p.matches_rom(kHashUpper));
      check("matches_rom alt hash", p.matches_rom(kHashAlt));
      check("matches_rom non-matching", !p.matches_rom(std::string(64, 'a')));
      check("matches_rom invalid short", !p.matches_rom("0123"));
      check("matches_rom invalid hex", !p.matches_rom(std::string(63, '0') + "g"));

      check("scenes count == 2", p.scenes.size() == 2);
      const auto& s0 = p.scenes[0];
      check("scene0 id", s0.id == "overworld");
      check("scene0 has_selector", s0.has_selector);
      check("scene0 wram_addr", s0.wram_addr == 4660);
      check("scene0 wram_value", s0.wram_value == 7);
      check("scene0 fov", approx(s0.camera.fov_deg, 42.5f));
      check("scene0 pitch", approx(s0.camera.pitch_deg, 30.0f));
      check("scene0 distance", approx(s0.camera.distance, 14.5f));
      check("scene0 ambient", approx(s0.light.ambient, 0.45f));
      check("scene0 bloom", approx(s0.light.bloom, 0.35f));
      check("scene0 dof", approx(s0.light.dof, 0.25f));
      check("scene0 layers count", s0.layers.size() == 2);
      check("scene0 layer0", s0.layers[0].layer == 0 && s0.layers[0].mode == RenderMode::Flat && approx(s0.layers[0].tilt_deg, 15.0f));
      check("scene0 layer2", s0.layers[1].layer == 2 && s0.layers[1].mode == RenderMode::Voxel && approx(s0.layers[1].depth, -3.5f));
      check("scene0 sprites count", s0.sprites.size() == 1);
      check("scene0 sprite mode", s0.sprites[0].mode == RenderMode::Voxel && approx(s0.sprites[0].height_scale, 1.75f));

      const auto& s1 = p.scenes[1];
      check("scene1 no selector", !s1.has_selector);
      check("scene1 default camera", approx(s1.camera.fov_deg, 35.0f) && approx(s1.camera.distance, 10.0f));
      check("scene1 default light", approx(s1.light.ambient, 0.65f));
      check("scene1 layer1 default mode Stock2D", s1.layers[0].mode == RenderMode::Stock2D);
      check("scene1 sprite default mode Flat", s1.sprites[0].mode == RenderMode::Flat);
    }
  }

  // 2. Partial/unknown keys and invalid per-element modes fall back safely to Stock2D with warnings.
  {
    const std::string json = std::string(R"({
      "schema_version": 1,
      "game_id": "synthetic-fallback",
      "rom_sha256": [")") + kHashLower + R"("],
      "extra_root_key": "ignored",
      "scenes": [
        {
          "id": "warn-scene",
          "future_flag": true,
          "camera": { "fov_deg": 50.0, "shutter": 180 },
          "light": { "ambient": 0.5, "fog": 0.1 },
          "layers": [
            { "layer": 0, "mode": "raytraced", "extra_layer": 1 },
            { "layer": 3, "mode": 42 }
          ],
          "sprites": [
            { "tile_hash": "deadbeef", "mode": "hologram", "extra_sprite": "x" }
          ]
        }
      ]
    })";

    const auto res = parse_profile_json(json);
    check("fallback parse ok", res.profile.has_value());
    check("fallback no error", res.error.empty());
    check("fallback warnings recorded", res.warnings.size() >= 7);
    if(res.profile) {
      const auto& s = res.profile->scenes[0];
      check("layer invalid string mode -> Stock2D", s.layers[0].mode == RenderMode::Stock2D);
      check("layer invalid type mode -> Stock2D", s.layers[1].mode == RenderMode::Stock2D);
      check("sprite invalid mode -> Stock2D", s.sprites[0].mode == RenderMode::Stock2D);
    }
  }

  // 3. Trust-boundary rejections (schema, hashes, bounds, malformed JSON, trailing data, depth).
  auto expect_rejected = [](const char* label, std::string_view json) {
    const auto res = parse_profile_json(json);
    check(label, !res.profile.has_value() && !res.error.empty());
  };

  const std::string valid_prefix = std::string(R"({"schema_version":1,"game_id":"g","rom_sha256":[")") + kHashLower + R"("],)";

  expect_rejected("reject empty string", "");
  expect_rejected("reject malformed JSON", "{bad json");
  expect_rejected("reject trailing garbage", valid_prefix + R"("scenes":[]} trailing)");
  expect_rejected("reject non-object root array", "[]");
  expect_rejected("reject non-object root string", "\"hello\"");
  expect_rejected("reject unsupported schema_version", std::string(R"({"schema_version":2,"game_id":"g","rom_sha256":[")") + kHashLower + R"("]})");
  expect_rejected("reject float schema_version", std::string(R"({"schema_version":1.5,"game_id":"g","rom_sha256":[")") + kHashLower + R"("]})");
  expect_rejected("reject empty game_id", std::string(R"({"schema_version":1,"game_id":"  ","rom_sha256":[")") + kHashLower + R"("]})");
  expect_rejected("reject empty rom_sha256", R"({"schema_version":1,"game_id":"g","rom_sha256":[]})");
  expect_rejected("reject short rom_sha256", R"({"schema_version":1,"game_id":"g","rom_sha256":["0123"]})");
  expect_rejected("reject non-hex rom_sha256", std::string(R"({"schema_version":1,"game_id":"g","rom_sha256":[")") + std::string(63, '0') + R"(z"]})");
  expect_rejected("reject layer < 0", valid_prefix + R"("scenes":[{"id":"s","layers":[{"layer":-1}]}]})");
  expect_rejected("reject layer > 3", valid_prefix + R"("scenes":[{"id":"s","layers":[{"layer":4}]}]})");
  expect_rejected("reject fov_deg < 5", valid_prefix + R"("scenes":[{"id":"s","camera":{"fov_deg":4.9}}]})");
  expect_rejected("reject fov_deg > 120", valid_prefix + R"("scenes":[{"id":"s","camera":{"fov_deg":120.1}}]})");
  expect_rejected("reject distance <= 0", valid_prefix + R"("scenes":[{"id":"s","camera":{"distance":0.0}}]})");
  expect_rejected("reject ambient > 1", valid_prefix + R"("scenes":[{"id":"s","light":{"ambient":1.1}}]})");
  expect_rejected("reject float overflow inf", valid_prefix + R"("scenes":[{"id":"s","layers":[{"layer":0,"depth":1e40}]}]})");
  expect_rejected("reject wram_addr >= 0x20000", valid_prefix + R"("scenes":[{"id":"s","wram_addr":131072,"wram_value":1}]})");
  expect_rejected("reject wram_value > 255", valid_prefix + R"("scenes":[{"id":"s","wram_addr":0,"wram_value":256}]})");
  expect_rejected("reject partial wram selector", valid_prefix + R"("scenes":[{"id":"s","wram_addr":16}]})");
  expect_rejected("reject height_scale <= 0", valid_prefix + R"("scenes":[{"id":"s","sprites":[{"tile_hash":"aa","height_scale":0.0}]}]})");
  expect_rejected("reject empty tile_hash", valid_prefix + R"("scenes":[{"id":"s","sprites":[{"tile_hash":""}]}]})");
  expect_rejected("reject deep JSON nesting", std::string(80, '[') + std::string(80, ']'));

  // 4. >1 MiB cap in parse_profile_json.
  {
    std::string huge = valid_prefix + R"("scenes":[]})";
    huge.append(MaxProfileBytes + 1 - huge.size(), ' ');
    expect_rejected("reject >1 MiB in-memory JSON", huge);
  }

  // 5. File loading: missing, empty, valid file, and >1 MiB file cap.
  {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() / ("snes2dhd-profile-test-" + std::to_string(now));
    std::filesystem::create_directories(dir);

    const auto missing_res = load_profile_file((dir / "missing.json").string());
    check("reject missing file", !missing_res.profile.has_value() && !missing_res.error.empty());

    const auto empty_path = dir / "empty.json";
    { std::ofstream f(empty_path, std::ios::binary); }
    const auto empty_res = load_profile_file(empty_path.string());
    check("reject empty file", !empty_res.profile.has_value() && !empty_res.error.empty());

    const auto valid_path = dir / "valid.json";
    {
      std::ofstream f(valid_path, std::ios::binary);
      f << valid_prefix << R"("scenes":[]})";
    }
    const auto valid_file_res = load_profile_file(valid_path.string());
    check("load valid file", valid_file_res.profile.has_value() && valid_file_res.profile->matches_rom(kHashUpper));

    const auto oversized_path = dir / "oversized.json";
    {
      std::ofstream f(oversized_path, std::ios::binary);
      std::string chunk(MaxProfileBytes + 16, ' ');
      f.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    }
    const auto oversized_res = load_profile_file(oversized_path.string());
    check("reject >1 MiB file", !oversized_res.profile.has_value() && !oversized_res.error.empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }

  if(failures) {
    std::fprintf(stderr, "profile: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("profile: all checks ok\n");
  return 0;
}
