#pragma once
// std
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
// schwing
#include "fontengine.hpp"
#include "glyphatlas.hpp"
#include "linetable.hpp"
#include "piecetable.hpp"
#include "textlayout.hpp"
#include "textshaper.hpp"

namespace swg {

struct rect {
  int x, y, w, h;
};

struct docpos {
  int line, column;
};

class plaindoc;

class host {
  friend class plaindoc;
  struct impl;

 public:
  virtual ~host() = default;
  virtual void on_invalidate(rect rc) = 0;

  // Render the visible window. Returns the layout used so platform code can
  // position the system caret, hit-test, etc.
  layout_result render(rect rc, int scroll_y = 0, int scroll_x = 0);

  // Caret operations operate on byte offsets in the document. `inspos()`
  // queries the current insertion point. Setting the caret implicitly clears
  // any active selection anchor (use `set_selection_anchor` afterwards if you
  // want to extend the selection).
  void caret(size_t byte_pos);
  size_t inspos() const { return inspos_; }

  // Motion granularity for `move_caret`.
  enum struct motion {
    char_left,      // previous UTF-8 codepoint
    char_right,     // next UTF-8 codepoint
    line_up,        // same column on previous visible line (uses `layout`)
    line_down,      // same column on next visible line (uses `layout`)
    line_home,      // first byte of current line
    line_end,       // last byte of current line (before EOL)
    doc_home,       // start of document
    doc_end,        // end of document
    page_up,        // one viewport-height of lines up (uses `layout`)
    page_down,      // one viewport-height of lines down (uses `layout`)
  };

  // Move the caret one step in the given direction. For `line_up` / `line_down`
  // a current `layout` is required (the geometric column is preserved via the
  // caret anchor whose `x` is closest to the current caret's `x`).
  // `move_caret` clears any selection anchor; use `shift_move_caret` to extend
  // the selection instead.
  void move_caret(motion m, const layout_result* layout = nullptr);

  // Move the caret like `move_caret`, but anchor the start of the selection at
  // the caret's previous position if no selection is currently active.
  void shift_move_caret(motion m, const layout_result* layout = nullptr);

  // Selection model. The selection is the half-open byte range
  // [min(anchor, inspos), max(anchor, inspos)). If `selection_anchor()` is
  // empty, no selection is active.
  void set_selection_anchor(std::optional<size_t> anchor);
  std::optional<size_t> selection_anchor() const { return sel_anchor_; }
  bool has_selection() const;
  std::pair<size_t, size_t> selection_range() const;
  std::string selected_text() const;
  // Delete the current selection. The caret is repositioned at the start of
  // the deleted range and the anchor is cleared. No-op when no selection.
  void delete_selection();
  // Set selection to the whole document.
  void select_all();
  // Replace the selection (or insert at the caret when there's no selection)
  // with the given UTF-8 text. The caret ends up at the end of the inserted
  // text and the anchor is cleared.
  void paste(std::string_view utf8);

  // Returns the byte offset whose caret anchor in `layout` is geometrically
  // closest to the pixel (`x`, `y`). Useful for mouse-click positioning.
  static size_t hit_test(const layout_result& layout, float x, float y);

  // Document-level operations.
  // Erase the entire document and clear the selection / caret.
  void clear();
  // Replace the entire document with `utf8`. The caret is moved to the start
  // of the document and any active selection is cleared. The document's EOL
  // mode is updated to `mode` (so subsequent linefeeds use the same EOL).
  void load_text(std::string_view utf8, eol mode);
  // Return the entire document content as UTF-8.
  std::string all_text() const;
  // Switch the document's EOL mode (re-classifies existing lines).
  void set_eol_mode(eol mode);

  // High-level text input operations. Each of these deletes the current
  // selection (when present) before performing its action, mirroring standard
  // text-control behavior.
  void insert_char(std::string_view u8char);
  void erase_char();
  void delete_char();
  void linefeed();

  // Undo / redo. Each `insert_char`, `linefeed`, `erase_char`, `delete_char`,
  // `delete_selection`, and `paste` records exactly one undo step (consecutive
  // `insert_char` calls are merged into one step until the chain is broken by
  // any non-insertion operation or caret movement).
  bool can_undo() const { return !undo_stack_.empty(); }
  bool can_redo() const { return !redo_stack_.empty(); }
  void undo();
  void redo();

  // Dirty-bit tracking. `is_dirty()` returns true iff the document has been
  // modified since the last `clear_dirty()` call. `clear()` and `load_text()`
  // implicitly clear the dirty bit (they represent a "fresh" document); every
  // other mutation sets it.
  bool is_dirty() const { return dirty_; }
  void clear_dirty() { dirty_ = false; }

  // Line/column at the given byte offset (1-based, like editors traditionally
  // surface to the user). Columns count UTF-8 code points, not bytes.
  docpos pos_to_linecol(size_t byte_pos) const;

  // Text search. `find_text` searches for the (possibly case-insensitive)
  // UTF-8 needle. The search starts from the end of the current selection
  // (when going `down`) or the start of the selection (when going up). On
  // match, the matched range is selected and the caret is moved to its
  // beginning; returns true. Returns false when no match exists in either
  // direction (the document is searched with wrap-around).
  // Case insensitivity is ASCII-only.
  bool find_text(std::string_view needle, bool match_case, bool down);

  // If the current selection equals `needle` (per `match_case`), replace it
  // with `replacement` and then advance to the next match. When no selection
  // matches, this behaves as a plain `find_text(down=true)`. Returns true if
  // either a replacement or a subsequent match happened.
  bool replace_text(std::string_view needle, std::string_view replacement,
                    bool match_case);

  // Replace every occurrence of `needle` with `replacement`. Returns the
  // number of replacements performed. Recorded as a single undo step.
  size_t replace_all(std::string_view needle, std::string_view replacement,
                     bool match_case);

  // Move the caret to the given 1-based line number. Clamps to the valid
  // range [1, line_count]. Clears any selection. Returns the resulting byte
  // offset.
  size_t goto_line(int line_1based);
  // Number of lines in the document.
  size_t line_count() const;

 protected:
  plaindoc* doc = nullptr;
  rect viewport = {};

 private:
  // A reversible edit applied to the document.
  struct edit_op {
    size_t pos = 0;                    // byte offset of the edit
    std::string erased;                // bytes that were erased at `pos`
    std::string inserted;              // bytes that were inserted at `pos`
    size_t caret_before = 0;
    size_t caret_after = 0;
    std::optional<size_t> anchor_before;
    bool mergeable = false;            // true if this is a single-char insertion
  };

  // Shared caret-motion logic for `move_caret` and `shift_move_caret`.
  void move_caret_impl(motion m, const layout_result* layout);

  // Apply a primitive edit and record it on the undo stack. The caret ends up
  // at `pos + inserted.size()` and the selection anchor is cleared. When
  // `mergeable` is true, the operation is merged into the previous one if the
  // previous one was also mergeable and is contiguous in the document.
  void apply_edit(size_t pos, size_t erase_len, std::string_view inserted, bool mergeable);

  size_t inspos_ = 0;
  std::optional<size_t> sel_anchor_;
  std::vector<edit_op> undo_stack_;
  std::vector<edit_op> redo_stack_;
  bool can_merge_next_ = false;
  bool dirty_ = false;
};

class plaindoc {
  friend class host;

 public:
  explicit plaindoc(host* host, std::string fontpath, double fontsize, eol eol)
      : ltable_(eol), host_(host), fontpath_(std::move(fontpath)), fontsize_(fontsize) {}
  ~plaindoc();

  void reset(std::optional<std::string> new_fontpath, std::optional<eol> new_eol,
             std::optional<double> new_fontsize = std::nullopt);
  void insert(size_t pos, std::string_view data);
  void erase(size_t pos, size_t length);
  std::string get(size_t pos, size_t length) const { return ptable_.get(pos, length); }
  size_t length() const { return ptable_.length(); }
  eol eol_mode() const { return ltable_.eol_mode(); }
  bool mixed_eol() const { return mixeol_; }

  // Lazily-initialized rendering pipeline. The render() call performs glyph
  // shaping and layout but does not draw to any surface; platform code reads
  // the layout_result and the atlas() to paint pixels using its native
  // graphics API.
  layout_result render(int viewport_w, int viewport_h, int scroll_y, int scroll_x = 0);

  const linetable& lines() const { return ltable_; }
  const piecetable& pieces() const { return ptable_; }
  // Accessors for platform-side renderers. Both are valid only after the
  // first render() call has lazily constructed them.
  const glyphatlas& atlas() const { return *atlas_; }
  glyphatlas& atlas() { return *atlas_; }
  const fontengine& font() const { return *fontengine_; }
  const std::string& fontpath() const { return fontpath_; }
  double fontsize() const { return fontsize_; }

 private:
  void ensure_render_resources();

  piecetable ptable_;
  linetable ltable_;
  host* host_;
  std::string fontpath_;
  double fontsize_;
  bool mixeol_ = false;

  std::unique_ptr<fontengine> fontengine_;
  std::unique_ptr<glyphatlas> atlas_;
  std::unique_ptr<textshaper> shaper_;
};

}  // namespace swg
