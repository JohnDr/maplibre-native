// Tests for the GL JS-aligned terrain camera / pointer primitives (patch 0053):
// TransformState::screenCoordinateToLatLngAtElevation, TransformState::raycastTerrain,
// Map::setLocationAtPoint / latLngForPixelAtElevation and Transform::putCenterOnTerrainKeepingCamera.
//
// Pixel convention: the TransformState functions take the pixel y UP (origin bottom-left); the Transform and Map
// wrappers take y DOWN and flip. A missing flip once made terrain drags overshoot at high pitch.

#include <mln/test/util.hpp>
#include <mln/test/stub_file_source.hpp>
#include <mln/test/stub_map_observer.hpp>

#include <mln/map/camera.hpp>
#include <mln/map/transform.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/test/map_adapter.hpp>
#include <mln/gfx/backend_scope.hpp>
#include <mln/gfx/headless_frontend.hpp>
#include <mln/map/map_options.hpp>
#include <mln/renderer/renderer.hpp>
#include <mln/util/client_options.hpp>
#include <mln/storage/resource_options.hpp>
#include <mln/util/geo.hpp>
#include <mln/util/run_loop.hpp>
#include <mln/util/size.hpp>

#include <gmock/gmock.h>
#include <cmath>
#include <functional>
#include <optional>

using namespace mln;

namespace {

constexpr double kMetersPerDegree = util::EARTH_RADIUS_M * M_PI / 180.0;

void setCamera(Transform& transform, double lat, double lon, double zoom, double bearing, double pitch) {
    transform.resize({800, 600});
    transform.setMaxPitch(85.0); // the default limit is 60 degrees; the app allows 85
    transform.jumpTo(CameraOptions().withCenter(LatLng{lat, lon}).withZoom(zoom).withBearing(bearing).withPitch(pitch));
}

using ElevationFn = std::function<std::optional<double>(const LatLng&)>;

} // namespace

// MARK: screenCoordinateToLatLngAtElevation

TEST(TerrainCamera, AtElevationRoundTripsLatLngToScreen) {
    // latLngToScreenCoordinate projects onto the plane through the centre (the centre altitude), and the state-level
    // functions share the y-up pixel convention, so the two must invert each other at that altitude.
    for (const double centreAltitude : {0.0, 1500.0}) {
        for (const double pitch : {0.0, 30.0, 60.0, 75.0}) {
            for (const double bearing : {0.0, 37.0, -120.0, 200.0}) {
                Transform transform;
            setCamera(transform, 46.0, 8.0, 11.0, bearing, pitch);
                TransformState state = transform.getState();
                state.setCenterAltitude(centreAltitude);
                SCOPED_TRACE(testing::Message() << "alt " << centreAltitude << " pitch " << pitch << " bearing "
                                                << bearing);

                for (const LatLng ll : {LatLng{46.0, 8.0}, LatLng{46.01, 8.02}, LatLng{45.99, 7.97}}) {
                    const ScreenCoordinate pixel = state.latLngToScreenCoordinate(ll);
                    const auto back = state.screenCoordinateToLatLngAtElevation(pixel, centreAltitude);
                    ASSERT_TRUE(back.has_value());
                    EXPECT_NEAR(back->latitude(), ll.latitude(), 1e-7);
                    EXPECT_NEAR(back->longitude(), ll.longitude(), 1e-7);
                }
            }
        }
    }
}

TEST(TerrainCamera, AtElevationCentrePixelAtCentreAltitudeIsCentre) {
    Transform transform;
            setCamera(transform, 10.0, 20.0, 12.0, 25.0, 60.0);
    TransformState state = transform.getState();
    state.setCenterAltitude(800.0);
    // y up: the centre row is the same either way.
    const auto centre = state.screenCoordinateToLatLngAtElevation({400.0, 300.0}, 800.0);
    ASSERT_TRUE(centre.has_value());
    // Metres convert to the matrix's z units at the centre latitude only (as in GL JS), so 800 m leaves a sub-metre
    // residue (1e-5 degrees is ~1 m).
    EXPECT_NEAR(centre->latitude(), 10.0, 1e-5);
    EXPECT_NEAR(centre->longitude(), 20.0, 1e-5);
}

TEST(TerrainCamera, AtElevationSlidesTowardsCameraAlongViewRay) {
    // The centre pixel's view ray descends towards the centre. A plane h metres above the centre altitude is met
    // h * tan(pitch) metres short of the centre, on the camera's side (opposite the bearing direction).
    for (const double pitch : {20.0, 45.0, 65.0}) {
        for (const double bearing : {0.0, 90.0, 215.0}) {
            Transform transform;
            setCamera(transform, 0.0, 0.0, 14.0, bearing, pitch);
            const TransformState& state = transform.getState();
            SCOPED_TRACE(testing::Message() << "pitch " << pitch << " bearing " << bearing);
            const double h = 400.0;
            const auto above = state.screenCoordinateToLatLngAtElevation({400.0, 300.0}, h);
            ASSERT_TRUE(above.has_value());
            const double dNorth = above->latitude() * kMetersPerDegree;
            const double dEast = above->longitude() * kMetersPerDegree;
            const double expected = h * std::tan(util::deg2rad(pitch));
            EXPECT_NEAR(std::hypot(dNorth, dEast), expected, expected * 2e-3);
            // Direction: the camera sits behind the centre, against the bearing.
            const double b = util::deg2rad(bearing);
            EXPECT_NEAR(dNorth, -expected * std::cos(b), expected * 2e-3);
            EXPECT_NEAR(dEast, -expected * std::sin(b), expected * 2e-3);
        }
    }
}

TEST(TerrainCamera, AtElevationIsLinearAlongTheRay) {
    // For one pixel, the unprojected ground position is linear in the plane's height.
    Transform transform;
            setCamera(transform, 47.0, 11.0, 12.0, 33.0, 62.0);
    const TransformState& state = transform.getState();
    const ScreenCoordinate pixel{250.0, 410.0}; // y up
    const auto a = state.screenCoordinateToLatLngAtElevation(pixel, 0.0);
    const auto m = state.screenCoordinateToLatLngAtElevation(pixel, 500.0);
    const auto b = state.screenCoordinateToLatLngAtElevation(pixel, 1000.0);
    ASSERT_TRUE(a && m && b);
    // Mercator y is non-linear in latitude; the span here is tiny, so a tight tolerance still holds.
    EXPECT_NEAR(m->latitude(), 0.5 * (a->latitude() + b->latitude()), 1e-5);
    EXPECT_NEAR(m->longitude(), 0.5 * (a->longitude() + b->longitude()), 1e-5);
    EXPECT_NE(a->latitude(), b->latitude());
}

TEST(TerrainCamera, AtElevationAboveHorizonIsNullopt) {
    Transform transform;
            setCamera(transform, 45.0, 5.0, 10.0, 0.0, 80.0);
    const TransformState& state = transform.getState();
    // y up: the top row looks above the horizon at pitch 80 (80 + half the vertical field of view > 90).
    EXPECT_FALSE(state.screenCoordinateToLatLngAtElevation({400.0, 600.0}, 0.0).has_value());
    // The bottom row looks steeply down and hits the plane.
    EXPECT_TRUE(state.screenCoordinateToLatLngAtElevation({400.0, 0.0}, 0.0).has_value());
}

TEST(TerrainCamera, AtElevationPlaneAboveTheCameraIsNullopt) {
    Transform transform;
            setCamera(transform, 45.0, 5.0, 14.0, 0.0, 50.0);
    const TransformState& state = transform.getState();
    EXPECT_FALSE(state.screenCoordinateToLatLngAtElevation({400.0, 300.0}, 1.0e6).has_value());
}

TEST(TerrainCamera, AtElevationMatchesTransformWrapperAtSeaLevel) {
    // The wrapper flips y; state.screenCoordinateToLatLng (centre plane, altitude 0) must agree with the AtElevation
    // path at elevation 0 when fed the flipped pixel.
    Transform transform;
            setCamera(transform, 30.0, -40.0, 9.0, 12.0, 70.0);
    const TransformState& state = transform.getState();
    for (const ScreenCoordinate down : {ScreenCoordinate{100.0, 500.0}, ScreenCoordinate{700.0, 400.0},
                                        ScreenCoordinate{400.0, 300.0}}) {
        const LatLng wrapped = transform.screenCoordinateToLatLng(down);
        const auto atElevation = state.screenCoordinateToLatLngAtElevation({down.x, 600.0 - down.y}, 0.0);
        ASSERT_TRUE(atElevation.has_value());
        EXPECT_NEAR(wrapped.latitude(), atElevation->latitude(), 1e-8);
        EXPECT_NEAR(wrapped.longitude(), atElevation->longitude(), 1e-8);
    }
}

// MARK: raycastTerrain

TEST(TerrainCamera, RaycastFlatTerrainMatchesPlane) {
    for (const double pitch : {0.0, 40.0, 70.0}) {
        for (const double elevation : {0.0, 350.0, 2500.0}) {
            Transform transform;
            setCamera(transform, 46.5, 9.0, 12.0, 20.0, pitch);
            const TransformState& state = transform.getState();
            SCOPED_TRACE(testing::Message() << "pitch " << pitch << " elevation " << elevation);
            const ElevationFn flat = [elevation](const LatLng&) { return std::optional<double>(elevation); };
            const ScreenCoordinate pixel{300.0, 200.0};
            const auto hit = state.raycastTerrain(pixel, flat);
            ASSERT_TRUE(hit.has_value());
            EXPECT_DOUBLE_EQ(hit->elevation, elevation);
            const auto plane = state.screenCoordinateToLatLngAtElevation(pixel, elevation);
            ASSERT_TRUE(plane.has_value());
            // 24 bisections of the ray section: well under a metre.
            EXPECT_NEAR(hit->latLng.latitude(), plane->latitude(), 2e-6);
            EXPECT_NEAR(hit->latLng.longitude(), plane->longitude(), 2e-6);
        }
    }
}

TEST(TerrainCamera, RaycastSlopeHitLiesOnTerrainAndOnTheRay) {
    Transform transform;
            setCamera(transform, 46.0, 8.0, 13.0, 0.0, 60.0);
    const TransformState& state = transform.getState();
    // Rises 1 m per 4 m north of the centre latitude.
    const ElevationFn slope = [](const LatLng& ll) {
        return std::optional<double>(0.25 * (ll.latitude() - 46.0) * kMetersPerDegree);
    };
    const ScreenCoordinate pixel{400.0, 300.0};
    const auto hit = state.raycastTerrain(pixel, slope);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(*slope(hit->latLng), hit->elevation, 1e-3);
    // The hit is on the pixel's ray at that height.
    const auto onRay = state.screenCoordinateToLatLngAtElevation(pixel, hit->elevation);
    ASSERT_TRUE(onRay.has_value());
    EXPECT_NEAR(hit->latLng.latitude(), onRay->latitude(), 2e-6);
    EXPECT_NEAR(hit->latLng.longitude(), onRay->longitude(), 2e-6);
    // The slope rises away from the camera, so the hit is above sea level and short of the sea-level hit.
    const auto sea = state.screenCoordinateToLatLngAtElevation(pixel, 0.0);
    ASSERT_TRUE(sea.has_value());
    EXPECT_GT(hit->elevation, 0.0);
}

TEST(TerrainCamera, RaycastCliffHitsTheFaceAtTheEdge) {
    Transform transform;
    setCamera(transform, 46.0, 8.0, 13.0, 0.0, 60.0);
    const TransformState& state = transform.getState();
    // A 100 m wall between the camera and the centre, 100 m south of the centre. The view ray descends at 30 degrees
    // below the horizon, so it is only ~58 m high there: it meets the wall face, not the ground at the centre.
    const double edgeLat = 46.0 - 100.0 / kMetersPerDegree;
    const ElevationFn cliff = [edgeLat](const LatLng& ll) {
        return std::optional<double>(ll.latitude() >= edgeLat ? 100.0 : 0.0);
    };
    const auto hit = state.raycastTerrain({400.0, 300.0}, cliff);
    ASSERT_TRUE(hit.has_value());
    EXPECT_DOUBLE_EQ(hit->elevation, 100.0);
    EXPECT_NEAR(hit->latLng.latitude(), edgeLat, 1e-6);
}

TEST(TerrainCamera, RaycastPassesOverALowCliffToTheGround) {
    Transform transform;
    setCamera(transform, 46.0, 8.0, 13.0, 0.0, 60.0);
    const TransformState& state = transform.getState();
    // Same wall but only 20 m high: the ray is ~58 m up at the edge and clears it, landing on the ground at the centre.
    const double edgeLat = 46.0 - 100.0 / kMetersPerDegree;
    const ElevationFn step = [edgeLat](const LatLng& ll) {
        return std::optional<double>(ll.latitude() >= edgeLat ? 20.0 : 0.0);
    };
    const auto hit = state.raycastTerrain({400.0, 300.0}, step);
    ASSERT_TRUE(hit.has_value());
    EXPECT_DOUBLE_EQ(hit->elevation, 20.0);
    // 20 m up the ray, short of the centre by 20 / tan(30 deg) = 34.6 m.
    EXPECT_NEAR(hit->latLng.latitude(), 46.0 - 34.64 / kMetersPerDegree, 3e-6);
}

TEST(TerrainCamera, RaycastMissesWhenTheTerrainIsBelowTheBand) {
    Transform transform;
            setCamera(transform, 46.0, 8.0, 12.0, 0.0, 45.0);
    const TransformState& state = transform.getState();
    // Deep sea floor: the ray never goes below it inside the searched band (-500 m up).
    const ElevationFn deep = [](const LatLng&) { return std::optional<double>(-2000.0); };
    EXPECT_FALSE(state.raycastTerrain({400.0, 300.0}, deep).has_value());
}

TEST(TerrainCamera, RaycastAboveHorizonIsNullopt) {
    Transform transform;
            setCamera(transform, 46.0, 8.0, 10.0, 0.0, 80.0);
    const TransformState& state = transform.getState();
    const ElevationFn flat = [](const LatLng&) { return std::optional<double>(0.0); };
    EXPECT_FALSE(state.raycastTerrain({400.0, 600.0}, flat).has_value()); // top row, y up
}

TEST(TerrainCamera, RaycastCameraInsideTerrainIsNullopt) {
    Transform transform;
            setCamera(transform, 46.0, 8.0, 14.0, 0.0, 60.0);
    const TransformState& state = transform.getState();
    // Terrain taller than the whole searched band: the ray starts below the surface and never enters from above.
    const ElevationFn huge = [](const LatLng&) { return std::optional<double>(100000.0); };
    EXPECT_FALSE(state.raycastTerrain({400.0, 300.0}, huge).has_value());
}

TEST(TerrainCamera, RaycastUnknownTerrainIsNullopt) {
    Transform transform;
            setCamera(transform, 46.0, 8.0, 12.0, 0.0, 50.0);
    const TransformState& state = transform.getState();
    const ElevationFn unknown = [](const LatLng&) { return std::optional<double>(); };
    EXPECT_FALSE(state.raycastTerrain({400.0, 300.0}, unknown).has_value());
    // Known near the camera but unknown further out along the ray: also no answer (no guessing).
    const ElevationFn partial = [](const LatLng& ll) {
        return ll.latitude() < 46.0 ? std::optional<double>(0.0) : std::optional<double>();
    };
    EXPECT_FALSE(state.raycastTerrain({400.0, 300.0}, partial).has_value());
}

// MARK: Map::setLocationAtPoint / latLngForPixelAtElevation

namespace {

class TerrainCameraMap {
public:
    util::RunLoop runLoop;
    std::shared_ptr<StubFileSource> fileSource = std::make_shared<StubFileSource>(ResourceOptions::Default(),
                                                                                 ClientOptions());
    StubMapObserver observer;
    HeadlessFrontend frontend{1};
    MapAdapter map{frontend,
                   observer,
                   fileSource,
                   MapOptions().withMapMode(MapMode::Static).withSize(frontend.getSize()).withPixelRatio(1)};

    TerrainCameraMap() { map.setBounds(BoundOptions().withMaxPitch(85.0)); }
};

} // namespace

TEST(TerrainCamera, MapPixelIsYDownAndMatchesTransformAtSeaLevel) {
    TerrainCameraMap test;
    test.map.jumpTo(CameraOptions().withCenter(LatLng{40.0, -3.0}).withZoom(10.0).withBearing(15.0).withPitch(70.0));
    const Size size = test.frontend.getSize();
    // A pixel in the lower part of the screen (y down) is nearer the camera than the centre. Missing the flip would
    // return the mirror point, beyond the centre.
    const ScreenCoordinate lower{size.width / 2.0, size.height * 0.9};
    const auto at = test.map.latLngForPixelAtElevation(lower, 0.0);
    ASSERT_TRUE(at.has_value());
    const LatLng reference = test.map.latLngForPixel(lower);
    EXPECT_NEAR(at->latitude(), reference.latitude(), 1e-7);
    EXPECT_NEAR(at->longitude(), reference.longitude(), 1e-7);
    // Bearing 15: north-up is rotated, but a lower pixel is still on the camera's side, i.e. south-ish of the centre.
    EXPECT_LT(at->latitude(), 40.0);
}

TEST(TerrainCamera, SetLocationAtPointKeepsGrabbedPointUnderThePixel) {
    for (const double pitch : {0.0, 45.0, 70.0}) {
        for (const double elevation : {0.0, 900.0}) {
            TerrainCameraMap test;
            test.map.jumpTo(
                CameraOptions().withCenter(LatLng{47.0, 10.0}).withZoom(12.0).withBearing(-40.0).withPitch(pitch));
            SCOPED_TRACE(testing::Message() << "pitch " << pitch << " elevation " << elevation);
            const Size size = test.frontend.getSize();
            const ScreenCoordinate grab{size.width * 0.35, size.height * 0.7};
            const auto grabbed = test.map.latLngForPixelAtElevation(grab, elevation);
            ASSERT_TRUE(grabbed.has_value());

            // Drag: the grabbed point is now wanted under a pixel 80 px right and 50 px up.
            const ScreenCoordinate target{grab.x + 80.0, grab.y - 50.0};
            ASSERT_TRUE(test.map.setLocationAtPoint(*grabbed, target, elevation));
            const auto under = test.map.latLngForPixelAtElevation(target, elevation);
            ASSERT_TRUE(under.has_value());
            // setLocationAtPoint is one pass (as in GL JS), and an elevation above 0 converts metres at the centre
            // latitude only: a residue of a few decimetres (1e-5 degrees is ~1 m) is expected.
            EXPECT_NEAR(under->latitude(), grabbed->latitude(), 1e-5);
            EXPECT_NEAR(under->longitude(), grabbed->longitude(), 1e-5);
            // And the camera did move (the centre is no longer where it was).
            EXPECT_NE(test.map.getCameraOptions().center->latitude(), 47.0);
        }
    }
}

TEST(TerrainCamera, SetLocationAtPointWithoutElevationUsesCentreAltitude) {
    TerrainCameraMap test;
    test.map.jumpTo(CameraOptions().withCenter(LatLng{10.0, 10.0}).withZoom(8.0).withPitch(30.0));
    const Size size = test.frontend.getSize();
    const ScreenCoordinate pixel{size.width * 0.6, size.height * 0.4};
    const auto before = test.map.latLngForPixelAtElevation(pixel, test.map.getElevation());
    ASSERT_TRUE(before.has_value());
    const LatLng wanted{before->latitude() + 0.01, before->longitude() - 0.02};
    ASSERT_TRUE(test.map.setLocationAtPoint(wanted, pixel, std::nullopt));
    const auto after = test.map.latLngForPixelAtElevation(pixel, test.map.getElevation());
    ASSERT_TRUE(after.has_value());
    EXPECT_NEAR(after->latitude(), wanted.latitude(), 1e-7);
    EXPECT_NEAR(after->longitude(), wanted.longitude(), 1e-7);
}

TEST(TerrainCamera, SetLocationAtPointCrossesTheAntimeridian) {
    TerrainCameraMap test;
    test.map.jumpTo(CameraOptions().withCenter(LatLng{10.0, 179.95}).withZoom(9.0).withPitch(40.0));
    const Size size = test.frontend.getSize();
    const ScreenCoordinate pixel{size.width * 0.5, size.height * 0.5};
    const auto start = test.map.latLngForPixelAtElevation(pixel, 0.0);
    ASSERT_TRUE(start.has_value());

    // The wanted location is 0.2 degrees east: across the antimeridian, given as -179.85 (wrapped).
    const LatLng wanted{start->latitude(), -179.85};
    ASSERT_TRUE(test.map.setLocationAtPoint(wanted, pixel, 0.0));
    const auto after = test.map.latLngForPixelAtElevation(pixel, 0.0);
    ASSERT_TRUE(after.has_value());
    // The camera takes the short way east (a few tenths of a degree), it does not swing round the world.
    double dLon = after->longitude() - (-179.85);
    dLon -= 360.0 * std::round(dLon / 360.0);
    EXPECT_NEAR(dLon, 0.0, 1e-6);
    const auto centre = test.map.getCameraOptions().center;
    ASSERT_TRUE(centre.has_value());
    double moved = centre->longitude() - 179.95;
    moved -= 360.0 * std::round(moved / 360.0);
    EXPECT_NEAR(moved, 0.2, 0.05);
}

TEST(TerrainCamera, SetLocationAtPointAboveHorizonFails) {
    TerrainCameraMap test;
    test.map.jumpTo(CameraOptions().withCenter(LatLng{45.0, 5.0}).withZoom(10.0).withPitch(80.0));
    const auto before = test.map.getCameraOptions();
    EXPECT_FALSE(test.map.setLocationAtPoint(LatLng{45.0, 5.0}, ScreenCoordinate{400.0, 1.0}, 0.0));
    const auto after = test.map.getCameraOptions();
    EXPECT_DOUBLE_EQ(before.center->latitude(), after.center->latitude());
    EXPECT_DOUBLE_EQ(before.center->longitude(), after.center->longitude());
}

// MARK: Transform::putCenterOnTerrainKeepingCamera

TEST(TerrainCamera, PutCenterOnTerrainKeepsCameraAndPicture) {
    for (const double pitch : {30.0, 60.0, 75.0}) {
        for (const double bearing : {0.0, 33.0, -110.0}) {
            Transform transform;
            setCamera(transform, 46.0, 8.0, 12.0, bearing, pitch);
            SCOPED_TRACE(testing::Message() << "pitch " << pitch << " bearing " << bearing);
            const double terrainHeight = 700.0;
            const ElevationFn flat = [terrainHeight](const LatLng&) { return std::optional<double>(terrainHeight); };

            const auto cameraBefore = transform.getFreeCameraOptions();
            std::vector<std::optional<LatLng>> before;
            const std::vector<ScreenCoordinate> pixels{{100, 100}, {400, 300}, {700, 100}, {250, 200}};
            for (const auto& p : pixels) {
                before.push_back(transform.getState().screenCoordinateToLatLngAtElevation(p, terrainHeight));
            }

            ASSERT_TRUE(transform.putCenterOnTerrainKeepingCamera(flat));

            // The centre now sits on the terrain.
            EXPECT_NEAR(transform.getState().getCenterAltitude(), terrainHeight, 1e-6);
            // The camera did not move or turn.
            const auto cameraAfter = transform.getFreeCameraOptions();
            ASSERT_TRUE(cameraBefore.position && cameraAfter.position);
            for (int i = 0; i < 3; ++i) {
                // The solve uses flat-earth metres per degree at the centre: measured drift up to 1.6e-7 world units (~6 m)
                // at pitch 75, where the camera is ~15 km from the centre.
                EXPECT_NEAR((*cameraBefore.position)[i], (*cameraAfter.position)[i], 3e-7);
            }
            ASSERT_TRUE(cameraBefore.orientation && cameraAfter.orientation);
            for (int i = 0; i < 4; ++i) {
                EXPECT_NEAR((*cameraBefore.orientation)[i], (*cameraAfter.orientation)[i], 1e-6);
            }
            // The picture of the terrain surface did not change: every pixel still sees the same ground point.
            for (size_t i = 0; i < pixels.size(); ++i) {
                const auto after = transform.getState().screenCoordinateToLatLngAtElevation(pixels[i], terrainHeight);
                ASSERT_TRUE(before[i] && after);
                // Same residue (measured up to 2.3e-5 degrees, ~2.5 m): 5e-5 degrees allowed. Pixels above the centre are not
                // checked: at pitch 75 they look ~40 km out at a grazing angle, where the same metre-sized pose error
                // grows to tens of metres (measured 2.5e-4 degrees).
                EXPECT_NEAR(before[i]->latitude(), after->latitude(), 5e-5);
                EXPECT_NEAR(before[i]->longitude(), after->longitude(), 5e-5);
            }
        }
    }
}

TEST(TerrainCamera, PutCenterOnTerrainFailsWithoutElevation) {
    Transform transform;
            setCamera(transform, 46.0, 8.0, 12.0, 0.0, 60.0);
    const auto cameraBefore = transform.getFreeCameraOptions();
    const ElevationFn unknown = [](const LatLng&) { return std::optional<double>(); };
    EXPECT_FALSE(transform.putCenterOnTerrainKeepingCamera(unknown));
    const auto cameraAfter = transform.getFreeCameraOptions();
    for (int i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ((*cameraBefore.position)[i], (*cameraAfter.position)[i]);
    }
}

// MARK: Globe: the terrain primitives are mercator-only and must degrade safely

namespace {

void setCameraGlobe(Transform& transform, double lat, double lon, double zoom, double bearing = 0, double pitch = 0) {
    transform.resize({800, 600});
    transform.setProjectionDefinition(ProjectionDefinition("vertical-perspective"));
    transform.jumpTo(CameraOptions().withCenter(LatLng{lat, lon}).withZoom(zoom).withBearing(bearing).withPitch(pitch));
}

} // namespace

TEST(TerrainCameraGlobe, RaycastTerrainIsNullopt) {
    Transform transform;
    setCameraGlobe(transform, 20.0, 30.0, 3.0, 10.0, 30.0);
    ASSERT_TRUE(transform.getState().isGlobeRendering());
    const ElevationFn flat = [](const LatLng&) { return std::optional<double>(0.0); };
    EXPECT_FALSE(transform.getState().raycastTerrain({400.0, 300.0}, flat).has_value());
}

TEST(TerrainCameraGlobe, PutCenterOnTerrainRefusesAndLeavesTheCamera) {
    Transform transform;
    setCameraGlobe(transform, 20.0, 30.0, 3.0, 10.0, 30.0);
    ASSERT_TRUE(transform.getState().isGlobeRendering());
    const double zoom = transform.getZoom();
    const LatLng centre = transform.getLatLng();
    const ElevationFn flat = [](const LatLng&) { return std::optional<double>(500.0); };
    EXPECT_FALSE(transform.putCenterOnTerrainKeepingCamera(flat));
    EXPECT_DOUBLE_EQ(transform.getZoom(), zoom);
    EXPECT_DOUBLE_EQ(transform.getLatLng().latitude(), centre.latitude());
    EXPECT_DOUBLE_EQ(transform.getLatLng().longitude(), centre.longitude());
}

TEST(TerrainCameraGlobe, AtElevationUsesTheGlobeUnprojectionAndIgnoresElevation) {
    Transform transform;
    setCameraGlobe(transform, 20.0, 30.0, 3.0, 25.0, 20.0);
    const TransformState& state = transform.getState();
    ASSERT_TRUE(state.isGlobeRendering());

    // The centre pixel is the centre, at any elevation (the sphere has no terrain plane).
    for (const double elevation : {0.0, 3000.0}) {
        const auto centre = state.screenCoordinateToLatLngAtElevation({400.0, 300.0}, elevation);
        ASSERT_TRUE(centre.has_value());
        EXPECT_NEAR(centre->latitude(), 20.0, 1e-6);
        EXPECT_NEAR(centre->longitude(), 30.0, 1e-6);
    }
    // Off-centre pixels agree with the plain globe unprojection (y up in both).
    const ScreenCoordinate pixel{300.0, 250.0};
    const auto atElevation = state.screenCoordinateToLatLngAtElevation(pixel, 1234.0);
    ASSERT_TRUE(atElevation.has_value());
    const LatLng plain = state.screenCoordinateToLatLng(pixel);
    EXPECT_DOUBLE_EQ(atElevation->latitude(), plain.latitude());
    EXPECT_DOUBLE_EQ(atElevation->longitude(), plain.longitude());
}

TEST(TerrainCameraGlobe, MapReportsGlobeAndHasNoTerrainPoint) {
    TerrainCameraMap test;
    EXPECT_FALSE(test.map.isGlobeRendering());
    test.map.setProjectionType(ProjectionType::Globe);
    test.map.jumpTo(CameraOptions().withCenter(LatLng{0.0, 0.0}).withZoom(2.0));
    EXPECT_TRUE(test.map.isGlobeRendering());
    // No terrain source: no terrain point, globe or not.
    EXPECT_FALSE(test.map.terrainPointForPixel({400.0, 300.0}).has_value());
}
