// Pure audio-driven and monotonic frame pacing regression.

#include <frontend/frame_pacer.hpp>

#include <cstdint>
#include <cstdio>
#include <optional>

namespace {

using snes2dhd::Region;
using namespace snes2dhd::detail;

int failures = 0;

auto check_eq(const char* label, std::uint64_t expected, std::uint64_t actual) -> void {
  if(expected == actual) return;
  std::fprintf(stderr, "frame_pacer: %s: %llu != %llu\n", label,
               (unsigned long long)actual, (unsigned long long)expected);
  failures++;
}

auto check_true(const char* label, bool cond) -> void {
  if(cond) return;
  std::fprintf(stderr, "frame_pacer: %s failed\n", label);
  failures++;
}

}  // namespace

auto main() -> int {
  // Exact NTSC (~60.0988 Hz) and PAL (~50.0070 Hz) intervals.
  check_eq("NTSC interval", 16'639'263ULL, frame_interval_ns(Region::NTSC));
  check_eq("PAL interval", 19'997'200ULL, frame_interval_ns(Region::PAL));

  // Monotonic fallback when audio is unavailable (std::nullopt).
  check_eq("NTSC monotonic wait", 10'000'000ULL,
           compute_wait_ns(Region::NTSC, 6'639'263ULL, 16'639'263ULL, std::nullopt, 48000));
  check_eq("NTSC monotonic overdue", 0ULL,
           compute_wait_ns(Region::NTSC, 20'000'000ULL, 16'639'263ULL, std::nullopt, 48000));
  check_eq("PAL monotonic wait", 15'000'000ULL,
           compute_wait_ns(Region::PAL, 4'997'200ULL, 19'997'200ULL, std::nullopt, 48000));

  // Audio active: low queue (< 1 frame) skips wait to avoid underrun.
  check_eq("audio underrun zero", 0ULL,
           compute_wait_ns(Region::NTSC, 1'000'000ULL, 16'639'263ULL, 0, 48000));
  check_eq("audio low queue", 0ULL,
           compute_wait_ns(Region::NTSC, 1'000'000ULL, 16'639'263ULL, 2000, 48000));
  check_eq("audio error negative", 0ULL,
           compute_wait_ns(Region::NTSC, 1'000'000ULL, 16'639'263ULL, -1, 48000));

  // Audio active: nominal queue (between 1 and 3 frames) follows clock deadline.
  const int ntsc_target = static_cast<int>(target_audio_bytes(Region::NTSC, 48000));
  const int pal_target = static_cast<int>(target_audio_bytes(Region::PAL, 48000));
  check_true("PAL target > NTSC target", pal_target > ntsc_target);
  check_eq("audio nominal clock wait", 8'000'000ULL,
           compute_wait_ns(Region::NTSC, 8'639'263ULL, 16'639'263ULL, ntsc_target - 1000, 48000));

  // Audio active: backpressure wait when queued bytes exceed target even if deadline passed.
  const std::uint64_t excess_wait =
      compute_wait_ns(Region::NTSC, 20'000'000ULL, 16'639'263ULL, ntsc_target + 3840, 48000);
  check_eq("audio excess 20ms backpressure", 20'000'000ULL, excess_wait);
  const std::uint64_t clamped_wait =
      compute_wait_ns(Region::NTSC, 20'000'000ULL, 16'639'263ULL, ntsc_target + 192000, 48000);
  check_eq("audio excess clamped to 2 frames", 16'639'263ULL * 2ULL, clamped_wait);

  // Deadline advancement: normal step vs long-stall reset (no catch-up burst).
  check_eq("initial deadline", 100'000'000ULL + 16'639'263ULL,
           advance_deadline_ns(Region::NTSC, 100'000'000ULL, 0ULL));
  check_eq("steady NTSC step", 33'278'526ULL,
           advance_deadline_ns(Region::NTSC, 16'639'263ULL, 16'639'263ULL));
  check_eq("steady PAL step", 39'994'400ULL,
           advance_deadline_ns(Region::PAL, 19'997'200ULL, 19'997'200ULL));
  check_eq("stall reset prevents burst", 500'000'000ULL + 16'639'263ULL,
           advance_deadline_ns(Region::NTSC, 500'000'000ULL, 16'639'263ULL));

  if(failures) {
    std::fprintf(stderr, "frame_pacer: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("frame_pacer: all checks ok\n");
  return 0;
}
