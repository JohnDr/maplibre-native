#include <mln/renderer/terrain_tile_manager.hpp>

#include <mln/geometry/dem_data.hpp>
#include <mln/math/clamp.hpp>
#include <mln/storage/file_source.hpp>
#include <mln/storage/resource.hpp>
#include <mln/storage/response.hpp>
#include <mln/util/async_request.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/image.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mln {

namespace {

constexpr double pi = 3.14159265358979323846;

std::tuple<uint8_t, uint32_t, uint32_t> keyFor(const CanonicalTileID& id) {
    return {id.z, id.x, id.y};
}

/// The continuous tile coordinate of `latLng` at `zoom`, in the range [0, 2^zoom].
void tileCoordinate(const LatLng& latLng, uint8_t zoom, double& x, double& y) {
    const double n = std::ldexp(1.0, zoom); // 2^zoom
    const double lat = util::clamp(latLng.latitude(), -util::LATITUDE_MAX, util::LATITUDE_MAX);
    x = (latLng.longitude() + 180.0) / 360.0 * n;
    const double sin = std::sin(lat * pi / 180.0);
    y = (0.5 - std::log((1.0 + sin) / (1.0 - sin)) / (4.0 * pi)) * n;
}

} // namespace

TerrainTileManager::TerrainTileManager(FileSource* fileSource_, Tileset tileset_)
    : fileSource(fileSource_),
      tileset(std::move(tileset_)) {}

TerrainTileManager::~TerrainTileManager() = default;

void TerrainTileManager::setTileset(Tileset tileset_) {
    // Tiles of a different tile source are not interchangeable; keep them only while the source stays the same.
    if (tileset.tiles != tileset_.tiles) {
        tiles.clear();
    }
    tileset = std::move(tileset_);
}

CanonicalTileID TerrainTileManager::tileFor(const LatLng& latLng, uint8_t zoom) {
    double x;
    double y;
    tileCoordinate(latLng, zoom, x, y);
    const auto dim = static_cast<double>(std::ldexp(1.0, zoom));
    return {zoom,
            static_cast<uint32_t>(util::clamp(std::floor(x), 0.0, dim - 1.0)),
            static_cast<uint32_t>(util::clamp(std::floor(y), 0.0, dim - 1.0))};
}

double TerrainTileManager::getElevation(const LatLng& latLng, uint8_t zoom) {
    const CanonicalTileID id = tileFor(latLng, zoom);
    const auto data = getDEMData(id);
    if (!data) {
        return 0.0;
    }
    double x;
    double y;
    tileCoordinate(latLng, zoom, x, y);
    const double dim = static_cast<double>(data->dim);
    return data->getElevation((x - id.x) * dim, (y - id.y) * dim);
}

std::optional<double> TerrainTileManager::sampleElevation(const LatLng& latLng, uint8_t zoom, uint8_t fallbackZoom) {
    bool stillLoading = false;
    const uint8_t zooms[2] = {zoom, fallbackZoom};
    for (int i = 0; i < 2; ++i) {
        const uint8_t z = zooms[i];
        if (i == 1 && z >= zoom) {
            break; // no coarser fallback
        }
        const CanonicalTileID id = tileFor(latLng, z);
        if (const auto data = getDEMData(id)) {
            double x;
            double y;
            tileCoordinate(latLng, z, x, y);
            const double dim = static_cast<double>(data->dim);
            return data->getElevation((x - id.x) * dim, (y - id.y) * dim);
        }
        if (!isFailed(id)) {
            stillLoading = true;
        }
    }
    return stillLoading ? std::nullopt : std::optional<double>(0.0);
}

uint8_t TerrainTileManager::terrainZoomFor(double mapZoom, uint16_t sourceTileSize) {
    const double terrainTileSize = static_cast<double>(sourceTileSize) * static_cast<double>(1u << deltaZoom);
    const double desired = std::floor(mapZoom + std::log2(512.0 / terrainTileSize));
    return static_cast<uint8_t>(std::clamp(desired, 0.0, static_cast<double>(maxTerrainZoom)));
}

std::optional<CanonicalTileID> TerrainTileManager::sourceTileFor(const CanonicalTileID& terrainTile) const {
    int z = static_cast<int>(terrainTile.z) - deltaZoom;
    z = std::min<int>(z, tileset.zoomRange.max);
    if (z < static_cast<int>(tileset.zoomRange.min) || z < 0) {
        return std::nullopt;
    }
    const int shift = terrainTile.z - z;
    return CanonicalTileID(static_cast<uint8_t>(z), terrainTile.x >> shift, terrainTile.y >> shift);
}

std::optional<TerrainTileManager::SourceTile> TerrainTileManager::getSourceTile(const CanonicalTileID& terrainTile) {
    const auto source = sourceTileFor(terrainTile);
    if (!source) {
        return std::nullopt;
    }
    if (auto data = getDEMData(*source)) {
        return SourceTile{std::move(data), *source};
    }
    // Still loading: use the nearest loaded ancestor meanwhile.
    for (int z = static_cast<int>(source->z) - 1; z >= static_cast<int>(tileset.zoomRange.min) && z >= 0; --z) {
        const int shift = source->z - z;
        const CanonicalTileID id(static_cast<uint8_t>(z), source->x >> shift, source->y >> shift);
        const auto it = tiles.find(keyFor(id));
        if (it != tiles.end() && it->second.data) {
            return SourceTile{it->second.data, id};
        }
    }
    return std::nullopt;
}

std::optional<std::pair<double, double>> TerrainTileManager::getMinMaxElevation(const CanonicalTileID& id) const {
    const auto source = sourceTileFor(id);
    if (!source) {
        return std::nullopt;
    }
    for (int z = source->z; z >= 0; --z) {
        const int shift = source->z - z;
        const auto it = tiles.find(keyFor(CanonicalTileID(static_cast<uint8_t>(z), source->x >> shift, source->y >> shift)));
        if (it != tiles.end() && it->second.data) {
            return std::make_pair(it->second.data->minElevation, it->second.data->maxElevation);
        }
    }
    return std::nullopt;
}

void TerrainTileManager::requestAncestor(const CanonicalTileID& id, uint8_t zoom) {
    if (id.z < zoom) {
        return;
    }
    const int shift = id.z - zoom;
    getDEMData(CanonicalTileID(zoom, id.x >> shift, id.y >> shift));
}

std::vector<CanonicalTileID> TerrainTileManager::takeUpdatedTiles() {
    std::vector<CanonicalTileID> result;
    result.reserve(updatedTiles.size());
    for (const auto& [z, x, y] : updatedTiles) {
        result.emplace_back(z, x, y);
    }
    updatedTiles.clear();
    return result;
}

bool TerrainTileManager::hasData() const {
    for (const auto& [key, entry] : tiles) {
        if (entry.data) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<DEMData> TerrainTileManager::getDEMData(const CanonicalTileID& id) {
    auto it = tiles.find(keyFor(id));
    if (it == tiles.end()) {
        it = tiles.emplace(keyFor(id), Entry{}).first;
    }
    if (it->second.data) {
        return it->second.data;
    }
    if (!it->second.request && !isFailed(id)) {
        startRequest(id);
    }
    return nullptr;
}

bool TerrainTileManager::isFailed(const CanonicalTileID& id) const {
    const auto it = tiles.find(keyFor(id));
    return it != tiles.end() && it->second.failed &&
           std::chrono::steady_clock::now() - it->second.failedAt < kRetryDelay;
}

void TerrainTileManager::startRequest(const CanonicalTileID& id) {
    if (fileSource == nullptr || tileset.tiles.empty()) {
        return;
    }

    const auto encoding = tileset.rasterEncoding.value_or(Tileset::RasterEncoding::Mapbox);
    Resource resource = Resource::tile(tileset.tiles.front(),
                                       1.0f,
                                       static_cast<int32_t>(id.x),
                                       static_cast<int32_t>(id.y),
                                       static_cast<int8_t>(id.z),
                                       tileset.scheme,
                                       Resource::LoadingMethod::NetworkOnly);

    auto& entry = tiles[keyFor(id)];
    entry.request = fileSource->request(resource, [this, id, encoding](const Response& res) {
        onTileResponse(id, res, encoding);
    });

    if (!entry.request) {
        tiles.erase(keyFor(id));
    }
}

void TerrainTileManager::onTileResponse(const CanonicalTileID& id,
                                        const Response& res,
                                        Tileset::RasterEncoding encoding) {
    const auto it = tiles.find(keyFor(id));
    if (it == tiles.end()) {
        return;
    }
    it->second.request.reset();

    if (res.error || !res.data) {
        it->second.failed = true;
        it->second.failedAt = std::chrono::steady_clock::now();
        return;
    }
    it->second.failed = false;

    try {
        PremultipliedImage image = decodeImage(*res.data);
        it->second.data = std::make_shared<DEMData>(image, encoding);
        updatedTiles.insert(it->first); // a tile first built flat (while missing/retrying) must be rebuilt
        // Share edge and corner pixels with the loaded neighbours at the same zoom, so a tile's mesh edge matches the
        // neighbour's (a 1-pixel border around each DEM, as the hillshade pipeline does).
        const auto tilesAcross = static_cast<int64_t>(1) << id.z;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int64_t ny = static_cast<int64_t>(id.y) + dy;
                if ((dx == 0 && dy == 0) || ny < 0 || ny >= tilesAcross) {
                    continue;
                }
                const auto nx = static_cast<uint32_t>((static_cast<int64_t>(id.x) + dx + tilesAcross) % tilesAcross);
                const auto neighbour = tiles.find(keyFor(CanonicalTileID(id.z, nx, static_cast<uint32_t>(ny))));
                if (neighbour == tiles.end() || !neighbour->second.data) {
                    continue;
                }
                it->second.data->backfillBorder(*neighbour->second.data, static_cast<int8_t>(dx), static_cast<int8_t>(dy));
                neighbour->second.data->backfillBorder(*it->second.data, static_cast<int8_t>(-dx), static_cast<int8_t>(-dy));
                updatedTiles.insert(neighbour->first);
            }
        }
        if (onTileLoaded) {
            onTileLoaded();
        }
    } catch (const std::exception&) {
        // A malformed DEM tile simply leaves the terrain flat there.
    }
}

} // namespace mln
