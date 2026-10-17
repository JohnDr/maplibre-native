#pragma once

#include <mln/renderer/layer_group.hpp>
#include <mln/shaders/shader_program_base.hpp>
#include <mln/util/tileset.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace mln {

namespace gfx {
class Context;
class ShaderRegistry;
} // namespace gfx

class TransformState;
class UpdateParameters;
class TerrainTileManager;

/// Renders the terrain as a shaded, elevated surface: for each DEM tile, a grid mesh whose vertices carry a
/// world-pixel elevation and a precomputed hillshade. Depth-writing; drawn after the flat layers (draping lands later).
class TerrainPass {
public:
    TerrainPass();
    ~TerrainPass();

    void update(gfx::ShaderRegistry&, gfx::Context&, const TransformState&, const UpdateParameters&);

    /// The layer group holding the terrain drawables, or null before the first enable.
    LayerGroupBase* getLayerGroup() const { return layerGroup.get(); }

    /// Called when a DEM tile loads, so the map can schedule another frame.
    void setTileLoadedCallback(std::function<void()> callback) { onTileLoaded = std::move(callback); }

private:
    TileLayerGroupPtr layerGroup;
    gfx::ShaderProgramBasePtr shader;
    std::unique_ptr<TerrainTileManager> terrain;
    std::optional<Tileset> tileset;
    std::function<void()> onTileLoaded;
    uint8_t demZoom = 0;
    double builtZoom = -1;
    double builtExaggeration = -1;
};

} // namespace mln
