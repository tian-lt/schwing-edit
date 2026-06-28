#pragma once

// std
#include <cstdint>
// swg
#include "resource.hpp"

namespace swg {

struct quad_vertex {
  int32_t x, y;
  int32_t u, v, layer;
};

class glstreamer {
 public:
  static inline constexpr int frame_count = 3;
  static inline constexpr int max_quads_per_frame = 1000;

  glstreamer();
  quad_vertex* begin();
  void end(size_t quad_count);

 private:
  unique_gl_fence fences_[frame_count];
  unique_gl_buffer vbos_[frame_count];
  unique_gl_vertext_array vaos_[frame_count];
  unique_gl_buffer ebo_;
  int curframe_ = 0;
};

}  // namespace swg