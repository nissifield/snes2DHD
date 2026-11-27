#pragma once

#include <core/core.hpp>

#include <SDL3/SDL_scancode.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace snes2dhd::detail {

// Maps the SDL keyboard snapshot to controller 1 buttons; zero while unfocused
// so a held key cannot survive a focus change.
inline auto keyboard_buttons(std::span<const bool> keys, bool focused) -> std::uint16_t {
  if(!focused) return 0;
  auto pressed = [&](SDL_Scancode code) {
    auto index = (std::size_t)code;
    return index < keys.size() && keys[index];
  };

  std::uint16_t mask = 0;
  if(pressed(SDL_SCANCODE_UP)) mask |= ButtonUp;
  if(pressed(SDL_SCANCODE_DOWN)) mask |= ButtonDown;
  if(pressed(SDL_SCANCODE_LEFT)) mask |= ButtonLeft;
  if(pressed(SDL_SCANCODE_RIGHT)) mask |= ButtonRight;
  if(pressed(SDL_SCANCODE_Z)) mask |= ButtonB;
  if(pressed(SDL_SCANCODE_X)) mask |= ButtonA;
  if(pressed(SDL_SCANCODE_A)) mask |= ButtonY;
  if(pressed(SDL_SCANCODE_S)) mask |= ButtonX;
  if(pressed(SDL_SCANCODE_Q)) mask |= ButtonL;
  if(pressed(SDL_SCANCODE_W)) mask |= ButtonR;
  if(pressed(SDL_SCANCODE_RETURN)) mask |= ButtonStart;
  if(pressed(SDL_SCANCODE_RSHIFT)) mask |= ButtonSelect;
  return mask;
}

}  // namespace snes2dhd::detail
