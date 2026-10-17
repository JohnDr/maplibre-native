#pragma once

#include <mln/style/sky.hpp>
#include <mln/style/properties.hpp>
#include <mln/style/property_value.hpp>
#include <mln/renderer/property_evaluator.hpp>
#include <mln/util/color.hpp>

namespace mln {
namespace style {

template <class T>
class SkyProperty {
public:
    using TransitionableType = Transitionable<PropertyValue<T>>;
    using UnevaluatedType = Transitioning<PropertyValue<T>>;
    using EvaluatorType = PropertyEvaluator<T>;
    using PossiblyEvaluatedType = T;
    using Type = T;
    static constexpr bool IsDataDriven = false;
    static constexpr bool IsOverridable = false;
};

// Defaults: the style-spec `sky` defaults.
struct SkyColor : SkyProperty<Color> {
    static Color defaultValue() { return *Color::parse("#88C6FC"); }
};
struct SkyHorizonColor : SkyProperty<Color> {
    static Color defaultValue() { return *Color::parse("#ffffff"); }
};
struct SkyFogColor : SkyProperty<Color> {
    static Color defaultValue() { return *Color::parse("#ffffff"); }
};
struct SkyFogGroundBlend : SkyProperty<float> {
    static float defaultValue() { return 0.5f; }
};
struct SkyHorizonFogBlend : SkyProperty<float> {
    static float defaultValue() { return 0.8f; }
};
struct SkySkyHorizonBlend : SkyProperty<float> {
    static float defaultValue() { return 0.8f; }
};
struct SkyAtmosphereBlend : SkyProperty<float> {
    static float defaultValue() { return 0.8f; }
};

using SkyProperties = Properties<SkyColor,
                                 SkyHorizonColor,
                                 SkyFogColor,
                                 SkyFogGroundBlend,
                                 SkyHorizonFogBlend,
                                 SkySkyHorizonBlend,
                                 SkyAtmosphereBlend>;

class Sky::Impl {
public:
    SkyProperties::Transitionable properties;
};

} // namespace style
} // namespace mln
