#include <mln/renderer/layers/raster_layer_tweaker.hpp>

#include <mln/gfx/context.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_tree.hpp>
#include <mln/shaders/raster_layer_ubo.hpp>
#include <mln/style/layers/raster_layer_properties.hpp>
#include <mln/util/convert.hpp>
#include <mln/gfx/image_drawable_data.hpp>
#include <mln/util/logging.hpp>
#include <mln/gfx/uniform_buffer.hpp>

#include <algorithm>
#include <cstring>

namespace mln {

using namespace style;
using namespace shaders;

void RasterLayerTweaker::execute([[maybe_unused]] LayerGroupBase& layerGroup,
                                 [[maybe_unused]] const PaintParameters& parameters) {
    if (layerGroup.empty()) {
        return;
    }

    const auto& evaluated = static_cast<const RasterLayerProperties&>(*evaluatedProperties).evaluated;

    const auto spinWeights = [](float spin) -> std::array<float, 4> {
        spin = util::deg2radf(spin);
        const float s = std::sin(spin);
        const float c = std::cos(spin);
        std::array<float, 4> spin_weights = {{(2 * c + 1) / 3,
                                              (-std::numbers::sqrt3_v<float> * s - c + 1) / 3,
                                              (std::numbers::sqrt3_v<float> * s - c + 1) / 3,
                                              0}};
        return spin_weights;
    };
    const auto saturationFactor = [](float saturation) -> float {
        if (saturation > 0) {
            return 1.f - 1.f / (1.001f - saturation);
        } else {
            return -saturation;
        }
    };
    const auto contrastFactor = [](float contrast) -> float {
        if (contrast > 0) {
            return 1 / (1 - contrast);
        } else {
            return 1 + contrast;
        }
    };

    if (!evaluatedPropsUniformBuffer || propertiesUpdated) {
        const RasterEvaluatedPropsUBO propsUBO = {
            .spin_weights = spinWeights(evaluated.get<RasterHueRotate>()),
            .tl_parent = {{0.0f, 0.0f}},
            .scale_parent = 1.0f,
            .buffer_scale = 1.0f,
            .fade_t = 1.0f,
            .opacity = evaluated.get<RasterOpacity>(),
            .brightness_low = evaluated.get<RasterBrightnessMin>(),
            .brightness_high = evaluated.get<RasterBrightnessMax>(),
            .saturation_factor = saturationFactor(evaluated.get<RasterSaturation>()),
            .contrast_factor = contrastFactor(evaluated.get<RasterContrast>()),
            .pad1 = 0,
            .pad2 = 0};
        parameters.context.emplaceOrUpdateUniformBuffer(evaluatedPropsUniformBuffer, &propsUBO);
        propertiesUpdated = false;
    }
    auto& layerUniforms = layerGroup.mutableUniformBuffers();
    layerUniforms.set(idRasterEvaluatedPropsUBO, evaluatedPropsUniformBuffer);

#if MLN_UBO_CONSOLIDATION
    int i = 0;
    const std::size_t drawableCount = layerGroup.getDrawableCount();
    // When draping, each terrain tile renders the same drawables with a different projection. Because every
    // drape target is recorded into one command buffer and the layer's uniform buffers are single-window, the
    // per-tile projection must live in its own block of the buffer, selected per draw via `uboIndex`. Without
    // terrain, `baseElement` is 0 and the layout is the normal one.
    // Reserve blocks [0, drawableCount) for the non-drape (main) pass; drape target `ordinal` uses the next block.
    const std::uint32_t dcU = static_cast<std::uint32_t>(drawableCount);
    const std::uint32_t baseElement =
        parameters.drapeTargetOrdinal >= 0
            ? (static_cast<std::uint32_t>(parameters.drapeTargetOrdinal) + 1u) * dcU
            : 0u;
    const std::uint32_t totalElements =
        parameters.drapeTargetCount > 0 ? (parameters.drapeTargetCount + 1u) * dcU : dcU;
    std::vector<RasterDrawableUBO> drawableUBOVector(drawableCount);
    std::vector<ProjectionUBO> projectionUBOVector(drawableCount);
#endif

    visitLayerGroupDrawables(layerGroup, [&](gfx::Drawable& drawable) {
        if (!checkTweakDrawable(drawable)) {
            return;
        }

        ProjectionData projection;
        if (!drawable.getTileID()) {
            // this is an image drawable
            if (const auto& data = drawable.getData()) {
                const gfx::ImageDrawableData& imageData = static_cast<const gfx::ImageDrawableData&>(*data);
                mat4 imageMatrix = imageData.matrix;
                multiplyWithProjectionMatrix(
                    /*in-out*/ imageMatrix, parameters, drawable, /*nearClipped*/ false, /*aligned*/ true);
                projection = parameters.state.getProjectionDataForMatrix(UnwrappedTileID(0, 0, 0), imageMatrix);
            } else {
                Log::Error(Event::General, "Invalid raster layer drawable: neither tile id nor image data is set.");
                return;
            }
        } else {
            // this is a tile drawable
            const UnwrappedTileID tileID = drawable.getTileID()->toUnwrapped();
            projection = getProjectionData(tileID,
                                           parameters,
                                           {0.f, 0.f},
                                           TranslateAnchorType::Viewport,
                                           false,
                                           false,
                                           drawable,
                                           !parameters.state.isChanging());
        }
        const auto& matrix = projection.mainMatrix;
#if MLN_UBO_CONSOLIDATION
        projectionUBOVector[i] = toProjectionUBO(projection);
#else
        const auto projectionUBO = toProjectionUBO(projection);
        drawable.mutableUniformBuffers().createOrUpdate(idProjectionUBO, &projectionUBO, parameters.context);
#endif

#if MLN_UBO_CONSOLIDATION
        drawableUBOVector[i] = {
#else
        const RasterDrawableUBO drawableUBO = {
#endif
            /* .matrix = */ util::cast<float>(matrix)
        };
#if MLN_UBO_CONSOLIDATION
        drawable.setUBOIndex(static_cast<int32_t>(baseElement + static_cast<std::uint32_t>(i++)));
#else
        auto& drawableUniforms = drawable.mutableUniformBuffers();
        drawableUniforms.createOrUpdate(idRasterDrawableUBO, &drawableUBO, parameters.context);
#endif
    });

#if MLN_UBO_CONSOLIDATION
    auto& context = parameters.context;

    const std::size_t blockBytes = sizeof(RasterDrawableUBO) * drawableCount;
    const std::size_t offsetBytes = sizeof(RasterDrawableUBO) * baseElement;
    const std::size_t totalBytes = sizeof(RasterDrawableUBO) * totalElements;
    if (!drawableUniformBuffer || drawableUniformBuffer->getSize() < totalBytes) {
        std::vector<std::uint8_t> initial(totalBytes, 0);
        drawableUniformBuffer = context.createUniformBuffer(initial.data(), totalBytes, false, true);
    }
    drawableUniformBuffer->update(drawableUBOVector.data(), blockBytes, offsetBytes);

    layerUniforms.set(idRasterDrawableUBO, drawableUniformBuffer);
    uploadProjectionUBOs(layerUniforms, projectionUBOVector, context, baseElement, totalElements);
#endif
}

} // namespace mln
