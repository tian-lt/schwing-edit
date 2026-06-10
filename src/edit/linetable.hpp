#pragma once
// std
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#ifdef SWGUT
namespace swg::ut::linetable_ut {
struct rebuild;
struct insert;
struct erase;
struct line_at_pos;
}  // namespace swg::ut::linetable_ut
#endif

namespace swg {

class piecetable;

enum class eol : std::uint8_t {
  lf,    // \n
  crlf,  // \r\n
  cr,    // \r
};

struct line {
  std::size_t beg;       // byte offset within the document
  std::size_t length;    // bytes (including terminator if present)
  std::size_t eol_bytes; // 0 if last/no terminator, 1 for \n or \r, 2 for \r\n
};

class linetable {
 public:
  linetable() = default;

  // Build the line index over `pt`. Returns true if more than one EOL kind was observed.
  bool rebuild(const piecetable& pt);

  // Notify of a byte-range insertion at position `pos` of length `n_inserted`.
  // The piecetable must already reflect the insertion before calling.
  // Returns true if more than one EOL kind is observed across the affected range.
  bool insert(const piecetable& pt, std::size_t pos, std::size_t n_inserted);

  // Notify of a byte-range erase at position `pos` of length `n_erased`.
  // The piecetable must already reflect the erase before calling.
  // Returns true if more than one EOL kind is observed across the affected range.
  bool erase(const piecetable& pt, std::size_t pos, std::size_t n_erased);

  // Number of logical lines (always >= 1).
  std::size_t line_count() const noexcept { return lines_.empty() ? 1 : lines_.size(); }

  // Get line by index. index must be < line_count(). If lines_ is empty (no content),
  // returns a synthetic {0, 0, 0} line.
  line at(std::size_t index) const noexcept {
    if (lines_.empty()) return line{0, 0, 0};
    return lines_[index];
  }

  // O(log n) lookup: returns the line index that contains byte `pos`. For pos == doc length
  // returns the last line index. Throws std::out_of_range if pos > doc length.
  std::size_t line_at_pos(std::size_t pos) const;

  // Convenience: get (line_index, column_in_bytes) for a byte position.
  std::pair<std::size_t, std::size_t> line_col(std::size_t pos) const;

  // The dominant EOL detected. Defaults to lf if buffer is empty.
  eol dominant_eol() const noexcept { return dominant_; }

  // The byte sequence for the dominant EOL.
  std::string_view eol_bytes_for(eol e) const noexcept;

  std::size_t doc_length() const noexcept { return doc_length_; }

 private:
  struct impl;

  // sorted by beg
  std::vector<line> lines_;
  std::size_t doc_length_ = 0;
  eol dominant_ = eol::lf;

#ifdef SWGUT
  friend struct swg::ut::linetable_ut::rebuild;
  friend struct swg::ut::linetable_ut::insert;
  friend struct swg::ut::linetable_ut::erase;
  friend struct swg::ut::linetable_ut::line_at_pos;
#endif
};

}  // namespace swg
