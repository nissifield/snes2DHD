// Synthetic Scene Builder tests (zero ROM data).

#include <scene/scene_builder.hpp>
#include <tools/tile_hash.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
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

auto write_reg16(snes2dhd::FrameState& frame, std::uint16_t scanline, std::uint8_t reg, std::uint16_t value) -> void {
  frame.reg_writes.push_back({scanline, reg, static_cast<std::uint8_t>(value & 0xffu)});
  frame.reg_writes.push_back({scanline, reg, static_cast<std::uint8_t>((value >> 8) & 0xffu)});
}

auto write_vram_block(snes2dhd::FrameState& frame, std::uint32_t base_addr, std::span<const std::uint8_t> data) -> void {
  for(std::size_t i = 0; i < data.size(); ++i) {
    frame.mem_writes.push_back({
      0,
      snes2dhd::MemorySpace::Vram,
      base_addr + static_cast<std::uint32_t>(i),
      data[i],
    });
  }
}

auto to_upper_ascii(std::string text) -> std::string {
  for(char& c : text) {
    if(c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
  }
  return text;
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
    check("no profile: mode7 nullopt", !s.mode7.has_value());
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
    check("unmatched selector: mode7 nullopt", !s.mode7.has_value());
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
    check("matched: mode7 nullopt when BGMODE != 7", !s.mode7.has_value());
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

  // 5. Mode 7 remains inactive without a matching profile or when latest BGMODE != 7.
  {
    const profile::Profile profile = make_test_profile();
    FrameState frame;
    frame.reg_writes.push_back({0, 0x05, 0x07});
    write_reg16(frame, 0, 0x1b, 0x0200u);

    check("mode7: no profile -> nullopt even with BGMODE=7", !build_scene(frame, std::nullopt).mode7.has_value());

    frame.mem_writes.push_back({0, MemorySpace::Wram, 0x1234, 1});
    check("mode7: unmatched selector -> nullopt even with BGMODE=7", !build_scene(frame, profile).mode7.has_value());

    frame.mem_writes.push_back({1, MemorySpace::Wram, 0x1234, 9});
    frame.reg_writes.push_back({40, 0x05, 0x01});  // latest BGMODE switches back to mode 1
    const Scene s = build_scene(frame, profile);
    check("mode7: latest BGMODE != 7 -> nullopt", !s.is_stock_2d && !s.mode7.has_value());
  }

  // 6. Mode 7 active with unwritten matrix/scroll defaults and no layer-0 rule.
  {
    profile::Profile profile;
    profile.game_id = "synthetic-mode7-defaults";
    profile.rom_sha256 = {std::string(64, '0')};
    profile::SceneRule overworld;
    overworld.id = "overworld";
    overworld.layers = {profile::LayerRule{1, profile::RenderMode::Flat, -2.0f, 0.0f}};
    profile.scenes = {overworld};

    FrameState frame;
    frame.reg_writes.push_back({0, 0x05, 0x01});
    frame.reg_writes.push_back({20, 0x05, 0x87});  // low 3 bits == 7 with upper bits set

    const Scene s = build_scene(frame, profile);
    check("mode7 defaults: active", s.mode7.has_value() && s.mode7->active);
    if(s.mode7) {
      check("mode7 defaults: identity matrix",
          approx(s.mode7->m7a, 1.0f) && approx(s.mode7->m7b, 0.0f)
          && approx(s.mode7->m7c, 0.0f) && approx(s.mode7->m7d, 1.0f));
      check("mode7 defaults: zero center/scroll/m7sel",
          s.mode7->center_x == 0 && s.mode7->center_y == 0
          && s.mode7->scroll_x == 0 && s.mode7->scroll_y == 0 && s.mode7->m7sel == 0);
      check("mode7 defaults: unconfigured layer 0 depth/tilt",
          approx(s.mode7->depth, 0.0f) && approx(s.mode7->tilt_deg, 0.0f));
    }
  }

  // 7. Mode 7 register latching: signed 8.8 M7A-D, signed 13-bit M7X/Y/HOFS/VOFS, M7SEL,
  //    shared latch across writes, layer-0 depth/tilt inheritance, and deterministic output.
  {
    profile::Profile profile = make_test_profile();
    profile.scenes[0].layers.push_back(profile::LayerRule{0, profile::RenderMode::Flat, -2.25f, 22.5f});

    FrameState frame;
    frame.mem_writes.push_back({0, MemorySpace::Wram, 0x1234, 9});
    frame.reg_writes.push_back({0, 0x05, 0x07});
    frame.reg_writes.push_back({0, 0x1a, 0x01});
    write_reg16(frame, 0, 0x1b, 0x0180u);   // initial M7A = +1.5, leaves m7_latch = 0x01
    frame.reg_writes.push_back({10, 0x1c, 0x02});  // single-byte M7B uses shared m7_latch -> 0x0201
    frame.reg_writes.push_back({10, 0x1a, 0xc3});  // M7SEL does not clobber shared m7_latch
    const Scene single_latch_scene = build_scene(frame, profile);
    check("mode7 shared latch single-byte write",
        single_latch_scene.mode7.has_value()
        && approx(single_latch_scene.mode7->m7b, 513.0f / 256.0f));

    write_reg16(frame, 120, 0x1b, 0xfe80u);  // latest M7A = -384 -> -1.5f
    write_reg16(frame, 120, 0x1c, 0xff40u);  // M7B = -192 -> -0.75f
    write_reg16(frame, 120, 0x1d, 0x00c0u);  // M7C = +192 -> +0.75f
    write_reg16(frame, 120, 0x1e, 0x0240u);  // M7D = +576 -> +2.25f
    write_reg16(frame, 120, 0x1f, 0xe100u);  // M7X = +256 (upper bits 13..15 masked)
    write_reg16(frame, 120, 0x20, 0x1f80u);  // M7Y = -128 (signed 13-bit)
    write_reg16(frame, 120, 0x0d, 0x1000u);  // M7HOFS = -4096 (min signed 13-bit)
    write_reg16(frame, 120, 0x0e, 0x0fffu);  // M7VOFS = +4095 (max signed 13-bit)

    const Scene s = build_scene(frame, profile);
    const Scene s_repeat = build_scene(frame, profile);
    check("mode7: deterministic repeat", s == s_repeat);
    check("mode7: active", s.mode7.has_value() && s.mode7->active);
    if(s.mode7) {
      check("mode7: signed 8.8 m7a..m7d",
          approx(s.mode7->m7a, -1.5f) && approx(s.mode7->m7b, -0.75f)
          && approx(s.mode7->m7c, 0.75f) && approx(s.mode7->m7d, 2.25f));
      check("mode7: signed 13-bit center_x/center_y",
          s.mode7->center_x == 256 && s.mode7->center_y == -128);
      check("mode7: signed 13-bit scroll_x/scroll_y",
          s.mode7->scroll_x == -4096 && s.mode7->scroll_y == 4095);
      check("mode7: m7sel", s.mode7->m7sel == 0xc3);
      check("mode7: inherited layer 0 depth/tilt",
          approx(s.mode7->depth, -2.25f) && approx(s.mode7->tilt_deg, 22.5f));
    }

    // Signed 16-bit 8.8 extreme boundaries (-128.0f and +127.99609375f).
    write_reg16(frame, 140, 0x1b, 0x8000u);
    write_reg16(frame, 140, 0x1d, 0x7fffu);
    const Scene extreme = build_scene(frame, profile);
    check("mode7: extreme 8.8 bounds",
        extreme.mode7.has_value()
        && approx(extreme.mode7->m7a, -128.0f)
        && approx(extreme.mode7->m7c, 32767.0f / 256.0f));
  }

  // 8. Real 4bpp VRAM tile SHA-256 sprite rule matching (table 0, table 1 with non-zero OBSEL,
  //    case-insensitive matching, last-write-wins, unmatched/unwritten safe flat fallback, determinism).
  {
    std::array<std::uint8_t, 32> stale_tile{};
    std::array<std::uint8_t, 32> table0_tile{};
    std::array<std::uint8_t, 32> table1_tile{};
    std::array<std::uint8_t, 32> partial_tile{};
    std::array<std::uint8_t, 32> unmatched_tile{};
    std::array<std::uint8_t, 32> zero_tile{};
    for(std::size_t i = 0; i < 32; ++i) {
      stale_tile[i] = static_cast<std::uint8_t>(0x10u + i);
      table0_tile[i] = static_cast<std::uint8_t>(0x40u + i);
      table1_tile[i] = static_cast<std::uint8_t>(0xa0u + i);
      unmatched_tile[i] = static_cast<std::uint8_t>(0xe0u + i);
    }
    partial_tile[0] = 0x7au;

    const std::string sha_table0_upper = to_upper_ascii(tools::hash_tile_bytes(table0_tile));
    const std::string sha_table0_lower = tools::hash_tile_bytes(table0_tile);
    const std::string sha_table1_lower = tools::hash_tile_bytes(table1_tile);
    std::string sha_partial_mixed = tools::hash_tile_bytes(partial_tile);
    for(std::size_t i = 0; i < sha_partial_mixed.size(); i += 2u) {
      if(sha_partial_mixed[i] >= 'a' && sha_partial_mixed[i] <= 'f') {
        sha_partial_mixed[i] = static_cast<char>(sha_partial_mixed[i] - 'a' + 'A');
      }
    }
    const std::string sha_zero_lower = tools::hash_tile_bytes(zero_tile);

    profile::Profile profile;
    profile.game_id = "synthetic-vram-sha256-test";
    profile.rom_sha256 = {std::string(64, '0')};

    profile::SceneRule stage;
    stage.id = "vram-stage";
    stage.sprites = {
      profile::SpriteRule{sha_table0_upper, profile::RenderMode::Voxel, 0.75f, 1.5f},
      profile::SpriteRule{sha_table0_lower, profile::RenderMode::Flat, 9.0f, 9.0f},  // duplicate: first rule wins
      profile::SpriteRule{sha_table1_lower, profile::RenderMode::Voxel, -0.5f, 2.25f},
      profile::SpriteRule{sha_partial_mixed, profile::RenderMode::Stock2D, 4.0f, 3.0f},
      profile::SpriteRule{sha_zero_lower, profile::RenderMode::Voxel, 3.0f, 4.0f},
    };
    profile.scenes = {stage};

    // Case A: OBSEL = 0x00 (base = 0x0000, gap = 0x0000) -> table 0 matching, partial Stock2D fallback,
    //         unwritten VRAM fallback (must not match zero_tile SHA), and unmatched VRAM fallback.
    FrameState frame0;
    write_vram_block(frame0, 0x0080u, stale_tile);
    write_vram_block(frame0, 0x0080u, table0_tile);  // last write wins at tile 0x04
    frame0.mem_writes.push_back({1, MemorySpace::Vram, 0x00c0u, 0x7au});  // partial write at tile 0x06
    write_vram_block(frame0, 0x0100u, unmatched_tile);  // tile 0x08 written, no matching rule
    frame0.mem_writes.push_back({2, MemorySpace::Vram, 65536u + 0x0140u, 0x55u});  // OOB VRAM ignored (tile 0x0a)

    write_oam_entry(frame0, 0, 16, 24, 0x04, 0x00);  // table 0 -> 0x0080 -> Voxel (uppercase rule)
    write_oam_entry(frame0, 1, 32, 24, 0x06, 0x00);  // table 0 -> 0x00c0 -> Stock2D rule -> flat fallback
    write_oam_entry(frame0, 2, 48, 24, 0x08, 0x00);  // table 0 -> 0x0100 -> unmatched SHA -> flat fallback
    write_oam_entry(frame0, 3, 64, 24, 0x09, 0x00);  // table 0 -> 0x0120 -> unwritten VRAM -> flat fallback
    write_oam_entry(frame0, 4, 80, 24, 0x0a, 0x00);  // table 0 -> 0x0140 -> only OOB write -> flat fallback

    const Scene s0 = build_scene(frame0, profile);
    check("vram sha256 table0: deterministic repeat", s0 == build_scene(frame0, profile));
    check("vram sha256 table0: 5 billboards", s0.billboards.size() == 5);
    if(s0.billboards.size() == 5) {
      check("vram sha256 table0: oam 0 uppercase voxel match",
          s0.billboards[0].oam_index == 0 && s0.billboards[0].voxel
          && approx(s0.billboards[0].depth, 0.75f) && approx(s0.billboards[0].height, 12.0f));
      check("vram sha256 table0: oam 1 Stock2D rule fallback",
          s0.billboards[1].oam_index == 1 && !s0.billboards[1].voxel
          && approx(s0.billboards[1].depth, 0.0f) && approx(s0.billboards[1].height, 8.0f));
      check("vram sha256 table0: oam 2 unmatched sha fallback",
          s0.billboards[2].oam_index == 2 && !s0.billboards[2].voxel
          && approx(s0.billboards[2].depth, 0.0f) && approx(s0.billboards[2].height, 8.0f));
      check("vram sha256 table0: oam 3 unwritten tile fallback",
          s0.billboards[3].oam_index == 3 && !s0.billboards[3].voxel
          && approx(s0.billboards[3].depth, 0.0f) && approx(s0.billboards[3].height, 8.0f));
      check("vram sha256 table0: oam 4 OOB write ignored fallback",
          s0.billboards[4].oam_index == 4 && !s0.billboards[4].voxel
          && approx(s0.billboards[4].depth, 0.0f) && approx(s0.billboards[4].height, 8.0f));
    }

    // Case B: Non-zero OBSEL ($2101) base & gap bits + OBJ table 1 + wrap mod 65536.
    // OBSEL = 0x0b: base = 3 -> 0xc000, gap = 1 -> 0x2000.
    // Table 0 tile 0x02 -> 0xc040; Table 1 tile 0x02 -> (0xc000 + 0x2000 + 0x2000 + 0x0040) & 0xffff = 0x0040.
    FrameState frame1;
    frame1.reg_writes.push_back({0, 0x01, 0x01});   // earlier OBSEL overridden
    frame1.reg_writes.push_back({5, 0x01, 0x0b});   // latest OBSEL: base=3 (0xc000), gap=1 (0x2000)
    write_vram_block(frame1, 0xc040u, table0_tile);
    write_vram_block(frame1, 0x0040u, table1_tile);
    write_vram_block(frame1, 0xe040u, unmatched_tile);  // table 1 with gap=0 would look here; must not match

    write_oam_entry(frame1, 0, 20, 40, 0x02, 0x00);  // table 0 -> 0xc040 -> Voxel (table0_tile)
    write_oam_entry(frame1, 1, 44, 40, 0x02, 0x01);  // table 1 -> 0x0040 -> Voxel (table1_tile)
    write_oam_entry(frame1, 2, 68, 40, 0x03, 0x01);  // table 1 -> 0x0060 -> unwritten -> flat fallback

    const Scene s1 = build_scene(frame1, profile);
    check("vram sha256 obsel: deterministic repeat", s1 == build_scene(frame1, profile));
    check("vram sha256 obsel: 3 billboards", s1.billboards.size() == 3);
    if(s1.billboards.size() == 3) {
      check("vram sha256 obsel: table 0 non-zero base",
          s1.billboards[0].oam_index == 0 && s1.billboards[0].voxel
          && approx(s1.billboards[0].depth, 0.75f) && approx(s1.billboards[0].height, 12.0f));
      check("vram sha256 obsel: table 1 base+gap+wrap",
          s1.billboards[1].oam_index == 1 && s1.billboards[1].voxel
          && approx(s1.billboards[1].depth, -0.5f) && approx(s1.billboards[1].height, 18.0f));
      check("vram sha256 obsel: table 1 unwritten fallback",
          s1.billboards[2].oam_index == 2 && !s1.billboards[2].voxel
          && approx(s1.billboards[2].depth, 0.0f) && approx(s1.billboards[2].height, 8.0f));
    }
  }

  if(failures) {
    std::fprintf(stderr, "scene_builder: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("scene_builder: all checks ok\n");
  return 0;
}
