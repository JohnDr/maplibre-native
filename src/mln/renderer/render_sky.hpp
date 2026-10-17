#pragma once

#include <mln/style/sky_impl.hpp>
#include <mln/util/color.hpp>
#include <mln/util/immutable.hpp>

namespace mln {

class TransitionParameters;
class PropertyEvaluationParameters;

/// The sky's values at the current zoom and transition time (premultiplied colours).
struct EvaluatedSky {
    Color skyColor;
    Color horizonColor;
    Color fogColor;
    float fogGroundBlend;
    float horizonFogBlend;
    float skyHorizonBlend;
    float atmosphereBlend;
};

/// Transitions and evaluates the style's `sky` (the way `RenderLight` does for the light).
class RenderSky {
public:
    explicit RenderSky(Immutable<style::Sky::Impl>);

    void transition(const TransitionParameters&);
    void evaluate(const PropertyEvaluationParameters&);
    bool hasTransition() const;

    const EvaluatedSky& getEvaluated() const { return values; }

    Immutable<style::Sky::Impl> impl;

private:
    style::SkyProperties::Unevaluated transitioning;
    EvaluatedSky values;
};

} // namespace mln
