#include <core/core.hpp>

#include <frontend/frame_pacer.hpp>
#include <frontend/gamepad_input.hpp>
#include <frontend/keyboard_input.hpp>
#include <frontend/render_toggle.hpp>
#include <profile/profile.hpp>
#include <scene/scene_builder.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::uintmax_t MaxStateFileBytes = 64ULL * 1024ULL * 1024ULL;

auto usage() -> void {
  std::fputs("usage: snes2dhd [--quit-after N] [--profile <path>] <rom>\n", stderr);
}

auto sdl_error(const char* call) -> void {
  std::fprintf(stderr, "%s: %s\n", call, SDL_GetError());
}

auto write_state_atomic(const std::string& path, std::span<const std::uint8_t> bytes) -> std::string {
  const std::filesystem::path tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if(!out) return "cannot open temporary state file: " + tmp.string();
    if(!bytes.empty()) {
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    out.flush();
    if(!out) {
      std::error_code ec;
      std::filesystem::remove(tmp, ec);
      return "failed to write state file: " + tmp.string();
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if(ec) {
    std::error_code rm_ec;
    std::filesystem::remove(tmp, rm_ec);
    return "failed to replace state file " + path + ": " + ec.message();
  }
  return {};
}

auto read_state_file(const std::string& path, std::vector<std::uint8_t>& out) -> std::string {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if(ec) return "cannot stat state file " + path + ": " + ec.message();
  if(size == 0 || size > MaxStateFileBytes) return "state file size out of bounds: " + path;

  std::ifstream in(path, std::ios::binary);
  if(!in) return "cannot open state file: " + path;
  out.resize(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
  if(!in || static_cast<std::uintmax_t>(in.gcount()) != size) {
    out.clear();
    return "failed to read state file: " + path;
  }
  return {};
}

auto open_first_gamepad() -> SDL_Gamepad* {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  if(!ids) return nullptr;
  SDL_Gamepad* pad = nullptr;
  for(int i = 0; i < count && !pad; i++) pad = SDL_OpenGamepad(ids[i]);
  SDL_free(ids);
  return pad;
}

auto read_gamepad(SDL_Gamepad* pad) -> snes2dhd::detail::GamepadSnapshot {
  snes2dhd::detail::GamepadSnapshot snap{};
  if(!pad) return snap;
  snap.south = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
  snap.east = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST);
  snap.west = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST);
  snap.north = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH);
  snap.dpad_up = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP);
  snap.dpad_down = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  snap.dpad_left = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
  snap.dpad_right = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
  snap.left_shoulder = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
  snap.right_shoulder = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
  snap.start = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START);
  snap.back = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
  snap.left_x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
  snap.left_y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
  return snap;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  long quit_after = 0;
  std::optional<std::string> profile_path;
  int arg = 1;
  while(arg < argc && argv[arg][0] == '-') {
    if(std::strcmp(argv[arg], "--quit-after") == 0) {
      if(arg + 1 >= argc) {
        usage();
        return 2;
      }
      char* end = nullptr;
      quit_after = std::strtol(argv[arg + 1], &end, 10);
      if(end == argv[arg + 1] || *end || quit_after < 1) {
        usage();
        return 2;
      }
      arg += 2;
    } else if(std::strcmp(argv[arg], "--profile") == 0) {
      if(arg + 1 >= argc || argv[arg + 1][0] == '\0') {
        usage();
        return 2;
      }
      profile_path = argv[arg + 1];
      arg += 2;
    } else {
      usage();
      return 2;
    }
  }
  if(arg + 1 != argc) {
    usage();
    return 2;
  }

  snes2dhd::detail::RenderToggle render_toggle;
  if(profile_path.has_value()) {
    auto res = snes2dhd::profile::load_profile_file(*profile_path);
    for(const auto& warning : res.warnings) {
      std::fprintf(stderr, "profile warning: %s\n", warning.c_str());
    }
    if(res.profile.has_value()) {
      render_toggle.loaded_profile = std::move(res.profile);
    } else {
      std::fprintf(stderr, "profile load: %s\n", res.error.c_str());
    }
  }

  const std::string rom_path = argv[arg];
  const std::string state_path = rom_path + ".state";
  const std::string srm_path = rom_path + ".srm";

  if(auto error = snes2dhd::load(rom_path); !error.empty()) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }

  if(snes2dhd::has_sram()) {
    std::error_code ec;
    if(std::filesystem::exists(srm_path, ec)) {
      if(auto error = snes2dhd::load_sram(srm_path); !error.empty()) {
        std::fprintf(stderr, "sram load: %s\n", error.c_str());
      }
    }
  }

  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    sdl_error("SDL_Init");
    if(snes2dhd::has_sram()) {
      if(auto error = snes2dhd::save_sram(srm_path); !error.empty()) {
        std::fprintf(stderr, "sram save: %s\n", error.c_str());
      }
    }
    snes2dhd::unload();
    return 1;
  }

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  SDL_AudioStream* audio_stream = nullptr;
  SDL_Gamepad* gamepad = open_first_gamepad();
  bool audio_inited = false;
  std::uint32_t texture_width = 0, texture_height = 0;

  // One exit path, so every failure still tears down SDL, saves SRAM and unloads the core.
  auto finish = [&](int code) {
    if(snes2dhd::has_sram()) {
      if(auto error = snes2dhd::save_sram(srm_path); !error.empty()) {
        std::fprintf(stderr, "sram save: %s\n", error.c_str());
      }
    }
    if(gamepad) SDL_CloseGamepad(gamepad);
    if(audio_stream) SDL_DestroyAudioStream(audio_stream);
    if(audio_inited) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    if(texture) SDL_DestroyTexture(texture);
    if(renderer) SDL_DestroyRenderer(renderer);
    if(window) SDL_DestroyWindow(window);
    snes2dhd::unload();
    SDL_Quit();
    return code;
  };

  auto window_title = [&]() -> std::string {
    return std::string("snes2dhd ") + render_toggle.mode_label();
  };

  if(!SDL_CreateWindowAndRenderer(window_title().c_str(), 768, 672, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
    sdl_error("SDL_CreateWindowAndRenderer");
    return finish(1);
  }

  // Pacing is driven by audio queue + region frame interval, so keep vsync off
  // on high-refresh or PAL displays when the backend allows it.
  (void)SDL_SetRenderVSync(renderer, 0);

  // Audio playback is best-effort so headless/offscreen runs without an audio device still succeed.
  if(SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    audio_inited = true;
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, snes2dhd::AudioSampleRate};
    audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if(audio_stream && !SDL_ResumeAudioStreamDevice(audio_stream)) {
      sdl_error("SDL_ResumeAudioStreamDevice");
      SDL_DestroyAudioStream(audio_stream);
      audio_stream = nullptr;
    }
  }

  const snes2dhd::Region active_region = snes2dhd::region();
  std::uint64_t next_deadline_ns = quit_after == 0
      ? SDL_GetTicksNS() + snes2dhd::detail::frame_interval_ns(active_region)
      : 0ULL;

  long frames = 0;
  for(;;) {
    SDL_Event event;
    bool quit = false;
    while(SDL_PollEvent(&event)) {
      if(event.type == SDL_EVENT_QUIT) {
        quit = true;
      } else if(event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if(event.key.scancode == SDL_SCANCODE_F2) {
          render_toggle.toggle();
          if(!SDL_SetWindowTitle(window, window_title().c_str())) {
            sdl_error("SDL_SetWindowTitle");
          }
        } else if(event.key.key == SDLK_F5) {
          const auto state = snes2dhd::save_state();
          if(state.empty()) {
            std::fputs("save state: core returned empty state\n", stderr);
          } else if(auto error = write_state_atomic(state_path, state); !error.empty()) {
            std::fprintf(stderr, "save state: %s\n", error.c_str());
          }
        } else if(event.key.key == SDLK_F9) {
          std::vector<std::uint8_t> state;
          if(auto error = read_state_file(state_path, state); !error.empty()) {
            std::fprintf(stderr, "load state: %s\n", error.c_str());
          } else if(auto load_err = snes2dhd::load_state(state); !load_err.empty()) {
            std::fprintf(stderr, "load state: %s\n", load_err.c_str());
          }
        }
      } else if(event.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad) {
        gamepad = SDL_OpenGamepad(event.gdevice.which);
      } else if(event.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad &&
                event.gdevice.which == SDL_GetGamepadID(gamepad)) {
        SDL_CloseGamepad(gamepad);
        gamepad = open_first_gamepad();
      }
    }
    if(quit) break;

    // Polling SDL_PollEvent above refreshed keyboard and gamepad state. Only a
    // focused window drives the core, so held inputs cannot survive a focus change.
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool focused = SDL_GetKeyboardFocus() == window;
    const auto pressed = keys ? std::span<const bool>(keys, SDL_SCANCODE_COUNT)
                              : std::span<const bool>();
    const auto pad_snapshot = read_gamepad(gamepad);
    snes2dhd::set_buttons(snes2dhd::detail::keyboard_buttons(pressed, focused) |
                          snes2dhd::detail::gamepad_buttons(pad_snapshot, focused));

    const snes2dhd::Frame& frame = snes2dhd::run_frame();
    // shortcut: SDL 2D presentation remains active until the GPU shader draw pass lands
    const snes2dhd::scene::Scene scene =
        snes2dhd::scene::build_scene(snes2dhd::last_frame_state(), render_toggle.active_profile());
    (void)scene;

    if(audio_stream) {
      const auto samples = snes2dhd::audio_samples();
      const int max_queued = snes2dhd::AudioSampleRate * 2 * (int)sizeof(std::int16_t) / 4;
      if(!samples.empty() && SDL_GetAudioStreamQueued(audio_stream) < max_queued) {
        if(!SDL_PutAudioStreamData(audio_stream, samples.data(), (int)(samples.size() * sizeof(std::int16_t)))) {
          sdl_error("SDL_PutAudioStreamData");
          return finish(1);
        }
      }
    }

    // The SNES can change resolution mid-game, so the texture follows the frame.
    if(!texture || frame.width != texture_width || frame.height != texture_height) {
      if(texture) SDL_DestroyTexture(texture);
      texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  (int)frame.width, (int)frame.height);
      if(!texture) {
        sdl_error("SDL_CreateTexture");
        return finish(1);
      }
      if(!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST)) {
        sdl_error("SDL_SetTextureScaleMode");
        return finish(1);
      }
      if(!SDL_SetRenderLogicalPresentation(renderer, (int)frame.width, (int)frame.height,
                                           SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        sdl_error("SDL_SetRenderLogicalPresentation");
        return finish(1);
      }
      texture_width = frame.width;
      texture_height = frame.height;
    }

    if(!SDL_UpdateTexture(texture, nullptr, frame.pixels.data(), (int)(frame.width * 4))) {
      sdl_error("SDL_UpdateTexture");
      return finish(1);
    }
    if(!SDL_RenderClear(renderer)) {
      sdl_error("SDL_RenderClear");
      return finish(1);
    }
    if(!SDL_RenderTexture(renderer, texture, nullptr, nullptr)) {
      sdl_error("SDL_RenderTexture");
      return finish(1);
    }
    if(!SDL_RenderPresent(renderer)) {
      sdl_error("SDL_RenderPresent");
      return finish(1);
    }

    if(quit_after) {
      if(++frames >= quit_after) break;
    } else {
      const std::uint64_t now_ns = SDL_GetTicksNS();
      const std::optional<int> queued = audio_stream
          ? std::optional<int>(SDL_GetAudioStreamQueued(audio_stream))
          : std::nullopt;
      const std::uint64_t wait_ns = snes2dhd::detail::compute_wait_ns(
          active_region, now_ns, next_deadline_ns, queued, snes2dhd::AudioSampleRate);
      if(wait_ns > 0) SDL_DelayNS(wait_ns);
      const std::uint64_t after_ns = wait_ns > 0 ? SDL_GetTicksNS() : now_ns;
      next_deadline_ns = snes2dhd::detail::advance_deadline_ns(active_region, after_ns, next_deadline_ns);
    }
  }

  return finish(0);
}
