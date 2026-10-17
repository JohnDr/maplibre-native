#pragma once

#include <mln/style/conversion.hpp>
#include <mln/style/property_value.hpp>
#include <mln/style/style_property.hpp>
#include <mln/style/transition_options.hpp>
#include <mln/util/color.hpp>
#include <mln/util/immutable.hpp>

#include <optional>
#include <string>

namespace mln {
namespace style {

class SkyObserver;

/// The style's root `sky` object (style-spec `sky`; MapLibre GL JS `style/sky.ts`): the colours and blends of the sky
/// gradient above the horizon, of the fog that fades the terrain into it and of the globe's atmosphere.
/// A style without a `sky` object has no sky; `sky: {}` has the default sky. Every property accepts a constant or a
/// zoom expression and has a transition (`<property>-transition`).
class Sky {
public:
    Sky();
    ~Sky();

    // Dynamic properties: `sky-color`, `horizon-color`, `fog-color`, `fog-ground-blend`, `horizon-fog-blend`,
    // `sky-horizon-blend`, `atmosphere-blend` and their `-transition` variants.
    std::optional<conversion::Error> setProperty(const std::string& name, const conversion::Convertible& value);
    StyleProperty getProperty(const std::string&) const;

    PropertyValue<Color> getSkyColor() const;
    void setSkyColor(PropertyValue<Color>);
    PropertyValue<Color> getHorizonColor() const;
    void setHorizonColor(PropertyValue<Color>);
    PropertyValue<Color> getFogColor() const;
    void setFogColor(PropertyValue<Color>);
    PropertyValue<float> getFogGroundBlend() const;
    void setFogGroundBlend(PropertyValue<float>);
    PropertyValue<float> getHorizonFogBlend() const;
    void setHorizonFogBlend(PropertyValue<float>);
    PropertyValue<float> getSkyHorizonBlend() const;
    void setSkyHorizonBlend(PropertyValue<float>);
    PropertyValue<float> getAtmosphereBlend() const;
    void setAtmosphereBlend(PropertyValue<float>);

    class Impl;
    Immutable<Impl> impl;
    explicit Sky(Immutable<Impl>);
    Mutable<Impl> mutableImpl() const;

    SkyObserver* observer = nullptr;
    void setObserver(SkyObserver*);
};

class SkyObserver {
public:
    virtual ~SkyObserver() = default;
    virtual void onSkyChanged(const Sky&) {}
};

} // namespace style
} // namespace mln
