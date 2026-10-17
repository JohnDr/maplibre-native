#pragma once

#include <mln/renderer/layer_group.hpp>
#include <mln/gfx/uniform_buffer.hpp>
#include <mln/gfx/texture2d.hpp>
#include <mln/shaders/shader_program_base.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/tile/tile_id.hpp>
#include <mln/util/tileset.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace mln {

namespace gfx {
class Context;
class ShaderRegistry;
} // namespace gfx

class TransformState;
class UpdateParameters;
struct EvaluatedSky;
class PaintParameters;
class TerrainTileManager;

/// Renders the terrain as a shaded, elevated surface: for each DEM tile, a grid mesh whose vertices carry a
/// world-pixel elevation and a precomputed hillshade. Depth-writing; drawn after the flat layers (draping lands later).
class TerrainPass {
public:
    TerrainPass();
    ~TerrainPass();

    void update(gfx::ShaderRegistry&, gfx::Context&, const TransformState&, const PaintParameters&, const UpdateParameters&, const EvaluatedSky*);

    /// The layer group holding the terrain drawables, or null before the first enable.
    LayerGroupBase* getLayerGroup() const { return layerGroup.get(); }

    /// Called when a DEM tile loads, so the map can schedule another frame.
    void setTileLoadedCallback(std::function<void()> callback) { onTileLoaded = std::move(callback); }

    /// The terrain tiles currently covered, in the same order as their drape textures.
    const std::vector<OverscaledTileID>& getCover() const { return cover; }

    /// Per-tile DEM elevation range for the tile covers (GL JS `Terrain#getMinMaxElevation`), or null without terrain.
    std::shared_ptr<const TileElevationRangeFn> elevationRangeFunction(double exaggeration) const;

    /// Bind each terrain drawable to the draped texture of its own tile (per-tile RTT).
    void applyDrapeTextures(const std::map<std::tuple<uint8_t, uint32_t, uint32_t>, gfx::Texture2DPtr>& textures);

    /// A function sampling the terrain elevation (world pixels) at a tile's centre, for symbol elevation.
    std::function<float(const UnwrappedTileID&)> elevationFunction(const TransformState& state, double exaggeration);

private:
    TileLayerGroupPtr layerGroup;
    gfx::ShaderProgramBasePtr shader;
    gfx::UniformBufferPtr fogUniformBuffer;
    gfx::UniformBufferPtr projectionUniformBuffer;
    std::vector<OverscaledTileID> cover;
    bool globeShader = false;
    std::shared_ptr<TerrainTileManager> terrain;
    std::optional<Tileset> tileset;
    std::function<void()> onTileLoaded;
    uint8_t demZoom = 0;
    double builtZoom = -1;
    double builtExaggeration = -1;
};

} // namespace mln
