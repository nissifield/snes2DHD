#include <renderer/renderer.hpp>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>

namespace snes2dhd::renderer {
namespace {

constexpr float kDefaultAspect = 4.0f / 3.0f;

auto sanitize_finite(float value, float fallback) -> float {
  return std::isfinite(value) ? value : fallback;
}

auto zero_neg_zero(float value) -> float {
  return value == 0.0f ? 0.0f : value;
}

auto deg_to_rad(float deg) -> float {
  return deg * (std::numbers::pi_v<float> / 180.0f);
}

auto mul_mat4(const Mat4& a, const Mat4& b) -> Mat4 {
  Mat4 out{};
  for(std::size_t c = 0; c < 4; ++c) {
    for(std::size_t r = 0; r < 4; ++r) {
      float sum = 0.0f;
      for(std::size_t k = 0; k < 4; ++k) {
        sum += a.m[k * 4 + r] * b.m[c * 4 + k];
      }
      out.m[c * 4 + r] = zero_neg_zero(sum);
    }
  }
  return out;
}

// SDL3 GPU uses [0, 1] clip-space Z; map [-100, 100] world Z so depth-offset layers stay visible.
auto make_ortho_view_proj() -> Mat4 {
  constexpr float z_near = -100.0f;
  constexpr float z_far = 100.0f;
  Mat4 m{};
  m.m[0] = 1.0f;
  m.m[5] = 1.0f;
  m.m[10] = -1.0f / (z_far - z_near);
  m.m[14] = -z_near / (z_far - z_near);
  m.m[15] = 1.0f;
  return m;
}

auto make_perspective_view_proj(const scene::Camera& cam, float viewport_aspect) -> Mat4 {
  const float aspect = (std::isfinite(viewport_aspect) && viewport_aspect > 0.0f)
      ? std::clamp(viewport_aspect, 1e-3f, 1e3f)
      : kDefaultAspect;
  const float fov_deg = std::clamp(sanitize_finite(cam.fov_deg, 35.0f), 5.0f, 120.0f);
  const float pitch_deg = std::clamp(sanitize_finite(cam.pitch_deg, 0.0f), -90.0f, 90.0f);
  const float distance = std::clamp(sanitize_finite(cam.distance, 1.0f), 0.1f, 10000.0f);

  constexpr float z_near = 0.05f;
  const float z_far = std::max(1000.0f, distance * 4.0f);
  const float f = 1.0f / std::tan(deg_to_rad(fov_deg) * 0.5f);

  Mat4 proj{};
  proj.m[0] = f / aspect;
  proj.m[5] = f;
  proj.m[10] = z_far / (z_near - z_far);
  proj.m[11] = -1.0f;
  proj.m[14] = (z_far * z_near) / (z_near - z_far);

  // Orbit camera at (0, d*sin(p), d*cos(p)) looking at origin without lookAt gimbal singularity at +-90 deg.
  const float p = deg_to_rad(pitch_deg);
  const float cp = zero_neg_zero(std::cos(p));
  const float sp = zero_neg_zero(std::sin(p));
  Mat4 view{};
  view.m[0] = 1.0f;
  view.m[5] = cp;
  view.m[6] = sp;
  view.m[9] = zero_neg_zero(-sp);
  view.m[10] = cp;
  view.m[14] = -distance;
  view.m[15] = 1.0f;

  return mul_mat4(proj, view);
}

auto build_lighting_uniforms(const scene::Scene& scene) -> LightingUniforms {
  LightingUniforms out{};
  out.ambient = std::clamp(sanitize_finite(scene.lighting.ambient, 1.0f), 0.0f, 1.0f);
  out.bloom = std::clamp(sanitize_finite(scene.lighting.bloom, 0.0f), 0.0f, 10.0f);
  out.dof = std::clamp(sanitize_finite(scene.lighting.dof, 0.0f), 0.0f, 10.0f);
  out.tilt_shift = (!scene.is_stock_2d && !scene.camera.orthographic)
      ? std::clamp(out.dof, 0.0f, 1.0f)
      : 0.0f;
  out._pad = 0.0f;

  const float dx = scene.lighting.direction.x;
  const float dy = scene.lighting.direction.y;
  const float dz = scene.lighting.direction.z;
  if(std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dz)) {
    const float max_abs = std::max({std::fabs(dx), std::fabs(dy), std::fabs(dz)});
    if(max_abs > 1e-6f) {
      // Scale by max component first so large finite vectors normalize without float square overflow.
      const float sx = dx / max_abs;
      const float sy = dy / max_abs;
      const float sz = dz / max_abs;
      const float len = std::sqrt(sx * sx + sy * sy + sz * sz);
      out.dir_x = zero_neg_zero(sx / len);
      out.dir_y = zero_neg_zero(sy / len);
      out.dir_z = zero_neg_zero(sz / len);
      return out;
    }
  }
  out.dir_x = 0.0f;
  out.dir_y = 0.0f;
  out.dir_z = -1.0f;
  return out;
}

auto append_quad(RenderBatch& batch, float x0, float y0, float x1, float y1,
                 float depth, float tilt_deg) -> void {
  const float safe_depth = std::clamp(sanitize_finite(depth, 0.0f), -10000.0f, 10000.0f);
  const float z_center = zero_neg_zero(-safe_depth);
  const float safe_tilt = std::clamp(sanitize_finite(tilt_deg, 0.0f), -90.0f, 90.0f);
  const float tilt_rad = deg_to_rad(safe_tilt);
  // Positive tilt_deg pitches the top edge away (-Z) so ground planes face an overhead camera (+Y).
  const float c = zero_neg_zero(std::cos(tilt_rad));
  const float s = zero_neg_zero(std::sin(tilt_rad));
  const float ny = s;
  const float nz = c;

  const float y_mid = 0.5f * (y0 + y1);
  auto emit_corner = [&](float x, float y, float u, float v) {
    const float dy = y - y_mid;
    const float ry = zero_neg_zero(y_mid + dy * c);
    const float rz = zero_neg_zero(z_center - dy * s);
    batch.vertices.push_back(Vertex{zero_neg_zero(x), ry, rz, u, v, 0.0f, ny, nz});
  };

  const std::uint32_t base = static_cast<std::uint32_t>(batch.vertices.size());
  emit_corner(x0, y0, 0.0f, 1.0f);
  emit_corner(x1, y0, 1.0f, 1.0f);
  emit_corner(x1, y1, 1.0f, 0.0f);
  emit_corner(x0, y1, 0.0f, 0.0f);

  batch.indices.push_back(base + 0u);
  batch.indices.push_back(base + 1u);
  batch.indices.push_back(base + 2u);
  batch.indices.push_back(base + 0u);
  batch.indices.push_back(base + 2u);
  batch.indices.push_back(base + 3u);
}

}  // namespace

RenderBatch build_render_batch(const scene::Scene& scene, float viewport_aspect) {
  RenderBatch batch;
  batch.is_stock_2d = scene.is_stock_2d;
  batch.view_proj = scene.camera.orthographic
      ? make_ortho_view_proj()
      : make_perspective_view_proj(scene.camera, viewport_aspect);
  batch.lighting = build_lighting_uniforms(scene);

  const std::size_t quad_count = scene.planes.size() + scene.billboards.size()
      + ((scene.mode7.has_value() && scene.mode7->active) ? 1u : 0u);
  batch.vertices.reserve(quad_count * 4u);
  batch.indices.reserve(quad_count * 6u);

  for(const auto& plane : scene.planes) {
    append_quad(batch, -1.0f, -1.0f, 1.0f, 1.0f, plane.depth, plane.tilt_deg);
  }

  const float scene_w = scene.width > 0 ? static_cast<float>(scene.width) : 256.0f;
  const float scene_h = scene.height > 0 ? static_cast<float>(scene.height) : 224.0f;
  for(const auto& bb : scene.billboards) {
    const float bx = sanitize_finite(bb.x, 0.0f);
    const float by = sanitize_finite(bb.y, 0.0f);
    const float bw = std::max(sanitize_finite(bb.width, 0.0f), 0.0f);
    const float bh = std::max(sanitize_finite(bb.height, 0.0f), 0.0f);
    const float x0 = (bx / scene_w) * 2.0f - 1.0f;
    const float x1 = ((bx + bw) / scene_w) * 2.0f - 1.0f;
    const float y_top = 1.0f - (by / scene_h) * 2.0f;
    const float y_bottom = 1.0f - ((by + bh) / scene_h) * 2.0f;
    append_quad(batch, x0, y_bottom, x1, y_top, bb.depth, 0.0f);
  }

  if(scene.mode7.has_value() && scene.mode7->active) {
    append_quad(batch, -1.0f, -1.0f, 1.0f, 1.0f, scene.mode7->depth, scene.mode7->tilt_deg);
  }

  return batch;
}

Renderer::~Renderer() {
  shutdown();
}

Renderer::Renderer(Renderer&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)),
      window_(std::exchange(other.window_, nullptr)),
      last_batch_(std::move(other.last_batch_)),
      last_error_(std::move(other.last_error_)) {}

Renderer& Renderer::operator=(Renderer&& other) noexcept {
  if(this == &other) return *this;
  shutdown();
  device_ = std::exchange(other.device_, nullptr);
  window_ = std::exchange(other.window_, nullptr);
  last_batch_ = std::move(other.last_batch_);
  last_error_ = std::move(other.last_error_);
  return *this;
}

bool Renderer::init(SDL_Window* window, bool debug_mode) {
  shutdown();
  last_error_.clear();

  constexpr SDL_GPUShaderFormat formats =
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL;
  SDL_GPUDevice* dev = SDL_CreateGPUDevice(formats, debug_mode, nullptr);
  if(!dev) {
    const char* err = SDL_GetError();
    last_error_ = (err && *err) ? err : "SDL_CreateGPUDevice failed";
    return false;
  }

  if(window) {
    if(!SDL_ClaimWindowForGPUDevice(dev, window)) {
      const char* err = SDL_GetError();
      last_error_ = (err && *err) ? err : "SDL_ClaimWindowForGPUDevice failed";
      SDL_DestroyGPUDevice(dev);
      return false;
    }
    window_ = window;
  }

  device_ = dev;
  return true;
}

void Renderer::shutdown() {
  if(device_) {
    if(window_) SDL_ReleaseWindowFromGPUDevice(device_, window_);
    SDL_DestroyGPUDevice(device_);
  }
  device_ = nullptr;
  window_ = nullptr;
}

const RenderBatch& Renderer::prepare_batch(const scene::Scene& scene, float viewport_aspect) {
  last_batch_ = build_render_batch(scene, viewport_aspect);
  return last_batch_;
}

}  // namespace snes2dhd::renderer
