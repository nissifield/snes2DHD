#include <core/core.hpp>

#include <frontend/frame_pacer.hpp>
#include <frontend/gamepad_input.hpp>
#include <frontend/keyboard_input.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>

namespace {

auto usage() -> void {
  std::fputs("usage: snes2dhd [--quit-after N] <rom>\n", stderr);
}

auto sdl_error(const char* call) -> void {
  std::fprintf(stderr, "%s: %s\n", call, SDL_GetError());
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
  int arg = 1;
  if(argc > 1 && std::strcmp(argv[1], "--quit-after") == 0) {
    if(argc < 4) {
      usage();
      return 2;
    }
    char* end = nullptr;
    quit_after = std::strtol(argv[2], &end, 10);
    if(end == argv[2] || *end || quit_after < 1) {
      usage();
      return 2;
    }
    arg = 3;
  }
  if(argc != arg + 1 || argv[arg][0] == '-') {
    usage();
    return 2;
  }

  if(auto error = snes2dhd::load(argv[arg]); !error.empty()) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }

  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    sdl_error("SDL_Init");
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

  // One exit path, so every failure still tears down SDL and the core.
  auto finish = [&](int code) {
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

  if(!SDL_CreateWindowAndRenderer("snes2DHD", 768, 672, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
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
