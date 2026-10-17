#pragma once

#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/vulkan/shader_program.hpp>

namespace mln {
namespace shaders {

/// The sky: a screen-filling quad shaded by the distance above the horizon line (MapLibre GL JS `sky.fragment.glsl`).
/// With the globe the horizon is the planet's limb and the sky fades out through the atmosphere. Drawn first, under
/// everything else; the area below the horizon is left transparent.
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
    vec4 sky_color;      // premultiplied
    vec4 horizon_color;  // premultiplied
    vec4 horizon;        // xy: a point on the horizon line (framebuffer pixels, y down); z: sky-horizon blend (px)
    vec4 normal;         // xy: horizon normal (pointing up the screen, y down)
    mat4 inv_proj;       // the globe's inverse projection (clip -> view)
    vec4 globe;          // xyz: globe centre in view space; w: globe radius (px)
    vec4 params;         // x: sky blend (projection transition); y: atmosphere blend; z: camera-to-centre distance;
                         // w: device pixel ratio
    vec4 viewport;       // xy: viewport size in framebuffer pixels
};

layout(std140, set = LAYER_SET_INDEX, binding = idSkyUBO) readonly buffer SkyUBOVector {
    SkyUBO sky_ubo[];
} skyVector;

const float ATMOSPHERE_HEIGHT_TO_GLOBE_RADIUS = 100000.0 / 6371000.0;

void main() {
    const SkyUBO sky = skyVector.sky_ubo[constant.ubo_index];
    const float sky_blend = sky.params.x;

    // Signed distance above the horizon, in pixels.
    float blend = (sky.horizon.y - gl_FragCoord.y) * sky.normal.y + (gl_FragCoord.x - sky.horizon.x) * sky.normal.x;
    float opacity = 1.0;

    if (sky_blend > 0.0) {
        // The view ray of this pixel (GL convention: NDC y up, so the framebuffer's top row is +1).
        const vec2 ndc = vec2(gl_FragCoord.x / sky.viewport.x * 2.0 - 1.0, 1.0 - gl_FragCoord.y / sky.viewport.y * 2.0);
        const vec3 ray = normalize((sky.inv_proj * vec4(ndc, 0.0, 1.0)).xyz);
        const vec3 globe_position = sky.globe.xyz;
        const float globe_radius = sky.globe.w;
        const float globe_distance = length(globe_position);
        const float angle_to_globe_center = acos(clamp(dot(ray, globe_position) / globe_distance, -1.0, 1.0));
        const float horizon_angle = asin(min(globe_radius / globe_distance, 1.0));
        blend = mix(blend,
                    ((angle_to_globe_center - horizon_angle) * sky.params.z + 1.0) * sky.params.w,
                    sky_blend);
        const float closest_approach = max(dot(ray, globe_position), 0.0);
        const float lowest_altitude = sqrt(max(globe_distance * globe_distance - closest_approach * closest_approach, 0.0)) - globe_radius;
        const float atmosphere_height = globe_radius * ATMOSPHERE_HEIGHT_TO_GLOBE_RADIUS;
        const float inside_atmosphere = 1.0 - smoothstep(atmosphere_height, 10.0 * atmosphere_height, globe_distance - globe_radius);
        const float through_atmosphere = 1.0 - smoothstep(0.0, atmosphere_height, lowest_altitude);
        opacity = inside_atmosphere * through_atmosphere * (1.0 - sky.params.y);
    }

    out_color = vec4(0.0);
    if (blend > 0.0) {
        if (blend < sky.horizon.z) {
            out_color = mix(sky.sky_color, sky.horizon_color, pow(1.0 - blend / sky.horizon.z, 2.0));
        } else {
            out_color = sky.sky_color;
        }
    }
    out_color *= mix(1.0, opacity, sky_blend);
}
)";
};

} // namespace shaders
} // namespace mln
