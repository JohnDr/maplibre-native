#include <mln/style/conversion/sky.hpp>
#include <mln/style/conversion_impl.hpp>
#include <mln/style/conversion/constant.hpp>

namespace mln {
namespace style {
namespace conversion {

namespace {

bool convertBlend(const Convertible& value, const char* name, float& out, Error& error) {
    const auto member = objectMember(value, name);
    if (!member) {
        return true;
    }
    const auto converted = convert<float>(*member, error);
    if (!converted) {
        return false;
    }
    out = std::clamp(*converted, 0.0f, 1.0f);
    return true;
}

bool convertColor(const Convertible& value, const char* name, Color& out, Error& error) {
    const auto member = objectMember(value, name);
    if (!member) {
        return true;
    }
    const auto converted = convert<Color>(*member, error);
    if (!converted) {
        return false;
    }
    out = *converted;
    return true;
}

} // namespace

std::optional<Sky> Converter<Sky>::operator()(const Convertible& value, Error& error) const {
    if (!isObject(value)) {
        error.message = "sky must be an object";
        return std::nullopt;
    }

    Sky sky;
    if (!convertColor(value, "sky-color", sky.skyColor, error) ||
        !convertColor(value, "horizon-color", sky.horizonColor, error) ||
        !convertColor(value, "fog-color", sky.fogColor, error) ||
        !convertBlend(value, "fog-ground-blend", sky.fogGroundBlend, error) ||
        !convertBlend(value, "horizon-fog-blend", sky.horizonFogBlend, error) ||
        !convertBlend(value, "sky-horizon-blend", sky.skyHorizonBlend, error) ||
        !convertBlend(value, "atmosphere-blend", sky.atmosphereBlend, error)) {
        return std::nullopt;
    }
    return {std::move(sky)};
}

} // namespace conversion
} // namespace style
} // namespace mln
