// Synthetic deterministic tile-hash and local FrameState dump/load tests (zero ROM data).

#include <tools/tile_hash.hpp>

#include <core/frame_state.hpp>
#include <core/tap_adapter.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

int failures = 0;

auto check(const char* label, bool cond) -> void {
  if(cond) return;
  std::fprintf(stderr, "tile_hash: %s failed\n", label);
  failures++;
}

auto is_lower_hex64(std::string_view s) -> bool {
  if(s.size() != 64) return false;
  for(char c : s) {
    if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

auto write_vram_block(snes2dhd::FrameState& frame, std::uint32_t base_addr, std::span<const std::uint8_t> data) -> void {
  for(std::size_t i = 0; i < data.size(); ++i) {
    frame.mem_writes.push_back(snes2dhd::MemWrite{
      0,
      snes2dhd::MemorySpace::Vram,
      base_addr + static_cast<std::uint32_t>(i),
      data[i],
    });
  }
}

struct TempDirGuard {
  std::filesystem::path dir;
  ~TempDirGuard() {
    if(dir.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

}  // namespace

auto main() -> int {
  using namespace snes2dhd;
  using namespace snes2dhd::tools;

  // 1. NIST SHA-256 test vectors (strictly lowercase 64-hex).
  {
    const std::string empty_hash = hash_tile_bytes({});
    check("sha256 empty format", is_lower_hex64(empty_hash));
    check("sha256 empty NIST vector",
          empty_hash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const std::array<std::uint8_t, 3> abc = {'a', 'b', 'c'};
    const std::string abc_hash = hash_tile_bytes(abc);
    check("sha256 abc format", is_lower_hex64(abc_hash));
    check("sha256 abc NIST vector",
          abc_hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const std::string_view multi = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    std::vector<std::uint8_t> multi_bytes(multi.begin(), multi.end());
    const std::string multi_hash = hash_tile_bytes(multi_bytes);
    check("sha256 multi-block format", is_lower_hex64(multi_hash));
    check("sha256 multi-block NIST vector",
          multi_hash == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  }

  // 2. Deterministic 4bpp VRAM reconstruction, sorting, overwrite, zero-skip, and OOB filtering.
  {
    std::array<std::uint8_t, 32> tile_a{};
    std::array<std::uint8_t, 32> tile_b_initial{};
    std::array<std::uint8_t, 32> tile_b_final{};
    std::array<std::uint8_t, 32> tile_cleared{};
    std::array<std::uint8_t, 32> all_zeros{};
    for(std::size_t i = 0; i < 32; ++i) {
      tile_a[i] = static_cast<std::uint8_t>(i + 1u);
      tile_b_initial[i] = static_cast<std::uint8_t>(0x10u + i);
      tile_b_final[i] = static_cast<std::uint8_t>(0xa0u + i);
      tile_cleared[i] = static_cast<std::uint8_t>(0x55u);
    }

    FrameState frame;
    // Non-VRAM writes at tile addresses must be ignored.
    frame.mem_writes.push_back(MemWrite{0, MemorySpace::Oam, 0x0000, 0x7f});
    frame.mem_writes.push_back(MemWrite{0, MemorySpace::Cgram, 0x0020, 0x7f});
    frame.mem_writes.push_back(MemWrite{0, MemorySpace::Wram, 0x0040, 0x7f});

    // Write out of address order: 0x0200, then 0x0040 initial, then 0x0080 (later cleared), then 0x0000.
    write_vram_block(frame, 0x0200, tile_a);
    write_vram_block(frame, 0x0040, tile_b_initial);
    write_vram_block(frame, 0x0080, tile_cleared);
    write_vram_block(frame, 0x0080, all_zeros);  // zero-cleared tile should be skipped
    write_vram_block(frame, 0x00c0, all_zeros);  // zero-written tile should be skipped
    write_vram_block(frame, 0x0000, tile_a);
    // Last write wins for tile 0x0040.
    write_vram_block(frame, 0x0040, tile_b_final);
    // Partial write at aligned tile 0xffe0 (last byte of 64 KiB VRAM) must be included.
    frame.mem_writes.push_back(MemWrite{5, MemorySpace::Vram, 0xffffu, 0x42u});
    // Out-of-range VRAM addresses (>= 65536) must be safely ignored.
    frame.mem_writes.push_back(MemWrite{5, MemorySpace::Vram, 65536u, 0xffu});
    frame.mem_writes.push_back(MemWrite{5, MemorySpace::Vram, 0x20000u, 0xffu});

    std::array<std::uint8_t, 32> expected_last_tile{};
    expected_last_tile[31] = 0x42u;

    const auto tiles = hash_vram_tiles(frame);
    check("4bpp count", tiles.size() == 4);
    if(tiles.size() == 4) {
      check("4bpp sorted[0] addr", tiles[0].vram_addr == 0x0000u);
      check("4bpp sorted[0] bpp", tiles[0].bpp == 4);
      check("4bpp sorted[0] sha256", tiles[0].sha256 == hash_tile_bytes(tile_a));

      check("4bpp sorted[1] addr", tiles[1].vram_addr == 0x0040u);
      check("4bpp sorted[1] bpp", tiles[1].bpp == 4);
      check("4bpp sorted[1] overwrite sha256", tiles[1].sha256 == hash_tile_bytes(tile_b_final));

      check("4bpp sorted[2] addr", tiles[2].vram_addr == 0x0200u);
      check("4bpp sorted[2] equal content hash", tiles[2].sha256 == tiles[0].sha256);

      check("4bpp sorted[3] last VRAM tile addr", tiles[3].vram_addr == 0xffe0u);
      check("4bpp sorted[3] sha256", tiles[3].sha256 == hash_tile_bytes(expected_last_tile));
    }

    // Invalid bpp rejection.
    check("invalid bpp 0", hash_vram_tiles(frame, 0).empty());
    check("invalid bpp 1", hash_vram_tiles(frame, 1).empty());
    check("invalid bpp 3", hash_vram_tiles(frame, 3).empty());
    check("invalid bpp 16", hash_vram_tiles(frame, 16).empty());
  }

  // 3. 2bpp (16 bytes/tile) and 8bpp (64 bytes/tile) tile extraction.
  {
    FrameState frame;
    std::array<std::uint8_t, 16> tile2_a{};
    std::array<std::uint8_t, 16> tile2_b{};
    for(std::size_t i = 0; i < 16; ++i) {
      tile2_a[i] = static_cast<std::uint8_t>(0x20u + i);
      tile2_b[i] = static_cast<std::uint8_t>(0x80u + i);
    }
    write_vram_block(frame, 0x0030, tile2_b);
    write_vram_block(frame, 0x0010, tile2_a);

    const auto tiles2 = hash_vram_tiles(frame, 2);
    check("2bpp count", tiles2.size() == 2);
    if(tiles2.size() == 2) {
      check("2bpp[0] addr", tiles2[0].vram_addr == 0x0010u);
      check("2bpp[0] bpp", tiles2[0].bpp == 2);
      check("2bpp[0] sha256", tiles2[0].sha256 == hash_tile_bytes(tile2_a));
      check("2bpp[1] addr", tiles2[1].vram_addr == 0x0030u);
      check("2bpp[1] bpp", tiles2[1].bpp == 2);
      check("2bpp[1] sha256", tiles2[1].sha256 == hash_tile_bytes(tile2_b));
    }

    FrameState frame8;
    std::array<std::uint8_t, 64> tile8_a{};
    std::array<std::uint8_t, 64> tile8_b{};
    for(std::size_t i = 0; i < 64; ++i) {
      tile8_a[i] = static_cast<std::uint8_t>(0x01u + i);
      tile8_b[i] = static_cast<std::uint8_t>(0x80u + i);
    }
    write_vram_block(frame8, 0x0080, tile8_b);
    write_vram_block(frame8, 0x0000, tile8_a);

    const auto tiles8 = hash_vram_tiles(frame8, 8);
    check("8bpp count", tiles8.size() == 2);
    if(tiles8.size() == 2) {
      check("8bpp[0] addr", tiles8[0].vram_addr == 0x0000u);
      check("8bpp[0] bpp", tiles8[0].bpp == 8);
      check("8bpp[0] sha256", tiles8[0].sha256 == hash_tile_bytes(tile8_a));
      check("8bpp[1] addr", tiles8[1].vram_addr == 0x0080u);
      check("8bpp[1] bpp", tiles8[1].bpp == 8);
      check("8bpp[1] sha256", tiles8[1].sha256 == hash_tile_bytes(tile8_b));
    }
  }

  // 4. Local temporary FrameState dump/load round-trip and error/size-cap rejection.
  {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    TempDirGuard guard{
      std::filesystem::temp_directory_path() / ("snes2dhd_tile_hash_test_" + std::to_string(stamp))
    };
    std::error_code ec;
    check("create temp dir", std::filesystem::create_directories(guard.dir, ec));

    TapAdapter tap;
    tap.begin_frame(101, 256, 224);
    tap.record_reg_write(0, 0x2105, 0x01);
    tap.record_mem_write(4, MemorySpace::Vram, 0x0020, 0x12);
    tap.record_mem_write(4, MemorySpace::Vram, 0x0021, 0x34);
    tap.record_mem_write(8, MemorySpace::Oam, 0x0000, 0x56);
    tap.record_mem_write(9, MemorySpace::Cgram, 0x0002, 0x78);
    tap.record_mem_write(10, MemorySpace::Wram, 0x0010, 0x9a);
    const FrameState captured = tap.finish_frame();

    const std::filesystem::path dump_path = guard.dir / "frame_101.bin";
    check("write dump ok", write_frame_state_dump(dump_path.string(), captured).empty());
    check("no leftover tmp file", !std::filesystem::exists(dump_path.string() + ".tmp", ec));

    FrameState loaded;
    check("read dump ok", read_frame_state_dump(dump_path.string(), loaded).empty());
    check("dump round-trip equality", loaded == captured);
    check("dump round-trip tile hashes", hash_vram_tiles(loaded, 4) == hash_vram_tiles(captured, 4));

    // Atomic overwrite of an existing dump file replaces contents cleanly.
    tap.begin_frame(102, 512, 224);
    tap.record_mem_write(1, MemorySpace::Vram, 0x0040, 0xfe);
    const FrameState updated = tap.finish_frame();
    check("overwrite dump ok", write_frame_state_dump(dump_path.string(), updated).empty());
    FrameState reloaded;
    check("read overwritten dump ok", read_frame_state_dump(dump_path.string(), reloaded).empty());
    check("overwritten dump equality", reloaded == updated);

    // Sentinel must remain untouched on all failed reads.
    FrameState sentinel = captured;

    check("reject empty write path", !write_frame_state_dump("", captured).empty());
    check("reject missing dir write",
          !write_frame_state_dump((guard.dir / "missing_subdir" / "dump.bin").string(), captured).empty());

    check("reject empty read path", !read_frame_state_dump("", sentinel).empty());
    check("reject missing file", !read_frame_state_dump((guard.dir / "missing.bin").string(), sentinel).empty());
    check("reject directory path", !read_frame_state_dump(guard.dir.string(), sentinel).empty());

    const std::filesystem::path empty_path = guard.dir / "empty.bin";
    {
      std::ofstream out(empty_path, std::ios::binary);
    }
    check("reject empty file", !read_frame_state_dump(empty_path.string(), sentinel).empty());

    const std::filesystem::path corrupt_path = guard.dir / "corrupt.bin";
    {
      std::ofstream out(corrupt_path, std::ios::binary);
      const char garbage[] = "NOT_A_VALID_FRAMESTATE_DUMP";
      out.write(garbage, sizeof(garbage) - 1);
    }
    check("reject corrupt file", !read_frame_state_dump(corrupt_path.string(), sentinel).empty());

    const std::filesystem::path huge_path = guard.dir / "oversized.bin";
    {
      std::ofstream out(huge_path, std::ios::binary);
    }
    std::filesystem::resize_file(huge_path, 16ULL * 1024ULL * 1024ULL + 1ULL, ec);
    check("create oversized file", !ec);
    check("reject >16 MiB file", !read_frame_state_dump(huge_path.string(), sentinel).empty());

    check("sentinel untouched after failures", sentinel == captured);
  }

  if(failures) {
    std::fprintf(stderr, "tile_hash: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("tile_hash: all checks ok\n");
  return 0;
}
