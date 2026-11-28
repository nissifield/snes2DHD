#include <scene/scene_builder.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace snes2dhd::scene {
namespace {

struct PpuState {
  std::uint8_t obsel = 0;
  std::uint8_t bgmode = 0;
  std::uint8_t m7sel = 0;
  std::uint8_t tm = 0;
  bool has_tm = false;
  std::uint8_t m7_latch = 0;
  std::uint16_t m7a = 0x0100u;
  std::uint16_t m7b = 0;
  std::uint16_t m7c = 0;
  std::uint16_t m7d = 0x0100u;
  std::uint16_t m7x = 0;
  std::uint16_t m7y = 0;
  std::uint16_t m7hofs = 0;
  std::uint16_t m7vofs = 0;
};

auto make_stock_2d_scene(const FrameState& frame) -> Scene {
  Scene scene;
  scene.is_stock_2d = true;
  scene.width = frame.width;
  scene.height = frame.height;
  scene.planes.push_back(Plane{0, 0.0f, 0.0f, true});
  return scene;
}

auto select_scene_rule(const FrameState& frame, const profile::Profile& profile) -> const profile::SceneRule* {
  if(profile.scenes.empty()) return nullptr;

  bool has_any_selector = false;
  for(const auto& s : profile.scenes) {
    if(s.has_selector) {
      has_any_selector = true;
      break;
    }
  }

  std::unordered_map<std::uint32_t, std::uint8_t> wram;
  if(has_any_selector) {
    for(const auto& m : frame.mem_writes) {
      if(m.space == MemorySpace::Wram) wram[m.addr] = m.value;
    }
  }

  const profile::SceneRule* default_scene = nullptr;
  for(const auto& s : profile.scenes) {
    if(s.has_selector) {
      const auto it = wram.find(s.wram_addr);
      if(it != wram.end() && it->second == s.wram_value) return &s;
    } else if(!default_scene) {
      default_scene = &s;
    }
  }
  return default_scene;
}

auto read_ppu_state(const FrameState& frame) -> PpuState {
  PpuState state;
  // Mode 7 write-twice registers ($210D, $210E, $211B-$2120) share one 8-bit hardware latch.
  auto latch_mode7 = [&](std::uint16_t& target, std::uint8_t value) {
    target = static_cast<std::uint16_t>((static_cast<std::uint16_t>(value) << 8) | state.m7_latch);
    state.m7_latch = value;
  };
  for(const auto& w : frame.reg_writes) {
    switch(w.reg) {
    case 0x01: state.obsel = w.value; break;
    case 0x05: state.bgmode = w.value; break;
    case 0x0d: latch_mode7(state.m7hofs, w.value); break;
    case 0x0e: latch_mode7(state.m7vofs, w.value); break;
    case 0x1a: state.m7sel = w.value; break;
    case 0x1b: latch_mode7(state.m7a, w.value); break;
    case 0x1c: latch_mode7(state.m7b, w.value); break;
    case 0x1d: latch_mode7(state.m7c, w.value); break;
    case 0x1e: latch_mode7(state.m7d, w.value); break;
    case 0x1f: latch_mode7(state.m7x, w.value); break;
    case 0x20: latch_mode7(state.m7y, w.value); break;
    case 0x2c:
      state.tm = w.value;
      state.has_tm = true;
      break;
    default: break;
    }
  }
  return state;
}

auto decode_m7_fixed8_8(std::uint16_t raw) -> float {
  return static_cast<float>(static_cast<std::int16_t>(raw)) / 256.0f;
}

auto decode_m7_signed13(std::uint16_t raw) -> std::int16_t {
  const std::uint16_t masked = static_cast<std::uint16_t>(raw & 0x1fffu);
  return static_cast<std::int16_t>((masked & 0x1000u) != 0 ? (masked | 0xe000u) : masked);
}

auto build_mode7(const profile::SceneRule& rule, const PpuState& ppu) -> std::optional<Mode7GroundPlane> {
  if((ppu.bgmode & 0x07u) != 7u) return std::nullopt;

  Mode7GroundPlane plane;
  plane.active = true;
  plane.m7a = decode_m7_fixed8_8(ppu.m7a);
  plane.m7b = decode_m7_fixed8_8(ppu.m7b);
  plane.m7c = decode_m7_fixed8_8(ppu.m7c);
  plane.m7d = decode_m7_fixed8_8(ppu.m7d);
  plane.center_x = decode_m7_signed13(ppu.m7x);
  plane.center_y = decode_m7_signed13(ppu.m7y);
  plane.scroll_x = decode_m7_signed13(ppu.m7hofs);
  plane.scroll_y = decode_m7_signed13(ppu.m7vofs);
  plane.m7sel = ppu.m7sel;
  for(const auto& layer_rule : rule.layers) {
    if(layer_rule.layer == 0) {
      plane.depth = layer_rule.depth;
      plane.tilt_deg = layer_rule.tilt_deg;
    }
  }
  return plane;
}

auto sprite_dimensions(std::uint8_t obsel, bool large) -> std::pair<int, int> {
  static constexpr std::array<int, 8> kSmallW = {8, 8, 8, 16, 16, 32, 16, 16};
  static constexpr std::array<int, 8> kSmallH = {8, 8, 8, 16, 16, 32, 32, 32};
  static constexpr std::array<int, 8> kLargeW = {16, 32, 64, 32, 64, 64, 32, 32};
  static constexpr std::array<int, 8> kLargeH = {16, 32, 64, 32, 64, 64, 64, 32};

  const std::size_t base = static_cast<std::size_t>((obsel >> 5) & 0x07u);
  if(large) return {kLargeW[base], kLargeH[base]};
  return {kSmallW[base], kSmallH[base]};
}

auto parse_hex_tile_id(std::string_view text) -> std::optional<std::uint16_t> {
  if(text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    text.remove_prefix(2);
  }
  if(text.empty() || text.size() > 4) return std::nullopt;

  std::uint32_t value = 0;
  for(char c : text) {
    value <<= 4;
    if(c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
    else if(c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
    else if(c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
    else return std::nullopt;
  }
  if(value > 0x1ffu) return std::nullopt;
  return static_cast<std::uint16_t>(value);
}

auto build_planes(const profile::SceneRule& rule, const PpuState& ppu) -> std::vector<Plane> {
  std::array<std::optional<Plane>, 4> by_layer;
  for(const auto& layer_rule : rule.layers) {
    if(layer_rule.layer < 0 || layer_rule.layer >= 4) continue;
    by_layer[static_cast<std::size_t>(layer_rule.layer)] = Plane{
      layer_rule.layer,
      layer_rule.depth,
      layer_rule.tilt_deg,
      layer_rule.mode == profile::RenderMode::Stock2D,
    };
  }
  if(ppu.has_tm) {
    for(int layer = 0; layer < 4; ++layer) {
      const std::size_t idx = static_cast<std::size_t>(layer);
      if((ppu.tm & (1u << layer)) != 0 && !by_layer[idx].has_value()) {
        by_layer[idx] = Plane{layer, 0.0f, 0.0f, true};
      }
    }
  }

  std::vector<Plane> planes;
  for(const auto& p : by_layer) {
    if(p) planes.push_back(*p);
  }
  return planes;
}

auto build_billboards(const FrameState& frame, const profile::SceneRule& rule, const PpuState& ppu)
    -> std::vector<Billboard> {
  std::array<std::uint8_t, 544> oam{};
  std::array<bool, 128> oam_written{};
  bool any_oam = false;
  for(const auto& m : frame.mem_writes) {
    if(m.space != MemorySpace::Oam || m.addr >= oam.size()) continue;
    oam[m.addr] = m.value;
    if(m.addr < 512) {
      oam_written[m.addr >> 2] = true;
      any_oam = true;
    }
  }
  if(!any_oam) return {};

  // shortcut: matches hex tile IDs (0..0x1ff) against OAM tile index; upgrade to full VRAM tile-data hashing in M3
  std::vector<std::pair<std::uint16_t, const profile::SpriteRule*>> sprite_rules;
  sprite_rules.reserve(rule.sprites.size());
  for(const auto& sr : rule.sprites) {
    if(const auto tile_id = parse_hex_tile_id(sr.tile_hash)) {
      sprite_rules.emplace_back(*tile_id, &sr);
    }
  }

  auto find_rule = [&](std::uint16_t tile_id) -> const profile::SpriteRule* {
    for(const auto& [id, sr] : sprite_rules) {
      if(id == tile_id) return sr;
    }
    return nullptr;
  };

  std::vector<Billboard> billboards;
  for(std::size_t i = 0; i < 128; ++i) {
    if(!oam_written[i]) continue;

    const std::uint8_t x_low = oam[4 * i + 0];
    const std::uint8_t y = oam[4 * i + 1];
    const std::uint8_t tile_low = oam[4 * i + 2];
    const std::uint8_t attr = oam[4 * i + 3];

    if(y >= frame.height) continue;

    const std::uint8_t hi = oam[512 + (i >> 2)];
    const int shift = static_cast<int>(i & 3u) * 2;
    const bool x_msb = ((hi >> shift) & 0x01u) != 0;
    const bool large = ((hi >> (shift + 1)) & 0x01u) != 0;

    const auto [sprite_w, sprite_h] = sprite_dimensions(ppu.obsel, large);
    const int x_raw = static_cast<int>(x_low) | (x_msb ? 0x100 : 0);
    const int x = x_msb ? (x_raw - 512) : x_raw;
    if(x + sprite_w <= 0 || x >= static_cast<int>(frame.width)) continue;

    const std::uint16_t tile_id = static_cast<std::uint16_t>(tile_low)
        | static_cast<std::uint16_t>(static_cast<std::uint16_t>(attr & 0x01u) << 8);
    const profile::SpriteRule* sr = find_rule(tile_id);

    Billboard bb;
    bb.oam_index = static_cast<std::uint8_t>(i);
    bb.x = static_cast<float>(x);
    bb.y = static_cast<float>(y);
    bb.width = static_cast<float>(sprite_w);
    bb.height = static_cast<float>(sprite_h);
    if(sr && sr->mode != profile::RenderMode::Stock2D) {
      bb.depth = sr->depth_bias;
      bb.height *= sr->height_scale;
      bb.voxel = (sr->mode == profile::RenderMode::Voxel);
    }
    billboards.push_back(bb);
  }
  return billboards;
}

}  // namespace

Scene build_scene(const FrameState& frame, const std::optional<profile::Profile>& profile) {
  if(!profile.has_value()) return make_stock_2d_scene(frame);

  const profile::SceneRule* matched = select_scene_rule(frame, *profile);
  if(!matched) return make_stock_2d_scene(frame);

  const PpuState ppu = read_ppu_state(frame);

  Scene scene;
  scene.is_stock_2d = false;
  scene.width = frame.width;
  scene.height = frame.height;
  scene.active_scene_id = matched->id;
  scene.camera = Camera{
    matched->camera.fov_deg,
    matched->camera.pitch_deg,
    matched->camera.distance,
    false,
  };
  scene.lighting = Lighting{
    matched->light.ambient,
    Vec3{matched->light.dir_x, matched->light.dir_y, matched->light.dir_z},
    matched->light.bloom,
    matched->light.dof,
  };
  scene.planes = build_planes(*matched, ppu);
  scene.billboards = build_billboards(frame, *matched, ppu);
  scene.mode7 = build_mode7(*matched, ppu);
  return scene;
}

}  // namespace snes2dhd::scene
