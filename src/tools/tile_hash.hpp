#pragma once

#include <core/frame_state.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace snes2dhd::tools {

struct TileEntry {
  std::uint32_t vram_addr = 0;
  std::uint8_t bpp = 4;
  std::string sha256;

  bool operator==(const TileEntry&) const = default;
};

std::string hash_tile_bytes(std::span<const std::uint8_t> tile_bytes);

std::vector<TileEntry> hash_vram_tiles(const FrameState& frame, std::uint8_t bpp = 4);

std::string write_frame_state_dump(const std::string& path, const FrameState& frame);
std::string read_frame_state_dump(const std::string& path, FrameState& out);

}  // namespace snes2dhd::tools
