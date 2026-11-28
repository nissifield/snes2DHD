// Synthetic Renderer geometry, projection, and lighting uniform tests (zero ROM data).

#include <renderer/renderer.hpp>
#include <scene/scene_builder.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

namespace {

int failures = 0;

auto check(const char* label, bool cond) -> void {
  if(cond) return;
  std::fprintf(stderr, "renderer: %s failed\n", label);
  failures++;
}

auto approx(float a, float b) -> bool {
  return std::fabs(a - b) < 1e-4f;
}

auto all_finite(const snes2dhd::renderer::Mat4& mat) -> bool {
  for(float v : mat.m) {
    if(!std::isfinite(v)) return false;
  }
  return true;
}

auto all_finite(const snes2dhd::renderer::Vertex& v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)
      && std::isfinite(v.u) && std::isfinite(v.v)
      && std::isfinite(v.nx) && std::isfinite(v.ny) && std::isfinite(v.nz);
}

auto transform_point(const snes2dhd::renderer::Mat4& m, float x, float y, float z,
                     float& nx, float& ny, float& nz) -> bool {
  const float cx = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
  const float cy = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
  const float cz = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
  const float cw = m.m[3] * x + m.m[7] * y + m.m[11] * z + m.m[15];
  if(!std::isfinite(cw) || std::fabs(cw) < 1e-6f) return false;
  nx = cx / cw;
  ny = cy / cw;
  nz = cz / cw;
  return std::isfinite(nx) && std::isfinite(ny) && std::isfinite(nz);
}

auto vec_len(float x, float y, float z) -> float {
  return std::sqrt(x * x + y * y + z * z);
}

auto check_quad_indices(const snes2dhd::renderer::RenderBatch& batch, std::size_t quad_idx) -> bool {
  const std::size_t vbase = quad_idx * 4u;
  const std::size_t ibase = quad_idx * 6u;
  if(vbase + 4u > batch.vertices.size() || ibase + 6u > batch.indices.size()) return false;
  const std::uint32_t expected[6] = {0u, 1u, 2u, 0u, 2u, 3u};
  for(std::size_t k = 0; k < 6u; ++k) {
    if(batch.indices[ibase + k] != static_cast<std::uint32_t>(vbase) + expected[k]) return false;
    if(batch.indices[ibase + k] >= batch.vertices.size()) return false;
  }
  return true;
}

}  // namespace

auto main() -> int {
  using namespace snes2dhd;
  using namespace snes2dhd::renderer;

  static_assert(sizeof(Mat4) == 64);
  static_assert(sizeof(Vertex) == 32);
  static_assert(sizeof(LightingUniforms) == 32);

  // 1. Stock-2D single-quad batch via Scene Builder fallback.
  {
    FrameState frame;
    const scene::Scene stock = scene::build_scene(frame, std::nullopt);
    const RenderBatch batch = build_render_batch(stock);

    check("stock 2d: is_stock_2d", batch.is_stock_2d);
    check("stock 2d: 4 vertices", batch.vertices.size() == 4);
    check("stock 2d: 6 indices", batch.indices.size() == 6);
    check("stock 2d: CCW indices", check_quad_indices(batch, 0));
    check("stock 2d: finite ortho matrix", all_finite(batch.view_proj)
        && approx(batch.view_proj.m[0], 1.0f) && approx(batch.view_proj.m[5], 1.0f)
        && batch.view_proj.m[10] != 0.0f && approx(batch.view_proj.m[11], 0.0f)
        && approx(batch.view_proj.m[15], 1.0f));
    check("stock 2d: ambient == 1.0", approx(batch.lighting.ambient, 1.0f));
    check("stock 2d: default light dir", approx(batch.lighting.dir_x, 0.0f)
        && approx(batch.lighting.dir_y, 0.0f) && approx(batch.lighting.dir_z, -1.0f));
    check("stock 2d: zero bloom/dof/tilt_shift", approx(batch.lighting.bloom, 0.0f)
        && approx(batch.lighting.dof, 0.0f) && approx(batch.lighting.tilt_shift, 0.0f)
        && approx(batch.lighting._pad, 0.0f));

    if(batch.vertices.size() == 4) {
      for(const auto& v : batch.vertices) {
        check("stock 2d: vertex finite", all_finite(v));
        float nx = 0.0f, ny = 0.0f, nz = 0.0f;
        check("stock 2d: clip-space bounds", transform_point(batch.view_proj, v.x, v.y, v.z, nx, ny, nz)
            && nx >= -1.001f && nx <= 1.001f && ny >= -1.001f && ny <= 1.001f
            && nz >= 0.0f && nz <= 1.0f);
      }
      check("stock 2d: corners",
          approx(batch.vertices[0].x, -1.0f) && approx(batch.vertices[0].y, -1.0f) && approx(batch.vertices[0].z, 0.0f)
          && approx(batch.vertices[2].x, 1.0f) && approx(batch.vertices[2].y, 1.0f)
          && approx(batch.vertices[2].nz, 1.0f));
    }
  }

  // 2. HD-2D multi-plane + billboard batch with perspective, pitch, tilt, and lighting normalization.
  {
    scene::Scene hd;
    hd.is_stock_2d = false;
    hd.width = 256;
    hd.height = 224;
    hd.camera = scene::Camera{42.5f, 30.0f, 14.5f, false};
    hd.lighting = scene::Lighting{0.45f, scene::Vec3{-0.2f, -0.9f, 0.4f}, 0.35f, 0.25f};
    hd.planes = {
      scene::Plane{0, 0.0f, 15.0f, false},
      scene::Plane{1, 2.0f, 0.0f, true},
    };
    hd.billboards = {
      scene::Billboard{2, 64.0f, 56.0f, -0.5f, 32.0f, 28.0f, false},
      scene::Billboard{5, 128.0f, 112.0f, 0.25f, 16.0f, 16.0f, true},
    };

    const RenderBatch batch = build_render_batch(hd, 16.0f / 9.0f);
    const RenderBatch batch_repeat = build_render_batch(hd, 16.0f / 9.0f);
    check("hd2d: deterministic repeat", batch == batch_repeat);
    check("hd2d: !is_stock_2d", !batch.is_stock_2d);
    check("hd2d: 16 vertices", batch.vertices.size() == 16);
    check("hd2d: 24 indices", batch.indices.size() == 24);
    for(std::size_t q = 0; q < 4; ++q) {
      check("hd2d: valid CCW indices", check_quad_indices(batch, q));
    }
    for(const auto& v : batch.vertices) {
      check("hd2d: finite vertex", all_finite(v));
    }

    check("hd2d: finite perspective matrix", all_finite(batch.view_proj)
        && batch.view_proj.m[0] > 0.0f && batch.view_proj.m[5] > 0.0f
        && batch.view_proj.m[11] < 0.0f && approx(batch.view_proj.m[15], 14.5f));
    const RenderBatch narrow = build_render_batch(hd, 4.0f / 3.0f);
    check("hd2d: viewport aspect scales m[0]", batch.view_proj.m[0] < narrow.view_proj.m[0]);

    // Plane 0 tilted 15 deg around X -> rotated unit normal.
    if(batch.vertices.size() == 16) {
      const float rad15 = 15.0f * (3.14159265f / 180.0f);
      check("hd2d: plane 0 rotated unit normal",
          approx(batch.vertices[0].nx, 0.0f)
          && approx(batch.vertices[0].ny, std::sin(rad15))
          && approx(batch.vertices[0].nz, std::cos(rad15))
          && approx(vec_len(batch.vertices[0].nx, batch.vertices[0].ny, batch.vertices[0].nz), 1.0f));
      check("hd2d: plane 0 tilted z offsets",
          approx(batch.vertices[0].z, std::sin(rad15))
          && approx(batch.vertices[2].z, -std::sin(rad15)));
      check("hd2d: plane 1 depth z == -2.0",
          approx(batch.vertices[4].z, -2.0f) && approx(batch.vertices[4].nz, 1.0f));

      // Billboard 0: x=64..96 / 256 -> [-0.5, -0.25], y=56..84 / 224 -> [0.25, 0.5], z = -(-0.5) = +0.5.
      check("hd2d: billboard 0 normalized bounds",
          approx(batch.vertices[8].x, -0.5f) && approx(batch.vertices[8].y, 0.25f)
          && approx(batch.vertices[10].x, -0.25f) && approx(batch.vertices[10].y, 0.5f)
          && approx(batch.vertices[8].z, 0.5f));
    }

    const float l_len = vec_len(batch.lighting.dir_x, batch.lighting.dir_y, batch.lighting.dir_z);
    check("hd2d: normalized light direction", approx(l_len, 1.0f)
        && batch.lighting.dir_x < 0.0f && batch.lighting.dir_y < 0.0f && batch.lighting.dir_z > 0.0f);
    check("hd2d: lighting uniforms",
        approx(batch.lighting.ambient, 0.45f) && approx(batch.lighting.bloom, 0.35f)
        && approx(batch.lighting.dof, 0.25f) && batch.lighting.tilt_shift > 0.0f);
  }

  // 3. Mode 7 active ground plane adds +4 vertices and +6 indices; inactive adds none.
  {
    scene::Scene s;
    s.is_stock_2d = false;
    s.camera = scene::Camera{35.0f, 25.0f, 10.0f, false};
    s.planes = {scene::Plane{1, -2.0f, 0.0f, false}};

    s.mode7 = scene::Mode7GroundPlane{};
    s.mode7->active = false;
    const RenderBatch inactive_batch = build_render_batch(s);
    check("mode7 inactive: 4 vertices", inactive_batch.vertices.size() == 4 && inactive_batch.indices.size() == 6);

    s.mode7->active = true;
    s.mode7->depth = 1.5f;
    s.mode7->tilt_deg = 22.5f;
    const RenderBatch active_batch = build_render_batch(s);
    check("mode7 active: 8 vertices, 12 indices",
        active_batch.vertices.size() == 8 && active_batch.indices.size() == 12);
    check("mode7 active: CCW quad indices", check_quad_indices(active_batch, 1));
    if(active_batch.vertices.size() == 8) {
      const float rad = 22.5f * (3.14159265f / 180.0f);
      const Vertex& m7v = active_batch.vertices[4];
      check("mode7 active: tilted normal and depth",
          approx(m7v.ny, std::sin(rad)) && approx(m7v.nz, std::cos(rad))
          && approx(m7v.z, -1.5f + std::sin(rad)));
    }
  }

  // 4. Zero-length / NaN light direction, bad aspect, and clamped bounds safety.
  {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    scene::Scene bad;
    bad.is_stock_2d = false;
    bad.width = 0;
    bad.height = 0;
    bad.camera = scene::Camera{0.0f, 180.0f, 0.0f, false};
    bad.lighting = scene::Lighting{-0.5f, scene::Vec3{0.0f, 0.0f, 0.0f}, -3.0f, 99.0f};
    bad.planes = {scene::Plane{0, nan, inf, false}};
    bad.billboards = {scene::Billboard{0, 128.0f, 112.0f, nan, 64.0f, 56.0f, false}};

    const RenderBatch safe_zero = build_render_batch(bad, 0.0f);
    const RenderBatch safe_nan = build_render_batch(bad, nan);
    const RenderBatch ref_aspect = build_render_batch(bad, 4.0f / 3.0f);
    check("safety: bad aspect matches 4:3", safe_zero == ref_aspect && safe_nan == ref_aspect);
    check("safety: finite clamped view_proj", all_finite(safe_zero.view_proj));
    check("safety: zero light -> (0,0,-1)", approx(safe_zero.lighting.dir_x, 0.0f)
        && approx(safe_zero.lighting.dir_y, 0.0f) && approx(safe_zero.lighting.dir_z, -1.0f));
    check("safety: clamped ambient/bloom/dof/tilt_shift",
        approx(safe_zero.lighting.ambient, 0.0f) && approx(safe_zero.lighting.bloom, 0.0f)
        && approx(safe_zero.lighting.dof, 10.0f) && approx(safe_zero.lighting.tilt_shift, 1.0f));
    for(const auto& v : safe_zero.vertices) {
      check("safety: finite vertices", all_finite(v));
    }
    if(safe_zero.vertices.size() == 8) {
      // Fallback scene dimensions 256x224 -> (128, 112) maps to (0, 0).
      check("safety: zero scene dimensions fallback 256x224",
          approx(safe_zero.vertices[4].x, 0.0f) && approx(safe_zero.vertices[4].y, -0.5f)
          && approx(safe_zero.vertices[6].x, 0.5f) && approx(safe_zero.vertices[6].y, 0.0f));
    }

    bad.lighting.direction = scene::Vec3{nan, 1.0f, 0.0f};
    bad.lighting.ambient = 2.5f;
    const RenderBatch nan_light = build_render_batch(bad, -2.0f);
    check("safety: NaN light -> (0,0,-1)", approx(nan_light.lighting.dir_x, 0.0f)
        && approx(nan_light.lighting.dir_y, 0.0f) && approx(nan_light.lighting.dir_z, -1.0f));
    check("safety: ambient clamped to 1.0", approx(nan_light.lighting.ambient, 1.0f));

    scene::Scene stock_dof;
    stock_dof.is_stock_2d = true;
    stock_dof.camera.orthographic = true;
    stock_dof.lighting.dof = 0.8f;
    stock_dof.planes = {scene::Plane{0, 0.0f, 0.0f, true}};
    const RenderBatch stock_batch = build_render_batch(stock_dof);
    check("safety: stock 2d forces tilt_shift == 0", approx(stock_batch.lighting.tilt_shift, 0.0f));
  }

  // 5. Renderer offscreen-safe device wrapper and batch preparation.
  {
    Renderer renderer;
    check("renderer wrapper: initially no device", !renderer.has_device() && renderer.device() == nullptr);
    const bool ok = renderer.init(nullptr, false);
    if(!ok) {
      check("renderer wrapper: offscreen init error recorded", !renderer.last_error().empty() && !renderer.has_device());
    } else {
      check("renderer wrapper: device valid", renderer.has_device() && renderer.device() != nullptr);
    }

    scene::Scene s;
    s.planes = {scene::Plane{0, 0.0f, 0.0f, true}};
    const RenderBatch expected = renderer.prepare_batch(s);
    check("renderer wrapper: prepare_batch matches build_render_batch",
        expected == build_render_batch(s) && renderer.last_batch() == expected);

    Renderer moved(std::move(renderer));
    check("renderer wrapper: move preserves batch", moved.last_batch() == expected);
    Renderer assigned;
    assigned = std::move(moved);
    check("renderer wrapper: move-assign preserves batch", assigned.last_batch() == expected);
    assigned.shutdown();
    check("renderer wrapper: shutdown clears device", !assigned.has_device() && assigned.device() == nullptr);
  }

  if(failures) {
    std::fprintf(stderr, "renderer: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("renderer: all checks ok\n");
  return 0;
}
