#include <renderer/post_fx.hpp>
#include <renderer/renderer.hpp>

#include <algorithm>
#include <cmath>

namespace snes2dhd::renderer {
namespace {

auto sanitize_finite(float value, float fallback) -> float {
  return std::isfinite(value) ? value : fallback;
}

auto zero_neg_zero(float value) -> float {
  return value == 0.0f ? 0.0f : value;
}

auto clamp_non_negative(float value, float max_val) -> float {
  return zero_neg_zero(std::clamp(sanitize_finite(value, 0.0f), 0.0f, max_val));
}

}  // namespace

RenderPlan plan_render_passes(const RenderBatch& batch) {
  RenderPlan plan;
  if(batch.is_stock_2d) {
    plan.bypass_post_fx = true;
    plan.passes = {PassKind::StockBlit};
    plan.post_fx = PostFxUniforms{};
    return plan;
  }

  plan.bypass_post_fx = false;
  plan.passes = {PassKind::ShadowMap, PassKind::MainScene};

  const float ambient = zero_neg_zero(std::clamp(sanitize_finite(batch.lighting.ambient, 1.0f), 0.0f, 1.0f));
  const float bloom = clamp_non_negative(batch.lighting.bloom, 10.0f);
  const float dof = clamp_non_negative(batch.lighting.dof, 10.0f);
  const float tilt_shift = clamp_non_negative(batch.lighting.tilt_shift, 1.0f);

  plan.post_fx.shadow_bias = 0.002f;
  // Keep directional shadow contrast active in HD-2D even at full ambient fill.
  plan.post_fx.shadow_strength = std::clamp(1.0f - 0.5f * ambient, 0.25f, 1.0f);

  if(bloom > 0.0f) {
    plan.passes.push_back(PassKind::BloomExtract);
    plan.passes.push_back(PassKind::BloomBlur);
    plan.post_fx.bloom_threshold = 0.8f;
    plan.post_fx.bloom_intensity = bloom;
  }

  if(dof > 0.0f || tilt_shift > 0.0f) {
    plan.passes.push_back(PassKind::DepthOfFieldTiltShift);
    plan.post_fx.dof_focus_depth = 0.5f;
    plan.post_fx.dof_aperture = dof;
    plan.post_fx.tilt_shift_strength = tilt_shift;
  }

  plan.post_fx._pad = 0.0f;
  return plan;
}

}  // namespace snes2dhd::renderer
