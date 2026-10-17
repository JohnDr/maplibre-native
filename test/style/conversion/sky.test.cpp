#include <mln/test/util.hpp>

#include <mln/style/conversion/json.hpp>
#include <mln/style/conversion/constant.hpp>
#include <mln/style/conversion/sky.hpp>
#include <mln/style/conversion_impl.hpp>
#include <mln/style/rapidjson_conversion.hpp>
#include <mln/util/color.hpp>

using namespace mln;
using namespace mln::style;
using namespace mln::style::conversion;

TEST(StyleConversion, SkyDefaults) {
    Error error;
    const auto sky = convertJSON<Sky>("{}", error);
    ASSERT_TRUE(sky.has_value()) << error.message;

    // The style-spec defaults.
    EXPECT_EQ(sky->skyColor, *Color::parse("#88C6FC"));
    EXPECT_EQ(sky->horizonColor, *Color::parse("#ffffff"));
    EXPECT_EQ(sky->fogColor, *Color::parse("#ffffff"));
    EXPECT_FLOAT_EQ(sky->fogGroundBlend, 0.5f);
    EXPECT_FLOAT_EQ(sky->horizonFogBlend, 0.8f);
    EXPECT_FLOAT_EQ(sky->skyHorizonBlend, 0.8f);
    EXPECT_FLOAT_EQ(sky->atmosphereBlend, 0.8f);
}

TEST(StyleConversion, SkyProperties) {
    Error error;
    const auto sky = convertJSON<Sky>(
        R"({"sky-color": "#199EF3", "horizon-color": "#ff0000", "fog-color": "#0000ff", "fog-ground-blend": 0.25,
            "horizon-fog-blend": 0.5, "sky-horizon-blend": 2.0, "atmosphere-blend": -1})",
        error);
    ASSERT_TRUE(sky.has_value()) << error.message;

    EXPECT_EQ(sky->skyColor, *Color::parse("#199EF3"));
    EXPECT_EQ(sky->horizonColor, *Color::parse("#ff0000"));
    EXPECT_EQ(sky->fogColor, *Color::parse("#0000ff"));
    EXPECT_FLOAT_EQ(sky->fogGroundBlend, 0.25f);
    EXPECT_FLOAT_EQ(sky->horizonFogBlend, 0.5f);
    // Blends are limited to [0, 1] (style-spec minimum / maximum).
    EXPECT_FLOAT_EQ(sky->skyHorizonBlend, 1.0f);
    EXPECT_FLOAT_EQ(sky->atmosphereBlend, 0.0f);
}

TEST(StyleConversion, SkyErrors) {
    Error error;
    EXPECT_FALSE(convertJSON<Sky>("[]", error).has_value());
    EXPECT_FALSE(convertJSON<Sky>(R"({"sky-color": 3})", error).has_value());
}
