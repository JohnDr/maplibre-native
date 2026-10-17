#pragma once

#include <mln/style/conversion.hpp>
#include <mln/style/style_property.hpp>
#include <mln/util/immutable.hpp>

#include <optional>
#include <string>

namespace mln {
namespace style {

class TerrainObserver;

/// The style's root `terrain` object: a `raster-dem` source whose elevation is draped over the map as a 3D surface.
class Terrain {
public:
    Terrain();
    ~Terrain();

    std::optional<conversion::Error> setProperty(const std::string& name, const conversion::Convertible& value);
    StyleProperty getProperty(const std::string&) const;

    static double getDefaultExaggeration();
    double getExaggeration() const;
    void setExaggeration(double);

    const std::string& getSource() const;
    void setSource(std::string);

    /// Whether a source has been assigned; an unset terrain renders the map flat.
    bool isEnabled() const;

    class Impl;
    Immutable<Impl> impl;
    explicit Terrain(Immutable<Impl>);
    Mutable<Impl> mutableImpl() const;

    TerrainObserver* observer = nullptr;
    void setObserver(TerrainObserver*);
};

} // namespace style
} // namespace mln
