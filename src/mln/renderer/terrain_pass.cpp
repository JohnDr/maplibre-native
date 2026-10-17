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
#include <mln/renderer/render_sky.hpp>
#include <mln/renderer/update_parameters.hpp>
#include <mln/shaders/shader_defines.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/projection.hpp>
#include <mln/util/tile_cover.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <unordered_set>

namespace mln {

namespace {


constexpr std::string_view TerrainShaderName = "TerrainShader";

// Grid resolution per DEM tile (segments, so the vertex grid is one larger): GL JS's terrain mesh is 128x128 for every
// tile, so coarse far-field tiles keep their relief too. 129^2 vertices still fit 16-bit indices.
constexpr std::size_t TerrainGridSegments = 128;
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

/// Builds a grid over one DEM tile: xy in tile units (0..EXTENT), z the elevation in metres (the camera's
/// world-to-camera matrix scales z by pixels-per-metre, like every 3D layer), w a hillshade.
TerrainMesh buildTerrainMesh(const DEMData* dem, double exaggeration, const CanonicalTileID& tile) {
    // A null `dem` is a tile the source does not have (open sea): flat at sea level, as GL JS treats missing DEM.
    const std::size_t n = TerrainGridPoints;
    const double dim = dem != nullptr ? static_cast<double>(dem->dim) : 256.0;
    // Ground width of this tile at its own latitude.
    const double tilesAcross = static_cast<double>(1ull << tile.z);
    const double tileLat = std::atan(std::sinh(std::numbers::pi * (1.0 - 2.0 * (tile.y + 0.5) / tilesAcross)));
    const double metersPerTile = util::M2PI * util::EARTH_RADIUS_M * std::cos(tileLat) / tilesAcross;
    const double pixelMeters = metersPerTile / dim;
    const double extent = static_cast<double>(util::EXTENT);

    std::vector<double> elevation(n * n);
    for (std::size_t j = 0; j < n; j++) {
        for (std::size_t i = 0; i < n; i++) {
            const double u = static_cast<double>(i) / static_cast<double>(TerrainGridSegments);
            const double v = static_cast<double>(j) / static_cast<double>(TerrainGridSegments);
            elevation[j * n + i] = dem != nullptr ? dem->getElevation(u * dim, v * dim) : 0.0;
        }
    }

    // Slopes straight from the DEM one pixel either side, so the hillshade at a tile edge uses the neighbour's pixels
    // (the backfilled border) and matches across the seam; grid neighbours would clamp at the edge.
    const auto demElevation = [&](double x, double y) {
        return dem != nullptr ? dem->getElevation(std::clamp(x, -1.0, dim), std::clamp(y, -1.0, dim)) : 0.0;
    };

    TerrainMesh mesh;
    mesh.vertices.resize(n * n * 4 * sizeof(float));
    auto* out = reinterpret_cast<float*>(mesh.vertices.data());
    for (std::size_t j = 0; j < n; j++) {
        for (std::size_t i = 0; i < n; i++) {
            const double e = elevation[j * n + i];
            const double px = static_cast<double>(i) / static_cast<double>(TerrainGridSegments) * dim;
            const double py = static_cast<double>(j) / static_cast<double>(TerrainGridSegments) * dim;
            const double dzdx = (demElevation(px + 1.0, py) - demElevation(px - 1.0, py)) / (2.0 * pixelMeters);
            const double dzdy = (demElevation(px, py + 1.0) - demElevation(px, py - 1.0)) / (2.0 * pixelMeters);
            const double length = std::sqrt(dzdx * dzdx + dzdy * dzdy + 1.0);
            // normal . light, with normal = (-dzdx, -dzdy, 1) / length
            const double diffuse = std::clamp((LightX * -dzdx + LightY * -dzdy + LightZ) / length, 0.0, 1.0);
            const double shade = 0.35 + 0.65 * diffuse;

            float* vertex = out + (j * n + i) * 4;
            vertex[0] = static_cast<float>(static_cast<double>(i) / static_cast<double>(TerrainGridSegments) * extent);
            vertex[1] = static_cast<float>(static_cast<double>(j) / static_cast<double>(TerrainGridSegments) * extent);
            vertex[2] = static_cast<float>(e * exaggeration);
            vertex[3] = static_cast<float>(shade);
        }
    }
    mesh.vertexCount = n * n;

    // Skirt (GL JS `_buildSkirts`): a frame hanging below each edge, so hairline gaps where tiles of different zoom
    // meet (their edge vertices differ) are covered. GL JS uses a fifth of the tile width.
    const double skirtMeters = metersPerTile / 5.0;
    const std::size_t skirtVertexBase = mesh.vertexCount;
    mesh.vertices.resize((n * n + 4 * n) * 4 * sizeof(float));
    out = reinterpret_cast<float*>(mesh.vertices.data());
    // Edge vertex indices, per side, in order along the edge.
    const auto edgeIndex = [&](int side, std::size_t k) -> std::size_t {
        switch (side) {
            case 0: return k;                  // north (j = 0)
            case 1: return (n - 1) * n + k;    // south (j = n-1)
            case 2: return k * n;              // west  (i = 0)
            default: return k * n + (n - 1);   // east  (i = n-1)
        }
    };
    for (int side = 0; side < 4; ++side) {
        for (std::size_t k = 0; k < n; ++k) {
            const float* src = out + edgeIndex(side, k) * 4;
            float* dst = out + (skirtVertexBase + static_cast<std::size_t>(side) * n + k) * 4;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2] - static_cast<float>(skirtMeters);
            dst[3] = src[3];
        }
    }
    mesh.vertexCount = n * n + 4 * n;

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
    for (int side = 0; side < 4; ++side) {
        for (std::size_t k = 0; k + 1 < n; ++k) {
            const auto top0 = static_cast<uint16_t>(edgeIndex(side, k));
            const auto top1 = static_cast<uint16_t>(edgeIndex(side, k + 1));
            const auto low0 = static_cast<uint16_t>(skirtVertexBase + static_cast<std::size_t>(side) * n + k);
            const auto low1 = static_cast<uint16_t>(low0 + 1);
            mesh.indices.insert(mesh.indices.end(), {top0, low0, top1, top1, low0, low1});
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
                         const UpdateParameters& updateParameters,
                         const EvaluatedSky* evaluatedSky) {
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
        terrain = std::make_shared<TerrainTileManager>(updateParameters.fileSource.get(), *tileset);
        terrain->setOnTileLoaded(onTileLoaded);
        if (layerGroup) {
            layerGroup->clearDrawables();
        }
        builtZoom = -1;
    }

    const bool globe = state.isGlobeRendering();
    if (!shader || globe != globeShader) {
        globeShader = globe;
        shader = context.getGenericShader(shaders,
                                          std::string(TerrainShaderName),
                                          globe ? gfx::ProjectionVariant::Globe : gfx::ProjectionVariant::Mercator);
        if (layerGroup) {
            layerGroup->clearDrawables();
        }
        builtZoom = -1;
    }
    if (!shader) {
        return;
    }
    if (!layerGroup) {
        constexpr std::size_t initialCapacity = 64;
        layerGroup = context.createTileLayerGroup(0, initialCapacity, "terrain");
    }

    // A neighbour's DEM border changed these tiles' edges: rebuild their meshes so shared edges agree.
    for (const auto& updated : terrain->takeUpdatedTiles()) {
        layerGroup->removeDrawablesIf([&](gfx::Drawable& drawable) {
            return drawable.getTileID() && drawable.getTileID()->canonical == updated;
        });
    }

    const double zoom = state.getZoom();
    const auto newDemZoom = static_cast<uint8_t>(std::clamp(std::floor(zoom) + 2.0, 0.0, 14.0));
    if (newDemZoom != demZoom || builtZoom < 0 ||
        builtExaggeration != updateParameters.terrainExaggeration) {
        demZoom = newDemZoom;
        builtZoom = zoom;
        builtExaggeration = updateParameters.terrainExaggeration;
        layerGroup->clearDrawables();
    }

    // Tile bounding boxes span each tile's own DEM elevation range (see TransformState::setTileElevationRange), so
    // tiles whose terrain rises into view are covered.
    TransformState coverState = state;
    coverState.setTileElevationRange(elevationRangeFunction(updateParameters.terrainExaggeration));
    // GL JS lets terrain tiles vary in zoom with their distance from the camera (`allowVariableZoom` is true with
    // terrain; `calculateTileZoom`): the near field keeps the DEM's zoom and the far field is coarser. A single zoom
    // reaches the horizon with thousands of tiles when pitched, each with a mesh, a drape render target and a DEM
    // request (that exhausted GPU memory and took the machine down).
    auto tileCover = util::tileCover({.transformState = coverState,
                                      .tileLodMinRadius = updateParameters.tileLodMinRadius,
                                      .tileLodScale = updateParameters.tileLodScale,
                                      .tileLodPitchThreshold = updateParameters.tileLodPitchThreshold,
                                      .tileLodMode = updateParameters.tileLodMode,
                                      .terrainVariableZoom = true},
                                     demZoom,
                                     Range<uint8_t>(0, demZoom));
    // Backstop only: the LOD cover is a few dozen tiles. It is sorted nearest the centre first.
    constexpr std::size_t maxTerrainTiles = 192;
    if (tileCover.size() > maxTerrainTiles) {
        tileCover.erase(tileCover.begin() + static_cast<std::ptrdiff_t>(maxTerrainTiles), tileCover.end());
    }

    const std::unordered_set<OverscaledTileID> covered(tileCover.begin(), tileCover.end());
    layerGroup->removeDrawablesIf(
        [&](gfx::Drawable& drawable) { return drawable.getTileID() && !covered.contains(*drawable.getTileID()); });

    cover = tileCover;

    std::unique_ptr<gfx::DrawableBuilder> builder;
    for (const auto& tileID : tileCover) {
        if (layerGroup->getDrawableCount(RenderPass::Translucent, tileID) > 0) {
            continue;
        }
        const auto& canonical = tileID.canonical;
        const auto data = terrain->getTileData(canonical);
        if (!data && !terrain->isMissing(canonical)) {
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

        auto mesh = buildTerrainMesh(data.get(), updateParameters.terrainExaggeration, canonical);
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
        if (!globeShader) {
            mat4 tileMatrix;
            paintParameters.state.matrixFor(tileMatrix, tileID);
            mat4 composed;
            matrix::multiply(composed, paintParameters.transformParams.projMatrix, tileMatrix);
            pd.mainMatrix = composed;
            pd.fallbackMatrix = composed;
        }
        projectionUBOs.emplace_back(LayerTweaker::toProjectionUBO(pd));
    });

    // Fog (GL JS terrain fog): the sky's fog colours and the depth range it blends over. Off without a sky, in the
    // globe projection, and below pitch 60 (GL JS `calculateFogBlendOpacity`: 0 below 60, ramping to 1 at 70).
    {
        struct alignas(16) TerrainFogUBO {
            std::array<float, 4> fogColor;
            std::array<float, 4> horizonColor;
            std::array<float, 4> params;
            std::array<float, 4> depthRange;
        };
        static_assert(sizeof(TerrainFogUBO) == 4 * 16);
        TerrainFogUBO fogUBO{};
        fogUBO.params = {1.0f, 0.0f, 1.0f, 0.0f};
        fogUBO.depthRange = {1.0f, 2.0f, 0.0f, 0.0f};
        double nearDistance = 1.0;
        double farDistance = 2.0;
        if (evaluatedSky != nullptr && !globeShader && state.getFogDepthRange(nearDistance, farDistance)) {
            const auto& sky = *evaluatedSky;
            const double pitchDegrees = state.getPitch() * 180.0 / std::numbers::pi;
            const double opacity = pitchDegrees < 60.0 ? 0.0 : (pitchDegrees < 70.0 ? (pitchDegrees - 60.0) / 10.0 : 1.0);
            fogUBO.fogColor = {sky.fogColor.r, sky.fogColor.g, sky.fogColor.b, sky.fogColor.a};
            fogUBO.horizonColor = {sky.horizonColor.r, sky.horizonColor.g, sky.horizonColor.b, sky.horizonColor.a};
            fogUBO.params = {sky.fogGroundBlend, static_cast<float>(opacity), sky.horizonFogBlend, 0.0f};
            fogUBO.depthRange = {static_cast<float>(nearDistance), static_cast<float>(farDistance), 0.0f, 0.0f};
        }
        if (!fogUniformBuffer) {
            fogUniformBuffer = context.createUniformBuffer(&fogUBO, sizeof(fogUBO), false, false);
        } else {
            fogUniformBuffer->update(&fogUBO, sizeof(fogUBO));
        }
        layerGroup->mutableUniformBuffers().set(shaders::idTerrainFogUBO, fogUniformBuffer);
    }

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

void TerrainPass::applyDrapeTextures(
    const std::map<std::tuple<uint8_t, uint32_t, uint32_t>, gfx::Texture2DPtr>& textures) {
    if (!layerGroup) {
        return;
    }
    visitLayerGroupDrawables(*layerGroup, [&](gfx::Drawable& drawable) {
        if (!drawable.getTileID()) {
            return;
        }
        const auto c = drawable.getTileID()->canonical;
        const auto it = textures.find({c.z, c.x, c.y});
        if (it != textures.end() && it->second) {
            drawable.setTexture(it->second, shaders::idTerrainDrapeTexture);
        }
    });
}

std::shared_ptr<const TileElevationRangeFn> TerrainPass::elevationRangeFunction(double exaggeration) const {
    if (!terrain) {
        return nullptr;
    }
    // A coarse DEM tile is requested for every tile the cover visits, so a tile whose own DEM is unknown still gets a
    // (conservative) elevation range from its ancestor instead of a flat sea-level box.
    constexpr uint8_t coarseZoom = 9;
    const uint8_t coarse = std::min(demZoom, coarseZoom);
    return std::make_shared<const TileElevationRangeFn>(
        [manager = std::weak_ptr<TerrainTileManager>(terrain), exaggeration, coarse](
            const CanonicalTileID& id) -> std::optional<std::pair<double, double>> {
            const auto locked = manager.lock();
            if (!locked) {
                return std::nullopt;
            }
            locked->requestAncestor(id, coarse);
            if (const auto range = locked->getMinMaxElevation(id)) {
                return std::make_pair(range->first * exaggeration, range->second * exaggeration);
            }
            return std::nullopt;
        });
}

std::function<float(const UnwrappedTileID&)> TerrainPass::elevationFunction(const TransformState& state,
                                                                            double exaggeration) {
    if (!terrain) {
        return {};
    }
    TerrainTileManager* manager = terrain.get();
    const double scale = state.getScale();
    const double worldSize = Projection::worldSize(scale);
    const uint8_t zoom = demZoom;
    // Metres, like the terrain mesh: the camera matrix scales z by pixels-per-metre.
    return [manager, scale, worldSize, zoom, exaggeration](const UnwrappedTileID& tileID) -> float {
        const double n = std::ldexp(1.0, tileID.canonical.z);
        const double wx = (tileID.canonical.x + 0.5) / n * worldSize;
        const double wy = (tileID.canonical.y + 0.5) / n * worldSize;
        const LatLng latLng = Projection::unproject({wx, wy}, scale);
        const double meters = manager->getElevation(latLng, zoom);
        return static_cast<float>(meters * exaggeration);
    };
}

} // namespace mln
