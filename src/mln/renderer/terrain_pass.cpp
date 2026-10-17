#include <mln/renderer/terrain_pass.hpp>

#include <mln/geometry/dem_data.hpp>
#include <mln/gfx/context.hpp>
#include <mln/gfx/cull_face_mode.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/gfx/drawable_builder.hpp>
#include <mln/gfx/drawable_tweaker.hpp>
#include <mln/gfx/projection_variant.hpp>
#include <mln/gfx/shader_registry.hpp>
#include <mln/gfx/uniform_buffer.hpp>
#include <mln/map/transform_state.hpp>
#include <mln/shaders/layer_ubo.hpp>
#include <mln/renderer/globe_tile_mesh.hpp> // SegmentVector
#include <mln/renderer/layer_tweaker.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/render_pass.hpp>
#include <mln/renderer/terrain_tile_manager.hpp>
#include <mln/renderer/update_parameters.hpp>
#include <mln/shaders/shader_defines.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/projection.hpp>
#include <mln/util/tile_cover.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace mln {

namespace {

constexpr std::string_view TerrainShaderName = "TerrainShader";

// Grid resolution per DEM tile (segments, so the vertex grid is one larger).
constexpr std::size_t TerrainGridSegments = 16;
constexpr std::size_t TerrainGridPoints = TerrainGridSegments + 1;

// A light pointing from the north-west, 45 degrees up.
constexpr double LightX = -0.7071067811865476 * 0.7071067811865476;
constexpr double LightY = -0.7071067811865476 * 0.7071067811865476;
constexpr double LightZ = 0.7071067811865476;

struct TerrainMesh {
    std::vector<uint8_t> vertices;
    std::size_t vertexCount = 0;
    std::vector<uint16_t> indices;
};

/// Builds a grid over one DEM tile: xy in tile units (0..EXTENT), z the elevation in world pixels, w a hillshade.
TerrainMesh buildTerrainMesh(const DEMData& dem, double exaggeration, double metersPerPixel) {
    const std::size_t n = TerrainGridPoints;
    const double dim = static_cast<double>(dem.dim);
    const double metersPerTile = metersPerPixel * util::tileSize_D;
    const double cellMeters = metersPerTile / static_cast<double>(TerrainGridSegments);
    const double pixelsPerMeter = 1.0 / metersPerPixel;
    const double extent = static_cast<double>(util::EXTENT);

    std::vector<double> elevation(n * n);
    for (std::size_t j = 0; j < n; j++) {
        for (std::size_t i = 0; i < n; i++) {
            const double u = static_cast<double>(i) / static_cast<double>(TerrainGridSegments);
            const double v = static_cast<double>(j) / static_cast<double>(TerrainGridSegments);
            elevation[j * n + i] = dem.getElevation(u * dim, v * dim);
        }
    }

    const auto sample = [&](std::size_t i, std::size_t j) {
        return elevation[std::min(j, n - 1) * n + std::min(i, n - 1)];
    };

    TerrainMesh mesh;
    mesh.vertices.resize(n * n * 4 * sizeof(float));
    auto* out = reinterpret_cast<float*>(mesh.vertices.data());
    for (std::size_t j = 0; j < n; j++) {
        for (std::size_t i = 0; i < n; i++) {
            const double e = elevation[j * n + i];
            const double dzdx = (sample(i + 1, j) - sample(i == 0 ? 0 : i - 1, j)) / (2.0 * cellMeters);
            const double dzdy = (sample(i, j + 1) - sample(i, j == 0 ? 0 : j - 1)) / (2.0 * cellMeters);
            const double length = std::sqrt(dzdx * dzdx + dzdy * dzdy + 1.0);
            // normal . light, with normal = (-dzdx, -dzdy, 1) / length
            const double diffuse = std::clamp((LightX * -dzdx + LightY * -dzdy + LightZ) / length, 0.0, 1.0);
            const double shade = 0.35 + 0.65 * diffuse;

            float* vertex = out + (j * n + i) * 4;
            vertex[0] = static_cast<float>(static_cast<double>(i) / static_cast<double>(TerrainGridSegments) * extent);
            vertex[1] = static_cast<float>(static_cast<double>(j) / static_cast<double>(TerrainGridSegments) * extent);
            vertex[2] = static_cast<float>(e * exaggeration * pixelsPerMeter);
            vertex[3] = static_cast<float>(shade);
        }
    }
    mesh.vertexCount = n * n;

    mesh.indices.reserve(TerrainGridSegments * TerrainGridSegments * 6);
    for (std::size_t j = 0; j < TerrainGridSegments; j++) {
        for (std::size_t i = 0; i < TerrainGridSegments; i++) {
            const auto a = static_cast<uint16_t>(j * n + i);
            const auto b = static_cast<uint16_t>(a + 1);
            const auto c = static_cast<uint16_t>(a + n);
            const auto d = static_cast<uint16_t>(c + 1);
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    return mesh;
}

} // namespace

TerrainPass::TerrainPass() = default;
TerrainPass::~TerrainPass() = default;

void TerrainPass::update(gfx::ShaderRegistry& shaders,
                         gfx::Context& context,
                         const TransformState& state,
                         const PaintParameters& paintParameters,
                         const UpdateParameters& updateParameters) {
    if (!updateParameters.terrainTileset || updateParameters.terrainTileset->tiles.empty()) {
        if (layerGroup) {
            layerGroup->clearDrawables();
        }
        terrain.reset();
        tileset.reset();
        return;
    }

    if (!tileset || *tileset != *updateParameters.terrainTileset) {
        tileset = updateParameters.terrainTileset;
        terrain = std::make_unique<TerrainTileManager>(updateParameters.fileSource.get(), *tileset);
        terrain->setOnTileLoaded(onTileLoaded);
        if (layerGroup) {
            layerGroup->clearDrawables();
        }
        builtZoom = -1;
    }

    if (!shader) {
        shader = context.getGenericShader(shaders, std::string(TerrainShaderName), gfx::ProjectionVariant::Mercator);
    }
    if (!shader) {
        return;
    }
    if (!layerGroup) {
        constexpr std::size_t initialCapacity = 64;
        layerGroup = context.createTileLayerGroup(0, initialCapacity, "terrain");
    }

    const double zoom = state.getZoom();
    const auto newDemZoom = static_cast<uint8_t>(std::clamp(std::floor(zoom) + 2.0, 0.0, 14.0));
    if (newDemZoom != demZoom || builtZoom < 0 || std::abs(builtZoom - zoom) > 0.25 ||
        builtExaggeration != updateParameters.terrainExaggeration) {
        demZoom = newDemZoom;
        builtZoom = zoom;
        builtExaggeration = updateParameters.terrainExaggeration;
        layerGroup->clearDrawables();
    }

    const double metersPerPixel = Projection::getMetersPerPixelAtLatitude(state.getLatLng().latitude(), zoom);

    const auto tileCover = util::tileCover({.transformState = state,
                                            .tileLodMinRadius = updateParameters.tileLodMinRadius,
                                            .tileLodScale = updateParameters.tileLodScale,
                                            .tileLodPitchThreshold = updateParameters.tileLodPitchThreshold,
                                            .tileLodMode = updateParameters.tileLodMode},
                                           demZoom,
                                           Range<uint8_t>(demZoom, demZoom));

    const std::unordered_set<OverscaledTileID> covered(tileCover.begin(), tileCover.end());
    layerGroup->removeDrawablesIf(
        [&](gfx::Drawable& drawable) { return drawable.getTileID() && !covered.contains(*drawable.getTileID()); });

    std::unique_ptr<gfx::DrawableBuilder> builder;
    for (const auto& tileID : tileCover) {
        if (layerGroup->getDrawableCount(RenderPass::Translucent, tileID) > 0) {
            continue;
        }
        const auto& canonical = tileID.canonical;
        const auto data = terrain->getTileData(canonical);
        if (!data) {
            continue; // still loading
        }

        if (!builder) {
            builder = context.createDrawableBuilder("terrain");
            builder->setShader(shader);
            builder->setRenderPass(RenderPass::Translucent);
            builder->setColorMode(gfx::ColorMode::unblended());
            builder->setIs3D(true);
            builder->setEnableColor(true);
            builder->setEnableDepth(true);
            builder->setDepthType(gfx::DepthMaskType::ReadWrite);
            builder->setCullFaceMode(gfx::CullFaceMode::disabled());
            builder->setVertexAttrId(shaders::idTerrainPosVertexAttribute);
        }

        auto mesh = buildTerrainMesh(*data, updateParameters.terrainExaggeration, metersPerPixel);
        const auto vertexCount = mesh.vertexCount;
        const auto indexCount = mesh.indices.size();
        builder->setRawVertices(std::move(mesh.vertices), vertexCount, gfx::AttributeDataType::Float4);
        SegmentVector segments;
        segments.emplace_back(0, 0, vertexCount, indexCount);
        builder->setSegments(gfx::Triangles(), std::move(mesh.indices), segments.data(), segments.size());
        builder->flush(context);

        for (auto& drawable : builder->clearDrawables()) {
            drawable->setTileID(tileID);
            layerGroup->addDrawable(RenderPass::Translucent, tileID, std::move(drawable));
        }
    }

    // Set the projection UBO here rather than in a tweaker: updateLayers runs before the upload pass, so the buffers
    // are uploaded with the current frame's matrices.
    // UBO consolidation is on for Vulkan: the shader reads the layer group's shared projection buffer, indexed by
    // each drawable's uboIndex. Populate it here, the way the layer tweakers' uploadProjectionUBOs does.
    std::vector<shaders::ProjectionUBO> projectionUBOs;
    projectionUBOs.reserve(layerGroup->getDrawableCount());
    uint32_t uboIndex = 0;
    visitLayerGroupDrawables(*layerGroup, [&](gfx::Drawable& drawable) {
        if (!drawable.getTileID()) {
            return;
        }
        drawable.setUBOIndex(uboIndex++);
        const auto tileID = drawable.getTileID()->toUnwrapped();
        auto pd = paintParameters.projectionDataForTile(tileID);
        mat4 tileMatrix;
        paintParameters.state.matrixFor(tileMatrix, tileID);
        mat4 composed;
        matrix::multiply(composed, paintParameters.transformParams.projMatrix, tileMatrix);
        pd.mainMatrix = composed;
        pd.fallbackMatrix = composed;
        projectionUBOs.emplace_back(LayerTweaker::toProjectionUBO(pd));
    });

    if (!projectionUBOs.empty()) {
        const std::size_t size = sizeof(shaders::ProjectionUBO) * projectionUBOs.size();
        if (!projectionUniformBuffer || projectionUniformBuffer->getSize() < size) {
            projectionUniformBuffer = context.createUniformBuffer(projectionUBOs.data(), size, false, true);
        } else {
            projectionUniformBuffer->update(projectionUBOs.data(), size);
        }
        layerGroup->mutableUniformBuffers().set(shaders::idProjectionUBO, projectionUniformBuffer);
    }
}

} // namespace mln
