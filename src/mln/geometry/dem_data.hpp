#pragma once

#include <mln/math/clamp.hpp>
#include <mln/util/image.hpp>
#include <mln/util/tileset.hpp>

#include <memory>
#include <array>
#include <cassert>
#include <vector>

namespace mln {

class DEMData {
public:
    DEMData(const PremultipliedImage& image, Tileset::RasterEncoding encoding);
    void backfillBorder(const DEMData& borderTileData, int8_t dx, int8_t dy);

    int32_t get(int32_t x, int32_t y) const;
    const std::array<float, 4>& getUnpackVector() const;

    /// Bilinearly interpolated elevation, in metres, at the continuous tile coordinate (`x`, `y`) within `[0, dim]`
    /// (coordinates down to -1 read the border pixel).
    /// Mirrors GL JS `DEMData#getElevation`, adapted to this class's one-pixel border.
    double getElevation(double x, double y) const;

    /// The elevation, in metres, at a point of the tile given in pixel EDGES: (0, 0) is the top-left corner of the first
    /// pixel and (`dim`, `dim`) the bottom-right corner of the last, so a tile-fraction `f` is `f * dim`. DEM pixel `i`
    /// holds the value at its centre, `i + 0.5` (GL JS: `DEM_CELL_CENTER_OFFSET = -0.5` on the CPU, `uv * dim + 1.5` in
    /// the terrain vertex shader). Use this, not `getElevation`, for anything placed on the map by position: terrain
    /// meshes, elevation queries, camera elevation. Reads the 1-pixel border at the tile edges.
    double getElevationAtPoint(double x, double y) const { return getElevation(x - 0.5, y - 0.5); }

    const PremultipliedImage* getImage() const { return &*image; }
    const std::shared_ptr<PremultipliedImage>& getImagePtr() const { return image; }

    const int32_t dim;
    const int32_t stride;
    const Tileset::RasterEncoding encoding;
    /// The lowest and highest elevation in the tile, in metres (GL JS `DEMData#min`/`#max`).
    double minElevation = 0.0;
    double maxElevation = 0.0;

private:
    std::shared_ptr<PremultipliedImage> image;

    size_t idx(const int32_t x, const int32_t y) const {
        assert(x >= -1);
        assert(x < dim + 1);
        assert(y >= -1);
        assert(y < dim + 1);
        return (y + 1) * stride + (x + 1);
    }
};

} // namespace mln
