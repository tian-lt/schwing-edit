// std
#include <algorithm>
#include <iterator>
// swg
#include "linetable.hpp"

namespace swg {

bool linetable::rebuild(const piecetable& ptable, eol eol) {
  eol_ = eol;
  linelist_.clear();
  constexpr size_t BUF_SIZE = 256;
  char buf[BUF_SIZE];
  const size_t total_len = ptable.length();
  size_t pos = 0;
  size_t line_beg = 0;
  bool pending_cr = false;    // last byte seen was a bare '\r' awaiting a possible '\n'
  size_t pending_cr_pos = 0;  // absolute position of that bare '\r'
  unsigned eol_mask = 0;      // bit 0: lf, bit 1: crlf, bit 2: cr
  auto push_line = [&](size_t end_exclusive, uint8_t eolb) {
    linelist_.push_back(
        line{.beg = line_beg, .length = end_exclusive - line_beg, .eol_bytes = eolb});
    line_beg = end_exclusive;
  };
  while (pos < total_len) {
    auto chunk = std::min(BUF_SIZE, total_len - pos);
    ptable.get_to(pos, std::span<char>{buf, chunk});
    for (size_t i = 0; i < chunk; ++i) {
      const char c = buf[i];
      const size_t cur = pos + i;
      if (pending_cr) {
        if (c == '\n') {
          eol_mask |= 0b010;  // crlf
          push_line(cur + 1, 2);
          pending_cr = false;
          continue;
        }
        // bare cr finishes the previous line
        eol_mask |= 0b100;  // cr
        push_line(pending_cr_pos + 1, 1);
        pending_cr = false;
        // fall through to process the current byte
      }
      if (c == '\r') {
        pending_cr = true;
        pending_cr_pos = cur;
      } else if (c == '\n') {
        eol_mask |= 0b001;  // lf
        push_line(cur + 1, 1);
      }
    }
    pos += chunk;
  }
  if (pending_cr) {
    eol_mask |= 0b100;  // cr
    push_line(pending_cr_pos + 1, 1);
  }
  if (line_beg < total_len) {
    push_line(total_len, 0);
  }
  // mixed EOLs iff more than one distinct terminator kind was observed
  return (eol_mask & (eol_mask - 1)) != 0;
}

bool linetable::insert(size_t pos, std::string_view data) {
  if (data.empty()) {
    return false;
  }

  // Ensure there is always a host line to attach the insertion to. An empty linelist
  // logically corresponds to a single zero-length line at offset 0.
  if (linelist_.empty()) {
    linelist_.push_back(line{.beg = 0, .length = 0, .eol_bytes = 0});
  }

  const size_t host_idx = line_at_pos(pos);
  const line host = linelist_[host_idx];
  const size_t before = pos - host.beg;       // bytes of host line preceding pos
  const size_t after = host.length - before;  // bytes of host line at/after pos

  // Scan `data` and collect the end-offsets (within `data`) of every terminator
  // along with how many bytes each terminator occupies (1 for bare cr/lf, 2 for crlf).
  std::vector<size_t> term_ends;
  std::vector<uint8_t> term_bytes;
  unsigned eol_mask = 0;  // bit 0: lf, bit 1: crlf, bit 2: cr
  bool pending_cr = false;
  size_t pending_cr_pos = 0;
  for (size_t i = 0; i < data.size(); ++i) {
    const char c = data[i];
    if (pending_cr) {
      if (c == '\n') {
        eol_mask |= 0b010;  // crlf
        term_ends.push_back(i + 1);
        term_bytes.push_back(2);
        pending_cr = false;
        continue;
      }
      eol_mask |= 0b100;  // bare cr
      term_ends.push_back(pending_cr_pos + 1);
      term_bytes.push_back(1);
      pending_cr = false;
      // fall through to reprocess the current byte
    }
    if (c == '\r') {
      pending_cr = true;
      pending_cr_pos = i;
    } else if (c == '\n') {
      eol_mask |= 0b001;  // lf
      term_ends.push_back(i + 1);
      term_bytes.push_back(1);
    }
  }
  if (pending_cr) {
    eol_mask |= 0b100;  // trailing bare cr at end of data
    term_ends.push_back(pending_cr_pos + 1);
    term_bytes.push_back(1);
  }

  const size_t shift = data.size();

  if (term_ends.empty()) {
    // No new line breaks: just grow the host line.
    linelist_[host_idx].length += shift;
    for (size_t i = host_idx + 1; i < linelist_.size(); ++i) {
      linelist_[i].beg += shift;
    }
  } else {
    // Build the replacement lines for the host line.
    std::vector<line> new_lines;
    new_lines.reserve(term_ends.size() + 1);
    // First replacement: host prefix + bytes up to first terminator in data.
    new_lines.push_back(line{.beg = host.beg,
                             .length = before + term_ends.front(),
                             .eol_bytes = term_bytes.front()});
    // Middle replacements: one per additional terminator in data.
    for (size_t i = 1; i < term_ends.size(); ++i) {
      new_lines.push_back(line{.beg = host.beg + before + term_ends[i - 1],
                               .length = term_ends[i] - term_ends[i - 1],
                               .eol_bytes = term_bytes[i]});
    }
    // Trailing replacement: bytes after the last terminator in data + host suffix.
    new_lines.push_back(line{.beg = host.beg + before + term_ends.back(),
                             .length = (data.size() - term_ends.back()) + after,
                             .eol_bytes = host.eol_bytes});

    auto it = linelist_.erase(linelist_.begin() + host_idx);
    it = linelist_.insert(it, new_lines.begin(), new_lines.end());

    // Shift every line after the replaced range by `shift`.
    for (size_t i = host_idx + new_lines.size(); i < linelist_.size(); ++i) {
      linelist_[i].beg += shift;
    }
  }

  // Mixed EOLs in the inserted data iff more than one terminator kind was seen.
  return (eol_mask & (eol_mask - 1)) != 0;
}

void linetable::erase(const piecetable& ptable_after, size_t pos, size_t length) {
  if (length == 0 || linelist_.empty()) {
    return;
  }

  // The deletion spans lines [i0..i1] in the OLD line table.
  const size_t i0 = line_at_pos(pos);
  const size_t i1 = line_at_pos(pos + length - 1);

  // Extend the rescan window by one line on each side to absorb any terminator
  // re-pairing across the deletion boundary (e.g. a bare-cr at the end of
  // line[i0-1] re-pairing with a new '\n' at the start of the merged region).
  const size_t window_lo_idx = (i0 > 0) ? (i0 - 1) : 0;
  const size_t window_hi_idx = (i1 + 1 < linelist_.size()) ? (i1 + 1) : i1;

  const size_t window_beg_old = linelist_[window_lo_idx].beg;
  const size_t window_end_old =
      linelist_[window_hi_idx].beg + linelist_[window_hi_idx].length;
  const size_t window_beg_post = window_beg_old;  // unchanged: erase is to its right
  const size_t window_len_post =
      (window_end_old - window_beg_old) - length;

  // Read the post-erase content of the rescan window so we can rebuild it
  // exactly. For typical edits this is a handful of bytes (the surviving
  // ends of the spanning lines plus their neighbors); even huge selection
  // deletes only touch the prefix of line[i0-1] through the suffix of
  // line[i1+1]: O(few line lengths).
  std::string content;
  if (window_len_post > 0) {
    content.resize(window_len_post);
    ptable_after.get_to(window_beg_post,
                        std::span<char>{content.data(), window_len_post});
  }

  // Decide whether the window's tail is allowed to be a "no-terminator"
  // residual line. It is iff our window ends at the actual end of the
  // document (i.e. there was no line after window_hi_idx in the old table).
  const bool window_reaches_doc_end = (window_hi_idx + 1 == linelist_.size());

  // Per-byte rescan of the window content. Mirrors `rebuild`'s logic so the
  // resulting structure is byte-identical to what a full rebuild would yield.
  std::vector<line> new_lines;
  size_t cursor = 0;  // position within `content` where the current line begins
  bool pending_cr = false;
  size_t pending_cr_pos = 0;
  auto push = [&](size_t end_in_content, uint8_t eolb) {
    new_lines.push_back(line{.beg = window_beg_post + cursor,
                             .length = end_in_content - cursor,
                             .eol_bytes = eolb});
    cursor = end_in_content;
  };
  for (size_t i = 0; i < window_len_post; ++i) {
    const char c = content[i];
    if (pending_cr) {
      if (c == '\n') {
        push(i + 1, 2);
        pending_cr = false;
        continue;
      }
      push(pending_cr_pos + 1, 1);
      pending_cr = false;
      // fall through
    }
    if (c == '\r') {
      pending_cr = true;
      pending_cr_pos = i;
    } else if (c == '\n') {
      push(i + 1, 1);
    }
  }
  if (pending_cr) {
    push(pending_cr_pos + 1, 1);
  }
  if (cursor < window_len_post) {
    // Residual content without a terminator. This is only meaningful when
    // the window reaches the end of the document; otherwise the document
    // continues past our window and the residual content is part of a line
    // that already exists past window_hi_idx — which we left untouched.
    // In practice, the window always extends to line[window_hi_idx]'s end,
    // so any residual must be the last line of the document. We assert this
    // implicitly via window_reaches_doc_end: if it doesn't hold, the residual
    // is impossible (line[window_hi_idx] had a terminator at its end, so the
    // scan would have closed the last line).
    (void)window_reaches_doc_end;
    push(window_len_post, 0);
  }

  // Splice new_lines in place of lines [window_lo_idx .. window_hi_idx].
  auto first = linelist_.begin() + static_cast<std::ptrdiff_t>(window_lo_idx);
  auto last = linelist_.begin() + static_cast<std::ptrdiff_t>(window_hi_idx + 1);
  linelist_.erase(first, last);
  linelist_.insert(linelist_.begin() + static_cast<std::ptrdiff_t>(window_lo_idx),
                   new_lines.begin(), new_lines.end());

  // Shift the .beg of every line after the spliced range by -length to
  // reflect the post-erase byte coordinate space.
  for (size_t i = window_lo_idx + new_lines.size(); i < linelist_.size(); ++i) {
    linelist_[i].beg -= length;
  }

  // An empty document collapses to an empty line list.
  if (linelist_.size() == 1 && linelist_.front().length == 0 &&
      linelist_.front().eol_bytes == 0) {
    linelist_.clear();
  }
}

size_t linetable::line_at_pos(size_t pos) const {
  auto it = std::ranges::upper_bound(linelist_, pos, {}, &line::beg);
  if (it == linelist_.begin()) {
    return 0;
  }
  return std::distance(linelist_.begin(), it) - 1;
}

}  // namespace swg
