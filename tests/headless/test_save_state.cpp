// Deterministic save-state round-trip, corrupt-buffer rejection, unloaded
// safety, and battery SRAM persistence regression.

#include <core/core.hpp>

#include <nall/hash/sha256.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

int failures = 0;

auto fail(const std::string& msg) -> void {
  std::fprintf(stderr, "save_state: %s\n", msg.c_str());
  failures++;
}

auto hash_frame(const snes2dhd::Frame& frame) -> std::string {
  nall::Hash::SHA256 sha256;
  sha256.input(std::span<const u8>(reinterpret_cast<const u8*>(frame.pixels.data()),
                                   frame.pixels.size() * sizeof(std::uint32_t)));
  return (const char*)sha256.digest();
}

auto run_frames(int count) -> std::string {
  std::string digest;
  for(int i = 0; i < count; i++) digest = hash_frame(snes2dhd::run_frame());
  return digest;
}

auto write_le16(std::vector<std::uint8_t>& rom, std::size_t offset, std::uint16_t value) -> void {
  rom[offset] = static_cast<std::uint8_t>(value & 0xff);
  rom[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

// Synthetic 32 KiB LoROM + 2 KiB battery SRAM:
// Reads 16-bit BGR555 colour from SRAM $70:0000-$70:0001 into CGRAM0, then
// stores green (0x03E0) into $70:0000-$70:0001 so the next boot shows green
// after save_sram() -> reload -> load_sram().
auto make_sram_test_rom() -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> rom(0x8000, 0xff);
  const std::vector<std::uint8_t> code = {
    0x78, 0x18, 0xfb,              // SEI; CLC; XCE
    0xa9, 0x80, 0x8d, 0x00, 0x21,  // LDA #$80; STA $2100 (force blank)
    0x9c, 0x21, 0x21,              // STZ $2121 (CGRAM index 0)
    0xaf, 0x00, 0x00, 0x70,        // LDA $700000
    0x8d, 0x22, 0x21,              // STA $2122
    0xaf, 0x01, 0x00, 0x70,        // LDA $700001
    0x29, 0x7f,                    // AND #$7F
    0x8d, 0x22, 0x21,              // STA $2122
    0xa9, 0xe0, 0x8f, 0x00, 0x00, 0x70,  // LDA #$E0; STA $700000 (write green BGR555)
    0xa9, 0x03, 0x8f, 0x01, 0x00, 0x70,  // LDA #$03; STA $700001
    0xa9, 0x0f, 0x8d, 0x00, 0x21,  // LDA #$0F; STA $2100 (screen on)
    0x80, 0xfe,                    // BRA $FE
  };
  for(std::size_t i = 0; i < code.size(); i++) rom[i] = code[i];

  const char* title = "SYNTH SRAM TEST      ";
  for(std::size_t i = 0; i < 21; i++) rom[0x7fc0 + i] = static_cast<std::uint8_t>(title[i]);
  rom[0x7fd5] = 0x20;  // LoROM
  rom[0x7fd6] = 0x02;  // ROM + RAM + Battery
  rom[0x7fd7] = 0x05;  // 32 KiB ROM
  rom[0x7fd8] = 0x01;  // 2 KiB SRAM
  rom[0x7fd9] = 0x01;  // NTSC
  rom[0x7fda] = 0x00;
  rom[0x7fdb] = 0x00;
  for(std::size_t vec = 0x7fe0; vec < 0x8000; vec += 2) write_le16(rom, vec, 0x8000);

  write_le16(rom, 0x7fdc, 0xffff);
  write_le16(rom, 0x7fde, 0x0000);
  std::uint32_t sum = 0;
  for(std::uint8_t b : rom) sum += b;
  const std::uint16_t checksum = static_cast<std::uint16_t>(sum & 0xffff);
  write_le16(rom, 0x7fdc, static_cast<std::uint16_t>(checksum ^ 0xffff));
  write_le16(rom, 0x7fde, checksum);
  return rom;
}

auto test_unloaded() -> void {
  if(!snes2dhd::save_state().empty()) fail("save_state when unloaded must return empty");
  const std::vector<std::uint8_t> dummy = {1, 2, 3, 4};
  if(snes2dhd::load_state(dummy).empty()) fail("load_state when unloaded must return error");
  if(snes2dhd::has_sram()) fail("has_sram when unloaded must be false");
  if(snes2dhd::load_sram("/nonexistent.srm").empty()) fail("load_sram when unloaded must return error");
  if(snes2dhd::save_sram("/nonexistent.srm").empty()) fail("save_sram when unloaded must return error");
}

auto test_determinism_and_corruption(const std::string& min_rom, const std::string& joypad_rom) -> void {
  // 1. Minimal ROM: save at frame 20 -> run 20 -> hash A -> load -> run 20 -> hash B == hash A.
  if(auto err = snes2dhd::load(min_rom); !err.empty()) {
    fail("load min_rom failed: " + err);
    return;
  }
  if(snes2dhd::has_sram()) fail("min_rom should not report SRAM");

  run_frames(20);
  const auto state = snes2dhd::save_state();
  if(state.empty()) {
    fail("save_state returned empty buffer");
    snes2dhd::unload();
    return;
  }
  const std::string hash_a = run_frames(20);

  // Corrupt inputs must return non-empty errors without crashing or corrupting emulation.
  if(snes2dhd::load_state({}).empty()) fail("empty buffer accepted");
  if(snes2dhd::load_state(std::span<const std::uint8_t>(state.data(), 16)).empty()) {
    fail("truncated header accepted");
  }
  if(snes2dhd::load_state(std::span<const std::uint8_t>(state.data(), state.size() - 1)).empty()) {
    fail("truncated payload accepted");
  }
  {
    auto bad_sig = state;
    bad_sig[0] ^= 0xff;
    if(snes2dhd::load_state(bad_sig).empty()) fail("bad signature accepted");
  }
  {
    auto bad_ver = state;
    bad_ver[5] = 'X';
    if(snes2dhd::load_state(bad_ver).empty()) fail("bad version accepted");
  }
  {
    auto oversized = state;
    oversized.push_back(0);
    if(snes2dhd::load_state(oversized).empty()) fail("oversized payload accepted");
  }

  // Emulation still runs cleanly after rejected corrupt states.
  const auto after_corrupt = snes2dhd::run_frame();
  if(after_corrupt.pixels.empty()) fail("frame empty after corrupt load_state");

  // Reload valid state and confirm exact frame-hash match after 20 frames.
  if(auto err = snes2dhd::load_state(state); !err.empty()) {
    fail("load_state valid buffer failed: " + err);
  }
  const std::string hash_b = run_frames(20);
  if(hash_a != hash_b) {
    fail("frame hash mismatch after load_state: " + hash_a + " != " + hash_b);
  }
  snes2dhd::unload();

  // 2. Dynamic joypad ROM: prove load_state restores prior state after divergence.
  if(auto err = snes2dhd::load(joypad_rom); !err.empty()) {
    fail("load joypad_rom failed: " + err);
    return;
  }
  snes2dhd::set_buttons(snes2dhd::ButtonB);
  run_frames(20);
  const auto joy_state = snes2dhd::save_state();
  snes2dhd::set_buttons(snes2dhd::ButtonA);
  const std::string joy_hash_a = run_frames(20);

  // Diverge with different buttons.
  snes2dhd::set_buttons(snes2dhd::ButtonX | snes2dhd::ButtonY);
  const std::string joy_hash_diverged = run_frames(20);
  if(joy_hash_diverged == joy_hash_a) fail("joypad ROM failed to diverge for control check");

  if(auto err = snes2dhd::load_state(joy_state); !err.empty()) {
    fail("load_state joypad failed: " + err);
  }
  snes2dhd::set_buttons(snes2dhd::ButtonA);
  const std::string joy_hash_b = run_frames(20);
  if(joy_hash_a != joy_hash_b) {
    fail("joypad frame hash mismatch after load_state: " + joy_hash_a + " != " + joy_hash_b);
  }
  snes2dhd::unload();
}

auto test_sram_persistence(const std::filesystem::path& dir) -> void {
  const auto rom_path = (dir / "sram.sfc").string();
  const auto srm_path = (dir / "sram.sfc.srm").string();
  {
    const auto rom = make_sram_test_rom();
    std::ofstream out(rom_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
  }

  // First boot: SRAM starts at 0xFF (white backdrop), ROM writes green (0x03E0) to SRAM.
  if(auto err = snes2dhd::load(rom_path); !err.empty()) {
    fail("load sram ROM failed: " + err);
    return;
  }
  if(!snes2dhd::has_sram()) {
    fail("sram ROM must report has_sram() == true");
    snes2dhd::unload();
    return;
  }
  const auto& first_frame = snes2dhd::run_frame();
  const std::uint32_t first_pixel = first_frame.pixels[(first_frame.height / 2) * first_frame.width + (first_frame.width / 2)];
  if(((first_pixel >> 16) & 0xff) < 0xc0) fail("initial uninitialized SRAM pixel expected bright white");
  if(auto err = snes2dhd::save_sram(srm_path); !err.empty()) {
    fail("save_sram failed: " + err);
  }
  snes2dhd::unload();

  // Second boot: load saved SRAM before running -> backdrop must now be green (R=0, G>0xC0, B=0).
  if(auto err = snes2dhd::load(rom_path); !err.empty()) {
    fail("reload sram ROM failed: " + err);
    return;
  }
  if(auto err = snes2dhd::load_sram(srm_path); !err.empty()) {
    fail("load_sram failed: " + err);
  }
  const auto& second_frame = snes2dhd::run_frame();
  const std::uint32_t second_pixel = second_frame.pixels[(second_frame.height / 2) * second_frame.width + (second_frame.width / 2)];
  const std::uint8_t r = (second_pixel >> 16) & 0xff;
  const std::uint8_t g = (second_pixel >> 8) & 0xff;
  const std::uint8_t b = second_pixel & 0xff;
  if(!(r == 0 && g > 0xc0 && b == 0)) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "reloaded SRAM pixel expected green, got %08x", second_pixel);
    fail(buf);
  }
  snes2dhd::unload();
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if(argc != 4) {
    std::fputs("usage: test_save_state <min.sfc> <joypad.sfc> <temp_dir>\n", stderr);
    return 2;
  }

  test_unloaded();
  test_determinism_and_corruption(argv[1], argv[2]);
  test_sram_persistence(argv[3]);

  if(failures) {
    std::fprintf(stderr, "save_state: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("save_state: all checks ok\n");
  return 0;
}
