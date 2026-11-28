#pragma once

#include <core/frame_state.hpp>
#include <profile/profile.hpp>
#include <scene/scene.hpp>

#include <optional>

namespace snes2dhd::scene {

Scene build_scene(const FrameState& frame, const std::optional<profile::Profile>& profile = std::nullopt);

}  // namespace snes2dhd::scene
