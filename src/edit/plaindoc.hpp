#pragma once
// std
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
// schwing
#include "fontengine.hpp"
#include "glstreamer.hpp"
#include "glyphatlas.hpp"
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
  virtual void on_invalidate(rect rc) = 0;

  void initialize_graphics();
  void render(rect rc);
  void caret(docpos pos);
  docpos caret() const;
  void insert_char(std::string_view u8char);
  void erase_char();
  void delete_char();
  void linefeed();

 protected:
  plaindoc* doc = nullptr;
  rect viewport = {};
  int dpi = 96;

 private:
  size_t inspos_ = 0;
  unique_gl_program glprog_;
  std::optional<glstreamer> streamer_;
  std::optional<glyphatlas> atlas_;
};

class plaindoc {
  friend class host;

 public:
  explicit plaindoc(host* host, std::string fontpath, double fontsize, eol eol);
  void reset(std::optional<std::string> new_fontpath, std::optional<eol> new_eol);
  void insert(size_t pos, std::string_view data);
  void erase(size_t pos, size_t length);
  std::string get(size_t pos, size_t length) const { return ptable_.get(pos, length); }
  size_t length() const { return ptable_.length(); }

 private:
  piecetable ptable_;
  linetable ltable_;
  host* host_;
  std::string fontpath_;
  std::vector<fontengine> fonts_;
  double fontsize_;
  eol eol_;
  bool mixeol_ = false;
};

}  // namespace swg
