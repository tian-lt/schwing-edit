#pragma once
// std
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
// schwing
#include "fontengine.hpp"
#include "glstreamer.hpp"
#include "glyphatlas.hpp"
#include "itemizer.hpp"
#include "linetable.hpp"
#include "piecetable.hpp"
#include "resource.hpp"

namespace swg {

struct rect {
  int x, y, w, h;
};

struct docpos {
  int line, column;
};

class host {
  friend class plaindoc;
  struct impl;

 public:
  virtual ~host() = default;
  virtual void on_invalidate() = 0;

  void initialize_graphics();
  void set(plaindoc* doc);
  void render();
  void resize(int width, int height);
  void caret(docpos pos);
  docpos caret() const;
  void insert_char(std::string_view u8char);
  void erase_char();
  void delete_char();
  void linefeed();

 protected:
  plaindoc* doc = nullptr;
  int width_ = 0;
  int height_ = 0;
  int dpi = 96;
  int tabsize = 4;

 private:
  size_t inspos_ = 0;
  unique_gl_program glprog_;
  std::optional<glstreamer> streamer_;
  std::optional<glyphatlas> atlas_;
  GLint loc_viewport_;
};

class plaindoc {
  friend class host;

 public:
  explicit plaindoc(double fontsize, eol eol,
                    std::optional<std::filesystem::path> filepath = std::nullopt);
  void insert(size_t pos, std::string_view data);
  void erase(size_t pos, size_t length);
  std::string get(size_t pos, size_t length) const { return ptable_.get(pos, length); }
  size_t length() const { return ptable_.length(); }

 private:
  piecetable ptable_;
  linetable ltable_;
  std::map<UScriptCode, fontengine> fonts_;
  unique_hb_buffer hbbuf_;
  double fontsize_;
  eol eol_;
  bool mixeol_ = false;
};

}  // namespace swg
