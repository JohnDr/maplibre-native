#include <mln/test/util.hpp>

#include <mln/style/conversion/json.hpp>
#include <mln/style/conversion/constant.hpp>
#include <mln/style/conversion/sky.hpp>
#include <mln/style/conversion_impl.hpp>
#include <mln/style/rapidjson_conversion.hpp>
#include <mln/style/sky_impl.hpp>
#include <mln/util/color.hpp>

using namespace mln;
using namespace mln::style;
using namespace mln::style::conversion;

TEST(StyleConversion, SkyDefaults) {
    Error error;
    const auto sky = convertJSON<Sky>("{}", error);
    ASSERT_TRUE(sky.has_value()) << error.message;

    // Unset properties are undefined and evaluate to the style-spec defaults.
    EXPECT_TRUE(sky->getSkyColor().isUndefined());
    EXPECT_EQ(SkyColor::defaultValue(), *Color::parse("#88C6FC"));
    EXPECT_EQ(SkyHorizonColor::defaultValue(), *Color::parse("#ffffff"));
    EXPECT_EQ(SkyFogColor::defaultValue(), *Color::parse("#ffffff"));
    EXPECT_FLOAT_EQ(SkyFogGroundBlend::defaultValue(), 0.5f);
    EXPECT_FLOAT_EQ(SkyHorizonFogBlend::defaultValue(), 0.8f);
    EXPECT_FLOAT_EQ(SkySkyHorizonBlend::defaultValue(), 0.8f);
    EXPECT_FLOAT_EQ(SkyAtmosphereBlend::defaultValue(), 0.8f);
}

TEST(StyleConversion, SkyConstants) {
    Error error;
    const auto sky = convertJSON<Sky>(
        R"({"sky-color": "#199EF3", "horizon-color": "#ff0000", "fog-color": "#0000ff", "fog-ground-blend": 0.25,
            "horizon-fog-blend": 0.5, "sky-horizon-blend": 0.6, "atmosphere-blend": 0.1})",
        error);
    ASSERT_TRUE(sky.has_value()) << error.message;

    ASSERT_TRUE(sky->getSkyColor().isConstant());
    EXPECT_EQ(sky->getSkyColor().asConstant(), *Color::parse("#199EF3"));
    EXPECT_EQ(sky->getHorizonColor().asConstant(), *Color::parse("#ff0000"));
    EXPECT_EQ(sky->getFogColor().asConstant(), *Color::parse("#0000ff"));
    EXPECT_FLOAT_EQ(sky->getFogGroundBlend().asConstant(), 0.25f);
    EXPECT_FLOAT_EQ(sky->getHorizonFogBlend().asConstant(), 0.5f);
    EXPECT_FLOAT_EQ(sky->getSkyHorizonBlend().asConstant(), 0.6f);
    EXPECT_FLOAT_EQ(sky->getAtmosphereBlend().asConstant(), 0.1f);
}

TEST(StyleConversion, SkyExpressionsAndTransitions) {
    Error error;
    const auto sky = convertJSON<Sky>(
        R"({"atmosphere-blend": ["interpolate", ["linear"], ["zoom"], 0, 1, 10, 1, 12, 0],
            "sky-color": ["interpolate", ["linear"], ["zoom"], 0, "red", 10, "blue"],
            "sky-color-transition": {"duration": 1500, "delay": 100}})",
        error);
    ASSERT_TRUE(sky.has_value()) << error.message;

    // Zoom expressions are accepted.
    EXPECT_TRUE(sky->getAtmosphereBlend().isExpression());
    EXPECT_TRUE(sky->getSkyColor().isExpression());

    const auto options = sky->impl->properties.template get<SkyColor>().options;
    ASSERT_TRUE(options.duration.has_value());
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(*options.duration).count(), 1500);
    ASSERT_TRUE(options.delay.has_value());
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(*options.delay).count(), 100);
}

TEST(StyleConversion, SkyErrors) {
    Error error;
    EXPECT_FALSE(convertJSON<Sky>("[]", error).has_value());
    EXPECT_FALSE(convertJSON<Sky>(R"({"sky-color": 3})", error).has_value());
    EXPECT_FALSE(convertJSON<Sky>(R"({"no-such-property": 1})", error).has_value());
    // Data-driven expressions are not allowed.
    EXPECT_FALSE(convertJSON<Sky>(R"({"fog-ground-blend": ["get", "x"]})", error).has_value());
}
