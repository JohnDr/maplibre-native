#pragma once

#include <mln/renderer/layer_group.hpp>
#include <mln/gfx/uniform_buffer.hpp>
#include <mln/shaders/shader_program_base.hpp>

#include <array>
#include <optional>

#include <mln/util/color.hpp>

namespace mln {

namespace gfx {
class Context;
class ShaderRegistry;
} // namespace gfx

class TransformState;
class PaintParameters;
struct EvaluatedSky;

/// The style light's direction, which the atmosphere uses as the sun (GL JS `getSunPos`).
struct SunLight {
    std::array<float, 3> position{{0.0f, 0.0f, 1.0f}}; // the light position as a Cartesian vector
    bool anchoredToMap = false;
};

/// Draws the style's `sky` (MapLibre GL JS `drawSky` / `drawAtmosphere`): a screen quad shaded by the distance above
/// the horizon (the planet's limb on the globe), and the globe's atmosphere over the scene.
class SkyPass {
public:
    SkyPass();
    ~SkyPass();

    void update(gfx::ShaderRegistry&,
                gfx::Context&,
                const TransformState&,
                const PaintParameters&,
                const EvaluatedSky*,
                const SunLight&,
                const std::optional<Color>& spaceColor);

    /// The layer group holding the sky quad (drawn first), or null before the first sky.
    LayerGroupBase* getLayerGroup() const { return skyGroup.get(); }
    /// The layer group holding the atmosphere quad (drawn over the scene), or null before the first globe sky.
    LayerGroupBase* getAtmosphereLayerGroup() const { return atmosphereGroup.get(); }

private:
    TileLayerGroupPtr skyGroup;
    TileLayerGroupPtr atmosphereGroup;
    gfx::ShaderProgramBasePtr skyShader;
    gfx::ShaderProgramBasePtr atmosphereShader;
    gfx::UniformBufferPtr skyBuffer;
    gfx::UniformBufferPtr atmosphereBuffer;
};

} // namespace mln
