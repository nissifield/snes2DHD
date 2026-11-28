#pragma once

#include <cstdint>
#include <vector>

namespace snes2dhd::renderer {

struct RenderBatch;

enum class PassKind : std::uint8_t {
  ShadowMap,
  MainScene,
  BloomExtract,
  BloomBlur,
  DepthOfFieldTiltShift,
  StockBlit,
};

struct PostFxUniforms {
  float bloom_threshold = 1.0f;
  float bloom_intensity = 0.0f;
  float dof_focus_depth = 0.0f;
  float dof_aperture = 0.0f;
  float tilt_shift_strength = 0.0f;
  float shadow_bias = 0.002f;
  float shadow_strength = 0.0f;
  float _pad = 0.0f;

  bool operator==(const PostFxUniforms&) const = default;
};

struct RenderPlan {
  std::vector<PassKind> passes;
  PostFxUniforms post_fx;
  bool bypass_post_fx = true;

  bool operator==(const RenderPlan&) const = default;
};

RenderPlan plan_render_passes(const RenderBatch& batch);

}  // namespace snes2dhd::renderer
