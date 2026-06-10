// std
#include <string>
#include <string_view>
#include <vector>
// deps
#include <gtest/gtest.h>
// swg
#include "plaindoc.hpp"

namespace swg::ut::plaindoc_ut {
namespace {

class capture_host : public host {
 public:
  std::size_t invalidate_count = 0;
  std::size_t doc_changed_count = 0;
  std::size_t caret_moved_count = 0;
  damage last_damage{};
  void on_invalidate() override { invalidate_count += 1; }
  void on_doc_changed(const damage& d) override {
    doc_changed_count += 1;
    last_damage = d;
  }
  void on_caret_moved() override { caret_moved_count += 1; }
};

TEST(plaindoc_basic, empty_document_has_one_line) {
  plaindoc doc;
  EXPECT_EQ(doc.length(), 0u);
  EXPECT_EQ(doc.lines().line_count(), 1u);
}

TEST(plaindoc_basic, load_text_and_view) {
  plaindoc doc;
  std::string s = "hello\nworld";
  doc.load_view(s);
  EXPECT_EQ(doc.length(), s.size());
  EXPECT_EQ(doc.lines().line_count(), 2u);
  doc.load_text("a\nb\nc");
  EXPECT_EQ(doc.length(), 5u);
  EXPECT_EQ(doc.lines().line_count(), 3u);
}

TEST(plaindoc_basic, insert_text_at_caret) {
  plaindoc doc;
  capture_host h;
  doc.attach_host(&h);
  doc.insert_text("hello");
  EXPECT_EQ(doc.length(), 5u);
  EXPECT_EQ(doc.caret(), 5u);
  EXPECT_EQ(doc.buffer().str(), "hello");
  EXPECT_GE(h.doc_changed_count, 1u);
}

TEST(plaindoc_basic, backspace_one_codepoint) {
  plaindoc doc;
  doc.insert_text("héllo");
  // length is 6 bytes; caret at 6.
  doc.backspace();
  EXPECT_EQ(doc.buffer().str(), "héll");
  // Now backspace through 'l', 'l', then 'é' (2 bytes).
  doc.backspace();
  doc.backspace();
  EXPECT_EQ(doc.buffer().str(), "hé");
  doc.backspace();
  EXPECT_EQ(doc.buffer().str(), "h");
}

TEST(plaindoc_basic, backspace_crlf_pair) {
  plaindoc doc;
  doc.set_document_eol(eol::crlf);
  doc.insert_text("a");
  doc.newline();  // inserts \r\n
  doc.insert_text("b");
  EXPECT_EQ(doc.buffer().str(), "a\r\nb");
  // caret is after b; backspace removes b
  doc.backspace();
  EXPECT_EQ(doc.buffer().str(), "a\r\n");
  // backspace at start of empty 2nd line removes \r\n together
  doc.backspace();
  EXPECT_EQ(doc.buffer().str(), "a");
}

TEST(plaindoc_basic, delete_forward) {
  plaindoc doc;
  doc.insert_text("abc");
  doc.set_caret(0, false);
  doc.delete_forward();
  EXPECT_EQ(doc.buffer().str(), "bc");
}

TEST(plaindoc_basic, newline_uses_document_eol) {
  plaindoc doc;
  doc.set_document_eol(eol::lf);
  doc.insert_text("a");
  doc.newline();
  doc.insert_text("b");
  EXPECT_EQ(doc.buffer().str(), "a\nb");

  plaindoc doc2;
  doc2.set_document_eol(eol::crlf);
  doc2.insert_text("a");
  doc2.newline();
  doc2.insert_text("b");
  EXPECT_EQ(doc2.buffer().str(), "a\r\nb");
}

TEST(plaindoc_motion, left_right_codepoint) {
  plaindoc doc;
  doc.insert_text("héllo");  // 6 bytes
  doc.set_caret(0, false);
  doc.move_caret(plaindoc::motion::right_char, false);  // past h
  EXPECT_EQ(doc.caret(), 1u);
  doc.move_caret(plaindoc::motion::right_char, false);  // past é (2 bytes)
  EXPECT_EQ(doc.caret(), 3u);
  doc.move_caret(plaindoc::motion::left_char, false);   // back over é
  EXPECT_EQ(doc.caret(), 1u);
}

TEST(plaindoc_motion, left_right_crlf_pair) {
  plaindoc doc;
  doc.set_document_eol(eol::crlf);
  doc.insert_text("a");
  doc.newline();
  doc.insert_text("b");  // "a\r\nb", caret at 4
  doc.set_caret(2, false);                              // between \r and \n
  // step right should jump over the LF to position 3
  doc.move_caret(plaindoc::motion::right_char, false);
  EXPECT_EQ(doc.caret(), 3u);
  doc.set_caret(3, false);
  doc.move_caret(plaindoc::motion::left_char, false);
  EXPECT_EQ(doc.caret(), 1u);
}

TEST(plaindoc_motion, line_up_down) {
  plaindoc doc;
  doc.load_text("hello\nworld\nfoo");
  doc.set_caret(7, false);  // line 1, col 1 ('o')
  doc.move_caret(plaindoc::motion::line_up, false);
  // line 0, col 1
  const auto [li, col] = doc.lines().line_col(doc.caret());
  EXPECT_EQ(li, 0u);
  EXPECT_EQ(col, 1u);
  doc.move_caret(plaindoc::motion::line_down, false);
  EXPECT_EQ(doc.caret(), 7u);
  doc.move_caret(plaindoc::motion::line_down, false);
  // line 2, col 1 — but if line 2 is "foo" (3 chars), col 1 = pos 13.
  EXPECT_EQ(doc.caret(), 13u);
}

TEST(plaindoc_motion, home_end) {
  plaindoc doc;
  doc.load_text("hello\nworld");
  doc.set_caret(3, false);
  doc.move_caret(plaindoc::motion::line_home, false);
  EXPECT_EQ(doc.caret(), 0u);
  doc.move_caret(plaindoc::motion::line_end, false);
  EXPECT_EQ(doc.caret(), 5u);
  doc.move_caret(plaindoc::motion::doc_end, false);
  EXPECT_EQ(doc.caret(), doc.length());
}

TEST(plaindoc_selection, selection_replace) {
  plaindoc doc;
  doc.load_text("hello world");
  doc.set_caret(0, false);
  doc.move_caret(plaindoc::motion::right_char, false);  // pos 1
  doc.move_caret(plaindoc::motion::right_char, true);   // select 'e'
  doc.move_caret(plaindoc::motion::right_char, true);   // select 'el'
  doc.move_caret(plaindoc::motion::right_char, true);   // select 'ell'
  doc.move_caret(plaindoc::motion::right_char, true);   // select 'ello'
  EXPECT_TRUE(doc.has_selection());
  doc.insert_text("ola");
  EXPECT_EQ(doc.buffer().str(), "hola world");
}

TEST(plaindoc_misc, loadview_then_edit_then_materialize) {
  std::string src = "hello world";
  plaindoc doc;
  doc.load_view(src);  // initbuf aliases src
  doc.set_caret(5, false);
  doc.insert_text(" brave");
  doc.materialize();
  // After materialize, src may be freed/changed; the doc must still be intact.
  src.assign(src.size(), 'X');
  EXPECT_EQ(doc.buffer().str(), "hello brave world");
}

}  // namespace
}  // namespace swg::ut::plaindoc_ut
