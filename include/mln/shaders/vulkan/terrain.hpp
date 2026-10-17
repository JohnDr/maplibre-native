#pragma once

#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/vulkan/shader_program.hpp>

namespace mln {
namespace shaders {

/// Renders the terrain surface: a 3D mesh whose vertices carry a world-pixel elevation and a precomputed shade.
/// The third vertex component is already in world pixels (metres converted with `pixelsPerMeter` on the CPU), so the
/// vertex shader is the same projection the other 3D layers use.
template <>
struct ShaderSource<BuiltIn::TerrainShader, gfx::Backend::Type::Vulkan> {
    static constexpr const char* name = "TerrainShader";

    static const std::array<AttributeInfo, 1> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 1> textures;

    static constexpr auto prelude = R"(
)";
    static constexpr auto vertex = R"(

// xy: tile units (0..EXTENT); z: world-pixel elevation; w: shade in [0, 1].
layout(location = 0) in vec4 in_position;

layout(push_constant) uniform Constants {
    int ubo_index;
} constant;

layout(std140, set = LAYER_SET_INDEX, binding = idProjectionUBO) readonly buffer ProjectionUBOVector {
    ProjectionUBO projection_ubo[];
} projectionVector;

layout(location = 0) out float v_shade;
layout(location = 1) out vec2 v_uv;

void main() {
    gl_Position = projectTileFor3D(in_position.xy, in_position.z, projectionVector.projection_ubo[constant.ubo_index]);
    applySurfaceTransform();
    v_shade = in_position.w;
    // Tile-local texture coordinate (0..1) for this tile's own drape texture.
    v_uv = in_position.xy / 8192.0;
}
)";

    static constexpr auto fragment = R"(

layout(location = 0) in float v_shade;
layout(location = 1) in vec2 v_uv;

layout(set = DRAWABLE_IMAGE_SET_INDEX, binding = 0) uniform sampler2D drapeSampler;

layout(location = 0) out vec4 out_color;

void main() {
    // Per-tile drape: this tile's own texture, shaded by the precomputed hillshade. Where no draping layer was
    // rendered (black, e.g. before imagery loads) fall back to a hypsometric tint so the relief is still visible.
    const vec4 draped = texture(drapeSampler, v_uv);
    const float shade = clamp(v_shade, 0.0, 1.0);
    const float lum = max(draped.r, max(draped.g, draped.b));
    const vec3 low = vec3(0.24, 0.42, 0.24);
    const vec3 high = vec3(0.86, 0.83, 0.72);
    const vec3 tint = mix(low, high, shade);
    out_color = vec4((lum < 0.02 ? tint : draped.rgb) * shade, 1.0);
}
)";
};

} // namespace shaders
} // namespace mln
