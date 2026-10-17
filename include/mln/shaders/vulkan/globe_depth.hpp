#pragma once

#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/vulkan/shader_program.hpp>

namespace mln {
namespace shaders {

constexpr auto globeDepthShaderPrelude = R"(

)";

/// Writes the planet surface into the depth buffer so 3D geometry behind the horizon is hidden.
template <>
struct ShaderSource<BuiltIn::GlobeDepthShader, gfx::Backend::Type::Vulkan> {
    static constexpr const char* name = "GlobeDepthShader";

    static const std::array<AttributeInfo, 1> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 0> textures;

    static constexpr auto prelude = globeDepthShaderPrelude;
    static constexpr auto vertex = R"(

layout(location = 0) in ivec2 in_position;

layout(push_constant) uniform Constants {
    int ubo_index;
} constant;

layout(std140, set = LAYER_SET_INDEX, binding = idProjectionUBO) readonly buffer ProjectionUBOVector {
    ProjectionUBO projection_ubo[];
} projectionVector;

void main() {
    gl_Position = projectTileFor3D(vec2(in_position), 0.0, projectionVector.projection_ubo[constant.ubo_index]);
    applySurfaceTransform();
}
)";

    static constexpr auto fragment = R"(

void main() {
}
)";
};

} // namespace shaders
} // namespace mln
