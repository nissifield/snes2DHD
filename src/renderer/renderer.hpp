#pragma once

#include <renderer/post_fx.hpp>
#include <scene/scene.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct SDL_GPUDevice;
struct SDL_Window;

namespace snes2dhd::renderer {

struct Mat4 {
  std::array<float, 16> m{};  // column-major

  bool operator==(const Mat4&) const = default;
};

struct Vertex {
  float x = 0.0f, y = 0.0f, z = 0.0f;
  float u = 0.0f, v = 0.0f;
  float nx = 0.0f, ny = 0.0f, nz = 1.0f;

  bool operator==(const Vertex&) const = default;
};

struct LightingUniforms {
  float ambient = 1.0f;
  float dir_x = 0.0f, dir_y = 0.0f, dir_z = -1.0f;
  float bloom = 0.0f;
  float dof = 0.0f;
  float tilt_shift = 0.0f;
  float _pad = 0.0f;

  bool operator==(const LightingUniforms&) const = default;
};

struct RenderBatch {
  Mat4 view_proj;
  LightingUniforms lighting;
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  bool is_stock_2d = true;

  bool operator==(const RenderBatch&) const = default;
};

RenderBatch build_render_batch(const scene::Scene& scene, float viewport_aspect = 4.0f / 3.0f);

class Renderer {
public:
  Renderer() = default;
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  Renderer(Renderer&& other) noexcept;
  Renderer& operator=(Renderer&& other) noexcept;

  // Creates an SDL3 GPU device (and optionally claims a window). Returns false
  // cleanly without a hardware GPU backend (e.g. SDL_VIDEO_DRIVER=offscreen).
  bool init(SDL_Window* window = nullptr, bool debug_mode = false);
  void shutdown();

  bool has_device() const noexcept { return device_ != nullptr; }
  SDL_GPUDevice* device() const noexcept { return device_; }
  const std::string& last_error() const noexcept { return last_error_; }

  const RenderBatch& prepare_batch(const scene::Scene& scene, float viewport_aspect = 4.0f / 3.0f);
  const RenderBatch& last_batch() const noexcept { return last_batch_; }
  bool submit_batch(const RenderBatch& batch);
  const RenderPlan& last_plan() const noexcept { return last_plan_; }

private:
  SDL_GPUDevice* device_ = nullptr;
  SDL_Window* window_ = nullptr;
  RenderBatch last_batch_{};
  RenderPlan last_plan_{};
  std::string last_error_;
};

}  // namespace snes2dhd::renderer
