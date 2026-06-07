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
  auto push_line = [&](size_t end_exclusive) {
    linelist_.push_back(line{.beg = line_beg, .length = end_exclusive - line_beg});
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
          push_line(cur + 1);
          pending_cr = false;
          continue;
        }
        // bare cr finishes the previous line
        eol_mask |= 0b100;  // cr
        push_line(pending_cr_pos + 1);
        pending_cr = false;
        // fall through to process the current byte
      }
      if (c == '\r') {
        pending_cr = true;
        pending_cr_pos = cur;
      } else if (c == '\n') {
        eol_mask |= 0b001;  // lf
        push_line(cur + 1);
      }
    }
    pos += chunk;
  }
  if (pending_cr) {
    eol_mask |= 0b100;  // cr
    push_line(pending_cr_pos + 1);
  }
  if (line_beg < total_len) {
    push_line(total_len);
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
    linelist_.push_back(line{.beg = 0, .length = 0});
  }

  const size_t host_idx = line_at_pos(pos);
  const line host = linelist_[host_idx];
  const size_t before = pos - host.beg;       // bytes of host line preceding pos
  const size_t after = host.length - before;  // bytes of host line at/after pos

  // Scan `data` and collect the end-offsets (within `data`) of every terminator.
  std::vector<size_t> term_ends;
  unsigned eol_mask = 0;  // bit 0: lf, bit 1: crlf, bit 2: cr
  bool pending_cr = false;
  size_t pending_cr_pos = 0;
  for (size_t i = 0; i < data.size(); ++i) {
    const char c = data[i];
    if (pending_cr) {
      if (c == '\n') {
        eol_mask |= 0b010;  // crlf
        term_ends.push_back(i + 1);
        pending_cr = false;
        continue;
      }
      eol_mask |= 0b100;  // bare cr
      term_ends.push_back(pending_cr_pos + 1);
      pending_cr = false;
      // fall through to reprocess the current byte
    }
    if (c == '\r') {
      pending_cr = true;
      pending_cr_pos = i;
    } else if (c == '\n') {
      eol_mask |= 0b001;  // lf
      term_ends.push_back(i + 1);
    }
  }
  if (pending_cr) {
    eol_mask |= 0b100;  // trailing bare cr at end of data
    term_ends.push_back(pending_cr_pos + 1);
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
    new_lines.push_back(line{.beg = host.beg, .length = before + term_ends.front()});
    // Middle replacements: one per additional terminator in data.
    for (size_t i = 1; i < term_ends.size(); ++i) {
      new_lines.push_back(line{.beg = host.beg + before + term_ends[i - 1],
                               .length = term_ends[i] - term_ends[i - 1]});
    }
    // Trailing replacement: bytes after the last terminator in data + host suffix.
    new_lines.push_back(line{.beg = host.beg + before + term_ends.back(),
                             .length = (data.size() - term_ends.back()) + after});

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

void linetable::erase(size_t pos, size_t length) {
  if (length == 0 || linelist_.empty()) {
    return;
  }

  const size_t doc_len = linelist_.back().beg + linelist_.back().length;
  if (pos >= doc_len) {
    return;  // erase starts at or beyond the end of the document: nothing to remove
  }

  const size_t erased = std::min(length, doc_len - pos);  // clamp the cut to the document
  const size_t end = pos + erased;                        // first byte kept after the cut

  if (erased == doc_len) {
    // The whole document was removed -> empty line table, matching rebuild() on "".
    linelist_.clear();
    return;
  }

  // The surviving content is the prefix of the first affected line (bytes before pos)
  // joined with the suffix of the line that contains `end` (bytes at/after end). Every
  // line strictly between them is consumed entirely; the join removes the terminators in
  // between, so those lines collapse into a single merged line.
  const size_t first_idx = line_at_pos(pos);
  const size_t tail_idx = line_at_pos(end);
  const line first = linelist_[first_idx];
  const line tail = linelist_[tail_idx];

  const size_t prefix_len = pos - first.beg;                 // kept bytes of the first line
  const size_t suffix_len = (tail.beg + tail.length) - end;  // kept bytes of the tail line

  const auto rm_begin = linelist_.begin() + first_idx;
  const auto rm_end = linelist_.begin() + tail_idx + 1;  // half-open

  if (prefix_len + suffix_len == 0) {
    // The cut left an empty line at the very end of the document. The previous line's
    // terminator now ends the document, so the empty trailing line is dropped (rebuild()
    // never emits one). Nothing follows it, so no position fix-up is required.
    linelist_.erase(rm_begin, rm_end);
    return;
  }

  const line merged{.beg = first.beg, .length = prefix_len + suffix_len};
  auto it = linelist_.erase(rm_begin, rm_end);
  linelist_.insert(it, merged);

  // Every line after the merged line slides left by the number of bytes removed.
  for (size_t i = first_idx + 1; i < linelist_.size(); ++i) {
    linelist_[i].beg -= erased;
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
