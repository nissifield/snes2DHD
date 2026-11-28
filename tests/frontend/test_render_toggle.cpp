// Pure Stock-2D vs HD-2D toggle state and Scene Builder A/B regression (zero ROM data).

#include <core/frame_state.hpp>
#include <frontend/render_toggle.hpp>
#include <profile/profile.hpp>
#include <scene/scene_builder.hpp>

#include <cstring>
#include <cstdio>
#include <optional>
#include <string>

namespace {

int failures = 0;

auto check(const char* label, bool cond) -> void {
  if(cond) return;
  std::fprintf(stderr, "render_toggle: %s failed\n", label);
  failures++;
}

auto make_synthetic_profile() -> snes2dhd::profile::Profile {
  using namespace snes2dhd::profile;
  Profile p;
  p.game_id = "synthetic-toggle-test";
  p.rom_sha256 = {std::string(64, '0')};

  SceneRule scene;
  scene.id = "synthetic-overworld";
  scene.has_selector = true;
  scene.wram_addr = 0x0100;
  scene.wram_value = 0x05;
  scene.camera = CameraPreset{40.0f, 25.0f, 12.0f};
  scene.layers = {LayerRule{0, RenderMode::Flat, -1.5f, 12.0f}};

  p.scenes = {scene};
  return p;
}

}  // namespace

auto main() -> int {
  using namespace snes2dhd;
  using namespace snes2dhd::detail;

  FrameState frame;
  frame.width = 256;
  frame.height = 224;
  frame.mem_writes.push_back({0, MemorySpace::Wram, 0x0100, 0x05});

  // 1. Default state is Stock 2D and toggle() is a no-op without a loaded profile.
  RenderToggle toggle_state;
  check("default hd2d_enabled is false", !toggle_state.hd2d_enabled);
  check("default loaded_profile empty", !toggle_state.loaded_profile.has_value());
  check("default active_profile empty", !toggle_state.active_profile().has_value());
  check("default mode_label is [Stock 2D]", std::strcmp(toggle_state.mode_label(), "[Stock 2D]") == 0);
  check("toggle without profile returns false", !toggle_state.toggle());
  check("toggle without profile keeps hd2d_enabled false", !toggle_state.hd2d_enabled);
  check("toggle without profile keeps active_profile empty", !toggle_state.active_profile().has_value());
  check("no profile scene is stock 2d", scene::build_scene(frame, toggle_state.active_profile()).is_stock_2d);

  // 2. With a synthetic loaded profile, startup stays Stock 2D and toggles cleanly A/B.
  toggle_state.loaded_profile = make_synthetic_profile();
  check("loaded profile starts in Stock 2D", !toggle_state.hd2d_enabled);
  check("loaded profile startup active_profile is nullopt", !toggle_state.active_profile().has_value());
  check("loaded profile startup mode_label is [Stock 2D]",
      std::strcmp(toggle_state.mode_label(), "[Stock 2D]") == 0);

  const scene::Scene startup_scene = scene::build_scene(frame, toggle_state.active_profile());
  check("startup build_scene returns stock 2d", startup_scene.is_stock_2d && startup_scene.camera.orthographic);

  check("first toggle enables HD-2D", toggle_state.toggle());
  check("hd2d_enabled is true after first toggle", toggle_state.hd2d_enabled);
  check("active_profile populated when HD-2D enabled", toggle_state.active_profile().has_value());
  check("mode_label is [HD-2D]", std::strcmp(toggle_state.mode_label(), "[HD-2D]") == 0);

  const scene::Scene hd_scene = scene::build_scene(frame, toggle_state.active_profile());
  check("HD-2D build_scene is not stock 2d", !hd_scene.is_stock_2d);
  check("HD-2D build_scene matched synthetic scene", hd_scene.active_scene_id == "synthetic-overworld");
  check("HD-2D build_scene uses perspective camera", !hd_scene.camera.orthographic);

  check("second toggle returns to Stock 2D", !toggle_state.toggle());
  check("hd2d_enabled is false after second toggle", !toggle_state.hd2d_enabled);
  check("active_profile empty after second toggle", !toggle_state.active_profile().has_value());
  check("mode_label returns to [Stock 2D]", std::strcmp(toggle_state.mode_label(), "[Stock 2D]") == 0);

  const scene::Scene stock_again = scene::build_scene(frame, toggle_state.active_profile());
  check("toggled-off build_scene returns stock 2d",
      stock_again.is_stock_2d && stock_again.camera.orthographic && stock_again.active_scene_id.empty());

  check("third toggle re-enables HD-2D deterministically",
      toggle_state.toggle() && scene::build_scene(frame, toggle_state.active_profile()) == hd_scene);

  // 3. Invalid/missing profile file leaves safe Stock 2D fallback state.
  const auto missing = profile::load_profile_file("/nonexistent/snes2dhd-missing-profile.json");
  check("missing profile fails without value", !missing.profile.has_value() && !missing.error.empty());

  if(failures) {
    std::fprintf(stderr, "render_toggle: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("render_toggle: all checks ok\n");
  return 0;
}
