// Synthetic PPU tap adapter and FrameState serialization round-trip regression.

#include <core/frame_state.hpp>
#include <core/tap_adapter.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

auto check(const char* label, bool cond) -> void {
  if(cond) return;
  std::fprintf(stderr, "frame_state: %s failed\n", label);
  failures++;
}

}  // namespace

auto main() -> int {
  snes2dhd::TapAdapter tap;

  // Frame 1: multi-scanline PPU register writes ($2100-$213F), out-of-range addr ignored,
  // and VRAM/OAM/CGRAM/WRAM writes.
  tap.begin_frame(42, 256, 224);
  tap.record_reg_write(0, 0x20ff, 0xaa);  // ignored (below $2100)
  tap.record_reg_write(0, 0x2100, 0x0f);
  tap.record_reg_write(12, 0x2105, 0x07);  // Mode 7 switch mid-frame
  tap.record_reg_write(120, 0x213f, 0x01);
  tap.record_reg_write(120, 0x2140, 0x55);  // ignored (APU port, above $213F)

  tap.record_mem_write(0, snes2dhd::MemorySpace::Vram, 0x0010, 0x5a);
  tap.record_mem_write(10, snes2dhd::MemorySpace::Oam, 0x0004, 0x80);
  tap.record_mem_write(64, snes2dhd::MemorySpace::Cgram, 0x0002, 0x1f);
  tap.record_mem_write(100, snes2dhd::MemorySpace::Wram, 0x001234, 0x07);

  const snes2dhd::FrameState captured = tap.finish_frame();
  check("schema_version", captured.schema_version == 1);
  check("frame_index", captured.frame_index == 42);
  check("dimensions", captured.width == 256 && captured.height == 224);
  check("reg_writes count", captured.reg_writes.size() == 3);
  check("reg 0", captured.reg_writes[0] == snes2dhd::RegWrite{0, 0x00, 0x0f});
  check("reg 1", captured.reg_writes[1] == snes2dhd::RegWrite{12, 0x05, 0x07});
  check("reg 2", captured.reg_writes[2] == snes2dhd::RegWrite{120, 0x3f, 0x01});
  check("mem_writes count", captured.mem_writes.size() == 4);
  check("mem wram", captured.mem_writes[3] == snes2dhd::MemWrite{100, snes2dhd::MemorySpace::Wram, 0x001234, 0x07});

  // Deterministic serialization and exact round-trip.
  const auto bytes_a = snes2dhd::serialize_frame_state(captured);
  const auto bytes_b = snes2dhd::serialize_frame_state(captured);
  check("deterministic bytes", bytes_a == bytes_b);

  snes2dhd::FrameState restored;
  std::string error;
  check("deserialize valid", snes2dhd::deserialize_frame_state(bytes_a, restored, error));
  check("no error on valid", error.empty());
  check("exact round-trip equality", restored == captured);

  // begin_frame() resets previous writes cleanly.
  tap.begin_frame(43, 512, 448);
  const auto& reset_frame = tap.finish_frame();
  check("reset frame_index", reset_frame.frame_index == 43);
  check("reset dimensions", reset_frame.width == 512 && reset_frame.height == 448);
  check("reset clears reg_writes", reset_frame.reg_writes.empty());
  check("reset clears mem_writes", reset_frame.mem_writes.empty());

  // Corrupt payload rejections leave `out` untouched and report a non-empty error.
  snes2dhd::FrameState sentinel = captured;
  {
    std::vector<std::uint8_t> empty;
    check("reject empty", !snes2dhd::deserialize_frame_state(empty, sentinel, error) && !error.empty());
  }
  {
    auto truncated = bytes_a;
    truncated.pop_back();
    check("reject truncated", !snes2dhd::deserialize_frame_state(truncated, sentinel, error) && !error.empty());
  }
  {
    auto bad_magic = bytes_a;
    bad_magic[0] = 'X';
    check("reject bad magic", !snes2dhd::deserialize_frame_state(bad_magic, sentinel, error) && !error.empty());
  }
  {
    auto bad_version = bytes_a;
    bad_version[4] = 99;
    check("reject bad version", !snes2dhd::deserialize_frame_state(bad_version, sentinel, error) && !error.empty());
  }
  {
    auto huge_count = bytes_a;
    huge_count[20] = 0xff;
    huge_count[21] = 0xff;
    huge_count[22] = 0xff;
    huge_count[23] = 0x7f;
    check("reject huge count", !snes2dhd::deserialize_frame_state(huge_count, sentinel, error) && !error.empty());
  }
  {
    auto bad_reg = bytes_a;
    bad_reg[32 + 2] = 0x40;
    check("reject out-of-range reg", !snes2dhd::deserialize_frame_state(bad_reg, sentinel, error) && !error.empty());
  }
  {
    auto bad_space = bytes_a;
    bad_space[32 + 3 * 4 + 2] = 9;
    check("reject invalid MemorySpace", !snes2dhd::deserialize_frame_state(bad_space, sentinel, error) && !error.empty());
  }
  check("out untouched after failures", sentinel == captured);

  if(failures) {
    std::fprintf(stderr, "frame_state: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("frame_state: all checks ok\n");
  return 0;
}
