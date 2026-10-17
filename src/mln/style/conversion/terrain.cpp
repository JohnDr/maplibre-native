#include <mln/style/conversion/terrain.hpp>
#include <mln/style/conversion_impl.hpp>
#include <mln/style/conversion/constant.hpp>

namespace mln {
namespace style {
namespace conversion {

std::optional<Terrain> Converter<Terrain>::operator()(const Convertible& value, Error& error) const {
    if (!isObject(value)) {
        error.message = "terrain must be an object";
        return std::nullopt;
    }

    Terrain terrain;

    if (const auto source = objectMember(value, "source")) {
        const auto converted = convert<std::string>(*source, error);
        if (!converted) {
            return std::nullopt;
        }
        terrain.setSource(*converted);
    }

    if (const auto exaggeration = objectMember(value, "exaggeration")) {
        const auto converted = convert<float>(*exaggeration, error);
        if (!converted) {
            return std::nullopt;
        }
        terrain.setExaggeration(*converted);
    }

    return {std::move(terrain)};
}

} // namespace conversion
} // namespace style
} // namespace mln
