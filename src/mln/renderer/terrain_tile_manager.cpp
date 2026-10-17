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
    if (!it->second.request) {
        startRequest(id);
    }
    return nullptr;
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
        return;
    }

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
