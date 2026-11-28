// Synthetic voxel extrusion mesh builder tests (zero ROM data).

#include <renderer/voxel_mesh.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

int failures = 0;

auto check(const char* label, bool cond) -> void {
  if(cond) return;
  std::fprintf(stderr, "voxel_mesh: %s failed\n", label);
  failures++;
}

auto approx(float a, float b) -> bool {
  return std::fabs(a - b) < 1e-4f;
}

auto all_finite(const snes2dhd::renderer::Vertex& v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)
      && std::isfinite(v.u) && std::isfinite(v.v)
      && std::isfinite(v.nx) && std::isfinite(v.ny) && std::isfinite(v.nz);
}

auto validate_mesh_invariants(const char* label, const snes2dhd::renderer::VoxelMesh& mesh) -> void {
  check(label, mesh.vertices.size() % 4u == 0u && mesh.indices.size() % 6u == 0u);
  check(label, mesh.indices.size() / 6u == mesh.vertices.size() / 4u);

  for(const auto& v : mesh.vertices) {
    if(!all_finite(v)) {
      check(label, false);
      return;
    }
    if(v.u < 0.0f || v.u > 1.0f || v.v < 0.0f || v.v > 1.0f) {
      check(label, false);
      return;
    }
    const float n_len_sq = v.nx * v.nx + v.ny * v.ny + v.nz * v.nz;
    if(!approx(n_len_sq, 1.0f)) {
      check(label, false);
      return;
    }
  }

  for(std::size_t f = 0u; f < mesh.indices.size(); f += 6u) {
    const std::uint32_t base = static_cast<std::uint32_t>((f / 6u) * 4u);
    const std::uint32_t expected[6] = {base + 0u, base + 1u, base + 2u, base + 0u, base + 2u, base + 3u};
    for(std::size_t k = 0u; k < 6u; ++k) {
      if(mesh.indices[f + k] != expected[k] || mesh.indices[f + k] >= mesh.vertices.size()) {
        check(label, false);
        return;
      }
    }
  }

  for(std::size_t t = 0u; t < mesh.indices.size(); t += 3u) {
    const auto& p0 = mesh.vertices[mesh.indices[t + 0u]];
    const auto& p1 = mesh.vertices[mesh.indices[t + 1u]];
    const auto& p2 = mesh.vertices[mesh.indices[t + 2u]];

    const float e1x = p1.x - p0.x;
    const float e1y = p1.y - p0.y;
    const float e1z = p1.z - p0.z;
    const float e2x = p2.x - p0.x;
    const float e2y = p2.y - p0.y;
    const float e2z = p2.z - p0.z;

    const float cx = e1y * e2z - e1z * e2y;
    const float cy = e1z * e2x - e1x * e2z;
    const float cz = e1x * e2y - e1y * e2x;
    const float c_len = std::sqrt(cx * cx + cy * cy + cz * cz);
    if(!std::isfinite(c_len) || c_len <= 1e-8f) {
      check(label, false);
      return;
    }

    const float ux = cx / c_len;
    const float uy = cy / c_len;
    const float uz = cz / c_len;
    for(std::size_t k = 0u; k < 3u; ++k) {
      const auto& pv = mesh.vertices[mesh.indices[t + k]];
      const float dot = ux * pv.nx + uy * pv.ny + uz * pv.nz;
      if(!approx(dot, 1.0f)) {
        check(label, false);
        return;
      }
    }
  }
}

auto count_normal(const snes2dhd::renderer::VoxelMesh& mesh, float nx, float ny, float nz) -> std::size_t {
  std::size_t faces = 0u;
  for(std::size_t i = 0u; i < mesh.vertices.size(); i += 4u) {
    const auto& v = mesh.vertices[i];
    if(approx(v.nx, nx) && approx(v.ny, ny) && approx(v.nz, nz)) {
      ++faces;
    }
  }
  return faces;
}

}  // namespace

auto main() -> int {
  using namespace snes2dhd::renderer;

  static_assert(sizeof(Vertex) == 32);

  // 1. Bounds, empty, transparent, undersized, oversized, and bad-depth safety.
  {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<std::uint8_t> one{1u};
    const std::vector<std::uint8_t> sixteen_zero(16u, 0u);
    const std::vector<std::uint8_t> fifteen_one(15u, 1u);
    const std::vector<std::uint8_t> oversized(65u, 1u);

    check("safety: empty mask 0x0", extrude_voxel_grid({}, 0u, 0u).vertices.empty());
    check("safety: empty mask 1x1", extrude_voxel_grid({}, 1u, 1u).vertices.empty());
    check("safety: width == 0", extrude_voxel_grid(one, 0u, 1u).vertices.empty());
    check("safety: height == 0", extrude_voxel_grid(one, 1u, 0u).vertices.empty());
    check("safety: width == 65", extrude_voxel_grid(oversized, 65u, 1u).vertices.empty());
    check("safety: height == 65", extrude_voxel_grid(oversized, 1u, 65u).vertices.empty());
    check("safety: undersized mask", extrude_voxel_grid(fifteen_one, 4u, 4u).vertices.empty());
    check("safety: all transparent", extrude_voxel_grid(sixteen_zero, 4u, 4u).vertices.empty());
    check("safety: depth == 0", extrude_voxel_grid(one, 1u, 1u, 0.0f).vertices.empty());
    check("safety: depth == -0", extrude_voxel_grid(one, 1u, 1u, -0.0f).vertices.empty());
    check("safety: depth < 0", extrude_voxel_grid(one, 1u, 1u, -1.0f).vertices.empty());
    check("safety: depth == NaN", extrude_voxel_grid(one, 1u, 1u, nan).vertices.empty());
    check("safety: depth == +Inf", extrude_voxel_grid(one, 1u, 1u, inf).vertices.empty());
    check("safety: depth == -Inf", extrude_voxel_grid(one, 1u, 1u, -inf).vertices.empty());
  }

  // 2. Single 1x1 opaque pixel emits 6 faces (24 vertices, 36 indices).
  {
    const std::array<std::uint8_t, 4> mask_with_extra{255u, 1u, 1u, 1u};
    const VoxelMesh unit = extrude_voxel_grid(std::span<const std::uint8_t>(mask_with_extra).first(1u), 1u, 1u);
    const VoxelMesh oversize_span = extrude_voxel_grid(mask_with_extra, 1u, 1u);

    check("1x1: 24 vertices", unit.vertices.size() == 24u);
    check("1x1: 36 indices", unit.indices.size() == 36u);
    check("1x1: ignores trailing mask bytes", unit == oversize_span);
    validate_mesh_invariants("1x1: invariants", unit);

    check("1x1: 1 face per axis direction",
        count_normal(unit, 0.0f, 0.0f, 1.0f) == 1u
        && count_normal(unit, 0.0f, 0.0f, -1.0f) == 1u
        && count_normal(unit, -1.0f, 0.0f, 0.0f) == 1u
        && count_normal(unit, 1.0f, 0.0f, 0.0f) == 1u
        && count_normal(unit, 0.0f, 1.0f, 0.0f) == 1u
        && count_normal(unit, 0.0f, -1.0f, 0.0f) == 1u);

    const VoxelMesh custom_depth = extrude_voxel_grid(std::span<const std::uint8_t>(mask_with_extra).first(1u), 1u, 1u, 0.4f);
    validate_mesh_invariants("1x1 custom depth: invariants", custom_depth);
    float min_z = 1e9f;
    float max_z = -1e9f;
    for(const auto& v : custom_depth.vertices) {
      if(v.z < min_z) min_z = v.z;
      if(v.z > max_z) max_z = v.z;
      check("1x1: cell-centered uv (0.5, 0.5)", approx(v.u, 0.5f) && approx(v.v, 0.5f));
    }
    check("1x1 custom depth: z bounds [-0.2, +0.2]", approx(min_z, -0.2f) && approx(max_z, 0.2f));
  }

  // 3. 2x1 and 1x2 adjacent opaque pixels cull the 2 shared internal faces (10 faces = 40 verts, 60 indices).
  {
    const std::array<std::uint8_t, 2> two_by_one{1u, 2u};
    const VoxelMesh horiz = extrude_voxel_grid(two_by_one, 2u, 1u);
    check("2x1: 40 vertices, 60 indices", horiz.vertices.size() == 40u && horiz.indices.size() == 60u);
    validate_mesh_invariants("2x1: invariants", horiz);
    check("2x1: 1 -X and 1 +X side face",
        count_normal(horiz, -1.0f, 0.0f, 0.0f) == 1u && count_normal(horiz, 1.0f, 0.0f, 0.0f) == 1u);
    check("2x1: cell UVs 0.25 and 0.75",
        approx(horiz.vertices[0].u, 0.25f) && approx(horiz.vertices[0].v, 0.5f)
        && approx(horiz.vertices[20].u, 0.75f) && approx(horiz.vertices[20].v, 0.5f));

    const VoxelMesh vert = extrude_voxel_grid(two_by_one, 1u, 2u);
    check("1x2: 40 vertices, 60 indices", vert.vertices.size() == 40u && vert.indices.size() == 60u);
    validate_mesh_invariants("1x2: invariants", vert);
    check("1x2: 1 +Y and 1 -Y side face",
        count_normal(vert, 0.0f, 1.0f, 0.0f) == 1u && count_normal(vert, 0.0f, -1.0f, 0.0f) == 1u);
    check("1x2: top cell at +Y with v=0.25, bottom cell at -Y with v=0.75",
        approx(vert.vertices[0].v, 0.25f) && vert.vertices[0].y >= 0.0f
        && approx(vert.vertices[20].v, 0.75f) && vert.vertices[20].y <= 0.0f);
  }

  // 4. 2x2 solid square culls 8 internal faces (16 faces = 64 vertices, 96 indices).
  {
    const std::array<std::uint8_t, 4> solid2x2{1u, 1u, 1u, 1u};
    const VoxelMesh mesh = extrude_voxel_grid(solid2x2, 2u, 2u);
    check("2x2 solid: 64 vertices, 96 indices", mesh.vertices.size() == 64u && mesh.indices.size() == 96u);
    validate_mesh_invariants("2x2 solid: invariants", mesh);
  }

  // 5. 3x3 hollow ring vs 3x3 solid tests internal boundary faces.
  {
    const std::array<std::uint8_t, 9> solid3x3{
        1u, 1u, 1u,
        1u, 1u, 1u,
        1u, 1u, 1u,
    };
    const std::array<std::uint8_t, 9> hollow3x3{
        1u, 1u, 1u,
        1u, 0u, 1u,
        1u, 1u, 1u,
    };
    const VoxelMesh solid = extrude_voxel_grid(solid3x3, 3u, 3u, 0.5f);
    const VoxelMesh hollow = extrude_voxel_grid(hollow3x3, 3u, 3u, 0.5f);

    check("3x3 solid: 30 faces (120 vertices, 180 indices)",
        solid.vertices.size() == 120u && solid.indices.size() == 180u);
    check("3x3 hollow: 32 faces (128 vertices, 192 indices)",
        hollow.vertices.size() == 128u && hollow.indices.size() == 192u);
    validate_mesh_invariants("3x3 solid: invariants", solid);
    validate_mesh_invariants("3x3 hollow: invariants", hollow);

    check("3x3 hollow: 4 side faces on each axis (3 outer + 1 inner hole)",
        count_normal(hollow, -1.0f, 0.0f, 0.0f) == 4u
        && count_normal(hollow, 1.0f, 0.0f, 0.0f) == 4u
        && count_normal(hollow, 0.0f, 1.0f, 0.0f) == 4u
        && count_normal(hollow, 0.0f, -1.0f, 0.0f) == 4u);
  }

  // 6. 64x64 max boundary checkerboard + deterministic repeat output.
  {
    std::vector<std::uint8_t> checker(64u * 64u, 0u);
    for(std::uint32_t y = 0u; y < 64u; ++y) {
      for(std::uint32_t x = 0u; x < 64u; ++x) {
        checker[y * 64u + x] = static_cast<std::uint8_t>((x + y) & 1u);
      }
    }
    const VoxelMesh a = extrude_voxel_grid(checker, 64u, 64u, 0.25f);
    const VoxelMesh b = extrude_voxel_grid(checker, 64u, 64u, 0.25f);
    check("64x64 checker: deterministic repeat", a == b);
    // 2048 opaque cells with zero orthogonal neighbors -> 2048 * 6 = 12288 faces.
    check("64x64 checker: 12288 faces",
        a.vertices.size() == 12288u * 4u && a.indices.size() == 12288u * 6u);
    validate_mesh_invariants("64x64 checker: invariants", a);
  }

  if(failures) {
    std::fprintf(stderr, "voxel_mesh: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("voxel_mesh: all checks ok\n");
  return 0;
}
