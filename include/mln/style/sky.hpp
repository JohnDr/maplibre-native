#pragma once

#include <mln/util/color.hpp>

namespace mln {
namespace style {

/// The style's root `sky` object (style-spec `sky`): the colours and blends of the sky gradient above the horizon and
/// of the fog that fades the terrain into it. Defaults are the style-spec defaults. A style without a `sky` object has
/// no sky; `sky: {}` has the default sky.
///
/// Values are constants for now (the spec also allows zoom expressions and transitions).
class Sky {
public:
    Color skyColor = *Color::parse("#88C6FC");
    Color horizonColor = *Color::parse("#ffffff");
    Color fogColor = *Color::parse("#ffffff");
    float fogGroundBlend = 0.5f;
    float horizonFogBlend = 0.8f;
    float skyHorizonBlend = 0.8f;
    float atmosphereBlend = 0.8f;

    bool operator==(const Sky&) const = default;
};

} // namespace style
} // namespace mln
