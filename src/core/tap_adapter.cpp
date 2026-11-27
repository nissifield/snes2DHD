#include <core/tap_adapter.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace snes2dhd {
namespace {

constexpr std::uint8_t Magic[4] = {'S', '2', 'F', 'S'};
constexpr std::size_t HeaderBytes = 32;
constexpr std::size_t RegWriteBytes = 4;
constexpr std::size_t MemWriteBytes = 8;
constexpr std::uint32_t MaxRegWrites = 1'000'000;
constexpr std::uint32_t MaxMemWrites = 4'000'000;

auto append_u16(std::vector<std::uint8_t>& out, std::uint16_t v) -> void {
  out.push_back(static_cast<std::uint8_t>(v & 0xff));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}

auto append_u32(std::vector<std::uint8_t>& out, std::uint32_t v) -> void {
  for(int i = 0; i < 4; i++) out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xff));
}

auto append_u64(std::vector<std::uint8_t>& out, std::uint64_t v) -> void {
  for(int i = 0; i < 8; i++) out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xff));
}

auto read_u16(std::span<const std::uint8_t> in, std::size_t off) -> std::uint16_t {
  return static_cast<std::uint16_t>(in[off])
       | static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[off + 1]) << 8);
}

auto read_u32(std::span<const std::uint8_t> in, std::size_t off) -> std::uint32_t {
  std::uint32_t v = 0;
  for(int i = 0; i < 4; i++) v |= static_cast<std::uint32_t>(in[off + i]) << (i * 8);
  return v;
}

auto read_u64(std::span<const std::uint8_t> in, std::size_t off) -> std::uint64_t {
  std::uint64_t v = 0;
  for(int i = 0; i < 8; i++) v |= static_cast<std::uint64_t>(in[off + i]) << (i * 8);
  return v;
}

}  // namespace

void TapAdapter::begin_frame(std::uint64_t frame_index, std::uint16_t width, std::uint16_t height) {
  state_.schema_version = FrameStateSchemaVersion;
  state_.frame_index = frame_index;
  state_.width = width;
  state_.height = height;
  state_.reg_writes.clear();
  state_.mem_writes.clear();
}

void TapAdapter::record_reg_write(std::uint16_t scanline, std::uint16_t addr, std::uint8_t value) {
  if(addr < 0x2100 || addr > 0x213f) return;
  state_.reg_writes.push_back(RegWrite{scanline, static_cast<std::uint8_t>(addr - 0x2100), value});
}

void TapAdapter::record_mem_write(std::uint16_t scanline, MemorySpace space, std::uint32_t addr, std::uint8_t value) {
  state_.mem_writes.push_back(MemWrite{scanline, space, addr, value});
}

const FrameState& TapAdapter::finish_frame() const {
  return state_;
}

std::vector<std::uint8_t> serialize_frame_state(const FrameState& state) {
  std::vector<std::uint8_t> out;
  out.reserve(HeaderBytes + state.reg_writes.size() * RegWriteBytes + state.mem_writes.size() * MemWriteBytes);

  out.insert(out.end(), Magic, Magic + 4);
  append_u32(out, state.schema_version);
  append_u64(out, state.frame_index);
  append_u16(out, state.width);
  append_u16(out, state.height);
  append_u32(out, static_cast<std::uint32_t>(state.reg_writes.size()));
  append_u32(out, static_cast<std::uint32_t>(state.mem_writes.size()));
  append_u32(out, 0u);

  for(const auto& w : state.reg_writes) {
    append_u16(out, w.scanline);
    out.push_back(w.reg);
    out.push_back(w.value);
  }
  for(const auto& m : state.mem_writes) {
    append_u16(out, m.scanline);
    out.push_back(static_cast<std::uint8_t>(m.space));
    out.push_back(m.value);
    append_u32(out, m.addr);
  }
  return out;
}

bool deserialize_frame_state(std::span<const std::uint8_t> bytes, FrameState& out, std::string& error) {
  error.clear();
  if(bytes.size() < HeaderBytes) {
    error = "truncated FrameState header";
    return false;
  }
  for(std::size_t i = 0; i < 4; i++) {
    if(bytes[i] != Magic[i]) {
      error = "invalid FrameState magic";
      return false;
    }
  }

  const std::uint32_t version = read_u32(bytes, 4);
  if(version != FrameStateSchemaVersion) {
    error = "unsupported FrameState schema_version";
    return false;
  }
  const std::uint64_t frame_index = read_u64(bytes, 8);
  const std::uint16_t width = read_u16(bytes, 16);
  const std::uint16_t height = read_u16(bytes, 18);
  const std::uint32_t reg_count = read_u32(bytes, 20);
  const std::uint32_t mem_count = read_u32(bytes, 24);
  const std::uint32_t reserved = read_u32(bytes, 28);

  if(reserved != 0) {
    error = "non-zero reserved header field";
    return false;
  }
  if(reg_count > MaxRegWrites || mem_count > MaxMemWrites) {
    error = "FrameState write count exceeds safety cap";
    return false;
  }

  const std::uint64_t expected_size = static_cast<std::uint64_t>(HeaderBytes)
      + static_cast<std::uint64_t>(reg_count) * RegWriteBytes
      + static_cast<std::uint64_t>(mem_count) * MemWriteBytes;
  if(expected_size != bytes.size()) {
    error = "FrameState payload length mismatch";
    return false;
  }

  FrameState parsed;
  parsed.schema_version = version;
  parsed.frame_index = frame_index;
  parsed.width = width;
  parsed.height = height;
  parsed.reg_writes.reserve(reg_count);
  parsed.mem_writes.reserve(mem_count);

  std::size_t off = HeaderBytes;
  for(std::uint32_t i = 0; i < reg_count; i++) {
    const std::uint16_t scanline = read_u16(bytes, off);
    const std::uint8_t reg = bytes[off + 2];
    const std::uint8_t value = bytes[off + 3];
    if(reg > 0x3f) {
      error = "FrameState reg index out of $2100-$213F range";
      return false;
    }
    parsed.reg_writes.push_back(RegWrite{scanline, reg, value});
    off += RegWriteBytes;
  }

  for(std::uint32_t i = 0; i < mem_count; i++) {
    const std::uint16_t scanline = read_u16(bytes, off);
    const std::uint8_t raw_space = bytes[off + 2];
    const std::uint8_t value = bytes[off + 3];
    const std::uint32_t addr = read_u32(bytes, off + 4);
    if(raw_space > static_cast<std::uint8_t>(MemorySpace::Wram)) {
      error = "FrameState invalid MemorySpace enum value";
      return false;
    }
    parsed.mem_writes.push_back(MemWrite{scanline, static_cast<MemorySpace>(raw_space), addr, value});
    off += MemWriteBytes;
  }

  out = std::move(parsed);
  return true;
}

}  // namespace snes2dhd
