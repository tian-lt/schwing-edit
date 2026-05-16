#pragma once
// std
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
// schwing
#include "fontengine.hpp"
#include "linetable.hpp"
#include "piecetable.hpp"

namespace swg {

struct rect {
  int x, y, w, h;
};

class display {
  friend class plaindoc;

 public:
  virtual ~display();
  virtual void on_invalidate(rect rc) = 0;
  void render();
};

class plaindoc {
 public:
  explicit plaindoc(display* disp, std::string fontpath, double fontsize, eol eol)
      : ltable_(eol), disp_(disp), fontpath_(fontpath), fontsize_(fontsize) {}
  void reset(std::optional<std::string> new_fontpath, std::optional<eol> new_eol);
  void insert(size_t pos, std::string_view data);
  void erase(size_t pos, size_t length);
  std::string get(size_t pos, size_t length) const { return ptable_.get(pos, length); }
  size_t length() const { return ptable_.length(); }

 private:
  piecetable ptable_;
  linetable ltable_;
  display* disp_;
  std::string fontpath_;
  std::vector<fontengine> fonts_;
  double fontsize_;
  bool mixeol_ = false;
};

}  // namespace swg
