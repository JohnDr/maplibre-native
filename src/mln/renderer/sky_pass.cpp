#include <mln/renderer/sky_pass.hpp>

#include <mln/gfx/color_mode.hpp>
#include <mln/gfx/context.hpp>
#include <mln/gfx/cull_face_mode.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/gfx/drawable_builder.hpp>
#include <mln/gfx/projection_variant.hpp>
#include <mln/gfx/uniform_buffer.hpp>
#include <mln/renderer/globe_tile_mesh.hpp> // SegmentVector
#include <mln/gfx/shader_registry.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_pass.hpp>
#include <mln/renderer/update_parameters.hpp>
#include <mln/shaders/shader_defines.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace mln {

namespace {

constexpr std::string_view SkyShaderName = "SkyShader";

// GL JS `maxMercatorHorizonAngle` (mercator_utils.ts).
constexpr double maxMercatorHorizonAngle = 89.25;

struct alignas(16) SkyUBO {
    std::array<float, 4> skyColor;
    std::array<float, 4> horizonColor;
    std::array<float, 4> horizon; // x, y: point on the horizon (framebuffer px, y down); z: sky-horizon blend (px)
    std::array<float, 4> normal;  // xy: up-the-screen normal (y down)
};
static_assert(sizeof(SkyUBO) == 4 * 16);

constexpr double degToRad(double d) {
    return d * std::numbers::pi / 180.0;
}

} // namespace

SkyPass::SkyPass() = default;
SkyPass::~SkyPass() = default;

void SkyPass::update(gfx::ShaderRegistry& shaders,
                     gfx::Context& context,
                     const TransformState& state,
                     const PaintParameters& paintParameters,
                     const UpdateParameters& updateParameters) {
    // The globe's sky (atmosphere) is not drawn yet; GL JS blends from this horizon sky to the atmosphere.
    if (!updateParameters.sky || state.isGlobeRendering()) {
        if (layerGroup) {
            layerGroup->clearDrawables();
        }
        return;
    }
    const style::Sky& sky = *updateParameters.sky;

    if (!shader) {
        shader = context.getGenericShader(shaders, std::string(SkyShaderName), gfx::ProjectionVariant::Mercator);
    }
    if (!shader) {
        return;
    }
    if (!layerGroup) {
        layerGroup = context.createTileLayerGroup(0, 1, "sky");
    }

    if (layerGroup->getDrawableCount() == 0) {
        auto builder = context.createDrawableBuilder("sky");
        builder->setShader(shader);
        builder->setRenderPass(RenderPass::Translucent);
        builder->setColorMode(gfx::ColorMode::alphaBlended());
        builder->setIs3D(false);
        builder->setEnableColor(true);
        builder->setEnableDepth(false);
        builder->setCullFaceMode(gfx::CullFaceMode::disabled());
        builder->setVertexAttrId(shaders::idSkyPosVertexAttribute);

        const std::array<int16_t, 8> corners{-1, -1, 1, -1, 1, 1, -1, 1};
        const auto* bytes = reinterpret_cast<const uint8_t*>(corners.data());
        builder->setRawVertices(std::vector<uint8_t>(bytes, bytes + sizeof(corners)), 4, gfx::AttributeDataType::Short2);
        const std::vector<uint16_t> indices{0, 1, 2, 0, 2, 3};
        SegmentVector segments;
        segments.emplace_back(0, 0, 4, indices.size());
        builder->setSegments(gfx::Triangles(), indices, segments.data(), segments.size());
        builder->flush(context);
        for (auto& drawable : builder->clearDrawables()) {
            drawable->setUBOIndex(0);
            const OverscaledTileID tileID(0, 0, 0);
            drawable->setTileID(tileID);
            layerGroup->addDrawable(RenderPass::Translucent, tileID, std::move(drawable));
        }
    }

    // GL JS `skyUniformValues`: the horizon line through (width/2 - h*sin(roll), height/2 + h*cos(roll)) with the
    // normal (-sin(roll), cos(roll)), h = getMercatorHorizon(transform); the blend distance is in pixels.
    const double pitchDeg = state.getPitch() * 180.0 / std::numbers::pi;
    const double horizon = state.getCameraToCenterDistance() *
                           std::min(std::tan(degToRad(90.0 - pitchDeg)) * 0.85,
                                    std::tan(degToRad(maxMercatorHorizonAngle - pitchDeg)));
    const double roll = state.getRoll();
    const double cosRoll = std::cos(roll);
    const double sinRoll = std::sin(roll);
    const double pixelRatio = paintParameters.pixelRatio;
    const double width = state.getSize().width;
    const double height = state.getSize().height;

    SkyUBO ubo{};
    ubo.skyColor = {sky.skyColor.r, sky.skyColor.g, sky.skyColor.b, sky.skyColor.a};
    ubo.horizonColor = {sky.horizonColor.r, sky.horizonColor.g, sky.horizonColor.b, sky.horizonColor.a};
    // GL JS measures y up from the bottom; the framebuffer here is y down, so the signs of the y terms flip.
    ubo.horizon = {static_cast<float>((width / 2.0 - horizon * sinRoll) * pixelRatio),
                   static_cast<float>((height / 2.0 - horizon * cosRoll) * pixelRatio),
                   static_cast<float>(sky.skyHorizonBlend * height / 2.0 * pixelRatio),
                   0.0f};
    ubo.normal = {static_cast<float>(-sinRoll), static_cast<float>(cosRoll), 0.0f, 0.0f};

    if (!uniformBuffer) {
        uniformBuffer = context.createUniformBuffer(&ubo, sizeof(ubo), false, true);
    } else {
        uniformBuffer->update(&ubo, sizeof(ubo));
    }
    layerGroup->mutableUniformBuffers().set(shaders::idDrawableReservedFragmentOnlyUBO, uniformBuffer);
}

} // namespace mln
