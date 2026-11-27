#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace snes2dhd {

enum class Region {
  NTSC,
  PAL,
};

enum class Mapping {
  LoROM,
  HiROM,
  ExHiROM,
};

inline constexpr int AudioSampleRate = 48000;

struct Frame {
  std::vector<std::uint32_t> pixels;  // width*height, row-major, no row padding, XRGB8888
  std::uint32_t width = 0, height = 0;
};

// Loads a ROM file and powers on the SNES. Returns an error message, or "" on success.
std::string load(const std::string& rom_path);

// Detected region and ROM mapping of the loaded cartridge (defaults to NTSC / LoROM when unloaded).
Region region();
Mapping mapping();

// Controller 1 buttons, one bit each, in ares Gamepad shift order.
enum Button : std::uint16_t {
  ButtonB      = 1 << 0,
  ButtonY      = 1 << 1,
  ButtonSelect = 1 << 2,
  ButtonStart  = 1 << 3,
  ButtonUp     = 1 << 4,
  ButtonDown   = 1 << 5,
  ButtonLeft   = 1 << 6,
  ButtonRight  = 1 << 7,
  ButtonA      = 1 << 8,
  ButtonX      = 1 << 9,
  ButtonL      = 1 << 10,
  ButtonR      = 1 << 11,
};

// Sets the held controller 1 buttons; applied at the next controller poll.
// Only bits 0-11 are used. Call between frames on the load/run_frame thread.
void set_buttons(std::uint16_t buttons);

// Runs emulation until ares delivers the next video frame, and returns it.
const Frame& run_frame();

// Interleaved stereo 16-bit PCM samples (AudioSampleRate Hz) emitted during the last run_frame().
std::span<const std::int16_t> audio_samples();

// Serializes the loaded system state deterministically, or returns empty when unloaded.
std::vector<std::uint8_t> save_state();

// Restores a serialized state into the loaded system. Returns "" on success or an error message.
std::string load_state(std::span<const std::uint8_t> data);

// Battery SRAM helpers for cartridges with mapped save RAM.
bool has_sram();
std::string load_sram(const std::string& path);
std::string save_sram(const std::string& path);

void unload();

}
