#include <mln/test/util.hpp>

#include <mln/geometry/dem_data.hpp>
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
