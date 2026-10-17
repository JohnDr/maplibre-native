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
    static const std::array<TextureInfo, 0> textures;

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

void main() {
    gl_Position = projectTileFor3D(in_position.xy, in_position.z, projectionVector.projection_ubo[constant.ubo_index]);
    applySurfaceTransform();
    v_shade = in_position.w;
}
)";

    static constexpr auto fragment = R"(

layout(location = 0) in float v_shade;
layout(location = 0) out vec4 out_color;

void main() {
    // A simple hypsometric-ish ramp: greener valleys, paler peaks, darkened by the precomputed shade.
    const vec3 low = vec3(0.24, 0.42, 0.24);
    const vec3 high = vec3(0.86, 0.83, 0.72);
    out_color = vec4(mix(low, high, clamp(v_shade, 0.0, 1.0)), 1.0);
}
)";
};

} // namespace shaders
} // namespace mln
