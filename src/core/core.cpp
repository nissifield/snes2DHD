#include "core.hpp"

#include <ares/ares.hpp>
#include <sfc/sfc.hpp>

// ares.cpp.in includes these framework sources itself, after ares.hpp.
#include <ares/debug/debug.cpp>
#include <ares/node/node.cpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

// ares expects the frontend to provide these globals; its own ares.cpp.in also
// pulls in generated resources and the gdb server, which the headless build
// does not need.
namespace ares {

Platform* platform = nullptr;
atomic<bool> _runAhead = false;

const string Name = "snes2dhd";
const string Version = "0.0.0";
const string Copyright = "";
const string License = "ISC";
const string LicenseURI = "https://opensource.org/licenses/ISC";
const string Website = "snes2dhd";
const string WebsiteURI = "";
const u32 SerializerSignature = 0x31545342;  // "BST1" (little-endian)

}

namespace snes2dhd {
namespace {

// ares generates this with sourcery from resource.bml. Only the optional
// Justifier/Super Scope crosshairs are used, so empty images are enough.
struct HeadlessPlatform : ares::Platform {
  std::mutex mutex;
  std::condition_variable frame_condition;
  Frame frame;
  bool frame_ready = false;
  std::vector<std::int16_t> audio_samples;
  Region detected_region = Region::NTSC;
  Mapping detected_mapping = Mapping::LoROM;
  std::shared_ptr<nall::vfs::directory> system_pak;
  std::shared_ptr<nall::vfs::directory> cartridge_pak;
  // Controller 1 buttons in ares Gamepad shift order. The mask is only touched
  // from the thread that calls load/set_buttons/run_frame/unload.
  std::array<ares::Node::Input::Button, 12> buttons;
  std::uint16_t button_mask = 0;

  auto pak(ares::Node::Object node) -> std::shared_ptr<nall::vfs::directory> override {
    return node->name() == "Super Famicom" ? system_pak : cartridge_pak;
  }

  auto video(ares::Node::Video::Screen, const u32* data, u32 pitch, u32 width, u32 height) -> void override {
    std::lock_guard lock(mutex);
    frame.width = width;
    frame.height = height;
    frame.pixels.resize((size_t)width * height);
    // pitch is in bytes and may include row padding; Frame has none.
    for(u32 y : range(height)) {
      auto row = (const std::uint32_t*)((const std::uint8_t*)data + (size_t)y * pitch);
      std::copy(row, row + width, frame.pixels.begin() + (size_t)y * width);
    }
    frame_ready = true;
    frame_condition.notify_one();
  }

  auto audio(ares::Node::Audio::Stream stream) -> void override {
    if(stream->channels() == 2) {
      std::array<f64, 2> samples{};
      while(stream->pending()) {
        stream->read(samples.data());
        for(f64 sample : samples) {
          const double scaled = std::clamp(sample * 32768.0, -32768.0, 32767.0);
          audio_samples.push_back((std::int16_t)std::lrint(scaled));
        }
      }
      return;
    }
    std::vector<f64> discard(stream->channels());
    while(stream->pending()) stream->read(discard.data());
  }

  auto input(ares::Node::Input::Input input) -> void override {
    auto button = std::dynamic_pointer_cast<ares::Core::Input::Button>(input);
    if(!button) return;
    // Drive only the nodes this platform owns; Reset and foreign devices stay false.
    for(std::size_t bit = 0; bit < buttons.size(); bit++) {
      if(buttons[bit].get() != button.get()) continue;
      button->setValue(button_mask & (1u << bit));
      return;
    }
    button->setValue(false);
  }
};

std::unique_ptr<HeadlessPlatform> instance;
ares::Node::System root;

struct CartridgeInfo {
  Region region = Region::NTSC;
  Mapping mapping = Mapping::LoROM;
};

constexpr std::size_t HeaderSize = 0x40;
constexpr std::size_t LoROMHeaderOffset = 0x7fc0;
constexpr std::size_t HiROMHeaderOffset = 0xffc0;
constexpr std::size_t ExHiROMHeaderOffset = 0x40ffc0;

auto read_le16(std::span<const u8> data, std::size_t offset) -> std::uint16_t {
  return (std::uint16_t)data[offset] | ((std::uint16_t)data[offset + 1] << 8);
}

auto strip_copier_header(std::span<const u8> rom) -> std::span<const u8> {
  if(rom.size() > 512 && (rom.size() % 1024) == 512) return rom.subspan(512);
  return rom;
}

auto is_pal_region(std::uint8_t code) -> bool {
  return (code >= 0x02 && code <= 0x0c) || code == 0x11;
}

auto valid_ascii_title(std::span<const u8> title) -> bool {
  bool has_visible = false;
  for(u8 byte : title) {
    if(byte == 0x00) continue;
    if(byte < 0x20 || byte > 0x7e) return false;
    if(byte != ' ') has_visible = true;
  }
  return has_visible;
}

auto score_header(std::span<const u8> rom, std::size_t offset, Mapping mapping) -> int {
  if(offset + HeaderSize > rom.size()) return -1;
  const auto h = rom.subspan(offset, HeaderSize);

  int score = 0;
  if((read_le16(h, 0x1c) ^ read_le16(h, 0x1e)) == 0xffff) score += 4;

  const u8 mode = h[0x15];
  const u8 base_mode = mode & 0xef;
  if(mapping == Mapping::LoROM && (base_mode == 0x20 || base_mode == 0x22 || base_mode == 0x23)) {
    score += 2;
  } else if(mapping == Mapping::HiROM && base_mode == 0x21) {
    score += 2;
  } else if(mapping == Mapping::ExHiROM && base_mode == 0x25) {
    score += 3;
  } else if((mode & 0xe0) != 0x20) {
    score -= 2;
  }

  const auto reset = read_le16(h, 0x3c);
  if(reset >= 0x8000 && reset != 0xffff) score += 2;
  else if(reset < 0x8000) score -= 2;

  if(valid_ascii_title(h.subspan(0, 21))) score += 2;
  if(h[0x17] >= 0x05 && h[0x17] <= 0x0d) score += 1;
  if(h[0x19] <= 0x14) score += 1;
  return score;
}

auto detect_cartridge(std::span<const u8> rom) -> CartridgeInfo {
  struct Candidate {
    std::size_t offset;
    Mapping mapping;
  };
  static constexpr Candidate candidates[] = {
    {LoROMHeaderOffset, Mapping::LoROM},
    {HiROMHeaderOffset, Mapping::HiROM},
    {ExHiROMHeaderOffset, Mapping::ExHiROM},
  };

  int best_score = 3;  // require a plausible header before overriding NTSC/LoROM defaults
  std::size_t best_offset = LoROMHeaderOffset;
  Mapping best_mapping = Mapping::LoROM;
  bool found = false;

  for(const auto& candidate : candidates) {
    const int score = score_header(rom, candidate.offset, candidate.mapping);
    if(score > best_score) {
      best_score = score;
      best_offset = candidate.offset;
      best_mapping = candidate.mapping;
      found = true;
    }
  }

  if(!found) return {};
  return {is_pal_region(rom[best_offset + 0x19]) ? Region::PAL : Region::NTSC, best_mapping};
}

auto board_name(Mapping mapping) -> const char* {
  switch(mapping) {
  case Mapping::LoROM: return "LOROM";
  case Mapping::HiROM: return "HIROM";
  case Mapping::ExHiROM: return "EXHIROM";
  }
  return "LOROM";
}

auto make_system_pak() -> std::shared_ptr<nall::vfs::directory> {
  static constexpr char boards[] =
    "database\n"
    "  revision: 2026-11-14\n"
    "board: LOROM\n"
    "  memory type=ROM content=Program\n"
    "    map address=00-7d,80-ff:8000-ffff mask=0x8000\n"
    "board: HIROM\n"
    "  memory type=ROM content=Program\n"
    "    map address=00-3f,80-bf:8000-ffff\n"
    "    map address=40-7d,c0-ff:0000-ffff\n"
    "board: EXHIROM\n"
    "  memory type=ROM content=Program\n"
    "    map address=80-bf:8000-ffff size=0x400000\n"
    "    map address=c0-ff:0000-ffff size=0x400000\n"
    "    map address=00-3f:8000-ffff base=0x400000\n"
    "    map address=40-7d:0000-ffff base=0x400000\n";

  auto pak = std::make_shared<nall::vfs::directory>();
  pak->append("boards.bml", std::span<const u8>((const u8*)boards, sizeof(boards) - 1));
  return pak;
}

auto make_cartridge_pak(std::span<const u8> rom, const CartridgeInfo& info) -> std::shared_ptr<nall::vfs::directory> {
  auto pak = std::make_shared<nall::vfs::directory>();
  pak->setAttribute("title", "snes2dhd");
  pak->setAttribute("region", info.region == Region::PAL ? "PAL" : "NTSC");
  // shortcut: plain ROM mapping (LoROM/HiROM/ExHiROM) only, no coprocessors or save RAM; upgrade when a game needs SRAM or special chips
  pak->setAttribute("board", board_name(info.mapping));
  pak->append("program.rom", rom);
  return pak;
}

}  // namespace

std::string load(const std::string& rom_path) {
  unload();

  std::ifstream file(rom_path, std::ios::binary);
  if(!file) return "cannot open ROM: " + rom_path;
  std::vector<u8> raw_rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if(raw_rom.empty()) return "empty ROM: " + rom_path;

  const auto rom = strip_copier_header(raw_rom);
  const auto info = detect_cartridge(rom);

  instance = std::make_unique<HeadlessPlatform>();
  instance->detected_region = info.region;
  instance->detected_mapping = info.mapping;
  instance->system_pak = make_system_pak();
  instance->cartridge_pak = make_cartridge_pak(rom, info);
  ares::platform = instance.get();

  // Selects the PPU implementation; must run before System::load maps the bus.
  ares::SuperFamicom::option("Pixel Accuracy", "false");

  const char* system_name = info.region == Region::PAL
    ? "[Nintendo] Super Famicom (PAL)"
    : "[Nintendo] Super Famicom (NTSC)";
  if(!ares::SuperFamicom::load(root, system_name)) {
    unload();
    return "ares: failed to load the Super Famicom system";
  }

  auto slot = root->find<ares::Node::Port>("Cartridge Slot");
  if(!slot) {
    unload();
    return "ares: no cartridge slot";
  }
  slot->allocate();
  slot->connect();
  if(!slot->connected()) {
    unload();
    return "ares: failed to connect the cartridge";
  }

  // Fixed controller 1 keyboard input: attach the Gamepad before powering so the
  // unchanged power sequence sees the final node tree.
  auto port = root->find<ares::Node::Port>("Controller Port 1");
  if(!port) {
    unload();
    return "ares: no controller port 1";
  }
  auto gamepad = port->allocate("Gamepad");
  if(!gamepad || port->connected().get() != gamepad.get()) {
    unload();
    return "ares: failed to connect the gamepad";
  }

  static constexpr const char* names[] = {
    "B", "Y", "Select", "Start", "Up", "Down", "Left", "Right", "A", "X", "L", "R",
  };
  for(std::size_t bit = 0; bit < instance->buttons.size(); bit++) {
    auto button = gamepad->find<ares::Node::Input::Button>(names[bit]);
    if(!button) {
      unload();
      return "ares: gamepad is missing a button";
    }
    instance->buttons[bit] = button;
  }

  // System::power seeds from the clock and fills chip RAM before a seed can be
  // set, so run its single power pass here from a fixed seed. Calling it twice
  // would leave one unconsumed Thread entry per chip after the first run.
  for(auto& setting : root->find<ares::Node::Setting::Setting>()) setting->setLatch();
  ares::SuperFamicom::random.entropy(ares::Random::Entropy::Low);
  ares::SuperFamicom::random.seed(ares::Random::Default);
  ares::SuperFamicom::cpu.power(false);
  ares::SuperFamicom::smp.power(false);
  ares::SuperFamicom::dsp.power(false);
  ares::SuperFamicom::ppu.power(false);
  ares::SuperFamicom::cartridge.power(false);
  ares::SuperFamicom::scheduler.power(ares::SuperFamicom::cpu);

  return {};
}

Region region() {
  return instance ? instance->detected_region : Region::NTSC;
}

Mapping mapping() {
  return instance ? instance->detected_mapping : Mapping::LoROM;
}

void set_buttons(std::uint16_t buttons) {
  if(!instance) return;  // nothing loaded: no-op, nothing retained
  instance->button_mask = buttons & 0x0fff;
}

const Frame& run_frame() {
  std::unique_lock lock(instance->mutex);
  instance->frame_ready = false;
  instance->audio_samples.clear();
  root->run();
  instance->frame_condition.wait(lock, [&] { return instance->frame_ready; });
  return instance->frame;
}

std::span<const std::int16_t> audio_samples() {
  if(!instance) return {};
  return {instance->audio_samples.data(), instance->audio_samples.size()};
}

void unload() {
  if(root) root->unload();
  root.reset();
  ares::platform = nullptr;
  instance.reset();
}

}  // namespace snes2dhd
