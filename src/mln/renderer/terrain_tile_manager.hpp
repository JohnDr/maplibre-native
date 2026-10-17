#pragma once

#include <mln/tile/tile_id.hpp>
#include <mln/util/geo.hpp>
#include <mln/util/tileset.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

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

    /// The elevation in metres at `latLng`, or nullopt while the DEM covering it is still loading. Unlike
    /// `getElevation` this tells "not loaded yet" apart from sea level. Samples the tile at `zoom`; until that has
    /// loaded it falls back to the tile at `fallbackZoom` (requested too, so a coarse answer arrives quickly), when
    /// that is coarser. When every tile it tried definitively has no data (a failed request), the terrain there is
    /// flat and the result is 0 metres.
    std::optional<double> sampleElevation(const LatLng& latLng, uint8_t zoom, uint8_t fallbackZoom);

    /// The elevation range (metres) of the terrain in tile `id`: GL JS `Terrain#getMinMaxElevation`. Uses the loaded DEM
    /// tile at `id`'s zoom, else the nearest loaded ancestor; nullopt when none has loaded. Never requests a tile.
    std::optional<std::pair<double, double>> getMinMaxElevation(const CanonicalTileID& id) const;

    /// Requests the DEM tile at `zoom` that contains `id` (a no-op when `id` is coarser), so that a coarse elevation
    /// range exists for the tile cover to use while the fine tiles are still unknown.
    void requestAncestor(const CanonicalTileID& id, uint8_t zoom);

    /// The tile's request failed or returned no data recently (a source without tiles over open sea): the terrain there
    /// is flat. False while it is loading and once it has loaded.
    bool isMissing(const CanonicalTileID& id) const { return isFailed(id); }

    /// Tiles whose DEM border was changed by a neighbour's data since the last call (their meshes must be rebuilt so
    /// shared edges agree; GL JS `DEMData#backfillBorder`).
    std::vector<CanonicalTileID> takeUpdatedTiles();

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
        /// The last request failed or returned no data (for example a DEM tile that does not exist over open ocean).
        /// The tile is not requested again before `kRetryDelay` has passed, so a missing tile cannot cause a request
        /// storm.
        bool failed = false;
        std::chrono::steady_clock::time_point failedAt;
    };

    static constexpr std::chrono::seconds kRetryDelay{20};

    /// Whether the tile's last request failed recently enough that it will not be retried yet.
    bool isFailed(const CanonicalTileID&) const;

    void startRequest(const CanonicalTileID&);
    void onTileResponse(const CanonicalTileID&, const Response&, Tileset::RasterEncoding);

    FileSource* fileSource;
    Tileset tileset;
    std::function<void()> onTileLoaded;
    std::map<std::tuple<uint8_t, uint32_t, uint32_t>, Entry> tiles;
    std::set<std::tuple<uint8_t, uint32_t, uint32_t>> updatedTiles;
};

} // namespace mln
