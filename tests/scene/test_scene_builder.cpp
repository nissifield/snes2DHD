// Synthetic Scene Builder tests (zero ROM data).

#include <scene/scene_builder.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace {

int failures = 0;

auto check(const char* label, bool cond) -> void {
  if(cond) return;
  std::fprintf(stderr, "scene_builder: %s failed\n", label);
  failures++;
}

auto approx(float a, float b) -> bool {
  return std::fabs(a - b) < 1e-4f;
}

auto write_oam_entry(snes2dhd::FrameState& frame, std::uint8_t index, std::uint8_t x_low, std::uint8_t y,
                     std::uint8_t tile, std::uint8_t attr) -> void {
  const std::uint32_t base = static_cast<std::uint32_t>(index) * 4u;
  frame.mem_writes.push_back({0, snes2dhd::MemorySpace::Oam, base + 0, x_low});
  frame.mem_writes.push_back({0, snes2dhd::MemorySpace::Oam, base + 1, y});
  frame.mem_writes.push_back({0, snes2dhd::MemorySpace::Oam, base + 2, tile});
  frame.mem_writes.push_back({0, snes2dhd::MemorySpace::Oam, base + 3, attr});
}

auto make_test_profile() -> snes2dhd::profile::Profile {
  using namespace snes2dhd::profile;
  Profile p;
  p.game_id = "synthetic-scene-test";
  p.rom_sha256 = {std::string(64, '0')};

  SceneRule town;
  town.id = "town";
  town.has_selector = true;
  town.wram_addr = 0x1234;
  town.wram_value = 9;
  town.camera = CameraPreset{42.5f, 30.0f, 14.5f};
  town.light = LightPreset{0.45f, -0.2f, -0.9f, 0.4f, 0.35f, 0.25f};
  town.layers = {
    LayerRule{2, RenderMode::Voxel, -3.5f, 0.0f},
    LayerRule{0, RenderMode::Flat, 0.0f, 15.0f},
    LayerRule{1, RenderMode::Flat, -1.0f, 5.0f},
    LayerRule{1, RenderMode::Stock2D, -1.5f, 0.0f},  // overrides duplicate layer 1
  };
  town.sprites = {
    SpriteRule{"012a", RenderMode::Voxel, 0.25f, 1.75f},
    SpriteRule{"0x0010", RenderMode::Flat, -0.5f, 1.25f},
    SpriteRule{"00ff", RenderMode::Stock2D, 9.0f, 3.0f},
    SpriteRule{"a1b2c3d4e5f60708", RenderMode::Voxel, 2.0f, 2.0f},
  };

  p.scenes = {town};
  return p;
}

}  // namespace

auto main() -> int {
  using namespace snes2dhd;
  using namespace snes2dhd::scene;

  // 1. No profile -> single stock-2D plane identity fallback.
  {
    FrameState frame;
    frame.width = 512;
    frame.height = 448;
    frame.reg_writes.push_back({0, 0x2c, 0x0f});
    frame.mem_writes.push_back({0, MemorySpace::Wram, 0x1234, 9});
    write_oam_entry(frame, 0, 20, 30, 0x2a, 0x01);

    const Scene s = build_scene(frame, std::nullopt);
    check("no profile: is_stock_2d", s.is_stock_2d);
    check("no profile: dimensions", s.width == 512 && s.height == 448);
    check("no profile: empty scene id", s.active_scene_id.empty());
    check("no profile: camera orthographic", s.camera == Camera{0.0f, 0.0f, 1.0f, true});
    check("no profile: default ambient lighting", s.lighting == Lighting{1.0f, Vec3{0.0f, 0.0f, -1.0f}, 0.0f, 0.0f});
    check("no profile: single stock_2d plane", s.planes.size() == 1 && s.planes[0] == Plane{0, 0.0f, 0.0f, true});
    check("no profile: empty billboards", s.billboards.empty());
  }

  // 2. Profile with unmatched WRAM selector -> safe stock-2D fallback.
  {
    const profile::Profile profile = make_test_profile();
    FrameState frame;
    frame.mem_writes.push_back({0, MemorySpace::Wram, 0x1234, 7});

    const Scene s = build_scene(frame, profile);
    check("unmatched selector: is_stock_2d", s.is_stock_2d);
    check("unmatched selector: single plane", s.planes.size() == 1 && s.planes[0] == Plane{0, 0.0f, 0.0f, true});
    check("unmatched selector: empty billboards", s.billboards.empty());
  }

  // 3. Matched WRAM selector (latest write wins), sorted planes, unconfigured active BG fallback,
  //    OAM sprite sizes/filtering/rules, and deterministic repeat output.
  {
    const profile::Profile profile = make_test_profile();
    FrameState frame;
    frame.mem_writes.push_back({0, MemorySpace::Wram, 0x1234, 7});
    frame.mem_writes.push_back({10, MemorySpace::Wram, 0x1234, 9});  // latest WRAM write matches

    frame.reg_writes.push_back({0, 0x2c, 0x01});
    frame.reg_writes.push_back({5, 0x2c, 0x0f});  // latest TM enables BG1..BG4 (layer 3 unconfigured)
    frame.reg_writes.push_back({0, 0x01, 0x20});
    frame.reg_writes.push_back({2, 0x01, 0x60});  // latest OBSEL baseSize=3 -> small 16x16, large 32x32

    // OAM slot 5 written before slot 2 -> billboards must still be sorted by oam_index.
    write_oam_entry(frame, 5, 100, 80, 0x2a, 0x01);  // tile 0x12a -> voxel rule
    write_oam_entry(frame, 2, 40, 60, 0x10, 0x00);   // tile 0x010 -> flat rule
    write_oam_entry(frame, 3, 248, 50, 0x55, 0x00);  // unmatched tile 0x055, x_msb=1 & large=1 -> x=-8, 32x32
    write_oam_entry(frame, 4, 12, 20, 0xff, 0x00);   // Stock2D rule -> safe flat fallback
    write_oam_entry(frame, 6, 32, 0xe0, 0x2a, 0x01); // hidden Y >= 224 -> ignored
    write_oam_entry(frame, 7, 32, 0xf0, 0x2a, 0x01); // hidden Y >= 224 -> ignored
    write_oam_entry(frame, 8, 0, 40, 0x2a, 0x01);    // x_msb=1, x_low=0 -> x=-256 -> ignored

    // High table:
    // Byte 512 covers OAM 0..3 -> sprite 3 bits at shift 6 (bit6=x_msb=1, bit7=large=1 -> 0xc0).
    frame.mem_writes.push_back({0, MemorySpace::Oam, 512, 0xc0});
    // Byte 513 covers OAM 4..7 -> sprite 5 bits at shift 2 (bit2=0, bit3=large=1 -> 0x08).
    frame.mem_writes.push_back({0, MemorySpace::Oam, 513, 0x08});
    // Byte 514 covers OAM 8..11 -> sprite 8 bits at shift 0 (bit0=x_msb=1 -> 0x01).
    // Also sets bit4=1 on sprite 10 whose low table was never written -> must stay ignored.
    frame.mem_writes.push_back({0, MemorySpace::Oam, 514, 0x11});

    const Scene s = build_scene(frame, profile);
    const Scene s_repeat = build_scene(frame, profile);
    check("matched: deterministic repeat", s == s_repeat);
    check("matched: !is_stock_2d", !s.is_stock_2d);
    check("matched: active_scene_id", s.active_scene_id == "town");
    check("matched: camera", !s.camera.orthographic && approx(s.camera.fov_deg, 42.5f)
        && approx(s.camera.pitch_deg, 30.0f) && approx(s.camera.distance, 14.5f));
    check("matched: lighting", approx(s.lighting.ambient, 0.45f) && approx(s.lighting.direction.x, -0.2f)
        && approx(s.lighting.direction.y, -0.9f) && approx(s.lighting.direction.z, 0.4f)
        && approx(s.lighting.bloom, 0.35f) && approx(s.lighting.dof, 0.25f));

    check("matched: 4 deduplicated sorted planes", s.planes.size() == 4);
    if(s.planes.size() == 4) {
      check("matched: plane 0", s.planes[0] == Plane{0, 0.0f, 15.0f, false});
      check("matched: plane 1 Stock2D", s.planes[1] == Plane{1, -1.5f, 0.0f, true});
      check("matched: plane 2 Voxel", s.planes[2] == Plane{2, -3.5f, 0.0f, false});
      check("matched: plane 3 unconfigured active fallback", s.planes[3] == Plane{3, 0.0f, 0.0f, true});
    }

    check("matched: 4 visible sorted billboards", s.billboards.size() == 4);
    if(s.billboards.size() == 4) {
      check("matched: oam 2 flat rule", s.billboards[0].oam_index == 2 && approx(s.billboards[0].x, 40.0f)
          && approx(s.billboards[0].y, 60.0f) && approx(s.billboards[0].depth, -0.5f)
          && approx(s.billboards[0].width, 16.0f) && approx(s.billboards[0].height, 20.0f)
          && !s.billboards[0].voxel);
      check("matched: oam 3 negative x fallback", s.billboards[1].oam_index == 3 && approx(s.billboards[1].x, -8.0f)
          && approx(s.billboards[1].y, 50.0f) && approx(s.billboards[1].depth, 0.0f)
          && approx(s.billboards[1].width, 32.0f) && approx(s.billboards[1].height, 32.0f)
          && !s.billboards[1].voxel);
      check("matched: oam 4 Stock2D rule fallback", s.billboards[2].oam_index == 4 && approx(s.billboards[2].depth, 0.0f)
          && approx(s.billboards[2].width, 16.0f) && approx(s.billboards[2].height, 16.0f)
          && !s.billboards[2].voxel);
      check("matched: oam 5 large voxel rule", s.billboards[3].oam_index == 5 && approx(s.billboards[3].x, 100.0f)
          && approx(s.billboards[3].y, 80.0f) && approx(s.billboards[3].depth, 0.25f)
          && approx(s.billboards[3].width, 32.0f) && approx(s.billboards[3].height, 56.0f)
          && s.billboards[3].voxel);
    }
  }

  // 4. Default scene (has_selector == false) applies when no selector matches,
  //    and explicit WRAM selector takes precedence over default scene.
  {
    const std::string json = R"({
      "schema_version": 1,
      "game_id": "synthetic-default-precedence",
      "rom_sha256": ["0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"],
      "scenes": [
        {
          "id": "default-scene",
          "layers": [ { "layer": 0, "mode": "flat", "depth": -2.0 } ]
        },
        {
          "id": "boss-room",
          "wram_addr": 256,
          "wram_value": 4,
          "layers": [ { "layer": 0, "mode": "voxel", "depth": -4.0 } ]
        }
      ]
    })";
    const auto parsed = profile::parse_profile_json(json);
    check("json profile valid", parsed.profile.has_value());

    FrameState frame;
    const Scene fallback_scene = build_scene(frame, parsed.profile);
    check("default scene chosen when no selector matches",
        !fallback_scene.is_stock_2d && fallback_scene.active_scene_id == "default-scene"
        && fallback_scene.planes.size() == 1 && approx(fallback_scene.planes[0].depth, -2.0f));

    frame.mem_writes.push_back({0, MemorySpace::Wram, 256, 4});
    const Scene selector_scene = build_scene(frame, parsed.profile);
    check("explicit WRAM selector overrides earlier default scene",
        !selector_scene.is_stock_2d && selector_scene.active_scene_id == "boss-room"
        && selector_scene.planes.size() == 1 && approx(selector_scene.planes[0].depth, -4.0f));
  }

  if(failures) {
    std::fprintf(stderr, "scene_builder: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("scene_builder: all checks ok\n");
  return 0;
}
