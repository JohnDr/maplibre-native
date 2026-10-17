#pragma once

#include <mln/style/terrain.hpp>

#include <string>

namespace mln {
namespace style {

class Terrain::Impl {
public:
    std::string source;
    double exaggeration = 1.0;
};

} // namespace style
} // namespace mln
