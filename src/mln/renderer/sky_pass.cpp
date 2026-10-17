#include <mln/renderer/sky_pass.hpp>

#include <mln/gfx/color_mode.hpp>
#include <mln/gfx/context.hpp>
#include <mln/gfx/cull_face_mode.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/gfx/drawable_builder.hpp>
#include <mln/gfx/projection_variant.hpp>
#include <mln/gfx/shader_registry.hpp>
#include <mln/gfx/uniform_buffer.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/renderer/globe_tile_mesh.hpp> // SegmentVector
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_pass.hpp>
#include <mln/renderer/render_sky.hpp>
#include <mln/shaders/shader_defines.hpp>
#include <mln/util/mat4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace mln {

namespace {

constexpr std::string_view SkyShaderName = "SkyShader";
constexpr std::string_view AtmosphereShaderName = "AtmosphereShader";

// GL JS `maxMercatorHorizonAngle` (mercator_utils.ts).
constexpr double maxMercatorHorizonAngle = 89.25;

// GL JS `ATMOSPHERE_HEIGHT_TO_GLOBE_RADIUS` (globe_utils.ts).
constexpr double atmosphereHeightToGlobeRadius = 100000.0 / 6371000.0;

struct alignas(16) SkyUBO {
    std::array<float, 4> skyColor;
    std::array<float, 4> horizonColor;
    std::array<float, 4> horizon; // x, y: point on the horizon (framebuffer px, y down); z: sky-horizon blend (px)
    std::array<float, 4> normal;  // xy: up-the-screen normal (y down)
    std::array<float, 16> invProj;
    std::array<float, 4> globe;   // xyz: globe centre in view space; w: radius (px)
    std::array<float, 4> params;  // sky blend, atmosphere blend, camera-to-centre distance, pixel ratio
    std::array<float, 4> viewport;
};
static_assert(sizeof(SkyUBO) == 11 * 16);

struct alignas(16) AtmosphereUBO {
    std::array<float, 16> invProj;
    std::array<float, 4> sunPos;
    std::array<float, 4> globe;
    std::array<float, 4> params;
    std::array<float, 4> viewport;
};
static_assert(sizeof(AtmosphereUBO) == 8 * 16);

constexpr double degToRad(double d) {
    return d * std::numbers::pi / 180.0;
}

gfx::ShaderProgramBasePtr getShader(gfx::ShaderRegistry& shaders, gfx::Context& context, std::string_view name) {
    return context.getGenericShader(shaders, std::string(name), gfx::ProjectionVariant::Mercator);
}

/// A drawable of one screen-filling quad, drawn with `shader`.
void addQuad(gfx::Context& context,
             TileLayerGroup& group,
             const gfx::ShaderProgramBasePtr& shader,
             const char* name,
             std::size_t vertexAttrId) {
    auto builder = context.createDrawableBuilder(name);
    builder->setShader(shader);
    builder->setRenderPass(RenderPass::Translucent);
    builder->setColorMode(gfx::ColorMode::alphaBlended());
    builder->setIs3D(false);
    builder->setEnableColor(true);
    builder->setEnableDepth(false);
    builder->setCullFaceMode(gfx::CullFaceMode::disabled());
    builder->setVertexAttrId(vertexAttrId);

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
        group.addDrawable(RenderPass::Translucent, tileID, std::move(drawable));
    }
}

/// The globe's view-space state (GL JS `getGlobeCenterInViewSpace`, `getGlobeRadiusPixels`): the inverse of the
/// globe's perspective projection and the globe centre in view space.
struct GlobeView {
    mat4 invProj;
    std::array<double, 3> position{};
    double radius = 1.0;
};

GlobeView globeView(const TransformState& state) {
    GlobeView view;
    const Size size = state.getSize();
    view.radius = state.getGlobeRadiusPixels();

    // The globe's perspective, as built in `VerticalPerspectiveProjection::globeViewProjectionMatrix`.
    mat4 proj;
    matrix::perspective(proj,
                        state.getFieldOfView(),
                        static_cast<double>(size.width) / size.height,
                        0.5,
                        state.getCameraToCenterDistance() + view.radius * 2.0);
    const ScreenCoordinate offset = state.getCenterOffset();
    proj[8] = -offset.x * 2.0 / size.width;
    proj[9] = offset.y * 2.0 / size.height;
    if (matrix::invert(view.invProj, proj)) {
        matrix::identity(view.invProj);
    }

    // The globe's centre is the origin of globe space: in clip space it is the translation column of the globe's
    // view-projection matrix; the inverse projection takes it to view space.
    const mat4& globeVP = state.getGlobeViewProjectionMatrix();
    const std::array<double, 4> clip{globeVP[12], globeVP[13], globeVP[14], globeVP[15]};
    std::array<double, 4> v{};
    for (int i = 0; i < 4; ++i) {
        v[i] = view.invProj[i] * clip[0] + view.invProj[4 + i] * clip[1] + view.invProj[8 + i] * clip[2] +
               view.invProj[12 + i] * clip[3];
    }
    view.position = {v[0] / v[3], v[1] / v[3], v[2] / v[3]};
    return view;
}

// GL JS `getAtmosphereAltitudeBlend`.
double atmosphereAltitudeBlend(double altitude, double globeRadius) {
    const double atmosphereHeight = globeRadius * atmosphereHeightToGlobeRadius;
    const double x = std::clamp((altitude - atmosphereHeight) / (9.0 * atmosphereHeight), 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

std::array<float, 16> toFloats(const mat4& m) {
    std::array<float, 16> out{};
    for (std::size_t i = 0; i < 16; ++i) {
        out[i] = static_cast<float>(m[i]);
    }
    return out;
}

// GL JS `getSunPos` (draw_sky.ts).
std::array<float, 4> sunPosition(const TransformState& state, const SunLight& light) {
    std::array<double, 3> p{-light.position[0], -light.position[1], -light.position[2]};
    if (light.anchoredToMap) {
        mat4 m;
        matrix::identity(m);
        matrix::rotate_z(m, m, state.getRoll());
        matrix::rotate_x(m, m, -state.getPitch());
        matrix::rotate_z(m, m, state.getBearing());
        matrix::rotate_x(m, m, degToRad(state.getLatLng().latitude()));
        matrix::rotate_y(m, m, -degToRad(state.getLatLng().longitude()));
        const double x = p[0], y = p[1], z = p[2];
        double w = m[3] * x + m[7] * y + m[11] * z + m[15];
        w = w != 0.0 ? w : 1.0;
        p = {(m[0] * x + m[4] * y + m[8] * z + m[12]) / w,
             (m[1] * x + m[5] * y + m[9] * z + m[13]) / w,
             (m[2] * x + m[6] * y + m[10] * z + m[14]) / w};
    }
    return {static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]), 0.0f};
}

} // namespace

SkyPass::SkyPass() = default;
SkyPass::~SkyPass() = default;

void SkyPass::update(gfx::ShaderRegistry& shaders,
                     gfx::Context& context,
                     const TransformState& state,
                     const PaintParameters& paintParameters,
                     const EvaluatedSky* evaluatedSky,
                     const SunLight& light,
                     const std::optional<Color>& spaceColor) {
    // Without a sky the area above the flat map's horizon shows the app's "space" backdrop colour, when it has one
    // (on the globe the backdrop is the clear colour already): the sky quad, drawn in that one colour.
    EvaluatedSky spaceSky{};
    if (evaluatedSky == nullptr && spaceColor && !state.isGlobeRendering()) {
        spaceSky = {*spaceColor, *spaceColor, *spaceColor, 0.0f, 0.0f, 0.0f, 0.0f};
        evaluatedSky = &spaceSky;
    }
    if (evaluatedSky == nullptr || state.getSize().isEmpty()) {
        if (skyGroup) {
            skyGroup->clearDrawables();
        }
        if (atmosphereGroup) {
            atmosphereGroup->clearDrawables();
        }
        return;
    }
    const EvaluatedSky& sky = *evaluatedSky;
    const bool globe = state.isGlobeRendering();

    if (!skyShader) {
        skyShader = getShader(shaders, context, SkyShaderName);
    }
    if (!skyShader) {
        return;
    }
    if (!skyGroup) {
        skyGroup = context.createTileLayerGroup(0, 1, "sky");
    }
    if (skyGroup->getDrawableCount() == 0) {
        addQuad(context, *skyGroup, skyShader, "sky", shaders::idSkyPosVertexAttribute);
    }

    const double pixelRatio = paintParameters.pixelRatio;
    const double width = state.getSize().width;
    const double height = state.getSize().height;

    // GL JS `skyUniformValues`: the horizon line through (width/2 - h*sin(roll), height/2 + h*cos(roll)) with the
    // normal (-sin(roll), cos(roll)), h = getMercatorHorizon(transform); the blend distance is in pixels.
    const double pitchDeg = state.getPitch() * 180.0 / std::numbers::pi;
    const double horizon = state.getCameraToCenterDistance() *
                           std::min(std::tan(degToRad(90.0 - pitchDeg)) * 0.85,
                                    std::tan(degToRad(maxMercatorHorizonAngle - pitchDeg)));
    const double roll = state.getRoll();
    const double cosRoll = std::cos(roll);
    const double sinRoll = std::sin(roll);

    GlobeView view;
    matrix::identity(view.invProj);
    double altitudeBlend = 0.0;
    if (globe) {
        view = globeView(state);
        const double distance = std::hypot(view.position[0], view.position[1], view.position[2]);
        altitudeBlend = atmosphereAltitudeBlend(distance - view.radius, view.radius);
    }
    const double projectionTransition = state.getProjectionTransition();

    SkyUBO ubo{};
    ubo.skyColor = {sky.skyColor.r, sky.skyColor.g, sky.skyColor.b, sky.skyColor.a};
    ubo.horizonColor = {sky.horizonColor.r, sky.horizonColor.g, sky.horizonColor.b, sky.horizonColor.a};
    // GL JS measures y up from the bottom; the framebuffer here is y down, so the signs of the y terms flip.
    ubo.horizon = {static_cast<float>((width / 2.0 - horizon * sinRoll) * pixelRatio),
                   static_cast<float>((height / 2.0 - horizon * cosRoll) * pixelRatio),
                   static_cast<float>(sky.skyHorizonBlend * height / 2.0 * pixelRatio),
                   0.0f};
    ubo.normal = {static_cast<float>(-sinRoll), static_cast<float>(cosRoll), 0.0f, 0.0f};
    ubo.invProj = toFloats(view.invProj);
    ubo.globe = {static_cast<float>(view.position[0]),
                 static_cast<float>(view.position[1]),
                 static_cast<float>(view.position[2]),
                 static_cast<float>(view.radius)};
    ubo.params = {static_cast<float>(projectionTransition),
                  static_cast<float>(sky.atmosphereBlend * altitudeBlend),
                  state.getCameraToCenterDistance(),
                  static_cast<float>(pixelRatio)};
    ubo.viewport = {static_cast<float>(width * pixelRatio), static_cast<float>(height * pixelRatio), 0.0f, 0.0f};

    if (!skyBuffer) {
        skyBuffer = context.createUniformBuffer(&ubo, sizeof(ubo), false, true);
    } else {
        skyBuffer->update(&ubo, sizeof(ubo));
    }
    skyGroup->mutableUniformBuffers().set(shaders::idDrawableReservedFragmentOnlyUBO, skyBuffer);

    // The atmosphere (GL JS `drawAtmosphere`): only with the globe, and nothing when it is fully transparent.
    const double atmosphereBlend = sky.atmosphereBlend * projectionTransition * altitudeBlend;
    if (!globe || atmosphereBlend <= 0.0) {
        if (atmosphereGroup) {
            atmosphereGroup->clearDrawables();
        }
        return;
    }
    if (!atmosphereShader) {
        atmosphereShader = getShader(shaders, context, AtmosphereShaderName);
    }
    if (!atmosphereShader) {
        return;
    }
    if (!atmosphereGroup) {
        atmosphereGroup = context.createTileLayerGroup(0, 1, "atmosphere");
    }
    if (atmosphereGroup->getDrawableCount() == 0) {
        addQuad(context, *atmosphereGroup, atmosphereShader, "atmosphere", shaders::idAtmospherePosVertexAttribute);
    }

    AtmosphereUBO atmosphere{};
    atmosphere.invProj = toFloats(view.invProj);
    atmosphere.sunPos = sunPosition(state, light);
    atmosphere.globe = ubo.globe;
    atmosphere.params = {static_cast<float>(atmosphereBlend), 0.0f, 0.0f, 0.0f};
    atmosphere.viewport = ubo.viewport;
    if (!atmosphereBuffer) {
        atmosphereBuffer = context.createUniformBuffer(&atmosphere, sizeof(atmosphere), false, true);
    } else {
        atmosphereBuffer->update(&atmosphere, sizeof(atmosphere));
    }
    atmosphereGroup->mutableUniformBuffers().set(shaders::idDrawableReservedFragmentOnlyUBO, atmosphereBuffer);
}

} // namespace mln
