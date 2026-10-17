#pragma once

#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/vulkan/shader_program.hpp>

namespace mln {
namespace shaders {

/// Renders the terrain surface: a 3D mesh whose vertices carry an elevation in metres and no shading (the relief
/// comes from the draped hillshade layer, as in GL JS).
/// The projection matrix scales z by pixels-per-metre (as for every 3D layer), so the vertex shader is the same
/// projection the other 3D layers use.
template <>
struct ShaderSource<BuiltIn::TerrainShader, gfx::Backend::Type::Vulkan> {
    static constexpr const char* name = "TerrainShader";

    static const std::array<AttributeInfo, 1> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 1> textures;

    static constexpr auto prelude = R"(

#define idTerrainFogUBO layerUBOStartId

)";
    static constexpr auto vertex = R"(

// xy: tile units (0..EXTENT); z: elevation in metres.
layout(location = 0) in vec3 in_position;

layout(push_constant) uniform Constants {
    int ubo_index;
} constant;

layout(std140, set = LAYER_SET_INDEX, binding = idProjectionUBO) readonly buffer ProjectionUBOVector {
    ProjectionUBO projection_ubo[];
} projectionVector;

layout(set = LAYER_SET_INDEX, binding = idTerrainFogUBO) uniform TerrainFogUBO {
    vec4 fog_color;      // premultiplied
    vec4 horizon_color;  // premultiplied
    vec4 params;         // x: fog-ground-blend, y: fog opacity (0 = off), z: horizon-fog-blend
    vec4 depth_range;    // x: near, y: far (fog depth range, in the projection's eye-space units)
} fog;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out float v_fog_depth;

void main() {
    gl_Position = projectTileFor3D(in_position.xy, in_position.z, projectionVector.projection_ubo[constant.ubo_index]);
    applySurfaceTransform();
    // GL JS `v_fog_depth`: the depth of this vertex in a projection whose near plane is the camera-to-sea-level
    // distance (terrain.vertex.glsl), in [0, 1]. For a perspective projection that is f*(d - n) / ((f - n)*d), with
    // d the eye-space depth, which is the clip w of the main projection.
    const float eyeDepth = max(gl_Position.w, 1e-6);
    v_fog_depth = fog.depth_range.y * (eyeDepth - fog.depth_range.x) / ((fog.depth_range.y - fog.depth_range.x) * eyeDepth);
    // Tile-local texture coordinate (0..1) for this tile's own drape texture.
    v_uv = in_position.xy / 8192.0;
}
)";

    static constexpr auto fragment = R"(

layout(location = 0) in vec2 v_uv;
layout(location = 1) in float v_fog_depth;

layout(set = LAYER_SET_INDEX, binding = idTerrainFogUBO) uniform TerrainFogUBO {
    vec4 fog_color;
    vec4 horizon_color;
    vec4 params;
    vec4 depth_range;
} fog;

vec4 gammaToLinear(vec4 color) {
    return pow(color, vec4(2.2));
}

vec4 linearToGamma(vec4 color) {
    return pow(color, vec4(1.0 / 2.2));
}

layout(set = DRAWABLE_IMAGE_SET_INDEX, binding = 0) uniform sampler2D drapeSampler;

layout(location = 0) out vec4 out_color;

void main() {
    // Per-tile drape: this tile's own texture, drawn as is (GL JS terrain.fragment.glsl: `fragColor = surface_color`).
    vec4 surface = texture(drapeSampler, v_uv);

    // GL JS terrain.fragment.glsl: blend the surface towards the fog / horizon colour with distance, in linear colour.
    const float ground_blend = fog.params.x;
    const float opacity = fog.params.y;
    const float horizon_fog_blend = fog.params.z;
    if (opacity > 0.0 && v_fog_depth > ground_blend) {
        const float blend_color = smoothstep(0.0, 1.0, max((v_fog_depth - horizon_fog_blend) / (1.0 - horizon_fog_blend), 0.0));
        const vec4 fog_horizon = mix(gammaToLinear(fog.fog_color), gammaToLinear(fog.horizon_color), blend_color);
        const float factor_fog = max(v_fog_depth - ground_blend, 0.0) / (1.0 - ground_blend);
        surface = linearToGamma(mix(gammaToLinear(surface), fog_horizon, pow(factor_fog, 2.0) * opacity));
    }
    out_color = surface;
}
)";
};

} // namespace shaders
} // namespace mln
