#pragma once

#include <profile/profile.hpp>

#include <optional>

namespace snes2dhd::detail {

// Pure A/B toggle state keeping Stock 2D as the default until a profile is loaded and enabled.
struct RenderToggle {
  bool hd2d_enabled = false;
  std::optional<profile::Profile> loaded_profile;

  bool toggle() {
    if(!loaded_profile.has_value()) {
      hd2d_enabled = false;
      return false;
    }
    hd2d_enabled = !hd2d_enabled;
    return hd2d_enabled;
  }

  const std::optional<profile::Profile>& active_profile() const {
    static const std::optional<profile::Profile> none{std::nullopt};
    return (hd2d_enabled && loaded_profile.has_value()) ? loaded_profile : none;
  }

  const char* mode_label() const {
    return (hd2d_enabled && loaded_profile.has_value()) ? "[HD-2D]" : "[Stock 2D]";
  }
};

}  // namespace snes2dhd::detail
