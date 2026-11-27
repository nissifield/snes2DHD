#pragma once

#include <core/frame_state.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace snes2dhd {

class TapAdapter {
public:
  void begin_frame(std::uint64_t frame_index, std::uint16_t width, std::uint16_t height);
  void record_reg_write(std::uint16_t scanline, std::uint16_t addr, std::uint8_t value);
  void record_mem_write(std::uint16_t scanline, MemorySpace space, std::uint32_t addr, std::uint8_t value);
  const FrameState& finish_frame() const;

private:
  FrameState state_;
};

std::vector<std::uint8_t> serialize_frame_state(const FrameState& state);
bool deserialize_frame_state(std::span<const std::uint8_t> bytes, FrameState& out, std::string& error);

}  // namespace snes2dhd
