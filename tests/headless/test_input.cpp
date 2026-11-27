// Controller 1 input regression: drive the fixed keypad mask through real
// emulation and compare every settled frame against a constant-colour ROM.
//
// Usage: test_input <joypad rom> <zero reference rom> <mask>:<reference rom> ...
// Masks are hexadecimal; the Python wrapper generates every reference ROM.

#include <core/core.hpp>

#include <nall/hash/sha256.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr int SettleFrames = 10;

struct Snapshot {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint32_t> pixels;
};

int failures = 0;

auto fail(const std::string& message) -> void {
  std::fprintf(stderr, "input: %s\n", message.c_str());
  failures++;
}

auto hash_frame(const Snapshot& snapshot) -> std::string {
  nall::Hash::SHA256 sha256;
  sha256.input(std::span<const u8>((const u8*)snapshot.pixels.data(),
                                   snapshot.pixels.size() * sizeof(std::uint32_t)));
  return (const char*)sha256.digest();
}

auto snap(const snes2dhd::Frame& frame) -> Snapshot {
  return {frame.width, frame.height, frame.pixels};
}

auto run(int frames) -> const snes2dhd::Frame& {
  const snes2dhd::Frame* frame = nullptr;
  for(int n = 0; n < frames; n++) frame = &snes2dhd::run_frame();
  return *frame;
}

auto load_rom(const std::string& path) -> bool {
  if(auto error = snes2dhd::load(path); !error.empty()) {
    fail("load " + path + ": " + error);
    return false;
  }
  return true;
}

auto candidate(const std::string& rom, std::uint16_t mask) -> Snapshot {
  if(!load_rom(rom)) return {};
  snes2dhd::set_buttons(mask);
  auto frame = snap(run(SettleFrames));
  snes2dhd::unload();
  return frame;
}

// Loads without calling set_buttons, so the default mask after load is what shows.
auto loaded_frame(const std::string& rom) -> Snapshot {
  if(!load_rom(rom)) return {};
  auto frame = snap(run(SettleFrames));
  snes2dhd::unload();
  return frame;
}

auto reference(const std::string& rom) -> Snapshot {
  if(!load_rom(rom)) return {};
  auto frame = snap(run(SettleFrames));
  snes2dhd::unload();
  return frame;
}

auto compare(const std::string& label, const Snapshot& actual, const Snapshot& expected) -> bool {
  if(actual.width != expected.width || actual.height != expected.height) {
    fail(label + ": size " + std::to_string(actual.width) + "x" + std::to_string(actual.height) +
         " != reference " + std::to_string(expected.width) + "x" + std::to_string(expected.height));
    return false;
  }
  for(std::size_t i = 0; i < actual.pixels.size(); i++) {
    if(actual.pixels[i] == expected.pixels[i]) continue;
    char detail[128];
    std::snprintf(detail, sizeof(detail), "pixel %zu %08x != %08x", i, actual.pixels[i], expected.pixels[i]);
    fail(label + ": " + detail);
    return false;
  }
  return true;
}

auto same(const Snapshot& a, const Snapshot& b) -> bool {
  return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
}

auto parse_mask(const std::string& text, std::uint16_t& mask) -> bool {
  char* end = nullptr;
  unsigned long value = std::strtoul(text.c_str(), &end, 16);
  if(end == text.c_str() || *end || value > 0xffff) return false;
  mask = (std::uint16_t)value;
  return true;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if(argc < 3) {
    std::fputs("usage: test_input <joypad rom> <zero reference rom> <mask>:<reference rom> ...\n", stderr);
    return 2;
  }
  const std::string joypad = argv[1];
  const std::string zero_reference = argv[2];
  Snapshot b_reference;
  bool have_b_reference = false;

  // Every supplied state: the mask is applied to the joypad ROM and the settled
  // frame must equal the constant-colour reference for that mask.
  for(int i = 3; i < argc; i++) {
    const std::string argument = argv[i];
    auto colon = argument.find(':');
    std::uint16_t mask = 0;
    if(colon == std::string::npos || !parse_mask(argument.substr(0, colon), mask)) {
      std::fprintf(stderr, "input: bad argument %s\n", argument.c_str());
      return 2;
    }
    const auto actual = candidate(joypad, mask);
    const auto expected = reference(argument.substr(colon + 1));
    if(mask == snes2dhd::ButtonB) {
      b_reference = expected;
      have_b_reference = true;
    }
    if(compare(argument.substr(0, colon), actual, expected))
      std::printf("input mask %04x ok %ux%u sha256=%s\n", mask, actual.width, actual.height,
                  hash_frame(actual).c_str());
  }

  // A fresh load starts with no buttons held.
  if(auto fresh = loaded_frame(joypad); compare("fresh load", fresh, reference(zero_reference)))
    std::printf("input fresh load zero ok\n");

  // A reload must clear the mask and the previous button nodes.
  {
    if(load_rom(joypad)) {
      snes2dhd::set_buttons(0x0fff);
      snes2dhd::unload();
      snes2dhd::set_buttons(0x0fff);  // unloaded call must be a safe no-op
      if(auto reloaded = loaded_frame(joypad); compare("reload clear", reloaded, reference(zero_reference)))
        std::printf("input reload clear ok\n");
    }
  }

  // A live replacement must clear a held mask without an explicit unload.
  {
    if(load_rom(joypad)) {
      snes2dhd::set_buttons(snes2dhd::ButtonB);
      const auto held = snap(run(SettleFrames));
      if(!have_b_reference) fail("replacement needs the wrapper's B reference");
      else if(!same(held, b_reference)) fail("pre-replacement B hold did not match the B reference");

      // No unload(): load() must replace the still-active, B-held ROM.
      if(auto replaced = loaded_frame(joypad);
         compare("replace while held", replaced, reference(zero_reference)))
        std::printf("input replace while held ok\n");
    }
  }

  // A failed replacement while active and B-held must leave a clean mask.
  {
    if(load_rom(joypad)) {
      snes2dhd::set_buttons(snes2dhd::ButtonB);
      const auto held = snap(run(SettleFrames));
      if(!have_b_reference) fail("replacement needs the wrapper's B reference");
      else if(!same(held, b_reference)) fail("pre-replacement B hold did not match the B reference");

      auto error = snes2dhd::load("input-test-missing.sfc");
      if(error.empty()) fail("missing load while held returned success");
      // No set_buttons(): the failed load itself must leave the mask clear.
      if(auto recovered = loaded_frame(joypad);
         compare("recover after failed load while held", recovered, reference(zero_reference)))
        std::printf("input failed load while held ok\n");
    }
  }

  // A failed load must not leave state behind.
  {
    auto error = snes2dhd::load("input-test-missing.sfc");
    if(error.empty()) fail("failed load returned success");
    snes2dhd::set_buttons(0x0fff);
    if(auto recovered = loaded_frame(joypad); compare("failed load clear", recovered, reference(zero_reference)))
      std::printf("input failed load clear ok\n");
  }

  // A held button stays held until release, and release returns to zero.
  {
    if(load_rom(joypad)) {
      snes2dhd::set_buttons(snes2dhd::ButtonB);
      const auto held = snap(run(SettleFrames));
      snes2dhd::set_buttons(snes2dhd::ButtonB);
      const auto repeated = snap(run(SettleFrames));
      snes2dhd::set_buttons(0);
      const auto released = snap(run(SettleFrames));
      snes2dhd::unload();
      if(!same(held, repeated)) fail("repeated hold changed the frame");
      else std::printf("input repeated hold ok\n");
      if(compare("release", released, reference(zero_reference)))
        std::printf("input release ok\n");
    }
  }

  // The same script twice must produce byte-identical frames.
  {
    const auto first = candidate(joypad, snes2dhd::ButtonB | snes2dhd::ButtonA);
    const auto second = candidate(joypad, snes2dhd::ButtonB | snes2dhd::ButtonA);
    if(!same(first, second)) fail("repeat script differed");
    else std::printf("input repeat script ok sha256=%s\n", hash_frame(first).c_str());
  }

  if(failures) {
    std::fprintf(stderr, "input: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("input: all states ok\n");
  return 0;
}
