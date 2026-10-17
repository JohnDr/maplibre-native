#pragma once

#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/vulkan/shader_program.hpp>

namespace mln {
namespace shaders {

/// The globe's atmosphere: single-scattering Rayleigh + Mie along each view ray (MapLibre GL JS
/// `atmosphere.fragment.glsl`, from https://github.com/wwwtyro/glsl-atmosphere). Drawn over the scene.
template <>
struct ShaderSource<BuiltIn::AtmosphereShader, gfx::Backend::Type::Vulkan> {
    static constexpr const char* name = "AtmosphereShader";

    static const std::array<AttributeInfo, 1> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 0> textures;

    static constexpr auto prelude = R"(

#define idAtmosphereUBO idDrawableReservedFragmentOnlyUBO

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

struct AtmosphereUBO {
    mat4 inv_proj;  // the globe's inverse projection (clip -> view)
    vec4 sun_pos;   // xyz
    vec4 globe;     // xyz: globe centre in view space; w: globe radius (px)
    vec4 params;    // x: atmosphere blend
    vec4 viewport;  // xy: viewport size in framebuffer pixels
};

layout(std140, set = LAYER_SET_INDEX, binding = idAtmosphereUBO) readonly buffer AtmosphereUBOVector {
    AtmosphereUBO atmosphere_ubo[];
} atmosphereVector;

const float PI = 3.141592653589793;
const int iSteps = 5;
const int jSteps = 3;

/* radius of the planet */
const float EARTH_RADIUS = 6371e3;
/* radius of the atmosphere */
const float ATMOS_RADIUS = 6471e3;

vec2 rsi(vec3 r0, vec3 rd, float sr) {
    // ray-sphere intersection that assumes the sphere is centred at the origin.
    // No intersection when result.x > result.y
    float a = dot(rd, rd);
    float b = 2.0 * dot(rd, r0);
    float c = dot(r0, r0) - (sr * sr);
    float d = (b * b) - 4.0 * a * c;
    if (d < 0.0) return vec2(1e5, -1e5);
    return vec2((-b - sqrt(d)) / (2.0 * a), (-b + sqrt(d)) / (2.0 * a));
}

vec4 atmosphere(vec3 r, vec3 r0, vec3 pSun, float iSun, float rPlanet, float rAtmos, vec3 kRlh, float kMie,
                float shRlh, float shMie, float g) {
    pSun = normalize(pSun);
    r = normalize(r);

    // Step size of the primary ray.
    vec2 p = rsi(r0, r, rAtmos);
    if (p.x > p.y) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    if (p.x < 0.0) {
        p.x = 0.0;
    }

    vec2 p2 = rsi(r0, r, rPlanet);
    if (p2.x <= p2.y && p2.x > 0.0) {
        p.y = min(p.y, p2.x);
    }
    float iStepSize = (p.y - p.x) / float(iSteps);
    float iTime = p.x + iStepSize * 0.5;

    vec3 totalRlh = vec3(0, 0, 0);
    vec3 totalMie = vec3(0, 0, 0);
    float iOdRlh = 0.0;
    float iOdMie = 0.0;

    float mu = dot(r, pSun);
    float mumu = mu * mu;
    float gg = g * g;
    float pRlh = 3.0 / (16.0 * PI) * (1.0 + mumu);
    float pMie = 3.0 / (8.0 * PI) * ((1.0 - gg) * (mumu + 1.0)) / (pow(1.0 + gg - 2.0 * mu * g, 1.5) * (2.0 + gg));

    for (int i = 0; i < iSteps; i++) {
        vec3 iPos = r0 + r * iTime;
        float iHeight = length(iPos) - rPlanet;

        float odStepRlh = exp(-iHeight / shRlh) * iStepSize;
        float odStepMie = exp(-iHeight / shMie) * iStepSize;
        iOdRlh += odStepRlh;
        iOdMie += odStepMie;

        float jStepSize = rsi(iPos, pSun, rAtmos).y / float(jSteps);
        float jTime = jStepSize * 0.5;
        float jOdRlh = 0.0;
        float jOdMie = 0.0;

        for (int j = 0; j < jSteps; j++) {
            vec3 jPos = iPos + pSun * jTime;
            float jHeight = length(jPos) - rPlanet;
            jOdRlh += exp(-jHeight / shRlh) * jStepSize;
            jOdMie += exp(-jHeight / shMie) * jStepSize;
            jTime += jStepSize;
        }

        vec3 attn = exp(-(kMie * (iOdMie + jOdMie) + kRlh * (iOdRlh + jOdRlh)));
        totalRlh += odStepRlh * attn;
        totalMie += odStepMie * attn;
        iTime += iStepSize;
    }

    // Approximate attenuation with a single "grayscale" value.
    float opacity = exp(-(length(kRlh) * length(totalRlh) + kMie * length(totalMie)));
    vec3 color = iSun * (pRlh * kRlh * totalRlh + pMie * kMie * totalMie);
    return vec4(color, opacity);
}

void main() {
    const AtmosphereUBO u = atmosphereVector.atmosphere_ubo[constant.ubo_index];

    const vec2 ndc = vec2(gl_FragCoord.x / u.viewport.x * 2.0 - 1.0, 1.0 - gl_FragCoord.y / u.viewport.y * 2.0);
    const vec3 view_direction = (u.inv_proj * vec4(ndc, 0.0, 1.0)).xyz;

    // The globe is small compared to the real Earth. To still have a correct atmosphere, the whole world is scaled to
    // EARTH_RADIUS and the camera position changed accordingly.
    vec3 scale_camera_pos = -u.globe.xyz * EARTH_RADIUS / u.globe.w;

    vec4 color = atmosphere(normalize(view_direction),             // ray direction
                            scale_camera_pos,                      // ray origin
                            u.sun_pos.xyz,                         // position of the sun
                            22.0,                                  // intensity of the sun
                            EARTH_RADIUS,                          // radius of the planet in metres
                            ATMOS_RADIUS,                          // radius of the atmosphere in metres
                            vec3(5.5e-6, 13.0e-6, 22.4e-6),        // Rayleigh scattering coefficient
                            21e-6,                                 // Mie scattering coefficient
                            8e3,                                   // Rayleigh scale height
                            1.2e3,                                 // Mie scale height
                            0.758);                                // Mie preferred scattering direction

    // Exposure, then gamma (the alpha channel too, so blending looks better).
    color.rgb = 1.0 - exp(-1.0 * color.rgb);
    color = pow(color, vec4(1.0 / 2.2));
    out_color = vec4(color.rgb, 1.0 - color.a) * u.params.x;
}
)";
};

} // namespace shaders
} // namespace mln
