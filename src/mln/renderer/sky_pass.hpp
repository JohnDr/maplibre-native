#pragma once

#include <mln/renderer/layer_group.hpp>
#include <mln/gfx/uniform_buffer.hpp>
#include <mln/shaders/shader_program_base.hpp>

namespace mln {

namespace gfx {
class Context;
class ShaderRegistry;
} // namespace gfx

class TransformState;
class UpdateParameters;
class PaintParameters;

/// Draws the style's `sky` (MapLibre GL JS `drawSky`): a screen quad, shaded by the distance above the horizon.
class SkyPass {
public:
    SkyPass();
    ~SkyPass();

    void update(gfx::ShaderRegistry&,
                gfx::Context&,
                const TransformState&,
                const PaintParameters&,
                const UpdateParameters&);

    /// The layer group holding the sky quad, or null before the first sky.
    LayerGroupBase* getLayerGroup() const { return layerGroup.get(); }

private:
    TileLayerGroupPtr layerGroup;
    gfx::ShaderProgramBasePtr shader;
    gfx::UniformBufferPtr uniformBuffer;
};

} // namespace mln
