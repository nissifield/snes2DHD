#pragma once

#include <core/core.hpp>

#include <cstdint>

namespace snes2dhd::detail {

inline constexpr std::int16_t GamepadAxisDeadzone = 16384;

struct GamepadSnapshot {
  bool south = false;
  bool east = false;
  bool west = false;
  bool north = false;
  bool dpad_up = false;
  bool dpad_down = false;
  bool dpad_left = false;
  bool dpad_right = false;
  bool left_shoulder = false;
  bool right_shoulder = false;
  bool start = false;
  bool back = false;
  std::int16_t left_x = 0;
  std::int16_t left_y = 0;
};

// Maps a standard SDL gamepad snapshot to controller 1 buttons using the
// physical SNES diamond layout; returns zero while unfocused so held inputs
// cannot survive a focus change.
inline auto gamepad_buttons(const GamepadSnapshot& pad, bool focused) -> std::uint16_t {
  if(!focused) return 0;

  std::uint16_t mask = 0;
  if(pad.south) mask |= ButtonB;
  if(pad.east) mask |= ButtonA;
  if(pad.west) mask |= ButtonY;
  if(pad.north) mask |= ButtonX;
  if(pad.left_shoulder) mask |= ButtonL;
  if(pad.right_shoulder) mask |= ButtonR;
  if(pad.start) mask |= ButtonStart;
  if(pad.back) mask |= ButtonSelect;
  if(pad.dpad_up || pad.left_y <= -GamepadAxisDeadzone) mask |= ButtonUp;
  if(pad.dpad_down || pad.left_y >= GamepadAxisDeadzone) mask |= ButtonDown;
  if(pad.dpad_left || pad.left_x <= -GamepadAxisDeadzone) mask |= ButtonLeft;
  if(pad.dpad_right || pad.left_x >= GamepadAxisDeadzone) mask |= ButtonRight;
  return mask;
}

}  // namespace snes2dhd::detail
