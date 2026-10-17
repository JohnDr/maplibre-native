#pragma once

#include <mln/annotation/annotation_manager.hpp>
#include <mln/map/map.hpp>
#include <mln/map/map_observer.hpp>
#include <mln/map/map_options.hpp>
#include <mln/map/mode.hpp>
#include <mln/map/transform.hpp>
#include <mln/renderer/renderer_frontend.hpp>
#include <mln/renderer/renderer_observer.hpp>
#include <mln/style/observer.hpp>
#include <mln/style/source.hpp>
#include <mln/style/style.hpp>
#include <mln/util/size.hpp>
#include <mln/tile/tile_operation.hpp>

#include <numbers>

namespace mln {

class FileSource;
class TerrainTileManager;
class ResourceTransform;

namespace gfx {
class ShaderRegistry;
} // namespace gfx

namespace util {
class ActionJournal;
} // namespace util

struct StillImageRequest {
    StillImageRequest(Map::StillImageCallback&& callback_)
        : callback(std::move(callback_)) {}

    Map::StillImageCallback callback;
};

class Map::Impl final : public TransformObserver, public style::Observer, public RendererObserver {
public:
    Impl(RendererFrontend&, MapObserver&, std::shared_ptr<FileSource>, const MapOptions&);
    ~Impl() final;

    // CFID: set the backdrop colour (space) used by the renderer.
    void setBackgroundColor(const Color& color) {
        backgroundColor = color;
        onUpdate();
    }

    // TransformObserver
    void onCameraWillChange(MapObserver::CameraChangeMode) final;
    void onCameraIsChanging() final;
    void onCameraDidChange(MapObserver::CameraChangeMode) final;

    // StyleObserver
    void onSourceChanged(style::Source&) final;
    void onUpdate() final;
    void onStyleLoading() final;
    void onStyleLoaded() final;
    void onStyleError(std::exception_ptr) final;
    void onSpriteLoaded(const std::optional<style::Sprite>&) final;
    void onSpriteError(const std::optional<style::Sprite>&, std::exception_ptr) final;
    void onSpriteRequested(const std::optional<style::Sprite>&) final;

    // RendererObserver
    void onInvalidate() final;
    void onResourceError(std::exception_ptr) final;
    void onWillStartRenderingFrame() final;
    void onDidFinishRenderingFrame(RenderMode, bool, bool, const gfx::RenderingStats&) final;
    void onWillStartRenderingMap() final;
    void onDidFinishRenderingMap() final;
    void onStyleImageMissing(const std::string&, const std::function<void()>&) final;
    void onRemoveUnusedStyleImages(const std::vector<std::string>&) final;
    void onRegisterShaders(gfx::ShaderRegistry&) final;

    void onPreCompileShader(shaders::BuiltIn, gfx::Backend::Type, const std::string&) final;
    void onPostCompileShader(shaders::BuiltIn, gfx::Backend::Type, const std::string&) final;
    void onShaderCompileFailed(shaders::BuiltIn, gfx::Backend::Type, const std::string&) final;
    void onGlyphsLoaded(const FontStack&, const GlyphRange&) final;
    void onGlyphsError(const FontStack&, const GlyphRange&, std::exception_ptr) final;
    void onGlyphsRequested(const FontStack&, const GlyphRange&) final;
    void onTileAction(TileOperation op, const OverscaledTileID&, const std::string&) final;
    void onRenderError(std::exception_ptr) final;
    void onSymbolError(const std::string&) final;

    // Map
    void jumpTo(const CameraOptions&);

    /// 3D terrain: the drawn terrain height at `latLng` in metres, without the exaggeration, or nullopt where it is
    /// not loaded (or there is no terrain). Fetches the DEM tiles it needs.
    std::optional<double> terrainElevationAt(const LatLng&);
    /// 3D terrain: sets the camera centre's altitude from the terrain under it and keeps the camera out of the terrain.
    void updateTerrainCamera();

    bool isRenderingStatsViewEnabled() const;
    void enableRenderingStatsView(bool value);

    MapObserver& observer;
    RendererFrontend& rendererFrontend;
    std::unique_ptr<util::ActionJournal> actionJournal;

    Transform transform;

    const MapMode mode;
    const float pixelRatio;
    const bool crossSourceCollisions;
    const bool fastPFOREnabled;

    MapDebugOptions debugOptions{MapDebugOptions::NoDebug};
    std::unique_ptr<gfx::RenderingStatsView> renderingStatsView;

    std::shared_ptr<FileSource> fileSource;

    std::unique_ptr<style::Style> style;
    AnnotationManager annotationManager;

    bool cameraMutated = false;

    uint8_t prefetchZoomDelta = util::DEFAULT_PREFETCH_ZOOM_DELTA;

    bool loading = false;
    bool rendererFullyLoaded;
    std::unique_ptr<StillImageRequest> stillImageRequest;

    double tileLodMinRadius = 3;
    double tileLodScale = 1;
    double tileLodPitchThreshold = (60.0 / 180.0) * std::numbers::pi;
    double tileLodZoomShift = 0;
    TileLodMode tileLodMode = TileLodMode::Default;

    // CFID: custom backdrop colour applied to the render-tree background.
    std::optional<Color> backgroundColor;

    // 3D terrain camera: a DEM sampler on this (map) thread, since the renderer's own one is not reachable from here.
    std::unique_ptr<TerrainTileManager> terrainElevation;
    double terrainElevationExaggeration = 1.0;
    uint16_t terrainTileSize = 512; // the terrain source's tile size (sets the terrain tiles' zoom)
    bool terrainElevationApplied = false;
};

// Forward declaration of this method is required for the MapProjection class
CameraOptions cameraForLatLngs(const std::vector<LatLng>& latLngs,
                               const Transform& transform,
                               const EdgeInsets& padding);

} // namespace mln
