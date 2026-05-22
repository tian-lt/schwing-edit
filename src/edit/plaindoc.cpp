// std
#include <algorithm>
#include <cassert>
// swg
#include "plaindoc.hpp"

namespace swg {

plaindoc::~plaindoc() = default;

void plaindoc::reset(std::optional<std::string> new_fontpath, std::optional<eol> new_eol) {
  if (new_fontpath.has_value() && new_fontpath != fontpath_) {
    fontpath_ = *new_fontpath;
    // Drop GPU-side resources so they get re-created at the next render.
    fontengine_.reset();
    atlas_.reset();
  }
  if (new_eol.has_value()) {
    mixeol_ = ltable_.rebuild(ptable_, *new_eol);
  }
}

void plaindoc::insert(size_t pos, std::string_view data) {
  ptable_.insert(pos, data);
  bool mixed_here = ltable_.insert(pos, data);
  mixeol_ = mixeol_ || mixed_here;
}

void plaindoc::erase(size_t pos, size_t length) {
  ptable_.erase(pos, length);
  // For now, rebuild the line table to keep line offsets in sync.
  mixeol_ = ltable_.rebuild(ptable_, ltable_.eol_mode());
}

void plaindoc::ensure_render_resources() {
  if (!fontengine_) {
    fontengine_ = std::make_unique<swg::fontengine>(fontpath_, fontsize_);
  }
  if (!atlas_) {
    atlas_ = std::make_unique<swg::glyphatlas>(fontengine_->face());
  }
  if (!shaper_) {
    shaper_ = std::make_unique<swg::textshaper>();
  }
}

layout_result plaindoc::render(int viewport_w, int viewport_h, int scroll_y, int scroll_x) {
  ensure_render_resources();
  layout_params params{
      .viewport_w = viewport_w,
      .viewport_h = viewport_h,
      .scroll_y = scroll_y,
      .scroll_x = scroll_x,
      .padding_x = 4,
      .padding_y = 2,
  };
  return layout_viewport(ptable_, ltable_, *fontengine_, *shaper_, *atlas_, params);
}

struct host::impl {
  static void post_edit(host* self, size_t /*pos_before*/, size_t /*pos_after*/) {
    // Invalidate the entire viewport. A more surgical line-range invalidation
    // can replace this later without touching callers.
    self->on_invalidate(self->viewport);
  }
};

layout_result host::render(rect rc, int scroll_y, int scroll_x) {
  return doc->render(rc.w, rc.h, scroll_y, scroll_x);
}

void host::caret(size_t byte_pos) {
  inspos_ = std::min(byte_pos, doc->length());
  sel_anchor_.reset();
  can_merge_next_ = false;
}

void host::set_selection_anchor(std::optional<size_t> anchor) {
  if (anchor.has_value()) {
    sel_anchor_ = std::min(*anchor, doc->length());
  } else {
    sel_anchor_.reset();
  }
  can_merge_next_ = false;
}

bool host::has_selection() const {
  return sel_anchor_.has_value() && *sel_anchor_ != inspos_;
}

std::pair<size_t, size_t> host::selection_range() const {
  if (!sel_anchor_.has_value()) return {inspos_, inspos_};
  size_t a = *sel_anchor_;
  size_t b = inspos_;
  if (a > b) std::swap(a, b);
  return {a, b};
}

std::string host::selected_text() const {
  if (!has_selection()) return {};
  auto [b, e] = selection_range();
  return doc->get(b, e - b);
}

void host::apply_edit(size_t pos, size_t erase_len, std::string_view inserted, bool mergeable) {
  edit_op op;
  op.pos = pos;
  op.erased = erase_len > 0 ? doc->get(pos, erase_len) : std::string{};
  op.inserted.assign(inserted.data(), inserted.size());
  op.caret_before = inspos_;
  op.anchor_before = sel_anchor_;
  op.mergeable = mergeable;
  if (erase_len > 0) doc->erase(pos, erase_len);
  if (!inserted.empty()) doc->insert(pos, inserted);
  inspos_ = pos + inserted.size();
  sel_anchor_.reset();
  op.caret_after = inspos_;
  dirty_ = true;
  // Try to merge into the previous op when both are mergeable insertions and
  // the new op is immediately contiguous to the previous one.
  if (mergeable && can_merge_next_ && !undo_stack_.empty()) {
    auto& last = undo_stack_.back();
    if (last.mergeable && last.erased.empty() && op.erased.empty() &&
        last.pos + last.inserted.size() == op.pos) {
      last.inserted += op.inserted;
      last.caret_after = op.caret_after;
      impl::post_edit(this, op.caret_before, inspos_);
      return;
    }
  }
  redo_stack_.clear();
  undo_stack_.push_back(std::move(op));
  can_merge_next_ = mergeable;
  impl::post_edit(this, undo_stack_.back().caret_before, inspos_);
}

void host::delete_selection() {
  if (!has_selection()) {
    sel_anchor_.reset();
    return;
  }
  auto [b, e] = selection_range();
  apply_edit(b, e - b, {}, /*mergeable=*/false);
}

void host::select_all() {
  can_merge_next_ = false;
  if (doc->length() == 0) {
    sel_anchor_.reset();
    inspos_ = 0;
    return;
  }
  sel_anchor_ = 0;
  inspos_ = doc->length();
}

void host::paste(std::string_view utf8) {
  size_t pos = inspos_;
  size_t erase_len = 0;
  if (has_selection()) {
    auto [b, e] = selection_range();
    pos = b;
    erase_len = e - b;
  }
  apply_edit(pos, erase_len, utf8, /*mergeable=*/false);
}

void host::insert_char(std::string_view u8char) {
  assert(u8char != "\r" && u8char != "\n" && u8char != "\b");
  size_t pos = inspos_;
  size_t erase_len = 0;
  if (has_selection()) {
    auto [b, e] = selection_range();
    pos = b;
    erase_len = e - b;
  }
  // Selection-replacing insertions cannot merge with a previous insertion.
  const bool mergeable = (erase_len == 0);
  apply_edit(pos, erase_len, u8char, mergeable);
}

void host::erase_char() {
  if (has_selection()) {
    delete_selection();
    return;
  }
  if (doc->length() == 0 || inspos_ == 0) {
    can_merge_next_ = false;
    return;
  }
  size_t l = std::min(6uz, inspos_);
  auto s = doc->get(inspos_ - l, l);
  size_t e = inspos_ - 1;
  for (auto it = s.rbegin(); it != s.rend(); ++it) {
    if ((*it & 0xC0) != 0x80) {
      break;
    }
    --e;
  }
  // If we backed up onto '\n' preceded by '\r', erase the CR too.
  if (e > 0 && inspos_ - e == 1) {
    std::string pair = doc->get(e - 1, 2);
    if (pair.size() == 2 && pair[0] == '\r' && pair[1] == '\n') {
      --e;
    }
  }
  apply_edit(e, inspos_ - e, {}, /*mergeable=*/false);
}

void host::delete_char() {
  if (has_selection()) {
    delete_selection();
    return;
  }
  if (inspos_ >= doc->length()) {
    can_merge_next_ = false;
    return;
  }
  // Determine the byte length of the next UTF-8 codepoint at inspos_.
  size_t remaining = doc->length() - inspos_;
  size_t probe = std::min<size_t>(6, remaining);
  auto s = doc->get(inspos_, probe);
  size_t len = 1;
  for (size_t i = 1; i < s.size(); ++i) {
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
      break;
    }
    ++len;
  }
  // Step over CRLF as a single unit.
  if (len == 1 && !s.empty() && s[0] == '\r' && inspos_ + 1 < doc->length()) {
    std::string n = doc->get(inspos_ + 1, 1);
    if (n == "\n") len = 2;
  }
  apply_edit(inspos_, len, {}, /*mergeable=*/false);
}

void host::linefeed() {
  size_t pos = inspos_;
  size_t erase_len = 0;
  if (has_selection()) {
    auto [b, e] = selection_range();
    pos = b;
    erase_len = e - b;
  }
  std::string_view eolstr;
  switch (doc->eol_mode()) {
    case eol::cr:   eolstr = "\r"; break;
    case eol::crlf: eolstr = "\r\n"; break;
    case eol::lf:   eolstr = "\n"; break;
    default: std::unreachable();
  }
  apply_edit(pos, erase_len, eolstr, /*mergeable=*/false);
}

void host::undo() {
  if (undo_stack_.empty()) return;
  edit_op op = std::move(undo_stack_.back());
  undo_stack_.pop_back();
  // Apply the inverse: remove the inserted bytes, re-insert the erased ones.
  if (!op.inserted.empty()) doc->erase(op.pos, op.inserted.size());
  if (!op.erased.empty()) doc->insert(op.pos, op.erased);
  inspos_ = op.caret_before;
  sel_anchor_ = op.anchor_before;
  redo_stack_.push_back(std::move(op));
  can_merge_next_ = false;
  dirty_ = true;
  impl::post_edit(this, inspos_, inspos_);
}

void host::redo() {
  if (redo_stack_.empty()) return;
  edit_op op = std::move(redo_stack_.back());
  redo_stack_.pop_back();
  if (!op.erased.empty()) doc->erase(op.pos, op.erased.size());
  if (!op.inserted.empty()) doc->insert(op.pos, op.inserted);
  inspos_ = op.caret_after;
  sel_anchor_.reset();
  undo_stack_.push_back(std::move(op));
  can_merge_next_ = false;
  dirty_ = true;
  impl::post_edit(this, inspos_, inspos_);
}

namespace {

// Number of bytes for the UTF-8 codepoint starting at `s[pos]`.
size_t utf8_char_len(const std::string& s, size_t pos) {
  if (pos >= s.size()) return 0;
  size_t len = 1;
  for (size_t i = pos + 1; i < s.size(); ++i) {
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) break;
    ++len;
  }
  return len;
}

// Length, in bytes, of the EOL terminator at the end of `[beg, beg+length)`.
size_t eol_byte_length(const piecetable& pt, const linetable::line& ln) {
  if (ln.length == 0) return 0;
  std::string last = pt.get(ln.beg + ln.length - 1, 1);
  if (last == "\n") {
    if (ln.length >= 2) {
      std::string prev = pt.get(ln.beg + ln.length - 2, 1);
      if (prev == "\r") return 2;
    }
    return 1;
  }
  if (last == "\r") return 1;
  return 0;
}

}  // namespace

void host::move_caret(host::motion m, const layout_result* layout) {
  sel_anchor_.reset();
  can_merge_next_ = false;
  move_caret_impl(m, layout);
}

void host::shift_move_caret(host::motion m, const layout_result* layout) {
  if (!sel_anchor_.has_value()) {
    sel_anchor_ = inspos_;
  }
  can_merge_next_ = false;
  move_caret_impl(m, layout);
}

void host::move_caret_impl(host::motion m, const layout_result* layout) {
  const auto& lt = doc->lines();
  const auto& pt = doc->pieces();
  const size_t doc_len = doc->length();
  const auto lines = lt.lines();

  auto current_line = [&]() -> size_t {
    if (lines.empty()) return 0;
    return lt.line_at_pos(inspos_);
  };

  switch (m) {
    case motion::char_left: {
      if (inspos_ == 0) return;
      // Walk back over any UTF-8 continuation bytes, then one lead byte.
      size_t e = inspos_ - 1;
      size_t scan = std::min<size_t>(6, inspos_);
      auto s = pt.get(inspos_ - scan, scan);
      for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if ((static_cast<unsigned char>(*it) & 0xC0) != 0x80) break;
        --e;
      }
      // If we landed on \n preceded by \r (CRLF), step back once more.
      if (e > 0) {
        std::string prev = pt.get(e - 1, 2);
        if (prev.size() == 2 && prev[0] == '\r' && prev[1] == '\n') {
          --e;
        }
      }
      inspos_ = e;
      break;
    }
    case motion::char_right: {
      if (inspos_ >= doc_len) return;
      std::string s = pt.get(inspos_, std::min<size_t>(6, doc_len - inspos_));
      size_t len = utf8_char_len(s, 0);
      // Step over CRLF as a single unit.
      if (len == 1 && !s.empty() && s[0] == '\r' && inspos_ + 1 < doc_len) {
        std::string n = pt.get(inspos_ + 1, 1);
        if (n == "\n") len = 2;
      }
      inspos_ += len;
      break;
    }
    case motion::line_home: {
      if (lines.empty()) {
        inspos_ = 0;
        break;
      }
      inspos_ = lines[current_line()].beg;
      break;
    }
    case motion::line_end: {
      if (lines.empty()) {
        inspos_ = 0;
        break;
      }
      const auto& ln = lines[current_line()];
      inspos_ = ln.beg + ln.length - eol_byte_length(pt, ln);
      break;
    }
    case motion::doc_home:
      inspos_ = 0;
      break;
    case motion::doc_end:
      inspos_ = doc_len;
      break;
    case motion::line_up:
    case motion::line_down:
    case motion::page_up:
    case motion::page_down: {
      const bool down = (m == motion::line_down || m == motion::page_down);
      int rows = 1;
      if (m == motion::page_up || m == motion::page_down) {
        if (layout && layout->line_height > 0 && viewport.h > 0) {
          rows = std::max(1, viewport.h / layout->line_height - 1);
        } else {
          rows = 1;
        }
      }
      if (!layout || layout->carets.empty()) {
        // Without a layout we can still move by line via the line table.
        size_t li = lines.empty() ? 0 : current_line();
        if (!down) {
          if (li == 0) return;
          li = (li > static_cast<size_t>(rows)) ? li - rows : 0;
        } else {
          if (lines.empty() || li + 1 >= lines.size()) return;
          li = std::min(li + static_cast<size_t>(rows), lines.size() - 1);
        }
        inspos_ = lines[li].beg;
        break;
      }
      // Find the anchor for the current insertion point to read its `x`.
      const caret_anchor* cur = nullptr;
      for (const auto& a : layout->carets) {
        if (a.byte_pos == inspos_) {
          cur = &a;
          break;
        }
      }
      const float dy_total = static_cast<float>(rows) * layout->line_height;
      const float target_y = cur ? (down ? cur->baseline_y + dy_total
                                         : cur->baseline_y - dy_total)
                                 : 0.0f;
      const float target_x = cur ? cur->x : 0.0f;
      // Pick the anchor closest to (target_x, target_y) with priority on y.
      const caret_anchor* best = nullptr;
      float best_score = 0.0f;
      for (const auto& a : layout->carets) {
        const float dy = a.baseline_y - target_y;
        const float dx = a.x - target_x;
        const float score = dy * dy * 1000.0f + dx * dx;
        if (!best || score < best_score) {
          best = &a;
          best_score = score;
        }
      }
      if (best) inspos_ = best->byte_pos;
      break;
    }
  }
  inspos_ = std::min(inspos_, doc_len);
}

void host::clear() {
  if (doc->length() > 0) {
    doc->erase(0, doc->length());
  }
  inspos_ = 0;
  sel_anchor_.reset();
  undo_stack_.clear();
  redo_stack_.clear();
  can_merge_next_ = false;
  dirty_ = false;
}

void host::load_text(std::string_view utf8, eol mode) {
  clear();
  doc->reset(std::nullopt, mode);
  if (!utf8.empty()) {
    doc->insert(0, utf8);
  }
  inspos_ = 0;
  sel_anchor_.reset();
  undo_stack_.clear();
  redo_stack_.clear();
  can_merge_next_ = false;
  dirty_ = false;
  on_invalidate(viewport);
}

std::string host::all_text() const {
  return doc->get(0, doc->length());
}

void host::set_eol_mode(eol mode) {
  doc->reset(std::nullopt, mode);
}

docpos host::pos_to_linecol(size_t byte_pos) const {
  size_t line_index = doc->lines().line_at_pos(byte_pos);
  auto lines = doc->lines().lines();
  if (line_index >= lines.size()) {
    return {static_cast<int>(line_index) + 1, 1};
  }
  size_t beg = lines[line_index].beg;
  if (byte_pos < beg) byte_pos = beg;
  std::string chunk = doc->get(beg, byte_pos - beg);
  int cp_count = 0;
  for (unsigned char c : chunk) {
    if ((c & 0xC0) != 0x80) ++cp_count;
  }
  return {static_cast<int>(line_index) + 1, cp_count + 1};
}

namespace {

std::string ascii_lower(std::string_view s) {
  std::string r(s);
  for (auto& c : r) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
  }
  return r;
}

}  // namespace

bool host::find_text(std::string_view needle, bool match_case, bool down) {
  if (needle.empty()) return false;
  const size_t doc_len = doc->length();
  std::string text = doc->get(0, doc_len);
  std::string hay = match_case ? text : ascii_lower(text);
  std::string nd = match_case ? std::string(needle) : ascii_lower(needle);
  size_t start = inspos();
  if (has_selection()) {
    auto [b, e] = selection_range();
    start = down ? e : b;
  }
  size_t pos = std::string::npos;
  if (down) {
    pos = hay.find(nd, start);
    if (pos == std::string::npos) {
      // Wrap around from beginning, but don't match the same position.
      pos = hay.find(nd, 0);
      if (pos == std::string::npos || pos >= start) return false;
    }
  } else {
    if (start > 0) {
      pos = hay.rfind(nd, start - 1);
    }
    if (pos == std::string::npos) {
      pos = hay.rfind(nd);
      if (pos == std::string::npos || (start > 0 && pos < start)) return false;
      if (pos == std::string::npos) return false;
    }
  }
  // Select the match: anchor at one end, caret at the other, so the matched
  // range is selected and the caret rests at the natural place to keep
  // searching from. Going down the caret is at the end of the match; going
  // up it sits at the start.
  set_selection_anchor(std::nullopt);
  if (down) {
    inspos_ = pos + needle.size();
    set_selection_anchor(pos);
  } else {
    inspos_ = pos;
    set_selection_anchor(pos + needle.size());
  }
  can_merge_next_ = false;
  on_invalidate(viewport);
  return true;
}

bool host::replace_text(std::string_view needle, std::string_view replacement,
                        bool match_case) {
  if (needle.empty()) return false;
  // If the current selection equals the needle, replace it.
  if (has_selection()) {
    std::string sel = selected_text();
    bool eq = match_case
                  ? (sel == std::string(needle))
                  : (ascii_lower(sel) == ascii_lower(needle));
    if (eq) {
      auto [b, e] = selection_range();
      apply_edit(b, e - b, replacement, /*mergeable=*/false);
      // Caret is at b + replacement.size(); find next from here going down.
      return find_text(needle, match_case, /*down=*/true) || true;
    }
  }
  // Otherwise just find the next match.
  return find_text(needle, match_case, /*down=*/true);
}

size_t host::replace_all(std::string_view needle, std::string_view replacement,
                         bool match_case) {
  if (needle.empty()) return 0;
  const size_t doc_len = doc->length();
  std::string text = doc->get(0, doc_len);
  std::string hay = match_case ? text : ascii_lower(text);
  std::string nd = match_case ? std::string(needle) : ascii_lower(needle);
  // Build the replaced text in one pass.
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  size_t count = 0;
  while (i < hay.size()) {
    size_t p = hay.find(nd, i);
    if (p == std::string::npos) {
      out.append(text, i, std::string::npos);
      break;
    }
    out.append(text, i, p - i);
    out.append(replacement.data(), replacement.size());
    i = p + nd.size();
    ++count;
  }
  if (count == 0) return 0;
  // Atomic replace as one undo step.
  apply_edit(0, doc_len, out, /*mergeable=*/false);
  on_invalidate(viewport);
  return count;
}

size_t host::goto_line(int line_1based) {
  const auto& lines = doc->lines().lines();
  if (lines.empty() || line_1based < 1) {
    inspos_ = 0;
  } else {
    size_t idx = static_cast<size_t>(line_1based - 1);
    if (idx >= lines.size()) idx = lines.size() - 1;
    inspos_ = lines[idx].beg;
  }
  sel_anchor_.reset();
  can_merge_next_ = false;
  on_invalidate(viewport);
  return inspos_;
}

size_t host::line_count() const {
  return doc->lines().line_count();
}

size_t host::hit_test(const layout_result& layout, float x, float y) {
  if (layout.carets.empty()) return 0;
  const caret_anchor* best = nullptr;
  float best_score = 0.0f;
  for (const auto& a : layout.carets) {
    const float dy = a.baseline_y - y;
    const float dx = a.x - x;
    // Strongly weight matching the correct line.
    const float score = dy * dy * 1000.0f + dx * dx;
    if (!best || score < best_score) {
      best = &a;
      best_score = score;
    }
  }
  return best ? best->byte_pos : 0;
}

}  // namespace swg
