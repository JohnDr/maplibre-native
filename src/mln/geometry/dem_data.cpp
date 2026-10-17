#include <mln/geometry/dem_data.hpp>
#include <mln/math/clamp.hpp>

#include <algorithm>
#include <cmath>

namespace mln {

DEMData::DEMData(const PremultipliedImage& _image, Tileset::RasterEncoding _encoding)
    : dim(_image.size.height),
      // extra two pixels per row for border backfilling on either edge
      stride(dim + 2),
      encoding(_encoding) {
    image = std::make_shared<PremultipliedImage>(Size(static_cast<uint32_t>(stride), static_cast<uint32_t>(stride)));
    if (_image.size.height != _image.size.width) {
        throw std::runtime_error("raster-dem tiles must be square.");
    }

    auto* dest = reinterpret_cast<uint32_t*>(image->data.get()) + stride + 1;
    auto* source = reinterpret_cast<uint32_t*>(_image.data.get());
    for (int32_t y = 0; y < dim; y++) {
        memcpy(dest, source, dim * 4);
        dest += stride;
        source += dim;
    }

    // in order to avoid flashing seams between tiles, here we are initially
    // populating a 1px border of pixels around the image with the data of the
    // nearest pixel from the image. this data is eventually replaced when the
    // tile's neighboring tiles are loaded and the accurate data can be
    // backfilled using DEMData#backfillBorder

    auto* data = reinterpret_cast<uint32_t*>(image->data.get());
    for (int32_t x = 0; x < dim; x++) {
        auto rowOffset = stride * (x + 1);
        // left vertical border
        data[rowOffset] = data[rowOffset + 1];

        // right vertical border
        data[rowOffset + dim + 1] = data[rowOffset + dim];
    }

    // top horizontal border with corners
    memcpy(data, data + stride, stride * 4);
    // bottom horizontal border with corners
    memcpy(data + (dim + 1) * stride, data + dim * stride, stride * 4);

    if (dim > 0) {
        int32_t lo = get(0, 0);
        int32_t hi = lo;
        for (int32_t y = 0; y < dim; y++) {
            for (int32_t x = 0; x < dim; x++) {
                const int32_t value = get(x, y);
                lo = std::min(lo, value);
                hi = std::max(hi, value);
            }
        }
        minElevation = lo;
        maxElevation = hi;
    }
}

// This function takes the DEMData from a neighboring tile and backfills the
// edge/corner data in order to create a one pixel "buffer" of image data around
// the tile. This is necessary because the hillshade formula calculates the
// dx/dz, dy/dz derivatives at each pixel of the tile by querying the 8
// surrounding pixels, and if we don't have the pixel buffer we get seams at
// tile boundaries.
void DEMData::backfillBorder(const DEMData& borderTileData, int8_t dx, int8_t dy) {
    auto& o = borderTileData;

    // Tiles from the same source should always be of the same dimensions.
    assert(dim == o.dim);

    // We determine the pixel range to backfill based which corner/edge
    // `borderTileData` represents. For example, dx = -1, dy = -1 represents the
    // upper left corner of the base tile, so we only need to backfill one pixel
    // at coordinates (-1, -1) of the tile image.
    int32_t xMin = dx * dim;
    int32_t xMax = dx * dim + dim;
    int32_t yMin = dy * dim;
    int32_t yMax = dy * dim + dim;

    if (dx == -1)
        xMin = xMax - 1;
    else if (dx == 1)
        xMax = xMin + 1;

    if (dy == -1)
        yMin = yMax - 1;
    else if (dy == 1)
        yMax = yMin + 1;

    int32_t ox = -dx * dim;
    int32_t oy = -dy * dim;

    auto* dest = reinterpret_cast<uint32_t*>(image->data.get());
    auto* source = reinterpret_cast<uint32_t*>(o.image->data.get());

    for (int32_t y = yMin; y < yMax; y++) {
        for (int32_t x = xMin; x < xMax; x++) {
            dest[idx(x, y)] = source[idx(x + ox, y + oy)];
        }
    }
}

int32_t DEMData::get(const int32_t x, const int32_t y) const {
    const auto& unpack = getUnpackVector();
    const uint8_t* value = image->data.get() + idx(x, y) * 4;
    return static_cast<int32_t>(value[0] * unpack[0] + value[1] * unpack[1] + value[2] * unpack[2] - unpack[3]);
}

double DEMData::getElevation(double x, double y) const {
    // The one-pixel border (backfilled from the neighbouring tiles) is addressable at -1 and `dim`.
    x = std::clamp(x, -1.0, static_cast<double>(dim));
    y = std::clamp(y, -1.0, static_cast<double>(dim));
    const auto x0 = static_cast<int32_t>(std::floor(x));
    const auto y0 = static_cast<int32_t>(std::floor(y));
    const auto x1 = std::min(x0 + 1, dim);
    const auto y1 = std::min(y0 + 1, dim);
    const double u = x - x0;
    const double v = y - y0;
    const double z00 = static_cast<double>(get(x0, y0));
    const double z10 = static_cast<double>(get(x1, y0));
    const double z01 = static_cast<double>(get(x0, y1));
    const double z11 = static_cast<double>(get(x1, y1));
    return z00 * (1.0 - u) * (1.0 - v) + z10 * u * (1.0 - v) + z01 * (1.0 - u) * v + z11 * u * v;
}

const std::array<float, 4>& DEMData::getUnpackVector() const {
    // https://www.mapbox.com/help/access-elevation-data/#mapbox-terrain-rgb
    static const std::array<float, 4> unpackMapbox = {{6553.6f, 25.6f, 0.1f, 10000.0f}};
    // https://aws.amazon.com/public-datasets/terrain/
    static const std::array<float, 4> unpackTerrarium = {{256.0f, 1.0f, 1.0f / 256.0f, 32768.0f}};

    return encoding == Tileset::RasterEncoding::Terrarium ? unpackTerrarium : unpackMapbox;
}

} // namespace mln
