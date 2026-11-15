#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace snes2dhd {

struct Frame {
  std::vector<std::uint32_t> pixels;  // width*height, row-major, no row padding, XRGB8888
  std::uint32_t width = 0, height = 0;
};

// Loads a ROM file and powers on the SNES. Returns an error message, or "" on success.
std::string load(const std::string& rom_path);

// Runs emulation until ares delivers the next video frame, and returns it.
const Frame& run_frame();

void unload();

}
