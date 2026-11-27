// Pure gamepad mapping regression: expected masks are written independently
// of the helper, which never sees SDL's live gamepad state here.

#include <frontend/gamepad_input.hpp>

#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

using snes2dhd::detail::GamepadSnapshot;

int failures = 0;

auto check(const char* label, std::uint16_t expected, std::uint16_t actual) -> void {
  if(expected == actual) return;
  std::fprintf(stderr, "gamepad: %s: %04x != %04x\n", label, actual, expected);
  failures++;
}

}  // namespace

auto main() -> int {
  check("idle", 0x000, snes2dhd::detail::gamepad_buttons(GamepadSnapshot{}, true));

  // One exact expectation per button, B=0x001 .. R=0x800.
  { GamepadSnapshot p{}; p.south = true; check("south->B", 0x001, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.west = true; check("west->Y", 0x002, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.back = true; check("back->Select", 0x004, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.start = true; check("start->Start", 0x008, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.dpad_up = true; check("dpad_up", 0x010, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.dpad_down = true; check("dpad_down", 0x020, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.dpad_left = true; check("dpad_left", 0x040, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.dpad_right = true; check("dpad_right", 0x080, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.east = true; check("east->A", 0x100, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.north = true; check("north->X", 0x200, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.left_shoulder = true; check("left_shoulder->L", 0x400, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.right_shoulder = true; check("right_shoulder->R", 0x800, snes2dhd::detail::gamepad_buttons(p, true)); }

  // Analog left stick deadzone boundaries (+/-16383 ignored, +/-16384 triggers).
  { GamepadSnapshot p{}; p.left_x = 16383; p.left_y = 16383; check("axis +16383 deadzone", 0x000, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.left_x = -16383; p.left_y = -16383; check("axis -16383 deadzone", 0x000, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.left_x = 16384; check("axis +16384->Right", 0x080, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.left_x = -16384; check("axis -16384->Left", 0x040, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.left_y = 16384; check("axis +16384->Down", 0x020, snes2dhd::detail::gamepad_buttons(p, true)); }
  { GamepadSnapshot p{}; p.left_y = -16384; check("axis -16384->Up", 0x010, snes2dhd::detail::gamepad_buttons(p, true)); }
  {
    GamepadSnapshot p{};
    p.left_x = std::numeric_limits<std::int16_t>::min();
    p.left_y = std::numeric_limits<std::int16_t>::max();
    check("axis extremes->Down+Left", 0x060, snes2dhd::detail::gamepad_buttons(p, true));
  }

  // Combined inputs (buttons + stick) and unfocused zero invariant.
  {
    GamepadSnapshot p{};
    p.south = true;
    p.east = true;
    p.left_x = 20000;
    check("combined", 0x181, snes2dhd::detail::gamepad_buttons(p, true));
  }
  {
    GamepadSnapshot all{};
    all.south = all.east = all.west = all.north = true;
    all.dpad_up = all.dpad_down = all.dpad_left = all.dpad_right = true;
    all.left_shoulder = all.right_shoulder = all.start = all.back = true;
    all.left_x = 32767;
    all.left_y = -32768;
    check("all focused", 0xFFF, snes2dhd::detail::gamepad_buttons(all, true));
    check("all unfocused", 0x000, snes2dhd::detail::gamepad_buttons(all, false));
  }

  if(failures) {
    std::fprintf(stderr, "gamepad: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("gamepad: all mappings ok\n");
  return 0;
}
