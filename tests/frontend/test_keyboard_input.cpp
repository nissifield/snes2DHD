// Pure keyboard mapping regression: expected masks are written independently
// of the helper, which never sees SDL's live keyboard state here.

#include <frontend/keyboard_input.hpp>

#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace {

using Keys = std::array<bool, SDL_SCANCODE_COUNT>;

int failures = 0;

auto check(const char* label, std::uint16_t expected, std::uint16_t actual) -> void {
  if(expected == actual) return;
  std::fprintf(stderr, "keyboard: %s: %04x != %04x\n", label, actual, expected);
  failures++;
}

struct Snapshot {
  Keys keys{};

  auto press(SDL_Scancode code) -> Snapshot& {
    keys[(std::size_t)code] = true;
    return *this;
  }

  auto release(SDL_Scancode code) -> Snapshot& {
    keys[(std::size_t)code] = false;
    return *this;
  }

  auto mask(bool focused = true) const -> std::uint16_t {
    return snes2dhd::detail::keyboard_buttons(keys, focused);
  }
};

}  // namespace

auto main() -> int {
  check("idle", 0x000, Snapshot{}.mask());

  // One exact expectation per fixed key, B=0x001 .. R=0x800.
  check("Up", 0x010, Snapshot{}.press(SDL_SCANCODE_UP).mask());
  check("Down", 0x020, Snapshot{}.press(SDL_SCANCODE_DOWN).mask());
  check("Left", 0x040, Snapshot{}.press(SDL_SCANCODE_LEFT).mask());
  check("Right", 0x080, Snapshot{}.press(SDL_SCANCODE_RIGHT).mask());
  check("Z", 0x001, Snapshot{}.press(SDL_SCANCODE_Z).mask());
  check("X", 0x100, Snapshot{}.press(SDL_SCANCODE_X).mask());
  check("A", 0x002, Snapshot{}.press(SDL_SCANCODE_A).mask());
  check("S", 0x200, Snapshot{}.press(SDL_SCANCODE_S).mask());
  check("Q", 0x400, Snapshot{}.press(SDL_SCANCODE_Q).mask());
  check("W", 0x800, Snapshot{}.press(SDL_SCANCODE_W).mask());
  check("Enter", 0x008, Snapshot{}.press(SDL_SCANCODE_RETURN).mask());
  check("Right Shift", 0x004, Snapshot{}.press(SDL_SCANCODE_RSHIFT).mask());

  // Keys that look like mappings but are not.
  check("letter B", 0x000, Snapshot{}.press(SDL_SCANCODE_B).mask());
  check("left shift", 0x000, Snapshot{}.press(SDL_SCANCODE_LSHIFT).mask());
  check("keypad enter", 0x000, Snapshot{}.press(SDL_SCANCODE_KP_ENTER).mask());
  check("space", 0x000, Snapshot{}.press(SDL_SCANCODE_SPACE).mask());
  check("escape", 0x000, Snapshot{}.press(SDL_SCANCODE_ESCAPE).mask());
  check("tab", 0x000, Snapshot{}.press(SDL_SCANCODE_TAB).mask());

  // Independent keys combine, and a short snapshot must not be read past its end.
  check("combined", 0x101, Snapshot{}.press(SDL_SCANCODE_Z).press(SDL_SCANCODE_X).mask());
  check("start+select", 0x00C,
        Snapshot{}.press(SDL_SCANCODE_RETURN).press(SDL_SCANCODE_RSHIFT).mask());
  std::array<bool, 4> short_keys{};
  check("short snapshot", 0x000,
        snes2dhd::detail::keyboard_buttons(short_keys, true));

  // Holding a key is stable; releasing it clears the bit.
  Snapshot held{};
  held.press(SDL_SCANCODE_Z).press(SDL_SCANCODE_UP);
  check("held", 0x011, held.mask());
  check("held again", 0x011, held.mask());
  held.release(SDL_SCANCODE_Z);
  check("one released", 0x010, held.mask());
  held.release(SDL_SCANCODE_UP);
  check("all released", 0x000, held.mask());

  // Unfocused input is zero; regaining focus resumes the keys still held.
  Snapshot pressed{};
  for(auto code : {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
                   SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_A, SDL_SCANCODE_S,
                   SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT}) {
    pressed.press(code);
  }
  check("unfocused", 0x000, pressed.mask(false));
  check("regained", 0xFFF, pressed.mask(true));

  if(failures) {
    std::fprintf(stderr, "keyboard: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("keyboard: all mappings ok\n");
  return 0;
}
