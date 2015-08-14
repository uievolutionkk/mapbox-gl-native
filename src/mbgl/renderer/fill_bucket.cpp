#include <mbgl/renderer/fill_bucket.hpp>
#include <mbgl/geometry/fill_buffer.hpp>
#include <mbgl/geometry/elements_buffer.hpp>
#include <mbgl/renderer/painter.hpp>
#include <mbgl/style/style.hpp>
#include <mbgl/style/style_layout.hpp>
#include <mbgl/shader/plain_shader.hpp>
#include <mbgl/shader/pattern_shader.hpp>
#include <mbgl/shader/outline_shader.hpp>
#include <mbgl/platform/gl.hpp>
#include <mbgl/platform/log.hpp>

#include <cassert>

struct geometry_too_long_exception : std::exception {};

using namespace mbgl;

void *FillBucket::alloc(void *, unsigned int size) {
    return ::malloc(size);
}

void *FillBucket::realloc(void *, void *ptr, unsigned int size) {
    return ::realloc(ptr, size);
}

void FillBucket::free(void *, void *ptr) {
    ::free(ptr);
}

FillBucket::FillBucket(FillVertexBuffer &vertexBuffer_,
                       TriangleElementsBuffer &triangleElementsBuffer_,
                       LineElementsBuffer &lineElementsBuffer_,
                       FillVertexBuffer &newStyleLineVertexBuffer_,
                       TriangleElementsBuffer &newStyleLineElementsBuffer_)
    : allocator(new TESSalloc{
          &alloc,
          &realloc,
          &free,
          nullptr, // userData
          64,      // meshEdgeBucketSize
          64,      // meshVertexBucketSize
          32,      // meshFaceBucketSize
          64,      // dictNodeBucketSize
          8,       // regionBucketSize
          128,     // extraVertices allocated for the priority queue.
      }),
      tesselator(tessNewTess(allocator)),
      vertexBuffer(vertexBuffer_),
      triangleElementsBuffer(triangleElementsBuffer_),
      lineElementsBuffer(lineElementsBuffer_),
      newStyleLineVertexBuffer(newStyleLineVertexBuffer_),
      newStyleLineElementsBuffer(newStyleLineElementsBuffer_),
      vertex_start(vertexBuffer_.index()),
      triangle_elements_start(triangleElementsBuffer_.index()),
      line_elements_start(lineElementsBuffer.index()),
      new_style_line_vertex_start(newStyleLineVertexBuffer_.index()),
      new_style_line_elements_start(newStyleLineElementsBuffer_.index()) {
          //Log::Warning(Event::General, "new_style_line_vertex_start %d new_style_line_elements_start %d", new_style_line_vertex_start,
                       //new_style_line_elements_start);
    assert(tesselator);
}

FillBucket::~FillBucket() {
    if (tesselator) {
        tessDeleteTess(tesselator);
    }
    if (allocator) {
        delete allocator;
    }
}

void FillBucket::addGeometry(const GeometryCollection& geometryCollection) {
    for (auto& line_ : geometryCollection) {
        for (auto& v : line_) {
            line.emplace_back(v.x, v.y);
        }
        if (!line.empty()) {
            clipper.AddPath(line, ClipperLib::ptSubject, true);
            line.clear();
            hasVertices = true;
        }
    }

    tessellate();
}

#define LINE_WIDTH 25.0

void norml(TESSreal *f1, TESSreal *f2) {
    TESSreal length = sqrtf((*f1) * (*f1) + (*f2) * (*f2));
    *f1 /= length;
    *f2 /= length;
    *f1 *= LINE_WIDTH;
    *f2 *= LINE_WIDTH;
}

void FillBucket::tessellate() {
    if (!hasVertices) {
        return;
    }
    hasVertices = false;

    std::vector<std::vector<ClipperLib::IntPoint>> polygons;
    clipper.Execute(ClipperLib::ctUnion, polygons, ClipperLib::pftEvenOdd, ClipperLib::pftEvenOdd);
    clipper.Clear();

    if (polygons.empty()) {
        return;
    }

    size_t total_vertex_count = 0;
    size_t line_vertex_count = 0;
    size_t line_index_count = 0;
    for (const auto& polygon : polygons) {
        total_vertex_count += polygon.size();
        line_vertex_count += polygon.size() * 8;
        line_index_count += polygon.size() * 4;
    }

    if (line_vertex_count > 65536) {
        throw geometry_too_long_exception();
    }

    if (lineGroups.empty() || (lineGroups.back()->vertex_length + line_vertex_count > 65535) ||
        (lineGroups.back()->elements_length + line_index_count > 65535)) {
        // Move to a new group because the old one can't hold the geometry.
        lineGroups.emplace_back(std::make_unique<LineGroup>());
    }
    //Log::Warning(Event::General, "Current vertex_length %d line_vertex_count %d", lineGroups.back()->vertex_length, line_vertex_count);
    

    assert(lineGroups.back());
    LineGroup& lineGroup = *lineGroups.back();
    //uint32_t lineIndex = lineGroup.vertex_length;

    for (const auto& polygon : polygons) {
        const size_t group_count = polygon.size();
        assert(group_count >= 3);

        std::vector<TESSreal> clipped_line;
        for (const auto& pt : polygon) {
            clipped_line.push_back(pt.X);
            clipped_line.push_back(pt.Y);
            vertexBuffer.add(pt.X, pt.Y);
        }

        for (size_t i = 0; i < group_count; i++) {
            const size_t prev_i = (i == 0 ? group_count : i) - 1;
            //lineElementsBuffer.add(lineIndex + prev_i, lineIndex + i);
            
            //Look up vertex prev_i and i, then construct a polygon of width (it will be from the parent, for now hard code a number)
            //by taking i - prev_i and turning it 90 degrees (which means btw (x,y) => (-y, x)), normalizing it and adding and removing it
            //from both i and prev_i, then add 6 indices
            
            const size_t pprev_i = (prev_i == 0 ? group_count : prev_i) - 1;
            const size_t n_i = (i == (group_count - 1) ? 0 : i) + 1;
            TESSreal prevX = clipped_line.at(2 * (prev_i));
            TESSreal prevY = clipped_line.at(2 * (prev_i) + 1);
            TESSreal X = clipped_line.at(2 * (i));
            TESSreal Y = clipped_line.at(2 *  (i) + 1);
            TESSreal widthVectorX = -(Y - prevY);
            TESSreal widthVectorY = (X - prevX);
            norml(&widthVectorX, &widthVectorY);
            size_t curIndex = newStyleLineVertexBuffer.index();
            
            Log::Warning(Event::General, "curIndex: %d, prevX: %5.2f prevY: %5.2f X: %5.2f Y: %5.2f",
                         curIndex, prevX, prevY, X, Y);
            newStyleLineVertexBuffer.add(prevX + widthVectorX, prevY + widthVectorY);
            newStyleLineVertexBuffer.add(prevX - widthVectorX, prevY - widthVectorY);
            newStyleLineVertexBuffer.add(X + widthVectorX, Y + widthVectorY);
            newStyleLineVertexBuffer.add(X - widthVectorX, Y - widthVectorY);
            newStyleLineElementsBuffer.add(curIndex, curIndex + 1, curIndex + 2);
            newStyleLineElementsBuffer.add(curIndex + 1, curIndex + 3, curIndex + 2);
            
            
            TESSreal pprevX = clipped_line.at(2 * pprev_i);
            TESSreal pprevY = clipped_line.at(2 * pprev_i + 1);
            
            TESSreal V1X = (X - prevX);
            TESSreal V1Y = (Y - prevY);
            norml(&V1X, &V1Y);
            TESSreal V2X = (pprevX - prevX);
            TESSreal V2Y = (pprevY - prevY);
            norml(&V2X, &V2Y);
            
            TESSreal bezelVectorX = -(V1X + V2X) / 2.0f;
            TESSreal bezelVectorY = -(V1Y + V2Y) / 2.0f;
            norml(&bezelVectorX, &bezelVectorY);
            
            newStyleLineVertexBuffer.add(prevX + bezelVectorX, prevY + bezelVectorY);
            newStyleLineVertexBuffer.add(prevX, prevY);
            
            //curIndex adds in widthVector, curIndex + 1 subtracts it.  Meanwhile index 4 goes in the
            //direction of bezelVector.  We want to go first to the end point, then out to the bezel point,
            //we want to go to whichever point is in that same direction from the end point.  So if the
            //bezelVector and the widthVector point in the same direction, we go to curIndex, otherwise we
            //go the other way.
            if ((widthVectorX * bezelVectorX + widthVectorY * bezelVectorY) > 0.0f) {
                //Log::Warning(Event::General, "widthVector [%5.2f, %5.2f] bezelVector [%5.2f, %5.2f]", widthVectorX, widthVectorY, bezelVectorX, bezelVectorY);
                newStyleLineElementsBuffer.add(curIndex + 5, curIndex + 4, curIndex);
            } else {
                newStyleLineElementsBuffer.add(curIndex + 5, curIndex + 4, curIndex + 1);
            }
            
            TESSreal nX = clipped_line.at(2 * n_i);
            TESSreal nY = clipped_line.at(2 * n_i + 1);
            
            V1X = (nX - X);
            V1Y = (nY - Y);
            norml(&V1X, &V1Y);
            V2X = (prevX - X);
            V2Y = (prevY - Y);
            norml(&V2X, &V2Y);

            bezelVectorX = -(V1X + V2X) / 2.0f;
            bezelVectorY = -(V1Y + V2Y) / 2.0f;
            norml(&bezelVectorX, &bezelVectorY);
            newStyleLineVertexBuffer.add(X + bezelVectorX, Y + bezelVectorY);
            newStyleLineVertexBuffer.add(X, Y);
            
            if ((widthVectorX * bezelVectorX + widthVectorY * bezelVectorY) > 0.0f) {
            //if ((widthVectorX * bezelVectorY + widthVectorY * bezelVectorX) > 0.0f) {
            //if ((widthVectorX * bezelVectorX - widthVectorY * bezelVectorY) > 0.0f) {
            //if (i == 0) {
                    newStyleLineElementsBuffer.add(curIndex + 7, curIndex + 6, curIndex + 2);
            } else {
                newStyleLineElementsBuffer.add(curIndex + 7, curIndex + 6, curIndex + 3);
            }
            //newStyleLineElementsBuffer.add(curIndex, curIndex, curIndex);
        }

        //lineIndex += group_count;

        tessAddContour(tesselator, vertexSize, clipped_line.data(), stride, (int)clipped_line.size() / vertexSize);
    }

    lineGroup.elements_length += line_index_count;

    if (tessTesselate(tesselator, TESS_WINDING_ODD, TESS_POLYGONS, vertices_per_group, vertexSize, 0)) {
        const TESSreal *vertices = tessGetVertices(tesselator);
        const size_t vertex_count = tessGetVertexCount(tesselator);
        TESSindex *vertex_indices = const_cast<TESSindex *>(tessGetVertexIndices(tesselator));
        const TESSindex *elements = tessGetElements(tesselator);
        const int triangle_count = tessGetElementCount(tesselator);

        for (size_t i = 0; i < vertex_count; ++i) {
            if (vertex_indices[i] == TESS_UNDEF) {
                vertexBuffer.add(::round(vertices[i * 2]), ::round(vertices[i * 2 + 1]));
                vertex_indices[i] = (TESSindex)total_vertex_count;
                total_vertex_count++;
            }
        }

        if (triangleGroups.empty() || (triangleGroups.back()->vertex_length + total_vertex_count > 65535)) {
            // Move to a new group because the old one can't hold the geometry.
            triangleGroups.emplace_back(std::make_unique<TriangleGroup>());
        }

        // We're generating triangle fans, so we always start with the first
        // coordinate in this polygon.
        assert(triangleGroups.back());
        TriangleGroup& triangleGroup = *triangleGroups.back();
        uint32_t triangleIndex = triangleGroup.vertex_length;

        for (int i = 0; i < triangle_count; ++i) {
            const TESSindex *element_group = &elements[i * vertices_per_group];

            if (element_group[0] != TESS_UNDEF && element_group[1] != TESS_UNDEF && element_group[2] != TESS_UNDEF) {
                const TESSindex a = vertex_indices[element_group[0]];
                const TESSindex b = vertex_indices[element_group[1]];
                const TESSindex c = vertex_indices[element_group[2]];

                if (a != TESS_UNDEF && b != TESS_UNDEF && c != TESS_UNDEF) {
                    triangleElementsBuffer.add(triangleIndex + a, triangleIndex + b, triangleIndex + c);
                } else {
#if defined(DEBUG)
                    // TODO: We're missing a vertex that was not part of the line.
                    Log::Error(Event::OpenGL, "undefined element buffer");
#endif
                }
            } else {
#if defined(DEBUG)
                Log::Error(Event::OpenGL, "undefined element buffer");
#endif
            }
        }

        triangleGroup.vertex_length += total_vertex_count;
        triangleGroup.elements_length += triangle_count;
    } else {
#if defined(DEBUG)
        Log::Error(Event::OpenGL, "tessellation failed");
#endif
    }

    //This is no longer true:
    // We're adding the total vertex count *after* we added additional vertices
    // in the tessellation step. They won't be part of the actual lines, but
    // we need to skip over them anyway if we draw the next group.
    lineGroup.vertex_length += line_vertex_count;
}

void FillBucket::upload() {
    vertexBuffer.upload();
    triangleElementsBuffer.upload();
    //lineElementsBuffer.upload();
    newStyleLineVertexBuffer.upload();
    newStyleLineElementsBuffer.upload();

    // From now on, we're going to render during the opaque and translucent pass.
    uploaded = true;
}

void FillBucket::render(Painter& painter,
                        const StyleLayer& layer_desc,
                        const TileID& id,
                        const mat4& matrix) {
    painter.renderFill(*this, layer_desc, id, matrix);
}

bool FillBucket::hasData() const {
    return !triangleGroups.empty() || !lineGroups.empty();
}

void FillBucket::drawElements(PlainShader& shader) {
    char *vertex_index = BUFFER_OFFSET(vertex_start * vertexBuffer.itemSize);
    char *elements_index = BUFFER_OFFSET(triangle_elements_start * triangleElementsBuffer.itemSize);
    for (auto& group : triangleGroups) {
        assert(group);
        group->array[0].bind(shader, vertexBuffer, triangleElementsBuffer, vertex_index);
        MBGL_CHECK_ERROR(glDrawElements(GL_TRIANGLES, group->elements_length * 3, GL_UNSIGNED_SHORT, elements_index));
        vertex_index += group->vertex_length * vertexBuffer.itemSize;
        elements_index += group->elements_length * triangleElementsBuffer.itemSize;
    }
}

void FillBucket::drawElements(PatternShader& shader) {
    char *vertex_index = BUFFER_OFFSET(vertex_start * vertexBuffer.itemSize);
    char *elements_index = BUFFER_OFFSET(triangle_elements_start * triangleElementsBuffer.itemSize);
    for (auto& group : triangleGroups) {
        assert(group);
        group->array[1].bind(shader, vertexBuffer, triangleElementsBuffer, vertex_index);
        MBGL_CHECK_ERROR(glDrawElements(GL_TRIANGLES, group->elements_length * 3, GL_UNSIGNED_SHORT, elements_index));
        vertex_index += group->vertex_length * vertexBuffer.itemSize;
        elements_index += group->elements_length * triangleElementsBuffer.itemSize;
    }
}

void FillBucket::drawVertices(OutlineShader& shader) {
    /*char *vertex_index = BUFFER_OFFSET(vertex_start * vertexBuffer.itemSize);
    char *elements_index = BUFFER_OFFSET(line_elements_start * lineElementsBuffer.itemSize);
    for (auto& group : lineGroups) {
        assert(group);
        group->array[0].bind(shader, vertexBuffer, lineElementsBuffer, vertex_index);
        MBGL_CHECK_ERROR(glDrawElements(GL_LINES, group->elements_length * 2, GL_UNSIGNED_SHORT, elements_index));
        vertex_index += group->vertex_length * vertexBuffer.itemSize;
        elements_index += group->elements_length * lineElementsBuffer.itemSize;
    }*/
    //elements_length is the number of triangles in the elements buffer => that * 3 is the number of indices, or * 6 is the
    //number of bytes.
    //vertex_length is the number of vertices in the vertices buffer => that * 12 is the number of bytes
    char *vertex_index = BUFFER_OFFSET(new_style_line_vertex_start * newStyleLineVertexBuffer.itemSize);
    char *elements_index = BUFFER_OFFSET(new_style_line_elements_start * newStyleLineElementsBuffer.itemSize);
    for (auto& group : lineGroups) {
        assert(group);
        //Log::Warning(Event::General, "Binding with vertex_index %d and elements_index %d and elements_length %d", vertex_index, elements_index, group->elements_length);
        group->array[0].bind(shader, newStyleLineVertexBuffer, newStyleLineElementsBuffer, vertex_index);
        if ((((unsigned long)elements_index) + (group->elements_length * 16)) < 65536)
            MBGL_CHECK_ERROR(glDrawElements(GL_TRIANGLES, group->elements_length * 3, GL_UNSIGNED_SHORT, elements_index));
        vertex_index += group->vertex_length * newStyleLineVertexBuffer.itemSize;
        elements_index += group->elements_length * newStyleLineElementsBuffer.itemSize;
    }
}
