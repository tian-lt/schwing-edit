#pragma once
// std
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
// swg
#include "linetable.hpp"
#include "piecetable.hpp"

namespace swg {

struct rect {
  std::int32_t x = 0, y = 0, w = 0, h = 0;
};

struct damage {
  std::size_t pos;          // byte position of the edit
  std::size_t erased_len;   // bytes erased
  std::size_t inserted_len; // bytes inserted
};

class plaindoc;

// Abstract platform customization point. Platforms (e.g. Windows app) implement
// this; the editor core invokes these hooks. NO Windows headers may appear in
// the interface.
class host {
 public:
  host() = default;
  virtual ~host() = default;
  host(const host&) = delete;
  host& operator=(const host&) = delete;

  // Invalidate all of the editor surface.
  virtual void on_invalidate() = 0;
  // Document changed; platforms may use this to compute a precise dirty rect
  // from the damage and re-scroll if the caret moved.
  virtual void on_doc_changed(const damage&) = 0;
  // Caret position changed only (no document change).
  virtual void on_caret_moved() = 0;
};

class plaindoc {
 public:
  plaindoc();

  void attach_host(host* h) noexcept { host_ = h; }
  host* attached_host() const noexcept { return host_; }

  // Load utf8 bytes by copy (the document owns its storage afterward).
  void load_text(std::string utf8);
  // Load utf8 bytes by view (caller MUST keep the storage alive until the next
  // load_text / load_view / materialize / destruction).
  void load_view(std::string_view utf8);

  // Force-copy any externally-referenced bytes into owned storage. Safe to call
  // before releasing the source mapping.
  void materialize();

  const piecetable& buffer() const noexcept { return pt_; }
  const linetable& lines() const noexcept { return lt_; }

  std::size_t length() const noexcept { return pt_.length(); }
  std::size_t caret() const noexcept { return caret_; }

  // Selection range. anchor == caret iff no selection.
  std::size_t anchor() const noexcept { return anchor_; }
  bool has_selection() const noexcept { return anchor_ != caret_; }
  std::pair<std::size_t, std::size_t> selection_range() const noexcept {
    return caret_ <= anchor_ ? std::make_pair(caret_, anchor_)
                             : std::make_pair(anchor_, caret_);
  }

  eol document_eol() const noexcept { return eol_; }
  void set_document_eol(eol e) noexcept { eol_ = e; }
  bool mixed_eol() const noexcept { return mixed_eol_; }

  // ---------- Editing operations (platform-agnostic) ----------

  // Insert utf8 bytes at the caret. If there is a selection, replace it.
  void insert_text(std::string_view utf8);

  // Backspace one UTF-8 codepoint at the caret. If selection, delete it.
  void backspace();

  // Delete one UTF-8 codepoint forward of the caret. If selection, delete it.
  void delete_forward();

  // Insert a newline (using document_eol) at the caret. If selection, replace it.
  void newline();

  // Replace the current selection (or insert at caret) with bytes. Convenience
  // for paste paths.
  void replace_selection(std::string_view utf8);

  // ---------- Caret motion ----------

  enum class motion : std::uint8_t {
    left_char,    // one utf-8 codepoint left
    right_char,   // one utf-8 codepoint right
    left_word,    // left to previous word boundary
    right_word,   // right to next word boundary
    line_up,
    line_down,
    line_home,    // start of line
    line_end,     // end of line
    doc_home,
    doc_end,
  };

  // Move caret. If `extend_selection` is true, keep anchor; otherwise collapse.
  void move_caret(motion m, bool extend_selection);

  // Set caret to an absolute byte position (clamps to valid utf-8 boundary).
  void set_caret(std::size_t pos, bool extend_selection);

  // Select all.
  void select_all();

  // ---------- Helpers ----------

  // Convert a (line, column-in-bytes) pair to an absolute byte position.
  std::size_t pos_from_line_col(std::size_t line_idx, std::size_t col_bytes) const noexcept;

  // Return the bytes for a line (without terminator). Heap-allocated copy.
  std::string get_line_text(std::size_t line_idx) const;

  // Round `pos` down to the nearest utf-8 codepoint start.
  std::size_t snap_to_codepoint_left(std::size_t pos) const;
  // Round `pos` up to the nearest utf-8 codepoint start.
  std::size_t snap_to_codepoint_right(std::size_t pos) const;

 private:
  struct impl;

  piecetable pt_;
  linetable lt_;
  host* host_ = nullptr;
  eol eol_ = eol::crlf;  // Windows-default
  bool mixed_eol_ = false;

  std::size_t caret_ = 0;
  std::size_t anchor_ = 0;

  // For vertical motion: preferred column (in bytes) at start of vertical run.
  // Reset on any non-vertical motion or edit.
  std::size_t pref_col_ = 0;
  bool pref_col_valid_ = false;
};

}  // namespace swg
