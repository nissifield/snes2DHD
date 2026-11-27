#pragma once

#include <core/core.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace snes2dhd::detail {

inline constexpr std::uint64_t NtscFrameIntervalNs = 16'639'263ULL;  // ~60.0988 fps
inline constexpr std::uint64_t PalFrameIntervalNs  = 19'997'200ULL;  // ~50.0070 fps

inline auto frame_interval_ns(Region region) -> std::uint64_t {
  return region == Region::PAL ? PalFrameIntervalNs : NtscFrameIntervalNs;
}

// Target queued audio buffer (~3 frames of stereo 16-bit PCM).
inline auto target_audio_bytes(Region region, int sample_rate) -> std::uint64_t {
  if(sample_rate <= 0) return 0;
  const std::uint64_t bytes_per_sec = static_cast<std::uint64_t>(sample_rate) * 4ULL;
  const std::uint64_t bytes_per_frame = (bytes_per_sec * frame_interval_ns(region)) / 1'000'000'000ULL;
  return bytes_per_frame * 3ULL;
}

// Computes how long to sleep after presenting a frame. When audio is active,
// skips waiting if queued audio is low (< 1 frame) to prevent underruns and
// applies backpressure when queued audio exceeds the ~3-frame target.
inline auto compute_wait_ns(Region region, std::uint64_t now_ns, std::uint64_t next_deadline_ns,
                            std::optional<int> queued_audio_bytes, int sample_rate) -> std::uint64_t {
  const std::uint64_t interval = frame_interval_ns(region);
  const std::uint64_t clock_wait = next_deadline_ns > now_ns ? (next_deadline_ns - now_ns) : 0ULL;

  if(!queued_audio_bytes.has_value() || sample_rate <= 0) return clock_wait;

  const std::uint64_t bytes_per_sec = static_cast<std::uint64_t>(sample_rate) * 4ULL;
  const std::uint64_t bytes_per_frame = (bytes_per_sec * interval) / 1'000'000'000ULL;
  const std::uint64_t target_bytes = bytes_per_frame * 3ULL;

  if(*queued_audio_bytes < 0 || static_cast<std::uint64_t>(*queued_audio_bytes) < bytes_per_frame) {
    return 0ULL;
  }

  const std::uint64_t queued = static_cast<std::uint64_t>(*queued_audio_bytes);
  if(queued > target_bytes) {
    const std::uint64_t excess = queued - target_bytes;
    const std::uint64_t audio_wait = std::min((excess * 1'000'000'000ULL) / bytes_per_sec, interval * 2ULL);
    return std::max(clock_wait, audio_wait);
  }

  return clock_wait;
}

// Advances the monotonic frame deadline by one region interval, resetting from
// now_ns after a long stall so pausing or dragging the window never bursts.
inline auto advance_deadline_ns(Region region, std::uint64_t now_ns,
                                std::uint64_t next_deadline_ns) -> std::uint64_t {
  const std::uint64_t interval = frame_interval_ns(region);
  if(next_deadline_ns == 0ULL || now_ns > next_deadline_ns + interval * 2ULL) {
    return now_ns + interval;
  }
  return next_deadline_ns + interval;
}

}  // namespace snes2dhd::detail
