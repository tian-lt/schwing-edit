// std
#include <algorithm>
#include <array>
#include <iterator>
#include <ranges>
#include <stdexcept>
// swg
#include "linetable.hpp"
#include "piecetable.hpp"

namespace swg {

struct linetable::impl {
  // Walk pt bytes [pos, pos+n) into a fixed scratch buffer, calling `visit(b)` for each byte.
  template <class F>
  static void walk(const piecetable& pt, std::size_t pos, std::size_t n, F&& visit) {
    constexpr std::size_t kScratch = 4096;
    std::array<char, kScratch> buf{};
    std::size_t remaining = n;
    while (remaining > 0) {
      const std::size_t chunk = std::min(remaining, kScratch);
      pt.get_to(pos, chunk, std::span<char>(buf.data(), chunk));
      for (std::size_t i = 0; i < chunk; ++i) {
        visit(buf[i]);
      }
      pos += chunk;
      remaining -= chunk;
    }
  }

  // Build line entries from raw bytes via successive byte visits.
  // Tracks observed EOL kinds via bit flags in `kinds`:
  //   bit 0 = lf, bit 1 = crlf, bit 2 = cr.
  struct scanner {
    std::vector<line>& out;
    std::size_t base;        // byte offset to add to local offsets
    std::size_t cur_beg;     // current line begin (absolute)
    std::size_t cur_off;     // bytes counted in current line so far
    bool last_was_cr;
    std::uint8_t kinds;

    void byte(char raw) {
      const auto b = static_cast<unsigned char>(raw);
      if (last_was_cr) {
        last_was_cr = false;
        if (b == '\n') {
          // The \n is part of an existing CRLF; extend last line.
          out.back().length += 1;
          out.back().eol_bytes = 2;
          kinds |= 0b010;
          cur_beg = base + cur_off + 1;
          cur_off += 1;
          return;
        } else {
          // Pure CR
          kinds |= 0b100;
        }
      }
      if (b == '\n') {
        const std::size_t llen = (base + cur_off) - cur_beg + 1;
        out.push_back(line{.beg = cur_beg, .length = llen, .eol_bytes = 1});
        kinds |= 0b001;
        cur_beg = base + cur_off + 1;
        cur_off += 1;
      } else if (b == '\r') {
        // Tentatively emit as CR; if the next byte is \n we will upgrade.
        const std::size_t llen = (base + cur_off) - cur_beg + 1;
        out.push_back(line{.beg = cur_beg, .length = llen, .eol_bytes = 1});
        cur_beg = base + cur_off + 1;
        cur_off += 1;
        last_was_cr = true;
      } else {
        cur_off += 1;
      }
    }

    // Finalize: emit the trailing (possibly empty) line that has no terminator.
    void finalize_trailing(std::size_t doc_end) {
      if (last_was_cr) {
        kinds |= 0b100;
        last_was_cr = false;
      }
      // Emit trailing line if there are unterminated bytes OR if the document is
      // empty and we haven't pushed anything (caller handles empty doc separately).
      if (cur_beg < doc_end) {
        const std::size_t llen = doc_end - cur_beg;
        out.push_back(line{.beg = cur_beg, .length = llen, .eol_bytes = 0});
      } else if (cur_beg == doc_end && (!out.empty() && out.back().eol_bytes != 0)) {
        // Buffer ends with a terminator: a virtual empty line follows.
        out.push_back(line{.beg = doc_end, .length = 0, .eol_bytes = 0});
      }
    }
  };

  static eol pick_dominant(std::uint8_t kinds) noexcept {
    // Priority: crlf > lf > cr (per Notepad convention).
    if (kinds & 0b010) return eol::crlf;
    if (kinds & 0b001) return eol::lf;
    if (kinds & 0b100) return eol::cr;
    return eol::lf;
  }

  // Returns true if more than one EOL kind is set in `kinds`.
  static bool mixed(std::uint8_t kinds) noexcept {
    return (kinds & (kinds - 1)) != 0;
  }
};

bool linetable::rebuild(const piecetable& pt) {
  lines_.clear();
  doc_length_ = pt.length();
  impl::scanner s{.out = lines_,
                  .base = 0,
                  .cur_beg = 0,
                  .cur_off = 0,
                  .last_was_cr = false,
                  .kinds = 0};
  impl::walk(pt, 0, doc_length_, [&](char b) { s.byte(b); });
  s.finalize_trailing(doc_length_);
  if (lines_.empty()) {
    // Empty document still has one synthetic line {0,0,0}.
    lines_.push_back(line{0, 0, 0});
  }
  dominant_ = impl::pick_dominant(s.kinds);
  return impl::mixed(s.kinds);
}

namespace {

// Find the line index whose range [beg, beg+length) contains `pos`.
// If pos == doc length, returns the last index.
std::size_t find_line(const std::vector<line>& lines, std::size_t pos) {
  // upper_bound on beg, then back up by one.
  const auto it = std::ranges::upper_bound(
      lines, pos, std::less{}, [](const line& l) { return l.beg; });
  if (it == lines.begin()) return 0;
  return static_cast<std::size_t>(std::distance(lines.begin(), it) - 1);
}

}  // namespace

std::size_t linetable::line_at_pos(std::size_t pos) const {
  if (pos > doc_length_) {
    throw std::out_of_range{"linetable::line_at_pos pos out of range"};
  }
  if (lines_.empty()) return 0;
  return find_line(lines_, pos);
}

std::pair<std::size_t, std::size_t> linetable::line_col(std::size_t pos) const {
  const std::size_t idx = line_at_pos(pos);
  const auto l = at(idx);
  return {idx, pos - l.beg};
}

std::string_view linetable::eol_bytes_for(eol e) const noexcept {
  switch (e) {
    case eol::lf:
      return "\n";
    case eol::crlf:
      return "\r\n";
    case eol::cr:
      return "\r";
  }
  return "\n";
}

bool linetable::insert(const piecetable& pt, std::size_t pos, std::size_t n_inserted) {
  doc_length_ = pt.length();
  if (n_inserted == 0) return false;
  if (pos > doc_length_) {
    throw std::out_of_range{"linetable::insert pos out of range"};
  }
  // Strategy: rebuild a small window from the start of the line that contained
  // pos (extended one line back if it ends in \r so we can re-evaluate possible
  // \r\n merges) through one line past it (so CRLF merges at the right boundary
  // are also handled inside one scanner pass).
  if (lines_.empty()) {
    return rebuild(pt);
  }

  std::size_t affected_first = find_line(lines_, pos);
  if (affected_first > 0) {
    const auto& prev = lines_[affected_first - 1];
    if (prev.length > 0) {
      const auto byte = pt.get(prev.beg + prev.length - 1, 1);
      if (!byte.empty() && byte[0] == '\r') {
        affected_first -= 1;
      }
    }
  }
  std::size_t affected_last = std::min(affected_first + 2, lines_.size() - 1);

  const std::size_t scan_beg = lines_[affected_first].beg;
  // Old end-of-affected boundary; in new coords it shifts by +n_inserted.
  const std::size_t scan_end_old = lines_[affected_last].beg + lines_[affected_last].length;
  const std::size_t scan_end_new = scan_end_old + n_inserted;

  std::vector<line> rebuilt;
  rebuilt.reserve(8);
  impl::scanner s{.out = rebuilt,
                  .base = scan_beg,
                  .cur_beg = scan_beg,
                  .cur_off = 0,
                  .last_was_cr = false,
                  .kinds = 0};
  impl::walk(pt, scan_beg, scan_end_new - scan_beg, [&](char b) { s.byte(b); });
  s.finalize_trailing(scan_end_new);
  if (rebuilt.empty()) {
    rebuilt.push_back(line{scan_beg, 0, 0});
  }

  // Shift trailing lines by +n_inserted.
  for (std::size_t i = affected_last + 1; i < lines_.size(); ++i) {
    lines_[i].beg += n_inserted;
  }
  // Splice.
  const auto first_it = lines_.begin() + static_cast<std::ptrdiff_t>(affected_first);
  const auto last_it = lines_.begin() + static_cast<std::ptrdiff_t>(affected_last + 1);
  lines_.erase(first_it, last_it);
  lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(affected_first),
                rebuilt.begin(), rebuilt.end());

  if (lines_.empty()) {
    lines_.push_back(line{0, 0, 0});
  }

  // Update dominant eol if any new kinds were observed.
  const std::uint8_t new_kinds = s.kinds;
  if (new_kinds != 0) {
    std::uint8_t merged = new_kinds;
    switch (dominant_) {
      case eol::lf:
        merged |= 0b001;
        break;
      case eol::crlf:
        merged |= 0b010;
        break;
      case eol::cr:
        merged |= 0b100;
        break;
    }
    dominant_ = impl::pick_dominant(merged);
    return impl::mixed(merged);
  }
  return false;
}

bool linetable::erase(const piecetable& pt, std::size_t pos, std::size_t n_erased) {
  if (n_erased == 0) return false;
  doc_length_ = pt.length();
  if (pos > doc_length_) {
    throw std::out_of_range{"linetable::erase pos out of range"};
  }
  if (lines_.empty()) {
    return rebuild(pt);
  }

  // Find first line whose OLD range starts at or before pos.
  std::size_t affected_first = find_line(lines_, pos);
  if (affected_first > 0) {
    const auto& prev = lines_[affected_first - 1];
    if (prev.length > 0) {
      // Previous-line last byte in OLD coords sits at prev.beg + prev.length - 1.
      // In NEW coords (after erase), if that position is < pos it's unchanged.
      if (prev.beg + prev.length - 1 < pos) {
        const auto byte = pt.get(prev.beg + prev.length - 1, 1);
        if (!byte.empty() && byte[0] == '\r') {
          affected_first -= 1;
        }
      }
    }
  }

  // Find last line whose OLD range intersects [pos, pos + n_erased).
  std::size_t affected_last = affected_first;
  for (std::size_t i = affected_first; i < lines_.size(); ++i) {
    affected_last = i;
    if (lines_[i].beg + lines_[i].length >= pos + n_erased) break;
  }
  // Expand one more line past to catch CRLF merge across the erase boundary.
  if (affected_last + 1 < lines_.size()) affected_last += 1;

  const std::size_t scan_beg = lines_[affected_first].beg;
  const std::size_t old_end = lines_[affected_last].beg + lines_[affected_last].length;
  // old_end is in OLD coords; after erase it shifts by -n_erased (since old_end > pos).
  const std::size_t scan_end_new =
      (old_end >= n_erased) ? (old_end - n_erased) : 0;

  std::vector<line> rebuilt;
  rebuilt.reserve(8);
  impl::scanner s{.out = rebuilt,
                  .base = scan_beg,
                  .cur_beg = scan_beg,
                  .cur_off = 0,
                  .last_was_cr = false,
                  .kinds = 0};
  impl::walk(pt, scan_beg, scan_end_new - scan_beg, [&](char b) { s.byte(b); });
  s.finalize_trailing(scan_end_new);
  if (rebuilt.empty()) {
    rebuilt.push_back(line{scan_beg, 0, 0});
  }

  for (std::size_t i = affected_last + 1; i < lines_.size(); ++i) {
    lines_[i].beg -= n_erased;
  }
  const auto first_it = lines_.begin() + static_cast<std::ptrdiff_t>(affected_first);
  const auto last_it = lines_.begin() + static_cast<std::ptrdiff_t>(affected_last + 1);
  lines_.erase(first_it, last_it);
  lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(affected_first),
                rebuilt.begin(), rebuilt.end());

  if (lines_.empty()) {
    lines_.push_back(line{0, 0, 0});
  }

  const std::uint8_t new_kinds = s.kinds;
  return impl::mixed(new_kinds);
}

}  // namespace swg
