#include <tools/tile_hash.hpp>

#include <core/tap_adapter.hpp>

#include <nall/hash/sha256.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace snes2dhd::tools {
namespace {

constexpr std::uint32_t VramBytes = 65536u;
constexpr std::uint32_t MinTileBytes = 16u;
constexpr std::uintmax_t MaxDumpFileBytes = 16ULL * 1024ULL * 1024ULL;

auto bytes_per_tile(std::uint8_t bpp) -> std::uint32_t {
  switch(bpp) {
  case 2: return 16u;
  case 4: return 32u;
  case 8: return 64u;
  default: return 0u;
  }
}

auto has_nonzero_byte(std::span<const std::uint8_t> tile) -> bool {
  for(std::uint8_t b : tile) {
    if(b != 0) return true;
  }
  return false;
}

}  // namespace

std::string hash_tile_bytes(std::span<const std::uint8_t> tile_bytes) {
  static constexpr char HexDigits[] = "0123456789abcdef";
  nall::Hash::SHA256 sha256;
  sha256.input(tile_bytes);
  const std::vector<std::uint8_t> digest = sha256.output();
  std::string out;
  out.reserve(digest.size() * 2u);
  for(std::uint8_t byte : digest) {
    out.push_back(HexDigits[(byte >> 4) & 0x0fu]);
    out.push_back(HexDigits[byte & 0x0fu]);
  }
  return out;
}

std::vector<TileEntry> hash_vram_tiles(const FrameState& frame, std::uint8_t bpp) {
  const std::uint32_t tile_bytes = bytes_per_tile(bpp);
  if(tile_bytes == 0u) return {};

  std::array<std::uint8_t, VramBytes> vram{};
  std::array<std::uint8_t, VramBytes / MinTileBytes> tile_written{};
  for(const auto& write : frame.mem_writes) {
    if(write.space != MemorySpace::Vram || write.addr >= VramBytes) continue;
    vram[static_cast<std::size_t>(write.addr)] = write.value;
    tile_written[static_cast<std::size_t>(write.addr / tile_bytes)] = 1u;
  }

  std::vector<TileEntry> entries;
  const std::uint32_t tile_count = VramBytes / tile_bytes;
  for(std::uint32_t tile_index = 0; tile_index < tile_count; ++tile_index) {
    if(tile_written[static_cast<std::size_t>(tile_index)] == 0u) continue;
    const std::uint32_t vram_addr = tile_index * tile_bytes;
    const auto tile = std::span<const std::uint8_t>(vram.data() + vram_addr, tile_bytes);
    if(!has_nonzero_byte(tile)) continue;
    entries.push_back(TileEntry{vram_addr, bpp, hash_tile_bytes(tile)});
  }
  return entries;
}

std::string write_frame_state_dump(const std::string& path, const FrameState& frame) {
  if(path.empty()) return "dump path must not be empty";

  const std::vector<std::uint8_t> bytes = snes2dhd::serialize_frame_state(frame);
  const std::filesystem::path target(path);
  const std::filesystem::path tmp(path + ".tmp");

  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if(!out) return "cannot open temporary FrameState dump file: " + tmp.string();
    if(!bytes.empty()) {
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    out.flush();
    if(!out) {
      std::error_code ec;
      std::filesystem::remove(tmp, ec);
      return "failed to write FrameState dump file: " + tmp.string();
    }
  }

  std::error_code ec;
  std::filesystem::rename(tmp, target, ec);
  if(ec) {
    std::error_code rm_ec;
    std::filesystem::remove(tmp, rm_ec);
    return "failed to replace FrameState dump file " + path + ": " + ec.message();
  }
  return {};
}

std::string read_frame_state_dump(const std::string& path, FrameState& out) {
  if(path.empty()) return "dump path must not be empty";

  std::error_code ec;
  if(!std::filesystem::is_regular_file(path, ec)) {
    if(ec) return "cannot stat FrameState dump " + path + ": " + ec.message();
    return "FrameState dump is not a regular file: " + path;
  }

  const auto size = std::filesystem::file_size(path, ec);
  if(ec) return "cannot stat FrameState dump " + path + ": " + ec.message();
  if(size == 0 || size > MaxDumpFileBytes) {
    return "FrameState dump size out of bounds: " + path;
  }

  std::ifstream in(path, std::ios::binary);
  if(!in) return "cannot open FrameState dump: " + path;

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
  if(!in || static_cast<std::uintmax_t>(in.gcount()) != size) {
    return "failed to read FrameState dump: " + path;
  }

  std::string error;
  if(!snes2dhd::deserialize_frame_state(bytes, out, error)) {
    return error.empty() ? "invalid FrameState dump: " + path : error;
  }
  return {};
}

}  // namespace snes2dhd::tools
