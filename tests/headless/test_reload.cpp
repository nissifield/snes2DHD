// In-process load/run/unload regression for the headless lifecycle.
//
// ares registers one Thread::EntryPoint per Thread::create(). A second power
// pass registers another entry for the same handle; after a run that entry is
// never consumed and survives unload. Step the real scheduler and require the
// entry list to be empty once every live thread has been entered.

#include <core/core.hpp>

#include <sfc/sfc.hpp>

#include <nall/hash/sha256.hpp>

#include <cstddef>
#include <cstdio>
#include <span>
#include <string>

namespace {

using ares::SuperFamicom::Thread;

auto entry_count() -> std::size_t {
  return Thread::EntryPoints().size();
}

auto hash_frame(const snes2dhd::Frame& frame) -> std::string {
  nall::Hash::SHA256 sha256;
  sha256.input(std::span<const u8>((const u8*)frame.pixels.data(), frame.pixels.size() * sizeof(std::uint32_t)));
  return (const char*)sha256.digest();
}

auto fail(const std::string& rom, int round, const char* phase, std::size_t entries, std::size_t threads) -> int {
  std::fprintf(stderr, "reload %d %s: %s has %zu entries, %zu live threads\n",
               round, rom.c_str(), phase, entries, threads);
  return 1;
}

auto exercise(const std::string& rom, int round, int frames) -> int {
  if(auto error = snes2dhd::load(rom); !error.empty()) {
    std::fprintf(stderr, "reload %d %s: load failed: %s\n", round, rom.c_str(), error.c_str());
    return 1;
  }

  auto threads = ares::SuperFamicom::scheduler.threads();
  auto loaded = entry_count();
  if(loaded != threads) return fail(rom, round, "load", loaded, threads);

  std::string digest;
  for(int frame = 0; frame < frames; frame++) digest = hash_frame(snes2dhd::run_frame());

  auto ran = entry_count();
  if(ran != 0) return fail(rom, round, "run", ran, threads);

  snes2dhd::unload();
  auto unloaded = entry_count();
  if(unloaded != 0) return fail(rom, round, "unload", unloaded, threads);

  std::printf("reload %d loaded=%zu ran=%zu unloaded=%zu sha256=%s\n",
              round, loaded, ran, unloaded, digest.c_str());
  return 0;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if(argc != 3) {
    std::fputs("usage: test_reload <rom> <rom>\n", stderr);
    return 2;
  }

  static constexpr int Frames = 60;
  static constexpr int Rounds = 3;
  for(int round = 0; round < Rounds; round++) {
    if(auto status = exercise(argv[1], round * 2 + 0, Frames)) return status;
    if(auto status = exercise(argv[2], round * 2 + 1, Frames)) return status;
  }
  return 0;
}
