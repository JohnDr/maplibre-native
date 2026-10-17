#pragma once

#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/vulkan/shader_program.hpp>

namespace mln {
namespace shaders {

/// The sky: a screen-filling quad shaded by the distance above the horizon line (MapLibre GL JS `sky.fragment.glsl`,
/// Mercator branch). Drawn first, under everything else; the area below the horizon is left transparent.
template <>
struct ShaderSource<BuiltIn::SkyShader, gfx::Backend::Type::Vulkan> {
    static constexpr const char* name = "SkyShader";

    static const std::array<AttributeInfo, 1> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 0> textures;

    static constexpr auto prelude = R"(

#define idSkyUBO idDrawableReservedFragmentOnlyUBO

)";
    static constexpr auto vertex = R"(

layout(location = 0) in ivec2 in_position;

void main() {
    gl_Position = vec4(vec2(in_position), 0.0, 1.0);
}
)";

    static constexpr auto fragment = R"(

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Constants {
    int ubo_index;
} constant;

struct SkyUBO {
    vec4 sky_color;     // premultiplied
    vec4 horizon_color; // premultiplied
    vec4 horizon;       // xy: a point on the horizon line (framebuffer pixels, y down); z: sky-horizon blend (pixels)
    vec4 normal;        // xy: horizon normal (pointing up the screen, y down)
};

layout(std140, set = LAYER_SET_INDEX, binding = idSkyUBO) readonly buffer SkyUBOVector {
    SkyUBO sky_ubo[];
} skyVector;

void main() {
    const SkyUBO sky = skyVector.sky_ubo[constant.ubo_index];

    // Signed distance above the horizon, in pixels.
    const float blend = (sky.horizon.y - gl_FragCoord.y) * sky.normal.y + (gl_FragCoord.x - sky.horizon.x) * sky.normal.x;

    out_color = vec4(0.0);
    if (blend > 0.0) {
        if (blend < sky.horizon.z) {
            out_color = mix(sky.sky_color, sky.horizon_color, pow(1.0 - blend / sky.horizon.z, 2.0));
        } else {
            out_color = sky.sky_color;
        }
    }
}
)";
};

} // namespace shaders
} // namespace mln
