#include <mln/style/sky.hpp>
#include <mln/style/sky_impl.hpp>
#include <mln/style/conversion/property_value.hpp>
#include <mln/style/conversion/transition_options.hpp>
#include <mln/style/conversion_impl.hpp>

#include <mapbox/eternal.hpp>

#include <utility>

namespace mln {
namespace style {

namespace {
SkyObserver nullObserver;

enum class Property : uint8_t {
    SkyColor,
    HorizonColor,
    FogColor,
    FogGroundBlend,
    HorizonFogBlend,
    SkyHorizonBlend,
    AtmosphereBlend,
    SkyColorTransition,
    HorizonColorTransition,
    FogColorTransition,
    FogGroundBlendTransition,
    HorizonFogBlendTransition,
    SkyHorizonBlendTransition,
    AtmosphereBlendTransition,
};

constexpr uint8_t toUint8(Property p) noexcept {
    return static_cast<uint8_t>(p);
}

constexpr const auto properties = mapbox::eternal::hash_map<mapbox::eternal::string, uint8_t>(
    {{"sky-color", toUint8(Property::SkyColor)},
     {"horizon-color", toUint8(Property::HorizonColor)},
     {"fog-color", toUint8(Property::FogColor)},
     {"fog-ground-blend", toUint8(Property::FogGroundBlend)},
     {"horizon-fog-blend", toUint8(Property::HorizonFogBlend)},
     {"sky-horizon-blend", toUint8(Property::SkyHorizonBlend)},
     {"atmosphere-blend", toUint8(Property::AtmosphereBlend)},
     {"sky-color-transition", toUint8(Property::SkyColorTransition)},
     {"horizon-color-transition", toUint8(Property::HorizonColorTransition)},
     {"fog-color-transition", toUint8(Property::FogColorTransition)},
     {"fog-ground-blend-transition", toUint8(Property::FogGroundBlendTransition)},
     {"horizon-fog-blend-transition", toUint8(Property::HorizonFogBlendTransition)},
     {"sky-horizon-blend-transition", toUint8(Property::SkyHorizonBlendTransition)},
     {"atmosphere-blend-transition", toUint8(Property::AtmosphereBlendTransition)}});

template <class P>
void setValue(Sky& sky, PropertyValue<typename P::Type> value) {
    auto impl_ = sky.mutableImpl();
    impl_->properties.template get<P>().value = std::move(value);
    sky.impl = std::move(impl_);
    sky.observer->onSkyChanged(sky);
}

template <class P>
void setTransition(Sky& sky, const TransitionOptions& options) {
    auto impl_ = sky.mutableImpl();
    impl_->properties.template get<P>().options = options;
    sky.impl = std::move(impl_);
    sky.observer->onSkyChanged(sky);
}

template <class P>
std::optional<conversion::Error> convertAndSet(Sky& sky, const conversion::Convertible& value) {
    conversion::Error error;
    // Zoom expressions are allowed (not feature or data-driven).
    std::optional<PropertyValue<typename P::Type>> typed = conversion::convert<PropertyValue<typename P::Type>>(
        value, error, false, false);
    if (!typed) {
        return error;
    }
    setValue<P>(sky, std::move(*typed));
    return std::nullopt;
}

} // namespace

Sky::Sky(Immutable<Sky::Impl> impl_)
    : impl(std::move(impl_)),
      observer(&nullObserver) {}

Sky::Sky()
    : Sky(makeMutable<Impl>()) {}

Sky::~Sky() = default;

void Sky::setObserver(SkyObserver* observer_) {
    observer = observer_ ? observer_ : &nullObserver;
}

Mutable<Sky::Impl> Sky::mutableImpl() const {
    return makeMutable<Impl>(*impl);
}

std::optional<conversion::Error> Sky::setProperty(const std::string& name, const conversion::Convertible& value) {
    const auto it = properties.find(name.c_str());
    if (it == properties.end()) {
        return conversion::Error{"sky doesn't support this property"};
    }
    switch (static_cast<Property>(it->second)) {
        case Property::SkyColor:
            return convertAndSet<SkyColor>(*this, value);
        case Property::HorizonColor:
            return convertAndSet<SkyHorizonColor>(*this, value);
        case Property::FogColor:
            return convertAndSet<SkyFogColor>(*this, value);
        case Property::FogGroundBlend:
            return convertAndSet<SkyFogGroundBlend>(*this, value);
        case Property::HorizonFogBlend:
            return convertAndSet<SkyHorizonFogBlend>(*this, value);
        case Property::SkyHorizonBlend:
            return convertAndSet<SkySkyHorizonBlend>(*this, value);
        case Property::AtmosphereBlend:
            return convertAndSet<SkyAtmosphereBlend>(*this, value);
        default:
            break;
    }

    conversion::Error error;
    std::optional<TransitionOptions> transition = conversion::convert<TransitionOptions>(value, error);
    if (!transition) {
        return error;
    }
    switch (static_cast<Property>(it->second)) {
        case Property::SkyColorTransition:
            setTransition<SkyColor>(*this, *transition);
            break;
        case Property::HorizonColorTransition:
            setTransition<SkyHorizonColor>(*this, *transition);
            break;
        case Property::FogColorTransition:
            setTransition<SkyFogColor>(*this, *transition);
            break;
        case Property::FogGroundBlendTransition:
            setTransition<SkyFogGroundBlend>(*this, *transition);
            break;
        case Property::HorizonFogBlendTransition:
            setTransition<SkyHorizonFogBlend>(*this, *transition);
            break;
        case Property::SkyHorizonBlendTransition:
            setTransition<SkySkyHorizonBlend>(*this, *transition);
            break;
        case Property::AtmosphereBlendTransition:
            setTransition<SkyAtmosphereBlend>(*this, *transition);
            break;
        default:
            return conversion::Error{"sky doesn't support this property"};
    }
    return std::nullopt;
}

StyleProperty Sky::getProperty(const std::string& name) const {
    const auto it = properties.find(name.c_str());
    if (it == properties.end()) {
        return {};
    }
    switch (static_cast<Property>(it->second)) {
        case Property::SkyColor:
            return conversion::makeStyleProperty(getSkyColor());
        case Property::HorizonColor:
            return conversion::makeStyleProperty(getHorizonColor());
        case Property::FogColor:
            return conversion::makeStyleProperty(getFogColor());
        case Property::FogGroundBlend:
            return conversion::makeStyleProperty(getFogGroundBlend());
        case Property::HorizonFogBlend:
            return conversion::makeStyleProperty(getHorizonFogBlend());
        case Property::SkyHorizonBlend:
            return conversion::makeStyleProperty(getSkyHorizonBlend());
        case Property::AtmosphereBlend:
            return conversion::makeStyleProperty(getAtmosphereBlend());
        case Property::SkyColorTransition:
            return conversion::makeStyleProperty(impl->properties.template get<SkyColor>().options);
        case Property::HorizonColorTransition:
            return conversion::makeStyleProperty(impl->properties.template get<SkyHorizonColor>().options);
        case Property::FogColorTransition:
            return conversion::makeStyleProperty(impl->properties.template get<SkyFogColor>().options);
        case Property::FogGroundBlendTransition:
            return conversion::makeStyleProperty(impl->properties.template get<SkyFogGroundBlend>().options);
        case Property::HorizonFogBlendTransition:
            return conversion::makeStyleProperty(impl->properties.template get<SkyHorizonFogBlend>().options);
        case Property::SkyHorizonBlendTransition:
            return conversion::makeStyleProperty(impl->properties.template get<SkySkyHorizonBlend>().options);
        case Property::AtmosphereBlendTransition:
            return conversion::makeStyleProperty(impl->properties.template get<SkyAtmosphereBlend>().options);
    }
    return {};
}

#define MLN_SKY_ACCESSORS(Name, Prop, Type)                                       \
    PropertyValue<Type> Sky::get##Name() const {                                  \
        return impl->properties.template get<Prop>().value;                       \
    }                                                                             \
    void Sky::set##Name(PropertyValue<Type> value) {                              \
        setValue<Prop>(*this, std::move(value));                                  \
    }

MLN_SKY_ACCESSORS(SkyColor, SkyColor, Color)
MLN_SKY_ACCESSORS(HorizonColor, SkyHorizonColor, Color)
MLN_SKY_ACCESSORS(FogColor, SkyFogColor, Color)
MLN_SKY_ACCESSORS(FogGroundBlend, SkyFogGroundBlend, float)
MLN_SKY_ACCESSORS(HorizonFogBlend, SkyHorizonFogBlend, float)
MLN_SKY_ACCESSORS(SkyHorizonBlend, SkySkyHorizonBlend, float)
MLN_SKY_ACCESSORS(AtmosphereBlend, SkyAtmosphereBlend, float)

#undef MLN_SKY_ACCESSORS

} // namespace style
} // namespace mln
