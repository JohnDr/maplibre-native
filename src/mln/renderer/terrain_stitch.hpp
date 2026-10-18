#pragma once

#include <mln/tile/tile_id.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace mln {
namespace terrain_stitch {

/// Sides of a terrain tile, in the order the mesh builder uses.
enum Side : int { North = 0, South = 1, West = 2, East = 3 };

/// The covering tile that borders `tile` on `side` and is coarser (a lower zoom), if there is one. Where the
/// neighbour is at the same zoom or finer (or not covered) the two meshes already meet, or the other tile adapts.
/// `isCovered` says whether a canonical tile is part of the terrain tile cover.
inline std::optional<CanonicalTileID> coarserNeighbour(const CanonicalTileID& tile,
                                                       int side,
                                                       const std::function<bool(const CanonicalTileID&)>& isCovered) {
    const int64_t tilesAcross = static_cast<int64_t>(1) << tile.z;
    int64_t x = tile.x;
    int64_t y = tile.y;
    switch (side) {
        case North: --y; break;
        case South: ++y; break;
        case West: --x; break;
        default: ++x; break;
    }
    if (x < 0 || y < 0 || x >= tilesAcross || y >= tilesAcross) {
        return std::nullopt; // outside the world (world wraps are not stitched)
    }
    if (isCovered(CanonicalTileID(tile.z, static_cast<uint32_t>(x), static_cast<uint32_t>(y)))) {
        return std::nullopt; // same zoom
    }
    for (int z = static_cast<int>(tile.z) - 1; z >= 0; --z) {
        const int shift = static_cast<int>(tile.z) - z;
        const CanonicalTileID ancestor(static_cast<uint8_t>(z), static_cast<uint32_t>(x >> shift), static_cast<uint32_t>(y >> shift));
        if (isCovered(ancestor)) {
            return ancestor;
        }
    }
    return std::nullopt; // finer neighbours: they adapt to this tile
}

/// Where along the coarse tile's facing edge (0..1) the point at `t` (0..1) along the fine tile's `side` edge lies.
/// The edges run along x for north/south and along y for west/east.
inline double coarseEdgeParameter(const CanonicalTileID& fine, const CanonicalTileID& coarse, int side, double t) {
    const bool alongX = side == North || side == South;
    const double fineIndex = alongX ? fine.x : fine.y;
    const double coarseIndex = alongX ? coarse.x : coarse.y;
    const double position = (fineIndex + t) / std::ldexp(1.0, fine.z); // 0..1 across the world
    return position * std::ldexp(1.0, coarse.z) - coarseIndex;
}

/// The height of the coarse mesh's edge at parameter `u` (0..1): the mesh's edge is the straight line between its
/// vertices, whose heights are `edge`.
inline double interpolateEdge(const std::vector<double>& edge, double u) {
    if (edge.empty()) {
        return 0.0;
    }
    const double position = std::clamp(u, 0.0, 1.0) * static_cast<double>(edge.size() - 1);
    const auto k = static_cast<std::size_t>(std::floor(position));
    const std::size_t next = std::min(k + 1, edge.size() - 1);
    const double f = position - static_cast<double>(k);
    return edge[k] * (1.0 - f) + edge[next] * f;
}

} // namespace terrain_stitch
} // namespace mln
