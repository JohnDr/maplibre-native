#include <mln/style/conversion/sky.hpp>
#include <mln/style/conversion_impl.hpp>

namespace mln {
namespace style {
namespace conversion {

std::optional<Sky> Converter<Sky>::operator()(const Convertible& value, Error& error) const {
    if (!isObject(value)) {
        error.message = "sky must be an object";
        return std::nullopt;
    }

    Sky sky;
    const auto failure = eachMember(value, [&](const std::string& name, const Convertible& member) -> std::optional<Error> {
        return sky.setProperty(name, member);
    });
    if (failure) {
        error = *failure;
        return std::nullopt;
    }
    return {std::move(sky)};
}

} // namespace conversion
} // namespace style
} // namespace mln
