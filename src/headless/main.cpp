#include <core/core.hpp>

#include <nall/hash/sha256.hpp>
#include <nall/string.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

namespace {

auto usage() -> void {
  std::fputs("usage: snes2dhd-headless --frames N <rom>\n", stderr);
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if(argc != 4 || std::strcmp(argv[1], "--frames") != 0) {
    usage();
    return 2;
  }

  char* end = nullptr;
  long frames = std::strtol(argv[2], &end, 10);
  if(end == argv[2] || *end || frames < 1) {
    usage();
    return 2;
  }

  if(auto error = snes2dhd::load(argv[3]); !error.empty()) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }

  const snes2dhd::Frame* frame = nullptr;
  for(long n = 0; n < frames; n++) frame = &snes2dhd::run_frame();

  nall::Hash::SHA256 sha256;
  sha256.input(std::span<const u8>((const u8*)frame->pixels.data(), frame->pixels.size() * sizeof(std::uint32_t)));
  auto digest = sha256.digest();

  std::printf("frame %ld %ux%u sha256=%s\n", frames, frame->width, frame->height, (const char*)digest);
  snes2dhd::unload();
  return 0;
}
