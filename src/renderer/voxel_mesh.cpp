#include <renderer/voxel_mesh.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace snes2dhd::renderer {
namespace {

constexpr std::uint32_t kMaxGridDim = 64u;

struct Vec3f {
  float x;
  float y;
  float z;
};

auto zero_neg_zero(float value) -> float {
  return value == 0.0f ? 0.0f : value;
}

auto append_face(VoxelMesh& mesh,
                 const std::array<Vec3f, 4>& corners,
                 float u,
                 float v,
                 Vec3f normal) -> void {
  const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
  for(const auto& pos : corners) {
    mesh.vertices.push_back(Vertex{
        zero_neg_zero(pos.x),
        zero_neg_zero(pos.y),
        zero_neg_zero(pos.z),
        u,
        v,
        normal.x,
        normal.y,
        normal.z,
    });
  }
  mesh.indices.push_back(base + 0u);
  mesh.indices.push_back(base + 1u);
  mesh.indices.push_back(base + 2u);
  mesh.indices.push_back(base + 0u);
  mesh.indices.push_back(base + 2u);
  mesh.indices.push_back(base + 3u);
}

}  // namespace

VoxelMesh extrude_voxel_grid(std::span<const std::uint8_t> opaque_mask,
                             std::uint32_t width,
                             std::uint32_t height,
                             float extrusion_depth) {
  if(width == 0u || height == 0u || width > kMaxGridDim || height > kMaxGridDim) {
    return {};
  }
  const std::size_t cell_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if(opaque_mask.size() < cell_count) {
    return {};
  }
  if(!std::isfinite(extrusion_depth) || extrusion_depth <= 0.0f) {
    return {};
  }

  std::size_t opaque_count = 0u;
  for(std::size_t i = 0u; i < cell_count; ++i) {
    if(opaque_mask[i] != 0u) ++opaque_count;
  }
  if(opaque_count == 0u) {
    return {};
  }

  auto is_opaque = [&](std::uint32_t x, std::uint32_t y) -> bool {
    return opaque_mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] != 0u;
  };

  VoxelMesh mesh;
  mesh.vertices.reserve(opaque_count * 16u);
  mesh.indices.reserve(opaque_count * 24u);

  const float inv_w = 1.0f / static_cast<float>(width);
  const float inv_h = 1.0f / static_cast<float>(height);
  const float half_z = 0.5f * extrusion_depth;
  const float z_back = -half_z;
  const float z_front = half_z;

  for(std::uint32_t y = 0u; y < height; ++y) {
    // Match image row order top-to-bottom (+Y up, v increases downward).
    const float y_top = 0.5f - static_cast<float>(y) * inv_h;
    const float y_bot = 0.5f - static_cast<float>(y + 1u) * inv_h;
    const float v = (static_cast<float>(y) + 0.5f) * inv_h;

    for(std::uint32_t x = 0u; x < width; ++x) {
      if(!is_opaque(x, y)) continue;

      const float x0 = static_cast<float>(x) * inv_w - 0.5f;
      const float x1 = static_cast<float>(x + 1u) * inv_w - 0.5f;
      const float u = (static_cast<float>(x) + 0.5f) * inv_w;

      // Front (+Z) and back (-Z) caps are always visible for any opaque cell.
      append_face(mesh,
                  {{{x0, y_bot, z_front}, {x1, y_bot, z_front}, {x1, y_top, z_front}, {x0, y_top, z_front}}},
                  u, v, {0.0f, 0.0f, 1.0f});
      append_face(mesh,
                  {{{x1, y_bot, z_back}, {x0, y_bot, z_back}, {x0, y_top, z_back}, {x1, y_top, z_back}}},
                  u, v, {0.0f, 0.0f, -1.0f});

      if(x == 0u || !is_opaque(x - 1u, y)) {
        append_face(mesh,
                    {{{x0, y_bot, z_back}, {x0, y_bot, z_front}, {x0, y_top, z_front}, {x0, y_top, z_back}}},
                    u, v, {-1.0f, 0.0f, 0.0f});
      }
      if(x + 1u == width || !is_opaque(x + 1u, y)) {
        append_face(mesh,
                    {{{x1, y_bot, z_front}, {x1, y_bot, z_back}, {x1, y_top, z_back}, {x1, y_top, z_front}}},
                    u, v, {1.0f, 0.0f, 0.0f});
      }
      if(y == 0u || !is_opaque(x, y - 1u)) {
        append_face(mesh,
                    {{{x0, y_top, z_front}, {x1, y_top, z_front}, {x1, y_top, z_back}, {x0, y_top, z_back}}},
                    u, v, {0.0f, 1.0f, 0.0f});
      }
      if(y + 1u == height || !is_opaque(x, y + 1u)) {
        append_face(mesh,
                    {{{x0, y_bot, z_back}, {x1, y_bot, z_back}, {x1, y_bot, z_front}, {x0, y_bot, z_front}}},
                    u, v, {0.0f, -1.0f, 0.0f});
      }
    }
  }

  return mesh;
}

}  // namespace snes2dhd::renderer
