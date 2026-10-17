#include <mln/test/util.hpp>

#include <mln/util/image.hpp>
#include <mln/util/tileset.hpp>
#include <mln/geometry/dem_data.hpp>

using namespace mln;

auto fakeImage = [](Size s) {
    PremultipliedImage img = PremultipliedImage(s);

    for (size_t i = 0; i < img.bytes(); i++) {
        img.data[i] = (i + 1) % 4 == 0 ? 1 : std::rand() % 255;
    }
    return img;
};

TEST(DEMData, ConstructorMapbox) {
    PremultipliedImage image = fakeImage({16, 16});
    DEMData demdata(image, Tileset::RasterEncoding::Mapbox);

    EXPECT_EQ(demdata.dim, 16);
    EXPECT_EQ(demdata.stride, 18);
    EXPECT_EQ(demdata.getImage()->bytes(), size_t(18 * 18 * 4));
};

TEST(DEMData, ConstructorTerrarium) {
    PremultipliedImage image = fakeImage({16, 16});
    DEMData demdata(image, Tileset::RasterEncoding::Terrarium);

    EXPECT_EQ(demdata.dim, 16);
    EXPECT_EQ(demdata.stride, 18);
    EXPECT_EQ(demdata.getImage()->bytes(), size_t(18 * 18 * 4));
};

TEST(DEMData, InitialBackfill) {
    PremultipliedImage image1 = fakeImage({4, 4});
    DEMData dem1(image1, Tileset::RasterEncoding::Mapbox);

    bool nonempty = true;
    // checking that a 1 px border around the fake image has been populated
    // with a non-empty pixel value
    for (int x = -1; x < 5; x++) {
        for (int y = -1; y < 5; y++) {
            if (dem1.get(x, y) == -65536) {
                nonempty = false;
                break;
            }
        }
    }
    EXPECT_TRUE(nonempty);

    bool verticalBorderMatch = true;
    int vertx[] = {-1, 4};
    for (int x : vertx) {
        for (int y = 0; y < 4; y++) {
            if (dem1.get(x, y) != dem1.get(x < 0 ? x + 1 : x - 1, y)) {
                verticalBorderMatch = false;
                break;
            }
        }
    }
    // vertical border of DEM data is initially equal to next column of data
    EXPECT_TRUE(verticalBorderMatch);

    // horizontal borders empty
    bool horizontalBorderMatch = true;
    int horiz[] = {-1, 4};
    for (int y : horiz) {
        for (int x = 0; x < 4; x++) {
            if (dem1.get(x, y) != dem1.get(x, y < 0 ? y + 1 : y - 1)) {
                horizontalBorderMatch = false;
                break;
            }
        }
    }
    // horizontal border of DEM data is initially equal to next row of data

    EXPECT_TRUE(horizontalBorderMatch);
    // -1, 1 corner initially equal to closest corner data
    EXPECT_TRUE(dem1.get(-1, 4) == dem1.get(0, 3));
    // 1, 1 corner initially equal to closest corner data
    EXPECT_TRUE(dem1.get(4, 4) == dem1.get(3, 3));
    // -1, -1 corner initially equal to closest corner data
    EXPECT_TRUE(dem1.get(-1, -1) == dem1.get(0, 0));
    // -1, 1 corner initially equal to closest corner data
    EXPECT_TRUE(dem1.get(4, -1) == dem1.get(3, 0));
};

TEST(DEMData, BackfillNeighbor) {
    PremultipliedImage image1 = fakeImage({4, 4});
    DEMData dem0(image1, Tileset::RasterEncoding::Mapbox);

    PremultipliedImage image2 = fakeImage({4, 4});
    DEMData dem1(image2, Tileset::RasterEncoding::Mapbox);

    dem0.backfillBorder(dem1, -1, 0);
    for (int y = 0; y < 4; y++) {
        // dx = -1, dy = 0, so the left edge of dem1 should equal the right edge
        // of dem0 backfills Left neighbor
        EXPECT_TRUE(dem0.get(-1, y) == dem1.get(3, y));
    }

    dem0.backfillBorder(dem1, 0, -1);
    // backfills TopCenter neighbor
    for (int x = 0; x < 4; x++) {
        EXPECT_TRUE(dem0.get(x, -1) == dem1.get(x, 3));
    }

    dem0.backfillBorder(dem1, 1, 0);
    // backfills Right neighbor
    for (int y = 0; y < 4; y++) {
        EXPECT_TRUE(dem0.get(4, y) == dem1.get(0, y));
    }

    dem0.backfillBorder(dem1, 0, 1);
    // backfills BottomCenter neighbor
    for (int x = 0; x < 4; x++) {
        EXPECT_TRUE(dem0.get(x, 4) == dem1.get(x, 0));
    }

    dem0.backfillBorder(dem1, -1, 1);
    // backfulls TopRight neighbor
    EXPECT_TRUE(dem0.get(-1, 4) == dem1.get(3, 0));

    dem0.backfillBorder(dem1, 1, 1);
    // backfulls BottomRight neighbor
    EXPECT_TRUE(dem0.get(4, 4) == dem1.get(0, 0));

    dem0.backfillBorder(dem1, -1, -1);
    // backfulls TopLeft neighbor
    EXPECT_TRUE(dem0.get(-1, -1) == dem1.get(3, 3));

    dem0.backfillBorder(dem1, 1, -1);
    // backfulls BottomLeft neighbor
    EXPECT_TRUE(dem0.get(4, -1) == dem1.get(0, 3));
};

TEST(DEMData, GetElevationMatchesGetAtIntegerCoordinates) {
    PremultipliedImage image = fakeImage({16, 16});
    DEMData demdata(image, Tileset::RasterEncoding::Mapbox);

    for (int32_t x = 0; x < 16; x++) {
        for (int32_t y = 0; y < 16; y++) {
            EXPECT_DOUBLE_EQ(demdata.getElevation(x, y), demdata.get(x, y));
        }
    }
};

TEST(DEMData, GetElevationBilinearMidpoint) {
    PremultipliedImage image = fakeImage({16, 16});
    DEMData demdata(image, Tileset::RasterEncoding::Mapbox);

    for (int32_t x = 0; x < 15; x++) {
        for (int32_t y = 0; y < 15; y++) {
            const double expected = 0.25 * (demdata.get(x, y) + demdata.get(x + 1, y) + demdata.get(x, y + 1) +
                                            demdata.get(x + 1, y + 1));
            EXPECT_DOUBLE_EQ(demdata.getElevation(x + 0.5, y + 0.5), expected);
        }
    }
};

// Terrain meshes sample their edge row at x == dim: after the neighbours backfill each other's border, both tiles read
// the same elevations along the shared edge, so a mesh edge has no crack.
TEST(DEMData, SharedEdgeIsContinuousAfterBackfill) {
    PremultipliedImage left = fakeImage({8, 8});
    PremultipliedImage right = fakeImage({8, 8});
    DEMData demLeft(left, Tileset::RasterEncoding::Terrarium);
    DEMData demRight(right, Tileset::RasterEncoding::Terrarium);

    demLeft.backfillBorder(demRight, 1, 0);
    demRight.backfillBorder(demLeft, -1, 0);

    // Rows 0..7 only: the corner pixel below the edge belongs to the diagonal and south neighbours (not loaded here;
    // the terrain mesh skirt covers that last cell until they arrive).
    for (double y = 0.0; y <= 7.0; y += 0.5) {
        EXPECT_DOUBLE_EQ(demLeft.getElevation(8.0, y), demRight.getElevation(0.0, y)) << "y=" << y;
    }
    // The border pixel is addressable below zero as well (used for edge slopes).
    EXPECT_DOUBLE_EQ(demRight.getElevation(-1.0, 3.0), static_cast<double>(demRight.get(-1, 3)));
}
