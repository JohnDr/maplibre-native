#include <mln/gfx/fill_generator.hpp>
#include <mln/gfx/polyline_generator.hpp>
#include <mln/util/subdivision.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#endif

#include <mapbox/earcut.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cassert>
#include <limits>

namespace mapbox {
namespace util {
template <>
struct nth<0, mln::GeometryCoordinate> {
    static int64_t get(const mln::GeometryCoordinate& t) { return t.x; };
};

template <>
struct nth<1, mln::GeometryCoordinate> {
    static int64_t get(const mln::GeometryCoordinate& t) { return t.y; };
};
} // namespace util
} // namespace mapbox

namespace mln {
namespace gfx {

struct GeometryTooLongException : std::exception {};

namespace {

// Clips a ring to a half-plane (axis 0=x, 1=y) via Sutherland-Hodgman.
GeometryCoordinates clipRingToHalfPlane(const GeometryCoordinates& ring, int axis, double bound, bool keepGreater) {
    GeometryCoordinates out;
    const std::size_t n = ring.size();
    if (n == 0) return out;
    for (std::size_t i = 0; i < n; i++) {
        const GeometryCoordinate& cur = ring[i];
        const GeometryCoordinate& prev = ring[(i + n - 1) % n];
        const double curVal = axis == 0 ? cur.x : cur.y;
        const double prevVal = axis == 0 ? prev.x : prev.y;
        const bool curIn = keepGreater ? curVal >= bound : curVal <= bound;
        const bool prevIn = keepGreater ? prevVal >= bound : prevVal <= bound;
        if (curIn != prevIn) {
            const double t = (bound - prevVal) / (curVal - prevVal);
            out.emplace_back(static_cast<int16_t>(std::lround(prev.x + t * (cur.x - prev.x))),
                             static_cast<int16_t>(std::lround(prev.y + t * (cur.y - prev.y))));
        }
        if (curIn) {
            out.push_back(cur);
        }
    }
    return out;
}

// Clips a polygon (all rings) to the tile bounds [0, EXTENT] on both axes. Without this the globe
// subdivision grid can place vertices up to a cell past the tile edge (the tile buffer rounds up to
// the next grid line), so neighbouring tiles overlap over that strip and both draw it - a darker band
// along the tile edge. GL JS clips the fill geometry to the tile before subdividing.
GeometryCollection clipPolygonToTile(const GeometryCollection& polygon) {
    GeometryCollection result;
    result.reserve(polygon.size());
    for (const auto& ring : polygon) {
        GeometryCoordinates r = clipRingToHalfPlane(ring, 0, 0.0, true);
        r = clipRingToHalfPlane(r, 0, static_cast<double>(util::EXTENT), false);
        r = clipRingToHalfPlane(r, 1, 0.0, true);
        r = clipRingToHalfPlane(r, 1, static_cast<double>(util::EXTENT), false);
        result.push_back(std::move(r));
    }
    return result;
}

std::size_t addRingVertices(gfx::VertexVector<FillLayoutVertex>& vertices, const GeometryCoordinates& ring) {
    for (auto& point : ring) {
        vertices.emplace_back(FillBucket::layoutVertex(point));
    }
    return ring.size();
}

std::size_t totalVerticesCheck(const GeometryCollection& polygon) {
    std::size_t totalVertices = 0;
    for (const auto& ring : polygon) {
        totalVertices += ring.size();
    }
    if (totalVertices > std::numeric_limits<uint16_t>::max()) throw GeometryTooLongException();
    return totalVertices;
}

void addFillIndices(SegmentVector& fillSegments,
                    gfx::IndexVector<gfx::Triangles>& fillIndexes,
                    const std::span<const uint32_t>& indices,
                    const std::size_t startVertices,
                    const std::size_t totalVertices) {
    const std::size_t nIndices = indices.size();
    assert(nIndices % 3 == 0);

    if (fillSegments.empty() ||
        fillSegments.back().vertexLength + totalVertices > std::numeric_limits<uint16_t>::max()) {
        fillSegments.emplace_back(startVertices, fillIndexes.elements());
    }

    auto& triangleSegment = fillSegments.back();
    assert(triangleSegment.vertexLength + totalVertices <= std::numeric_limits<uint16_t>::max());
    const auto triangleIndex = static_cast<uint16_t>(triangleSegment.vertexLength);

    for (std::size_t i = 0; i < nIndices; i += 3) {
        assert(
            std::max({triangleIndex + indices[i + 0], triangleIndex + indices[i + 1], triangleIndex + indices[i + 2]}) <
            triangleIndex + totalVertices);
        fillIndexes.emplace_back(
            triangleIndex + indices[i], triangleIndex + indices[i + 1], triangleIndex + indices[i + 2]);
    }

    triangleSegment.vertexLength += totalVertices;
    triangleSegment.indexLength += nIndices;
}

void addOutlineIndices(const std::size_t base,
                       const std::size_t nVertices,
                       SegmentVector& lineSegments,
                       gfx::IndexVector<gfx::Lines>& lineIndexes) {
    if (nVertices == 0) return;

    if (lineSegments.empty() || lineSegments.back().vertexLength + nVertices > std::numeric_limits<uint16_t>::max()) {
        lineSegments.emplace_back(base, lineIndexes.elements());
    }

    auto& lineSegment = lineSegments.back();
    assert(lineSegment.vertexLength <= std::numeric_limits<uint16_t>::max());
    const auto lineIndex = static_cast<uint16_t>(lineSegment.vertexLength);

    lineIndexes.emplace_back(static_cast<uint16_t>(lineIndex + nVertices - 1), lineIndex);
    for (std::size_t i = 1; i < nVertices; i++) {
        lineIndexes.emplace_back(static_cast<uint16_t>(lineIndex + i - 1), static_cast<uint16_t>(lineIndex + i));
    }

    lineSegment.vertexLength += nVertices;
    lineSegment.indexLength += nVertices * 2;
}

void addSubdividedPolygon(const util::SubdivisionResult& subdivided,
                          gfx::VertexVector<FillLayoutVertex>& fillVertices,
                          gfx::IndexVector<gfx::Triangles>& fillIndexes,
                          SegmentVector& fillSegments,
                          gfx::IndexVector<gfx::Lines>& lineIndexes,
                          SegmentVector& lineSegments) {
    const std::size_t totalVertices = subdivided.vertices.size() / 2;
    if (totalVertices > maxSegmentVertices) {
        throw GeometryTooLongException();
    }
    const std::size_t startVertices = fillVertices.elements();
    for (std::size_t i = 0; i < totalVertices; i++) {
        fillVertices.emplace_back(
            FillBucket::layoutVertex({subdivided.vertices[i * 2], subdivided.vertices[i * 2 + 1]}));
    }
    addFillIndices(fillSegments, fillIndexes, subdivided.triangleIndices, startVertices, totalVertices);

    for (const auto& line : subdivided.lineIndexLists) {
        if (line.empty()) {
            continue;
        }
        if (lineSegments.empty() || lineSegments.back().vertexLength + totalVertices > maxSegmentVertices) {
            lineSegments.emplace_back(startVertices, lineIndexes.elements());
        }
        auto& lineSegment = lineSegments.back();
        const auto base = static_cast<uint16_t>(lineSegment.vertexLength);
        std::size_t emitted = 0;
        for (std::size_t i = 0; i + 1 < line.size(); i += 2) {
            const uint32_t i0 = line[i];
            const uint32_t i1 = line[i + 1];
            // Skip outline segments lying along the tile's boundary (x or y == 0 / EXTENT).
            // These are artifacts of clipping the fill to the tile, not real polygon edges. Both
            // neighbouring tiles would otherwise stroke the shared edge, doubling into a darker line.
            const bool onLeftEdge =
                subdivided.vertices[i0 * 2] == 0 && subdivided.vertices[i1 * 2] == 0;
            const bool onRightEdge = subdivided.vertices[i0 * 2] == util::EXTENT &&
                                     subdivided.vertices[i1 * 2] == util::EXTENT;
            const bool onBottomEdge =
                subdivided.vertices[i0 * 2 + 1] == 0 && subdivided.vertices[i1 * 2 + 1] == 0;
            const bool onTopEdge = subdivided.vertices[i0 * 2 + 1] == util::EXTENT &&
                                   subdivided.vertices[i1 * 2 + 1] == util::EXTENT;
            if (onLeftEdge || onRightEdge || onBottomEdge || onTopEdge) {
                continue;
            }
            lineIndexes.emplace_back(static_cast<uint16_t>(base + i0), static_cast<uint16_t>(base + i1));
            ++emitted;
        }
        lineSegment.indexLength += emitted * 2;
    }
    if (!lineSegments.empty()) {
        lineSegments.back().vertexLength += totalVertices;
    }
}

} // namespace

void generateFillBuffers(const GeometryCollection& geometry,
                         gfx::VertexVector<FillLayoutVertex>& fillVertices,
                         gfx::IndexVector<Triangles>& fillIndexes,
                         SegmentVector& fillSegments) {
    for (auto& polygon : classifyRings(geometry)) {
        // Optimize polygons with many interior rings for earcut tessellation.
        limitHoles(polygon, 500);

        std::size_t totalVertices = totalVerticesCheck(polygon);
        std::size_t startVertices = fillVertices.elements();

        for (const auto& ring : polygon) {
            addRingVertices(fillVertices, ring);
        }

        std::vector<uint32_t> indices = mapbox::earcut(polygon);
        addFillIndices(fillSegments, fillIndexes, indices, startVertices, totalVertices);
    }
}

void generateFillAndOutineBuffers(const GeometryCollection& geometry,
                                  gfx::VertexVector<FillLayoutVertex>& vertices,
                                  gfx::IndexVector<gfx::Triangles>& fillIndexes,
                                  SegmentVector& fillSegments,
                                  gfx::IndexVector<gfx::Lines>& lineIndexes,
                                  SegmentVector& lineSegments,
                                  const CanonicalTileID& canonical,
                                  uint32_t subdivisionGranularity) {
    for (auto& polygon : classifyRings(geometry)) {
        // Optimize polygons with many interior rings for earcut tessellation.
        limitHoles(polygon, 500);

        if (subdivisionGranularity >= 2) {
            addSubdividedPolygon(util::subdividePolygonWithinLimit(clipPolygonToTile(polygon),
                                                                   canonical,
                                                                   subdivisionGranularity,
                                                                   /*generateOutlineLines=*/true,
                                                                   maxSegmentVertices),
                                 vertices,
                                 fillIndexes,
                                 fillSegments,
                                 lineIndexes,
                                 lineSegments);
            continue;
        }

        std::size_t totalVertices = totalVerticesCheck(polygon);
        std::size_t startVertices = vertices.elements();

        for (const auto& ring : polygon) {
            std::size_t base = vertices.elements();
            std::size_t nVertices = addRingVertices(vertices, ring);
            addOutlineIndices(base, nVertices, lineSegments, lineIndexes);
        }

        std::vector<uint32_t> indices = mapbox::earcut(polygon);
        addFillIndices(fillSegments, fillIndexes, indices, startVertices, totalVertices);
    }
}

void generateFillAndOutineBuffers(const GeometryCollection& geometry,
                                  gfx::VertexVector<FillLayoutVertex>& fillVertices,
                                  gfx::IndexVector<gfx::Triangles>& fillIndexes,
                                  SegmentVector& fillSegments,
                                  gfx::VertexVector<LineLayoutVertex>& lineVertices,
                                  gfx::IndexVector<gfx::Triangles>& lineIndexes,
                                  SegmentVector& lineSegments) {
    gfx::PolylineGenerator<LineLayoutVertex, SegmentBase> lineGenerator(
        lineVertices,
        LineBucket::layoutVertex,
        lineSegments,
        [](std::size_t vertexOffset, std::size_t indexOffset) -> SegmentBase {
            return SegmentBase(vertexOffset, indexOffset);
        },
        [](auto& seg) -> SegmentBase& { return seg; },
        lineIndexes);

    gfx::PolylineGeneratorOptions lineOptions;
    lineOptions.type = FeatureType::Polygon;

    for (auto& polygon : classifyRings(geometry)) {
        // Optimize polygons with many interior rings for earcut tessellation.
        limitHoles(polygon, 500);

        std::size_t totalVertices = totalVerticesCheck(polygon);
        std::size_t startVertices = fillVertices.elements();

        for (const auto& ring : polygon) {
            addRingVertices(fillVertices, ring);
            lineGenerator.generate(ring, lineOptions);
        }

        std::vector<uint32_t> indices = mapbox::earcut(polygon);
        addFillIndices(fillSegments, fillIndexes, indices, startVertices, totalVertices);
    }
}

void generateFillAndOutineBuffers(const GeometryCollection& geometry,
                                  gfx::VertexVector<FillLayoutVertex>& fillVertices,
                                  gfx::IndexVector<gfx::Triangles>& fillIndexes,
                                  SegmentVector& fillSegments,
                                  gfx::VertexVector<LineLayoutVertex>& lineVertices,
                                  gfx::IndexVector<gfx::Triangles>& lineIndexes,
                                  SegmentVector& lineSegments,
                                  gfx::IndexVector<gfx::Lines>& basicLineIndexes,
                                  SegmentVector& basicLineSegments,
                                  const CanonicalTileID& canonical,
                                  uint32_t subdivisionGranularity) {
    gfx::PolylineGenerator<LineLayoutVertex, SegmentBase> lineGenerator(
        lineVertices,
        LineBucket::layoutVertex,
        lineSegments,
        [](std::size_t vertexOffset, std::size_t indexOffset) -> SegmentBase {
            return SegmentBase(vertexOffset, indexOffset);
        },
        [](auto& seg) -> SegmentBase& { return seg; },
        lineIndexes);

    gfx::PolylineGeneratorOptions lineOptions;
    lineOptions.type = FeatureType::Polygon;

    // If we have pre-tessellated geometry, multi-polygons are tessellated
    // together, so we need to add them to the fill segment all at once.
    if (!geometry.getTriangles().empty()) {
        const std::size_t startVertices = fillVertices.elements();
        std::size_t totalVertices = 0;
        for (const auto& polygon : geometry) {
            totalVertices += polygon.size();
            addRingVertices(fillVertices, polygon);
        }
        addFillIndices(fillSegments, fillIndexes, geometry.getTriangles(), startVertices, totalVertices);
        return;
    }

    for (auto& polygon : classifyRings(geometry)) {
        // Optimize polygons with many interior rings for earcut tessellation.
        limitHoles(polygon, 500);

        if (subdivisionGranularity >= 2) {
            for (const auto& ring : polygon) {
                lineGenerator.generate(util::subdivideVertexLine(ring, subdivisionGranularity, /*isRing=*/true),
                                       lineOptions);
            }
            addSubdividedPolygon(util::subdividePolygonWithinLimit(polygon,
                                                                   canonical,
                                                                   subdivisionGranularity,
                                                                   /*generateOutlineLines=*/true,
                                                                   maxSegmentVertices),
                                 fillVertices,
                                 fillIndexes,
                                 fillSegments,
                                 basicLineIndexes,
                                 basicLineSegments);
            continue;
        }

        const std::size_t totalVertices = totalVerticesCheck(polygon);
        const std::size_t startVertices = fillVertices.elements();

        for (const auto& ring : polygon) {
            const std::size_t base = fillVertices.elements();
            const std::size_t nVertices = addRingVertices(fillVertices, ring);
            addOutlineIndices(base, nVertices, basicLineSegments, basicLineIndexes);
            lineGenerator.generate(ring, lineOptions);
        }

        // tessellate, if no triangles are provided
        std::vector<uint32_t> indices = mapbox::earcut(polygon);

        addFillIndices(fillSegments, fillIndexes, indices, startVertices, totalVertices);
    }
}

} // namespace gfx

} // namespace mln
