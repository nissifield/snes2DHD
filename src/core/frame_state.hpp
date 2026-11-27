#pragma once

#include <cstdint>
#include <vector>

namespace snes2dhd {

inline constexpr std::uint32_t FrameStateSchemaVersion = 1;

enum class MemorySpace : std::uint8_t {
  Vram = 0,
  Oam = 1,
  Cgram = 2,
  Wram = 3,
};

struct RegWrite {
  std::uint16_t scanline = 0;
  std::uint8_t reg = 0;    // $2100-$213F stored as addr - 0x2100
  std::uint8_t value = 0;

  bool operator==(const RegWrite&) const = default;
};

struct MemWrite {
  std::uint16_t scanline = 0;
  MemorySpace space = MemorySpace::Vram;
  std::uint32_t addr = 0;
  std::uint8_t value = 0;

  bool operator==(const MemWrite&) const = default;
};

struct FrameState {
  std::uint32_t schema_version = FrameStateSchemaVersion;
  std::uint64_t frame_index = 0;
  std::uint16_t width = 256;
  std::uint16_t height = 224;
  std::vector<RegWrite> reg_writes;
  std::vector<MemWrite> mem_writes;

  bool operator==(const FrameState&) const = default;
};

}  // namespace snes2dhd
