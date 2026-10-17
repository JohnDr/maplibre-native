#pragma once

#include <mln/tile/tile_id.hpp>
#include <mln/util/geo.hpp>
#include <mln/util/tileset.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <tuple>

namespace mln {

class AsyncRequest;
class DEMData;
class FileSource;
struct Response;

/// Fetches and caches `raster-dem` tiles for the terrain, independent of any layer, and samples their elevation.
/// The terrain counterpart of GL JS's `TerrainTileManager`, with a terrain-owned tile set: a `raster-dem` source is
/// only loaded when a *layer* uses it, and terrain has no layer, so it manages its own tiles.
class TerrainTileManager {
public:
    TerrainTileManager(FileSource* fileSource, Tileset tileset);
    ~TerrainTileManager();

    void setTileset(Tileset);

    /// The DEM tile (z, x, y) covering `latLng` at `zoom`.
    static CanonicalTileID tileFor(const LatLng&, uint8_t zoom);

    /// The elevation in metres at `latLng`, sampled from the DEM tile covering it at `zoom`. Requests the tile when it
    /// is missing, and returns 0 until it has loaded.
    double getElevation(const LatLng& latLng, uint8_t zoom);

    /// Whether any DEM tile has been loaded.
    bool hasData() const;

    /// The DEM data for a tile, requesting it when necessary. Null until it has loaded.
    std::shared_ptr<DEMData> getTileData(const CanonicalTileID& id) { return getDEMData(id); }

    /// Called when a tile finishes loading, so the map can schedule a repaint.
    void setOnTileLoaded(std::function<void()> callback) { onTileLoaded = std::move(callback); }

protected:
    /// The DEM data for a tile, requesting it when necessary. Overridable so tests can inject data.
    virtual std::shared_ptr<DEMData> getDEMData(const CanonicalTileID&);

private:
    struct Entry {
        std::shared_ptr<DEMData> data;
        std::unique_ptr<AsyncRequest> request;
    };

    void startRequest(const CanonicalTileID&);
    void onTileResponse(const CanonicalTileID&, const Response&, Tileset::RasterEncoding);

    FileSource* fileSource;
    Tileset tileset;
    std::function<void()> onTileLoaded;
    std::map<std::tuple<uint8_t, uint32_t, uint32_t>, Entry> tiles;
};

} // namespace mln
