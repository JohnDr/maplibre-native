#include <mln/shaders/vulkan/terrain.hpp>
#include <mln/shaders/shader_defines.hpp>

namespace mln {
namespace shaders {

using TerrainShaderSource = ShaderSource<BuiltIn::TerrainShader, gfx::Backend::Type::Vulkan>;

const std::array<AttributeInfo, 1> TerrainShaderSource::attributes = {
    AttributeInfo{0, gfx::AttributeDataType::Float4, idTerrainPosVertexAttribute},
};
const std::array<TextureInfo, 0> TerrainShaderSource::textures = {};

} // namespace shaders
} // namespace mln
