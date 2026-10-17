#pragma once

#include <mln/renderer/globe_depth_pass.hpp>
#include <mln/renderer/sky_pass.hpp>
#include <mln/renderer/render_sky.hpp>
#include <mln/renderer/terrain_pass.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/actor/scheduler.hpp>
#include <mln/renderer/renderer.hpp>
#include <mln/renderer/render_source_observer.hpp>
#include <mln/renderer/render_light.hpp>
#include <mln/style/image.hpp>
#include <mln/style/source.hpp>
#include <mln/style/layer.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/map/zoom_history.hpp>
#include <mln/text/cross_tile_symbol_index.hpp>
#include <mln/text/glyph_manager_observer.hpp>
#include <mln/renderer/image_manager_observer.hpp>
#include <mln/text/placement.hpp>
#include <mln/renderer/render_tree.hpp>

#include <map>
#include <memory>
#include <functional>
#include <set>
#include <tuple>
#include <string>
#include <unordered_map>
#include <vector>

namespace mln {
class ChangeRequest;
class RendererObserver;
class RenderSource;
class UpdateParameters;
class RenderStaticData;
class RenderedQueryOptions;
class SourceQueryOptions;
class GlyphManager;
class ImageManager;
class LineAtlas;
class PatternAtlas;
class CrossTileSymbolIndex;
class RenderTree;

namespace gfx {
class ShaderRegistry;
class Drawable;
class UploadPass;
using DrawablePtr = std::shared_ptr<Drawable>;
class DynamicTextureAtlas;
using DynamicTextureAtlasPtr = std::shared_ptr<gfx::DynamicTextureAtlas>;
} // namespace gfx

class UnwrappedTileID;

namespace style {
class LayerProperties;
} // namespace style

using ImmutableLayer = Immutable<style::Layer::Impl>;

class RenderOrchestrator final : public GlyphManagerObserver, public ImageManagerObserver, public RenderSourceObserver {
public:
    RenderOrchestrator(bool backgroundLayerAsColor_,
                       TaggedScheduler& threadPool_,
                       const std::optional<std::string>& localFontFamily_);
    ~RenderOrchestrator() override;

#if MLN_RENDER_BACKEND_OPENGL
    void enableAndroidEmulatorGoldfishMitigation(bool enable) { androidGoldfishMitigationEnabled = enable; }
#endif

    void markContextLost() { contextLost = true; };
    // TODO: Introduce RenderOrchestratorObserver.
    void setObserver(RendererObserver*);

    std::unique_ptr<RenderTree> createRenderTree(const std::shared_ptr<UpdateParameters>&, gfx::DynamicTextureAtlasPtr);

    std::vector<Feature> queryRenderedFeatures(const ScreenLineString&, const RenderedQueryOptions&) const;
    std::vector<Feature> querySourceFeatures(const std::string& sourceID, const SourceQueryOptions&) const;
    std::vector<Feature> queryShapeAnnotations(const ScreenLineString&) const;

    FeatureExtensionValue queryFeatureExtensions(const std::string& sourceID,
                                                 const Feature& feature,
                                                 const std::string& extension,
                                                 const std::string& extensionField,
                                                 const std::optional<std::map<std::string, Value>>& args) const;

    void setFeatureState(const std::string& sourceID,
                         const std::optional<std::string>& layerID,
                         const std::string& featureID,
                         const FeatureState& state);

    void getFeatureState(FeatureState& state,
                         const std::string& sourceID,
                         const std::optional<std::string>& layerID,
                         const std::string& featureID) const;

    void removeFeatureState(const std::string& sourceID,
                            const std::optional<std::string>& sourceLayerID,
                            const std::optional<std::string>& featureID,
                            const std::optional<std::string>& stateKey);

    void setTileCacheEnabled(bool);
    bool getTileCacheEnabled() const;
    void reduceMemoryUse();
    void dumpDebugLogs();
    void collectPlacedSymbolData(bool);
    const std::vector<PlacedSymbolData>& getPlacedSymbolsData() const;
    void clearData();

    void update(const std::shared_ptr<UpdateParameters>&);

    bool addLayerGroup(LayerGroupBasePtr);
    bool removeLayerGroup(const LayerGroupBasePtr&);
    size_t numLayerGroups() const noexcept;
    void updateLayerIndex(LayerGroupBasePtr, int32_t newIndex);

    LayerGroupBase* getGlobeDepthLayerGroup() const { return globeDepthPass.getLayerGroup(); }

    LayerGroupBase* getTerrainLayerGroup() const { return terrainPass.getLayerGroup(); }
    LayerGroupBase* getSkyLayerGroup() const { return skyPass.getLayerGroup(); }
    LayerGroupBase* getAtmosphereLayerGroup() const { return skyPass.getAtmosphereLayerGroup(); }

    // Sampler of the terrain elevation (world pixels) at a tile centre, for symbol elevation.
    std::function<float(const UnwrappedTileID&)> getTerrainElevationFunction(const TransformState& state,
                                                                             double exaggeration) {
        return terrainPass.elevationFunction(state, exaggeration);
    }

    // True when per-tile terrain drape targets exist: the drape-eligible layers are rendered into their textures and
    // drawn by the terrain, so they must not also be drawn directly (their drawables are skipped); the remaining
    // "live" layers (symbols) draw on top of the terrain.
    bool isTerrainDrapeActive() const { return !terrainDrapeTargets.empty(); }
    // Whether the style layer at this index is rendered into the terrain drape (GL JS `LAYERS_TO_TEXTURES`).
    bool isDrapeLayer(int32_t layerIndex) const { return drapeLayerIndices.contains(layerIndex); }

    template <typename Func /* void(LayerGroupBase&) */>
    void visitLayerGroups(Func f) {
        for (auto& pair : layerGroupsByLayerIndex) {
            if (pair.second) {
                try {
                    f(*pair.second);
                } catch (...) {
                    observer->onRenderError(std::current_exception());
                }
            }
        }
    }

    template <typename Func /* void(LayerGroupBase&) */>
    void visitLayerGroupsReversed(Func f) {
        for (auto rit = layerGroupsByLayerIndex.rbegin(); rit != layerGroupsByLayerIndex.rend(); ++rit) {
            if (rit->second) {
                try {
                    f(*rit->second);
                } catch (...) {
                    observer->onRenderError(std::current_exception());
                }
            }
        }
    }

    void updateLayers(gfx::ShaderRegistry&,
                      gfx::Context&,
                      const TransformState&,
                      const std::shared_ptr<UpdateParameters>&,
                      const PaintParameters& paintParameters,
                      const RenderTree&);

    void processChanges();

    bool addRenderTarget(RenderTargetPtr);
    bool removeRenderTarget(const RenderTargetPtr&);

    template <typename Func /* void(RenderTarget&) */>
    void visitRenderTargets(Func f) {
        for (auto& renderTarget : renderTargets) {
            f(*renderTarget);
        }
    }

    void updateDebugLayerGroups(const RenderTree& renderTree, PaintParameters& parameters);

    // Terrain drape: per-tile render targets holding the draped map layers, rendered with a per-tile projection.
    void uploadTerrainDrapeTargets(gfx::UploadPass&);
    void renderTerrainDrapeTargets(const RenderTree&, PaintParameters&);

    template <typename Func /* void(LayerGroupBase&) */>
    void visitDebugLayerGroups(Func f) {
        for (auto& pair : debugLayerGroups) {
            if (pair.second) {
                f(*pair.second);
            }
        }
    }

    const ZoomHistory& getZoomHistory() const { return zoomHistory; }

private:
    bool isLoaded() const;
    bool hasTransitions(TimePoint) const;

    RenderSource* getRenderSource(const std::string& id) const;

    RenderLayer* getRenderLayer(const std::string& id);
    const RenderLayer* getRenderLayer(const std::string& id) const;

    void queryRenderedSymbols(std::unordered_map<std::string, std::vector<Feature>>& resultsByLayer,
                              const ScreenLineString& geometry,
                              const std::unordered_map<std::string, const RenderLayer*>& layers,
                              const RenderedQueryOptions& options) const;

    std::vector<Feature> queryRenderedFeatures(const ScreenLineString&,
                                               const RenderedQueryOptions&,
                                               const std::unordered_map<std::string, const RenderLayer*>&) const;

    // GlyphManagerObserver implementation.
    void onGlyphsLoaded(const FontStack&, const GlyphRange&) override;
    void onGlyphsError(const FontStack&, const GlyphRange&, std::exception_ptr) override;
    void onGlyphsRequested(const FontStack&, const GlyphRange&) override;
    // RenderSourceObserver implementation.
    void onTileChanged(RenderSource&, const OverscaledTileID&) override;
    void onTileError(RenderSource&, const OverscaledTileID&, std::exception_ptr) override;
    void onTileAction(RenderSource&, TileOperation, const OverscaledTileID&, const std::string&) override;
    void onSymbolError(RenderSource&, const std::string&) override;

    // ImageManagerObserver implementation
    void onStyleImageMissing(const std::string&, const std::function<void()>&) override;
    void onRemoveUnusedStyleImages(const std::vector<std::string>&) override;

    /// Move changes into the pending set, clearing the provided collection
    void addChanges(UniqueChangeRequestVec&);

    RendererObserver* observer;

    ZoomHistory zoomHistory;
    TransformState transformState;

    std::shared_ptr<GlyphManager> glyphManager;
    std::shared_ptr<ImageManager> imageManager;
    std::unique_ptr<LineAtlas> lineAtlas;
    std::unique_ptr<PatternAtlas> patternAtlas;

    Immutable<std::vector<Immutable<style::Image::Impl>>> imageImpls;
    Immutable<std::vector<Immutable<style::Source::Impl>>> sourceImpls;
    Immutable<std::vector<Immutable<style::Layer::Impl>>> layerImpls;

    std::unordered_map<std::string, std::unique_ptr<RenderSource>> renderSources;
    std::unordered_map<std::string, std::unique_ptr<RenderLayer>> renderLayers;
    RenderLight renderLight;

    CrossTileSymbolIndex crossTileSymbolIndex;
    PlacementController placementController;

    const bool backgroundLayerAsColor;
    bool contextLost = false;
    bool placedSymbolDataCollected = false;
    bool tileCacheEnabled = true;

#if MLN_RENDER_BACKEND_OPENGL
    bool androidGoldfishMitigationEnabled{false};
#endif

    // Vectors with reserved capacity of layerImpls->size() to avoid
    // reallocation on each frame.
    std::vector<Immutable<style::LayerProperties>> filteredLayersForSource;
    RenderLayerReferences orderedLayers;
    RenderLayerReferences layersNeedPlacement;

    TaggedScheduler threadPool;

    std::vector<std::unique_ptr<ChangeRequest>> pendingChanges;

    using LayerGroupMap = std::multimap<int32_t, LayerGroupBasePtr>;
    LayerGroupMap layerGroupsByLayerIndex;

    std::vector<RenderTargetPtr> renderTargets;

    // Per-tile offscreen targets holding the map layers draped over the terrain, keyed by tile (z, x, y).
    std::map<std::tuple<uint8_t, uint32_t, uint32_t>, RenderTargetPtr> terrainDrapeTargets;
    uint32_t terrainDrapeTileSize = 0;
    // Per-tile fingerprint of the state the drape texture was rendered from (GL JS `RTTFingerprint`): the style
    // identity and the source tiles under the drape tile with their revisions. A tile is re-rendered only when it
    // changes.
    std::map<std::tuple<uint8_t, uint32_t, uint32_t>, uint64_t> terrainDrapeFingerprints;
    std::set<std::tuple<uint8_t, uint32_t, uint32_t>> drapeTilesToRender;
    // How often each source tile's data changed, keyed by (source id, tile z, x, y).
    std::map<std::tuple<std::string, uint8_t, uint32_t, uint32_t>, uint64_t> drapeTileRevisions;
    // Style layer indices rendered into the drape (background/fill/line/raster/hillshade/color-relief).
    std::set<int32_t> drapeLayerIndices;
    RenderItem::DebugLayerGroupMap debugLayerGroups;
    GlobeDepthPass globeDepthPass;
    SkyPass skyPass;
    std::optional<RenderSky> renderSky;
    TerrainPass terrainPass;
};

} // namespace mln
