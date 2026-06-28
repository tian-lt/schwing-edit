// std
#include <cstdint>
#include <memory>
#include <vector>
// swg
#include "glstreamer.hpp"

namespace swg {

constexpr int vertices_per_quad = 4;
constexpr int indices_per_quad = 6;
constexpr int frame_vertices = glstreamer::max_quads_per_frame * vertices_per_quad;
constexpr int frame_bytes = frame_vertices * sizeof(quad_vertex);
constexpr int total_vertics = frame_vertices * glstreamer::frame_count;
constexpr int total_vertex_bytes = total_vertics * sizeof(quad_vertex);

glstreamer::glstreamer() {
  {  // create the static ebo (index buffer)
    std::vector<uint32_t> idx;
    idx.reserve(max_quads_per_frame * frame_count * indices_per_quad);
    for (uint32_t q = 0; q < max_quads_per_frame * frame_count; ++q) {
      uint32_t b = q * 4;
      idx.push_back(b + 0);
      idx.push_back(b + 1);
      idx.push_back(b + 2);
      idx.push_back(b + 2);
      idx.push_back(b + 1);
      idx.push_back(b + 3);
    }
    glGenBuffers(1, ebo_.put());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_.get());
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(),
                 GL_STATIC_DRAW);
  }
  GLuint vbos[frame_count];
  GLuint vaos[frame_count];
  glGenBuffers(frame_count, vbos);
  glGenVertexArrays(frame_count, vaos);
  for (int i = 0; i < frame_count; ++i) {
    vbos_[i] = unique_gl_buffer{vbos[i]};
    vaos_[i] = unique_gl_vertext_array{vaos[i]};
    glBindBuffer(GL_ARRAY_BUFFER, vbos_[i].get());
    glBufferData(GL_ARRAY_BUFFER, frame_bytes, nullptr, GL_STREAM_DRAW);

    glBindVertexArray(vaos_[i].get());
    glBindBuffer(GL_ARRAY_BUFFER, vbos_[i].get());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_.get());

    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(0, 2, GL_INT, sizeof(quad_vertex), (void*)offsetof(quad_vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(1, 3, GL_INT, sizeof(quad_vertex), (void*)offsetof(quad_vertex, u));
  }
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

quad_vertex* glstreamer::begin() {
  if (fences_[curframe_]) {
    GLenum r = glClientWaitSync(fences_[curframe_].get(), GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    while (r == GL_TIMEOUT_EXPIRED) {
      r = glClientWaitSync(fences_[curframe_].get(), 0, 1'000'000);  // 1ms
    }
    fences_[curframe_].reset();
  }
  glBindBuffer(GL_ARRAY_BUFFER, vbos_[curframe_].get());
  void* ptr =
      glMapBufferRange(GL_ARRAY_BUFFER, 0, frame_bytes,
                       GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
  return reinterpret_cast<quad_vertex*>(ptr);
}

void glstreamer::end(size_t quad_count) {
  glBindBuffer(GL_ARRAY_BUFFER, vbos_[curframe_].get());
  glUnmapBuffer(GL_ARRAY_BUFFER);
  glBindVertexArray(vaos_[curframe_].get());
  glDrawElements(GL_TRIANGLES, (GLsizei)(quad_count * indices_per_quad), GL_UNSIGNED_INT, nullptr);
  fences_[curframe_] = unique_gl_fence{glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0)};
  curframe_ = (curframe_ + 1) % frame_count;
}

}  // namespace swg