#include <mln/renderer/terrain_tile_manager.hpp>

#include <mln/geometry/dem_data.hpp>
#include <mln/math/clamp.hpp>
#include <mln/storage/file_source.hpp>
#include <mln/storage/resource.hpp>
#include <mln/storage/response.hpp>
#include <mln/util/async_request.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/image.hpp>

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
        if (onTileLoaded) {
            onTileLoaded();
        }
    } catch (const std::exception&) {
        // A malformed DEM tile simply leaves the terrain flat there.
    }
}

} // namespace mln
