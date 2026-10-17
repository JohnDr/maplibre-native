#include <mln/shaders/vulkan/atmosphere.hpp>
#include <mln/shaders/shader_defines.hpp>

namespace mln {
namespace shaders {

using AtmosphereShaderSource = ShaderSource<BuiltIn::AtmosphereShader, gfx::Backend::Type::Vulkan>;

const std::array<AttributeInfo, 1> AtmosphereShaderSource::attributes = {
    AttributeInfo{0, gfx::AttributeDataType::Short2, idAtmospherePosVertexAttribute},
};
const std::array<TextureInfo, 0> AtmosphereShaderSource::textures = {};

} // namespace shaders
} // namespace mln
