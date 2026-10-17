#include <mln/renderer/render_sky.hpp>

namespace mln {

RenderSky::RenderSky(Immutable<style::Sky::Impl> impl_)
    : impl(std::move(impl_)),
      transitioning(impl->properties.untransitioned()) {
    const auto evaluated = style::SkyProperties::PossiblyEvaluated{};
    values = {evaluated.get<style::SkyColor>(),
              evaluated.get<style::SkyHorizonColor>(),
              evaluated.get<style::SkyFogColor>(),
              evaluated.get<style::SkyFogGroundBlend>(),
              evaluated.get<style::SkyHorizonFogBlend>(),
              evaluated.get<style::SkySkyHorizonBlend>(),
              evaluated.get<style::SkyAtmosphereBlend>()};
}

void RenderSky::transition(const TransitionParameters& parameters) {
    transitioning = impl->properties.transitioned(parameters, std::move(transitioning));
}

void RenderSky::evaluate(const PropertyEvaluationParameters& parameters) {
    const auto evaluated = transitioning.evaluate(parameters);
    values = {evaluated.get<style::SkyColor>(),
              evaluated.get<style::SkyHorizonColor>(),
              evaluated.get<style::SkyFogColor>(),
              evaluated.get<style::SkyFogGroundBlend>(),
              evaluated.get<style::SkyHorizonFogBlend>(),
              evaluated.get<style::SkySkyHorizonBlend>(),
              evaluated.get<style::SkyAtmosphereBlend>()};
}

bool RenderSky::hasTransition() const {
    return transitioning.hasTransition();
}

} // namespace mln
