#pragma once

#include <renderer/renderer.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace snes2dhd::renderer {

struct VoxelMesh {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;

  bool operator==(const VoxelMesh&) const = default;
};

VoxelMesh extrude_voxel_grid(std::span<const std::uint8_t> opaque_mask,
                             std::uint32_t width,
                             std::uint32_t height,
                             float extrusion_depth = 1.0f);

}  // namespace snes2dhd::renderer
