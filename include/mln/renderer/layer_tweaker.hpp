#pragma once

#include <mln/map/projection_base.hpp>
#include <mln/shaders/layer_ubo.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/util/immutable.hpp>
#include <mln/util/containers.hpp>
#include <mln/util/mat4.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace mln {

namespace gfx {
class Context;
class Drawable;
class UniformBuffer;
class UniformBufferArray;
using UniformBufferPtr = std::shared_ptr<UniformBuffer>;
} // namespace gfx

namespace style {
class LayerProperties;
enum class TranslateAnchorType : bool;
} // namespace style

class LayerGroupBase;
class PaintParameters;
class RenderTree;
class TransformState;
class UnwrappedTileID;

/**
    Base class for layer tweakers, which manipulate layer group per frame
 */
class LayerTweaker {
protected:
    LayerTweaker(std::string id, Immutable<style::LayerProperties> properties);

public:
    LayerTweaker() = delete;
    virtual ~LayerTweaker() = default;

    const std::string& getID() const { return id; }

    virtual void execute(LayerGroupBase&, const PaintParameters&) = 0;

    void updateProperties(Immutable<style::LayerProperties>);

    /// Calculate matrices for this tile.
    /// @param nearClipped If true, the near plane is moved further to enhance depth buffer precision.
    /// @param inViewportPixelUnits If false, the translation is scaled based on the current zoom.
    static mat4 getTileMatrix(const UnwrappedTileID&,
                              const PaintParameters&,
                              const std::array<float, 2>& translation,
                              style::TranslateAnchorType,
                              bool nearClipped,
                              bool inViewportPixelUnits,
                              const gfx::Drawable& drawable,
                              bool aligned = false);

    /// The projection contract for this tile: `getTileMatrix` plus the per-tile projection fields.
    static ProjectionData getProjectionData(const UnwrappedTileID&,
                                            const PaintParameters&,
                                            const std::array<float, 2>& translation,
                                            style::TranslateAnchorType,
                                            bool nearClipped,
                                            bool inViewportPixelUnits,
                                            const gfx::Drawable& drawable,
                                            bool aligned = false);

    static shaders::ProjectionUBO toProjectionUBO(const ProjectionData&);

    /// Radians per pixel on the sphere for a tile (GL JS `globeExtrudeScale`); `latitudeScale` is cos(center latitude).
    static float globeExtrudeScale(const UnwrappedTileID&, float zoom, double latitudeScale);

protected:
    /// Determine whether this tweaker should apply to the given drawable
    bool checkTweakDrawable(const gfx::Drawable&) const;

#if MLN_UBO_CONSOLIDATION
    /// Upload the per-drawable projection blocks of this layer, indexed like the drawable UBO array
    void uploadProjectionUBOs(gfx::UniformBufferArray& layerUniforms,
                              const std::vector<shaders::ProjectionUBO>&,
                              gfx::Context&,
                              std::uint32_t baseElement = 0,
                              std::uint32_t totalElements = 0);
    gfx::UniformBufferPtr projectionUniformBuffer;

    /// Block layout of a layer's consolidated UBO arrays when it is drawn into terrain drape targets. All drape
    /// targets are recorded into one command buffer and each has its own projection, so every target needs its own
    /// block of `drawableCount` elements, selected per draw through `uboIndex`. Block 0 is the normal (non-drape)
    /// pass, drape target `ordinal` uses block `ordinal + 1`. Without draping `base` is 0 and `total` is
    /// `drawableCount`.
    struct DrapeUBOWindow {
        std::uint32_t base = 0;
        std::uint32_t total = 0;
    };
    static DrapeUBOWindow drapeUBOWindow(const PaintParameters&, std::size_t drawableCount);

    /// Write `count` elements of `elementSize` bytes at the window's block, sizing the buffer for all blocks.
    static void updateWindowedBuffer(gfx::UniformBufferPtr&,
                                     gfx::Context&,
                                     const void* data,
                                     std::size_t elementSize,
                                     std::size_t count,
                                     const DrapeUBOWindow&);
#endif

    /// Multiplies with the projection matrix (either default, near clipped or aligned) for the given drawable
    static void multiplyWithProjectionMatrix(/*in-out*/ mat4& matrix,
                                             const PaintParameters& parameters,
                                             const gfx::Drawable& drawable,
                                             bool nearClipped,
                                             bool aligned);

    std::string id;
    Immutable<style::LayerProperties> evaluatedProperties;

    // Indicates that the evaluated properties have changed
    bool propertiesUpdated = true;
};

} // namespace mln
