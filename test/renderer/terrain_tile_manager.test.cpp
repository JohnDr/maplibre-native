#include <mln/test/util.hpp>

#include <mln/geometry/dem_data.hpp>
#include <mln/renderer/terrain_stitch.hpp>
#include <mln/renderer/terrain_tile_manager.hpp>
#include <mln/util/image.hpp>
#include <mln/util/tileset.hpp>

using namespace mln;

namespace {

PremultipliedImage zeroImage(Size s) {
    PremultipliedImage image(s);
    for (size_t i = 0; i < image.bytes(); i++) {
        image.data[i] = 0;
    }
    return image;
}

/// Injects DEM data instead of fetching it, so the coordinate mapping and sampling can be tested alone.
class TestTerrainTileManager final : public TerrainTileManager {
public:
    using TerrainTileManager::TerrainTileManager;

    std::shared_ptr<DEMData> injected;

protected:
    std::shared_ptr<DEMData> getDEMData(const CanonicalTileID&) override { return injected; }
};

} // namespace

TEST(TerrainTileManager, TileFor) {
    // At lng 0, lat 0 the world centre is tile (2, 2) at z2.
    EXPECT_TRUE(TerrainTileManager::tileFor({0.0, 0.0}, 2) == CanonicalTileID(2, 2, 2));
    // Longitude -180 and 180 clamp to the first and last column.
    EXPECT_EQ(TerrainTileManager::tileFor({0.0, -180.0}, 2).x, 0u);
    EXPECT_EQ(TerrainTileManager::tileFor({0.0, 180.0}, 2).x, 3u);
}

TEST(TerrainTileManager, GetElevationUniform) {
    // A uniform all-zero Mapbox DEM decodes to a constant elevation of -baseShift = -10000 m everywhere.
    auto dem = std::make_shared<DEMData>(zeroImage({16, 16}), Tileset::RasterEncoding::Mapbox);

    TestTerrainTileManager manager(nullptr, Tileset{});
    manager.injected = dem;

    EXPECT_DOUBLE_EQ(manager.getElevation({45.0, 10.0}, 12), -10000.0);
    EXPECT_DOUBLE_EQ(manager.getElevation({-33.0, 151.0}, 10), -10000.0);
}

TEST(TerrainTileManager, TerrainZoomFollowsGLJS) {
    // GL JS: coveringTiles with tileSize = source.tileSize * 2 on a 512-px transform => floor(zoom + log2(512 / that)).
    EXPECT_EQ(TerrainTileManager::terrainZoomFor(13.3, 512), 12);
    EXPECT_EQ(TerrainTileManager::terrainZoomFor(14.5, 512), 13);
    EXPECT_EQ(TerrainTileManager::terrainZoomFor(13.3, 256), 13); // 256-px sources: terrain tiles are 512 px
    EXPECT_EQ(TerrainTileManager::terrainZoomFor(0.4, 512), 0);
    EXPECT_EQ(TerrainTileManager::terrainZoomFor(40.0, 256), TerrainTileManager::maxTerrainZoom);
}

TEST(TerrainTileManager, SourceTileIsOneZoomCoarserAndCapped) {
    Tileset tileset;
    tileset.zoomRange = {1, 12};
    TerrainTileManager manager(nullptr, tileset);
    // deltaZoom 1: the terrain tile (z, x, y) samples the DEM tile (z - 1, x / 2, y / 2).
    EXPECT_TRUE(manager.sourceTileFor({10, 5, 7}) == CanonicalTileID(9, 2, 3));
    // Capped at the source's maximum zoom (the DEM is then overzoomed).
    EXPECT_TRUE(manager.sourceTileFor({16, 100, 200}) == CanonicalTileID(12, 100 >> 4, 200 >> 4));
    // Below the source's minimum zoom there is none.
    EXPECT_FALSE(manager.sourceTileFor({1, 0, 0}).has_value());
}

TEST(TerrainStitch, CoarserNeighbour) {
    using namespace terrain_stitch;
    // Tile 14/7964/5044 has a same-zoom west neighbour and a coarser (z13) north one.
    const auto covered = [](const CanonicalTileID& id) {
        return (id.z == 14 && id.x == 7963 && id.y == 5044) || (id.z == 13 && id.x == 3982 && id.y == 2521) ||
               (id.z == 14 && id.x == 7964 && id.y == 5044);
    };
    const CanonicalTileID tile(14, 7964, 5044);
    EXPECT_FALSE(coarserNeighbour(tile, West, covered).has_value());  // same zoom
    EXPECT_FALSE(coarserNeighbour(tile, East, covered).has_value());  // not covered (finer or off screen)
    const auto north = coarserNeighbour(tile, North, covered);
    ASSERT_TRUE(north.has_value());
    EXPECT_TRUE(*north == CanonicalTileID(13, 3982, 2521)); // (7964, 5043) >> 1
    // Off the world edge: nothing.
    EXPECT_FALSE(coarserNeighbour(CanonicalTileID(3, 0, 0), North, covered).has_value());
}

TEST(TerrainStitch, EdgeParameterAndInterpolation) {
    using namespace terrain_stitch;
    // A z14 tile that is the right half of its z13 parent along x: its edge spans u = 0.5 .. 1.
    const CanonicalTileID fine(14, 7965, 5045);
    const CanonicalTileID coarse(13, 3982, 2522);
    EXPECT_DOUBLE_EQ(coarseEdgeParameter(fine, coarse, North, 0.0), 0.5);
    EXPECT_DOUBLE_EQ(coarseEdgeParameter(fine, coarse, North, 1.0), 1.0);
    EXPECT_DOUBLE_EQ(coarseEdgeParameter(fine, coarse, North, 0.5), 0.75);
    // The coarse mesh edge is the line between its vertices.
    const std::vector<double> edge{0.0, 10.0, 30.0};
    EXPECT_DOUBLE_EQ(interpolateEdge(edge, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(interpolateEdge(edge, 0.25), 5.0);
    EXPECT_DOUBLE_EQ(interpolateEdge(edge, 0.75), 20.0);
    EXPECT_DOUBLE_EQ(interpolateEdge(edge, 1.0), 30.0);
}

TEST(TerrainTileManager, GetElevationUsesDemPixelCentres) {
    // Terrarium: elevation = R * 256 + G + B / 256 - 32768, so R = 128 and G = x gives a ramp with elevation == pixel x.
    PremultipliedImage image({8, 8});
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            auto* pixel = image.data.get() + (y * 8 + x) * 4;
            pixel[0] = 128;
            pixel[1] = static_cast<uint8_t>(x);
            pixel[2] = 0;
            pixel[3] = 255;
        }
    }
    auto dem = std::make_shared<DEMData>(image, Tileset::RasterEncoding::Terrarium);
    TestTerrainTileManager manager(nullptr, Tileset{});
    manager.injected = dem;

    // GL JS places DEM pixel i at tile fraction (i + 0.5) / dim: a point at the centre of pixel 3 reads exactly 3.
    const double fraction = (3.0 + 0.5) / 8.0;
    const double lng = -180.0 + (2.0 + fraction) / 4.0 * 360.0; // tile x = 2 at zoom 2
    EXPECT_NEAR(manager.getElevation({10.0, lng}, 2), 3.0, 1e-6);
}
