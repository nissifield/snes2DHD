#include <core/core.hpp>

#include <frontend/keyboard_input.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

namespace {

auto usage() -> void {
  std::fputs("usage: snes2dhd [--quit-after N] <rom>\n", stderr);
}

auto sdl_error(const char* call) -> void {
  std::fprintf(stderr, "%s: %s\n", call, SDL_GetError());
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

  if(!SDL_Init(SDL_INIT_VIDEO)) {
    sdl_error("SDL_Init");
    snes2dhd::unload();
    return 1;
  }

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  std::uint32_t texture_width = 0, texture_height = 0;

  // One exit path, so every failure still tears down SDL and the core.
  auto finish = [&](int code) {
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

  // shortcut: paced by vsync, wrong speed on non-60 Hz displays; upgrade to audio-driven pacing when audio lands
  if(!SDL_SetRenderVSync(renderer, 1)) {
    sdl_error("SDL_SetRenderVSync");
    return finish(1);
  }

  long frames = 0;
  for(;;) {
    SDL_Event event;
    bool quit = false;
    while(SDL_PollEvent(&event)) {
      if(event.type == SDL_EVENT_QUIT) quit = true;
    }
    if(quit) break;

    // Polling SDL_PollEvent above refreshed the keyboard snapshot. Only a focused
    // window drives the core, so a key held across a focus change cannot stick.
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool focused = SDL_GetKeyboardFocus() == window;
    const auto pressed = keys ? std::span<const bool>(keys, SDL_SCANCODE_COUNT)
                              : std::span<const bool>();
    snes2dhd::set_buttons(snes2dhd::detail::keyboard_buttons(pressed, focused));

    const snes2dhd::Frame& frame = snes2dhd::run_frame();

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

    if(quit_after && ++frames >= quit_after) break;
  }

  return finish(0);
}
