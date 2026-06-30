// std
#include <limits>
#include <string>
#include <string_view>

// gtest
#include <gtest/gtest.h>

// schwing
#include "linetable.hpp"
#include "plaindoc.hpp"

namespace swg::ut::host_ut {

namespace {

// A host that bypasses the GL setup (host::set) by assigning the document
// directly, and records the UI callbacks so tests can assert on them.
struct recording_host : swg::host {
  int invalidates = 0;
  int vscrolls = 0;
  vscroll_state last{};

  void on_invalidate() override { ++invalidates; }
  void on_vscroll(vscroll_state state) override {
    last = state;
    ++vscrolls;
  }

  void open(plaindoc* d, int w, int h) {
    doc = d;
    resize(w, h);
  }
  void reset_counts() {
    invalidates = 0;
    vscrolls = 0;
  }
};

// fontsize 12 @ 96 dpi => line_height = ceil(12 * 1.5) = 18.
// height 180 => exactly 10 visible (page) lines.
constexpr double font_size = 12.0;
constexpr int page = 10;
constexpr int view_w = 800;
constexpr int view_h = 180;

// Build "L0\nL1\n...\nL{n-1}" (no trailing newline) so the line count is exactly n.
std::string make_lines(int n) {
  std::string s;
  for (int i = 0; i < n; ++i) {
    if (i != 0) {
      s += '\n';
    }
    s += "L" + std::to_string(i);
  }
  return s;
}

std::string repeat(std::string_view unit, int n) {
  std::string s;
  s.reserve(unit.size() * (size_t)n);
  for (int i = 0; i < n; ++i) {
    s += unit;
  }
  return s;
}

}  // namespace

// ===-------------
// line/page metrics
TEST(host_metrics_tests, empty_doc_has_no_lines) {
  plaindoc pd{font_size, eol::lf};
  recording_host h;
  h.open(&pd, view_w, view_h);
  EXPECT_EQ(h.line_count(), 0);
  EXPECT_EQ(h.top_line(), 0);
}

TEST(host_metrics_tests, counts_lines_without_trailing_newline) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(3));
  recording_host h;
  h.open(&pd, view_w, view_h);
  EXPECT_EQ(h.line_count(), 3);
}

TEST(host_metrics_tests, trailing_newline_adds_empty_line) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, "L0\nL1\n");
  recording_host h;
  h.open(&pd, view_w, view_h);
  EXPECT_EQ(h.line_count(), 3);  // L0, L1, ""
}

TEST(host_metrics_tests, page_lines_follow_viewport_height) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(100));
  recording_host h;
  h.open(&pd, view_w, view_h);
  EXPECT_EQ(h.page_lines(), page);
  h.resize(view_w, view_h * 2);
  EXPECT_EQ(h.page_lines(), page * 2);
}

// ===----------------------
// vscroll_state notification
TEST(host_vscroll_tests, single_line_has_no_scroll_range) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, "only one line");
  recording_host h;
  h.open(&pd, view_w, view_h);
  EXPECT_EQ(h.line_count(), 1);
  EXPECT_EQ(h.last.total_lines, 1);
  EXPECT_EQ(h.last.page_lines, page);
  EXPECT_EQ(h.last.top_line, 0);
  EXPECT_EQ(h.last.max_top_line, 0);
}

TEST(host_vscroll_tests, fitting_multiline_allows_scroll_beyond_last) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(3));  // fits within 10 page lines
  recording_host h;
  h.open(&pd, view_w, view_h);
  EXPECT_EQ(h.last.total_lines, 3);
  EXPECT_EQ(h.last.max_top_line, 2);  // last line can reach the top
}

TEST(host_vscroll_tests, overflow_max_top_is_last_line) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(50));
  recording_host h;
  h.open(&pd, view_w, view_h);
  EXPECT_EQ(h.last.total_lines, 50);
  EXPECT_EQ(h.last.max_top_line, 49);
}

// ===-----------
// scroll_to_line
TEST(host_scroll_tests, scroll_within_range) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(50));
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.scroll_to_line(5);
  EXPECT_EQ(h.top_line(), 5);
  EXPECT_EQ(h.last.top_line, 5);
}

TEST(host_scroll_tests, scroll_clamps_high_and_low) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(50));
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.scroll_to_line(1000);
  EXPECT_EQ(h.top_line(), 49);  // clamped to max_top_line
  h.scroll_to_line(-10);
  EXPECT_EQ(h.top_line(), 0);
}

TEST(host_scroll_tests, redundant_scroll_is_a_no_op) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(50));
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.scroll_to_line(7);
  h.reset_counts();
  h.scroll_to_line(7);  // same position: no notify, no redraw
  EXPECT_EQ(h.vscrolls, 0);
  EXPECT_EQ(h.invalidates, 0);
}

TEST(host_scroll_tests, resize_keeps_valid_top_and_reclamps) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(50));
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.scroll_to_line(49);
  EXPECT_EQ(h.top_line(), 49);
  h.resize(view_w, view_h * 2);  // page doubles, max_top_line stays 49
  EXPECT_EQ(h.page_lines(), page * 2);
  EXPECT_EQ(h.top_line(), 49);
}

// ===-----------------
// caret get/set mapping
TEST(host_caret_tests, default_caret_is_origin) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(5));
  recording_host h;
  h.open(&pd, view_w, view_h);
  EXPECT_EQ(h.caret().line, 0);
  EXPECT_EQ(h.caret().column, 0);
}

TEST(host_caret_tests, set_get_roundtrip_ascii) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, "hello\nworld\nfoo");
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.caret({.line = 1, .column = 3});
  EXPECT_EQ(h.caret().line, 1);
  EXPECT_EQ(h.caret().column, 3);
}

TEST(host_caret_tests, column_clamps_to_line_end_excluding_eol) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, "ab\nlongerline");
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.caret({.line = 0, .column = 100});
  EXPECT_EQ(h.caret().line, 0);
  EXPECT_EQ(h.caret().column, 2);  // "ab" -> 2 columns, the \n is excluded
}

TEST(host_caret_tests, line_clamps_to_last) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(5));
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.caret({.line = 999, .column = 0});
  EXPECT_EQ(h.caret().line, 4);
}

TEST(host_caret_tests, columns_count_codepoints_not_bytes) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, "x\n" "\xC3\xA9\xC3\xA9" "z");  // line 1 = "ééz", é = 2 bytes
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.caret({.line = 1, .column = 2});  // after the two é
  EXPECT_EQ(h.caret().line, 1);
  EXPECT_EQ(h.caret().column, 2);
  h.caret({.line = 1, .column = 100});  // end of "ééz" = 3 codepoints
  EXPECT_EQ(h.caret().column, 3);
}

// ===------------------------
// caret-follow (auto scrolling)
TEST(host_caret_tests, caret_below_view_scrolls_down) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(50));
  recording_host h;
  h.open(&pd, view_w, view_h);  // top 0, page 10
  h.caret({.line = 30, .column = 0});
  EXPECT_EQ(h.top_line(), 30 - page + 1);  // 21: caret on last visible row
}

TEST(host_caret_tests, caret_above_view_scrolls_up) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(50));
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.scroll_to_line(30);
  h.caret({.line = 5, .column = 0});
  EXPECT_EQ(h.top_line(), 5);  // caret on top row
}

TEST(host_caret_tests, caret_within_view_does_not_scroll) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(50));
  recording_host h;
  h.open(&pd, view_w, view_h);  // top 0, page 10
  h.caret({.line = 5, .column = 0});
  EXPECT_EQ(h.top_line(), 0);
}

TEST(host_caret_tests, jump_to_end_puts_last_line_at_bottom) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, make_lines(50));
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.caret({.line = h.line_count() - 1, .column = std::numeric_limits<int>::max()});
  EXPECT_EQ(h.caret().line, 49);
  EXPECT_EQ(h.top_line(), 49 - page + 1);  // 40
}

// ===-----------
// editing + caret
TEST(host_edit_tests, typing_builds_line_and_advances_caret) {
  plaindoc pd{font_size, eol::lf};
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.insert_char("a");
  h.insert_char("b");
  EXPECT_EQ(h.line_count(), 1);
  EXPECT_EQ(h.caret().column, 2);
}

TEST(host_edit_tests, linefeed_starts_new_line) {
  plaindoc pd{font_size, eol::lf};
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.insert_char("a");
  h.linefeed();
  EXPECT_EQ(h.line_count(), 2);
  EXPECT_EQ(h.caret().line, 1);
  EXPECT_EQ(h.caret().column, 0);
}

TEST(host_edit_tests, backspace_removes_whole_codepoint) {
  plaindoc pd{font_size, eol::lf};
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.insert_char("a");
  h.insert_char("\xC3\xA9");  // é
  EXPECT_EQ(h.caret().column, 2);
  h.erase_char();
  EXPECT_EQ(h.caret().column, 1);
  EXPECT_EQ(pd.get(0, pd.length()), "a");
}

TEST(host_edit_tests, typing_past_viewport_autoscrolls) {
  plaindoc pd{font_size, eol::lf};
  recording_host h;
  h.open(&pd, view_w, view_h);  // page 10
  for (int i = 0; i < 20; ++i) {
    h.linefeed();
  }
  EXPECT_EQ(h.caret().line, 20);
  EXPECT_EQ(h.top_line(), 20 - page + 1);  // 11
}

// ===-------------------------------------------------------------------
// chunk-boundary corner cases. set_caret/caret_docpos read the line in
// 256-byte chunks, so these lines exceed 256 bytes and place codepoints
// straddling the boundary to exercise the cross-chunk re-read path.
TEST(host_chunk_tests, ascii_column_across_chunk_boundary) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, std::string(300, 'a'));  // single 300-char line
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.caret({.line = 0, .column = 260});
  EXPECT_EQ(h.caret().column, 260);
  h.insert_char("X");
  EXPECT_EQ(pd.get(259, 3), "aXa");  // X landed exactly at byte 260
}

TEST(host_chunk_tests, ascii_column_exactly_at_chunk_size) {
  plaindoc pd{font_size, eol::lf};
  pd.insert(0, std::string(300, 'a'));
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.caret({.line = 0, .column = 256});
  EXPECT_EQ(h.caret().column, 256);
  h.insert_char("X");
  EXPECT_EQ(pd.get(255, 3), "aXa");
}

TEST(host_chunk_tests, two_byte_codepoint_straddles_boundary) {
  plaindoc pd{font_size, eol::lf};
  // é (0xC3 0xA9) occupies bytes 255 and 256, spanning the chunk boundary.
  std::string line = std::string(255, 'a') + "\xC3\xA9" + std::string(10, 'z');
  pd.insert(0, line);
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.caret({.line = 0, .column = 256});  // just after é
  EXPECT_EQ(h.caret().column, 256);
  h.insert_char("X");
  EXPECT_EQ(pd.get(255, 3), "\xC3\xA9" "X");  // é then X
}

TEST(host_chunk_tests, three_byte_codepoint_straddles_boundary) {
  plaindoc pd{font_size, eol::lf};
  // € (0xE2 0x82 0xAC) occupies bytes 254, 255, 256: two bytes land in the
  // first chunk and the third in the next, forcing a full re-read.
  std::string line = std::string(254, 'a') + "\xE2\x82\xAC" + std::string(10, 'z');
  pd.insert(0, line);
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.caret({.line = 0, .column = 255});  // just after €
  EXPECT_EQ(h.caret().column, 255);
  h.insert_char("X");
  EXPECT_EQ(pd.get(254, 4), "\xE2\x82\xAC" "X");  // € then X
}

TEST(host_chunk_tests, getter_counts_codepoints_across_chunks) {
  plaindoc pd{font_size, eol::lf};
  std::string line = "a" + repeat("\xC3\xA9", 200);  // 1 + 400 bytes; é straddles boundary
  pd.insert(0, line);
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.caret({.line = 0, .column = std::numeric_limits<int>::max()});  // end of line
  EXPECT_EQ(h.caret().column, 201);  // 'a' + 200 é
}

TEST(host_chunk_tests, roundtrip_columns_across_chunks) {
  plaindoc pd{font_size, eol::lf};
  std::string line = "a" + repeat("\xC3\xA9", 200);
  pd.insert(0, line);
  recording_host h;
  h.open(&pd, view_w, view_h);
  for (int col : {0, 1, 127, 128, 129, 150, 200, 201}) {
    h.caret({.line = 0, .column = col});
    EXPECT_EQ(h.caret().column, col) << "column " << col;
  }
}

TEST(host_chunk_tests, end_of_long_crlf_line_excludes_terminator) {
  plaindoc pd{font_size, eol::crlf};
  pd.insert(0, std::string(300, 'a') + "\r\nnext");
  recording_host h;
  h.open(&pd, view_w, view_h);
  h.caret({.line = 0, .column = std::numeric_limits<int>::max()});
  EXPECT_EQ(h.caret().line, 0);
  EXPECT_EQ(h.caret().column, 300);  // 300 a's, the \r\n excluded
}

}  // namespace swg::ut::host_ut
