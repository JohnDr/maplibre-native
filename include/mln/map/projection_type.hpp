#pragma once

namespace mln {

/**
 * @brief The map projection used to render the world.
 *
 * This is the projection *seam* only: selecting a projection is recorded on
 * the map, but only `Mercator` is rendered today. `Globe` will draw the world
 * on a sphere once the globe work lands (see the globe design proposal).
 *
 * Note: distinct from @ref ProjectionMode, which controls the (unrelated)
 * axonometric skew rendering.
 */
enum class ProjectionType : int {
    /** Web Mercator, the flat default. */
    Mercator = 0,
    /** Globe: the world rendered as a sphere, morphing back to Mercator at
        high zoom. Not rendered yet. */
    Globe,
};

} // namespace mln
