// Synthetic post-FX pass planning and offscreen submit tests (zero ROM data).

#include <renderer/post_fx.hpp>
#include <renderer/renderer.hpp>
#include <scene/scene_builder.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int failures = 0;

auto check(const char* label, bool cond) -> void {
  if(cond) return;
  std::fprintf(stderr, "post_fx: %s failed\n", label);
  failures++;
}

auto approx(float a, float b) -> bool {
  return std::fabs(a - b) < 1e-4f;
}

auto all_finite_non_neg(const snes2dhd::renderer::PostFxUniforms& u) -> bool {
  return std::isfinite(u.bloom_threshold) && u.bloom_threshold >= 0.0f
      && std::isfinite(u.bloom_intensity) && u.bloom_intensity >= 0.0f
      && std::isfinite(u.dof_focus_depth) && u.dof_focus_depth >= 0.0f
      && std::isfinite(u.dof_aperture) && u.dof_aperture >= 0.0f
      && std::isfinite(u.tilt_shift_strength) && u.tilt_shift_strength >= 0.0f
      && std::isfinite(u.shadow_bias) && u.shadow_bias >= 0.0f
      && std::isfinite(u.shadow_strength) && u.shadow_strength >= 0.0f
      && std::isfinite(u._pad) && u._pad == 0.0f;
}

}  // namespace

auto main() -> int {
  using namespace snes2dhd;
  using namespace snes2dhd::renderer;

  static_assert(sizeof(PassKind) == 1);
  static_assert(sizeof(PostFxUniforms) == 32);
  static_assert(std::is_trivially_copyable_v<PostFxUniforms>);

  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  // 1. Stock-2D bypasses all post-FX passes and zeroes intensities.
  {
    FrameState frame;
    const scene::Scene stock = scene::build_scene(frame, std::nullopt);
    const RenderBatch stock_batch = build_render_batch(stock);
    const RenderPlan plan = plan_render_passes(stock_batch);

    check("stock 2d: bypass_post_fx", plan.bypass_post_fx);
    check("stock 2d: single StockBlit pass",
        plan.passes == std::vector<PassKind>{PassKind::StockBlit});
    check("stock 2d: zero intensities",
        all_finite_non_neg(plan.post_fx)
        && approx(plan.post_fx.bloom_intensity, 0.0f)
        && approx(plan.post_fx.dof_aperture, 0.0f)
        && approx(plan.post_fx.tilt_shift_strength, 0.0f)
        && approx(plan.post_fx.shadow_strength, 0.0f));

    // Even if a stock-2D batch carries stray post-FX inputs, stock bypass stays strict.
    RenderBatch dirty_stock = stock_batch;
    dirty_stock.lighting.bloom = 2.0f;
    dirty_stock.lighting.dof = 1.5f;
    dirty_stock.lighting.tilt_shift = 0.8f;
    dirty_stock.lighting.ambient = nan;
    check("stock 2d: ignores stray post-FX inputs", plan_render_passes(dirty_stock) == plan);
  }

  // 2. HD-2D shadow + scene only when bloom, DoF, and tilt-shift are zero.
  {
    scene::Scene hd;
    hd.is_stock_2d = false;
    hd.camera = scene::Camera{35.0f, 25.0f, 10.0f, false};
    hd.lighting = scene::Lighting{0.6f, scene::Vec3{0.2f, -0.8f, 0.5f}, 0.0f, 0.0f};
    const RenderBatch batch = build_render_batch(hd);
    const RenderPlan plan = plan_render_passes(batch);

    check("hd2d base: !bypass_post_fx", !plan.bypass_post_fx);
    check("hd2d base: ShadowMap then MainScene",
        plan.passes == (std::vector<PassKind>{PassKind::ShadowMap, PassKind::MainScene}));
    check("hd2d base: active shadow uniforms",
        all_finite_non_neg(plan.post_fx)
        && plan.post_fx.shadow_bias > 0.0f
        && plan.post_fx.shadow_strength > 0.0f
        && approx(plan.post_fx.bloom_intensity, 0.0f)
        && approx(plan.post_fx.dof_aperture, 0.0f)
        && approx(plan.post_fx.tilt_shift_strength, 0.0f));
  }

  // 3. HD-2D bloom-only appends BloomExtract + BloomBlur after MainScene.
  {
    RenderBatch bloom_only;
    bloom_only.is_stock_2d = false;
    bloom_only.lighting.bloom = 0.45f;
    const RenderPlan plan = plan_render_passes(bloom_only);

    check("hd2d bloom: pass order",
        plan.passes == (std::vector<PassKind>{
            PassKind::ShadowMap, PassKind::MainScene, PassKind::BloomExtract, PassKind::BloomBlur}));
    check("hd2d bloom: uniforms",
        all_finite_non_neg(plan.post_fx)
        && plan.post_fx.bloom_threshold > 0.0f
        && approx(plan.post_fx.bloom_intensity, 0.45f)
        && approx(plan.post_fx.dof_aperture, 0.0f)
        && approx(plan.post_fx.tilt_shift_strength, 0.0f)
        && plan.post_fx.shadow_strength > 0.0f);
  }

  // 4. HD-2D DoF or tilt-shift individually appends DepthOfFieldTiltShift.
  {
    RenderBatch dof_only;
    dof_only.is_stock_2d = false;
    dof_only.lighting.dof = 0.3f;
    const RenderPlan dof_plan = plan_render_passes(dof_only);
    check("hd2d dof-only: pass order",
        dof_plan.passes == (std::vector<PassKind>{
            PassKind::ShadowMap, PassKind::MainScene, PassKind::DepthOfFieldTiltShift}));
    check("hd2d dof-only: uniforms",
        all_finite_non_neg(dof_plan.post_fx)
        && dof_plan.post_fx.dof_focus_depth > 0.0f
        && approx(dof_plan.post_fx.dof_aperture, 0.3f)
        && approx(dof_plan.post_fx.tilt_shift_strength, 0.0f));

    RenderBatch tilt_only;
    tilt_only.is_stock_2d = false;
    tilt_only.lighting.tilt_shift = 0.5f;
    const RenderPlan tilt_plan = plan_render_passes(tilt_only);
    check("hd2d tilt-only: pass order",
        tilt_plan.passes == (std::vector<PassKind>{
            PassKind::ShadowMap, PassKind::MainScene, PassKind::DepthOfFieldTiltShift}));
    check("hd2d tilt-only: uniforms",
        all_finite_non_neg(tilt_plan.post_fx)
        && tilt_plan.post_fx.dof_focus_depth > 0.0f
        && approx(tilt_plan.post_fx.dof_aperture, 0.0f)
        && approx(tilt_plan.post_fx.tilt_shift_strength, 0.5f));
  }

  // 5. HD-2D full pass order (shadows + scene + bloom + DoF/tilt-shift) and determinism.
  {
    scene::Scene hd;
    hd.is_stock_2d = false;
    hd.camera = scene::Camera{40.0f, 30.0f, 12.0f, false};
    hd.lighting = scene::Lighting{0.45f, scene::Vec3{-0.2f, -0.9f, 0.4f}, 0.6f, 0.4f};
    hd.planes = {scene::Plane{0, 0.0f, 15.0f, false}};

    const RenderBatch batch = build_render_batch(hd);
    const RenderPlan plan = plan_render_passes(batch);
    const RenderPlan plan_repeat = plan_render_passes(batch);

    check("hd2d full: deterministic repeat", plan == plan_repeat);
    check("hd2d full: !bypass_post_fx", !plan.bypass_post_fx);
    check("hd2d full: full pass order",
        plan.passes == (std::vector<PassKind>{
            PassKind::ShadowMap,
            PassKind::MainScene,
            PassKind::BloomExtract,
            PassKind::BloomBlur,
            PassKind::DepthOfFieldTiltShift,
        }));
    check("hd2d full: derived uniforms",
        all_finite_non_neg(plan.post_fx)
        && plan.post_fx.bloom_threshold > 0.0f
        && approx(plan.post_fx.bloom_intensity, 0.6f)
        && plan.post_fx.dof_focus_depth > 0.0f
        && approx(plan.post_fx.dof_aperture, 0.4f)
        && approx(plan.post_fx.tilt_shift_strength, 0.4f)
        && plan.post_fx.shadow_bias > 0.0f
        && approx(plan.post_fx.shadow_strength, 0.775f));
  }

  // 6. NaN, Inf, negative, and out-of-range uniform clamping safety.
  {
    RenderBatch bad;
    bad.is_stock_2d = false;
    bad.lighting.ambient = nan;
    bad.lighting.bloom = -2.5f;
    bad.lighting.dof = inf;
    bad.lighting.tilt_shift = -inf;

    const RenderPlan safe_bad = plan_render_passes(bad);
    check("safety: NaN/Inf/neg -> base passes only",
        safe_bad.passes == (std::vector<PassKind>{PassKind::ShadowMap, PassKind::MainScene}));
    check("safety: NaN/Inf/neg uniforms finite non-negative",
        all_finite_non_neg(safe_bad.post_fx)
        && approx(safe_bad.post_fx.bloom_intensity, 0.0f)
        && approx(safe_bad.post_fx.dof_aperture, 0.0f)
        && approx(safe_bad.post_fx.tilt_shift_strength, 0.0f)
        && safe_bad.post_fx.shadow_strength > 0.0f);

    RenderBatch huge;
    huge.is_stock_2d = false;
    huge.lighting.ambient = -10.0f;
    huge.lighting.bloom = 999.0f;
    huge.lighting.dof = 999.0f;
    huge.lighting.tilt_shift = 99.0f;

    const RenderPlan safe_huge = plan_render_passes(huge);
    check("safety: huge positive clamped",
        all_finite_non_neg(safe_huge.post_fx)
        && approx(safe_huge.post_fx.bloom_intensity, 10.0f)
        && approx(safe_huge.post_fx.dof_aperture, 10.0f)
        && approx(safe_huge.post_fx.tilt_shift_strength, 1.0f)
        && approx(safe_huge.post_fx.shadow_strength, 1.0f));
  }

  // 7. Offscreen-safe Renderer::submit_batch and last_plan() recording.
  {
    Renderer uninit;
    RenderBatch hd_batch;
    hd_batch.is_stock_2d = false;
    hd_batch.lighting.bloom = 0.35f;
    hd_batch.lighting.dof = 0.25f;
    hd_batch.lighting.tilt_shift = 0.25f;

    const bool uninit_ok = uninit.submit_batch(hd_batch);
    const RenderPlan expected_hd = plan_render_passes(hd_batch);
    check("submit_batch: uninit returns false", !uninit_ok);
    check("submit_batch: uninit records error", !uninit.last_error().empty());
    check("submit_batch: uninit records last_plan", uninit.last_plan() == expected_hd);

    Renderer moved(std::move(uninit));
    check("submit_batch: move preserves last_plan", moved.last_plan() == expected_hd);

    Renderer assigned;
    assigned = std::move(moved);
    check("submit_batch: move-assign preserves last_plan", assigned.last_plan() == expected_hd);

    // Also test after init(nullptr, false) whether offscreen or GPU-backed.
    (void)assigned.init(nullptr, false);
    RenderBatch stock_batch;
    stock_batch.is_stock_2d = true;
    const bool submitted = assigned.submit_batch(stock_batch);
    check("submit_batch: init state return matches device presence",
        assigned.has_device() ? submitted : (!submitted && !assigned.last_error().empty()));
    check("submit_batch: updates last_plan to stock bypass",
        assigned.last_plan() == plan_render_passes(stock_batch));
  }

  if(failures) {
    std::fprintf(stderr, "post_fx: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("post_fx: all checks ok\n");
  return 0;
}
