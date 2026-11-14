#include "core.hpp"

#include <ares/ares.hpp>
#include <sfc/sfc.hpp>

// ares.cpp.in includes these framework sources itself, after ares.hpp.
#include <ares/debug/debug.cpp>
#include <ares/node/node.cpp>

#include <algorithm>
#include <condition_variable>
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
  std::shared_ptr<nall::vfs::directory> system_pak;
  std::shared_ptr<nall::vfs::directory> cartridge_pak;

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
    std::vector<f64> samples(stream->channels());
    while(stream->pending()) stream->read(samples.data());
  }

  auto input(ares::Node::Input::Input input) -> void override {
    if(auto button = std::dynamic_pointer_cast<ares::Core::Input::Button>(input)) {
      button->setValue(false);
    }
  }
};

std::unique_ptr<HeadlessPlatform> instance;
ares::Node::System root;

auto make_system_pak() -> std::shared_ptr<nall::vfs::directory> {
  static constexpr char boards[] =
    "database\n"
    "  revision: 2026-11-14\n"
    "board: LOROM\n"
    "  memory type=ROM content=Program\n"
    "    map address=00-7d,80-ff:8000-ffff mask=0x8000\n";

  auto pak = std::make_shared<nall::vfs::directory>();
  pak->append("boards.bml", std::span<const u8>((const u8*)boards, sizeof(boards) - 1));
  return pak;
}

auto make_cartridge_pak(const std::vector<u8>& rom) -> std::shared_ptr<nall::vfs::directory> {
  auto pak = std::make_shared<nall::vfs::directory>();
  pak->setAttribute("title", "snes2dhd");
  pak->setAttribute("region", "NTSC");
  // shortcut: LoROM only, no coprocessors; upgrade when a test ROM needs HiROM or a coprocessor
  pak->setAttribute("board", "LOROM");
  pak->append("program.rom", std::span<const u8>(rom.data(), rom.size()));
  return pak;
}

}  // namespace

std::string load(const std::string& rom_path) {
  unload();

  std::ifstream file(rom_path, std::ios::binary);
  if(!file) return "cannot open ROM: " + rom_path;
  std::vector<u8> rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if(rom.empty()) return "empty ROM: " + rom_path;

  instance = std::make_unique<HeadlessPlatform>();
  instance->system_pak = make_system_pak();
  instance->cartridge_pak = make_cartridge_pak(rom);
  ares::platform = instance.get();

  // Selects the PPU implementation; must run before System::load maps the bus.
  ares::SuperFamicom::option("Pixel Accuracy", "false");

  if(!ares::SuperFamicom::load(root, "[Nintendo] Super Famicom (NTSC)")) {
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

  root->power();
  // System::power seeds from the clock and fills chip RAM before a seed can be
  // set, so redo the power pass from a fixed seed to make that RAM reproducible.
  ares::SuperFamicom::random.seed(ares::Random::Default);
  ares::SuperFamicom::cpu.power(false);
  ares::SuperFamicom::smp.power(false);
  ares::SuperFamicom::dsp.power(false);
  ares::SuperFamicom::ppu.power(false);
  ares::SuperFamicom::cartridge.power(false);
  ares::SuperFamicom::scheduler.power(ares::SuperFamicom::cpu);

  return {};
}

const Frame& run_frame() {
  std::unique_lock lock(instance->mutex);
  instance->frame_ready = false;
  root->run();
  instance->frame_condition.wait(lock, [&] { return instance->frame_ready; });
  return instance->frame;
}

void unload() {
  if(root) root->unload();
  root.reset();
  ares::platform = nullptr;
  instance.reset();
}

}  // namespace snes2dhd
