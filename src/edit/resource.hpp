#pragma once
// std
#include <cassert>
#include <memory>
#include <optional>
#include <type_traits>
// deps
#include <freetype/freetype.h>
#include <glad/glad.h>
#include <harfbuzz/hb-ft.h>
#include <harfbuzz/hb.h>

namespace swg {

namespace details {

template <class T, class Deleter>
  requires requires(T t, Deleter d) { d(t); }
struct unique_resource {
  explicit unique_resource(T res) : res_(res) {}
  unique_resource() = default;
  unique_resource(std::nullptr_t) noexcept {}
  unique_resource(const unique_resource&) noexcept = delete;
  unique_resource(unique_resource&& rhs) noexcept {
    if (this != &rhs) {
      res_ = std::exchange(rhs.res_, std::nullopt);
    }
  }
  unique_resource& operator=(const unique_resource&) noexcept = delete;
  unique_resource& operator=(unique_resource&& rhs) noexcept {
    if (this != &rhs) {
      res_ = std::exchange(rhs.res_, std::nullopt);
    }
    return *this;
  }
  ~unique_resource() {
    if (res_) {
      Deleter{}(*res_);
    }
  }
  explicit operator bool() const noexcept { return res_.has_value(); }
  bool has_value() const noexcept { return res_.has_value(); }
  T get() const noexcept {
    assert(res_);
    return *res_;
  }
  T release() noexcept {
    assert(res_.has_value());
    T res = *res_;
    res_ = std::nullopt;
    return res;
  };

 private:
  std::optional<T> res_;
};

struct ft_face_deleter {
  void operator()(FT_Face);
};
struct hb_font_deleter {
  void operator()(hb_font_t*);
};
struct shader_deleter {
  void operator()(GLuint);
};
struct gl_program_deleter {
  void operator()(GLuint program);
};

}  // namespace details

using unique_ft_face = std::unique_ptr<std::remove_pointer_t<FT_Face>, details::ft_face_deleter>;
using unique_hb_font = std::unique_ptr<hb_font_t, details::hb_font_deleter>;
using unique_shader = details::unique_resource<GLuint, details::shader_deleter>;
using unique_gl_program = details::unique_resource<GLuint, details::gl_program_deleter>;

FT_Library get_ft_library();
void initialize();
void uninitialize();

void check_fterror(FT_Error error_code);
void check_ptr(void* ptr, const char* message);
inline void check_ptr(void* ptr) { check_ptr(ptr, ""); }

}  // namespace swg
