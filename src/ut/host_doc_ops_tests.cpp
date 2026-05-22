// std
#include <filesystem>
#include <memory>
#include <string>
// gtest
#include <gtest/gtest.h>
// swg
#include <plaindoc.hpp>
// ut
#include "font_env.hpp"

namespace ut::host_doc_ops_ut {

class fake_host : public swg::host {
 public:
  explicit fake_host(std::string fontpath, swg::eol mode = swg::eol::lf)
      : doc_(std::make_unique<swg::plaindoc>(this, std::move(fontpath), 12.0, mode)) {
    doc = doc_.get();
    viewport = {0, 0, 800, 600};
  }
  void on_invalidate(swg::rect) override { ++invalidates_; }
  swg::plaindoc* document() { return doc_.get(); }
  int invalidates_ = 0;

 private:
  std::unique_ptr<swg::plaindoc> doc_;
};

class host_doc_ops : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(swg::ut::arial_path())) {
      GTEST_SKIP() << "Arial not found at " << swg::ut::arial_path();
    }
    host_ = std::make_unique<fake_host>(swg::ut::arial_path());
  }
  std::unique_ptr<fake_host> host_;
};

TEST_F(host_doc_ops, clear_empty_doc) {
  EXPECT_EQ(host_->document()->length(), 0u);
  host_->clear();
  EXPECT_EQ(host_->document()->length(), 0u);
  EXPECT_EQ(host_->inspos(), 0u);
  EXPECT_FALSE(host_->has_selection());
}

TEST_F(host_doc_ops, clear_resets_caret_and_selection) {
  host_->document()->insert(0, "abcdef");
  host_->caret(3);
  host_->set_selection_anchor(0);
  host_->clear();
  EXPECT_EQ(host_->document()->length(), 0u);
  EXPECT_EQ(host_->inspos(), 0u);
  EXPECT_FALSE(host_->has_selection());
}

TEST_F(host_doc_ops, load_text_replaces_content) {
  host_->document()->insert(0, "old text");
  host_->load_text("brand new content", swg::eol::lf);
  EXPECT_EQ(host_->all_text(), "brand new content");
  EXPECT_EQ(host_->inspos(), 0u);
  EXPECT_FALSE(host_->has_selection());
}

TEST_F(host_doc_ops, load_text_switches_eol_mode) {
  host_->load_text("a\r\nb\r\nc", swg::eol::crlf);
  EXPECT_EQ(host_->document()->eol_mode(), swg::eol::crlf);
  EXPECT_EQ(host_->document()->lines().line_count(), 3u);
}

TEST_F(host_doc_ops, load_text_empty_is_well_defined) {
  host_->document()->insert(0, "abc");
  host_->load_text("", swg::eol::lf);
  EXPECT_EQ(host_->document()->length(), 0u);
  EXPECT_EQ(host_->inspos(), 0u);
}

TEST_F(host_doc_ops, all_text_returns_full_content) {
  host_->document()->insert(0, "line1\nline2\nline3");
  EXPECT_EQ(host_->all_text(), "line1\nline2\nline3");
}

TEST_F(host_doc_ops, set_eol_mode_changes_default_for_linefeed) {
  host_->set_eol_mode(swg::eol::crlf);
  host_->linefeed();
  EXPECT_EQ(host_->all_text(), "\r\n");
  EXPECT_EQ(host_->inspos(), 2u);
}

TEST_F(host_doc_ops, load_text_then_edit_uses_new_eol) {
  host_->load_text("abc", swg::eol::crlf);
  host_->caret(3);
  host_->linefeed();
  EXPECT_EQ(host_->all_text(), "abc\r\n");
}

TEST_F(host_doc_ops, load_text_invalidates_viewport) {
  host_->invalidates_ = 0;
  host_->load_text("hello", swg::eol::lf);
  EXPECT_GT(host_->invalidates_, 0);
}

// Dirty bit ----------------------------------------------------------------

TEST_F(host_doc_ops, is_dirty_starts_false) {
  EXPECT_FALSE(host_->is_dirty());
}

TEST_F(host_doc_ops, edits_set_dirty) {
  host_->insert_char("a");
  EXPECT_TRUE(host_->is_dirty());
}

TEST_F(host_doc_ops, clear_dirty_clears_flag) {
  host_->insert_char("a");
  host_->clear_dirty();
  EXPECT_FALSE(host_->is_dirty());
}

TEST_F(host_doc_ops, load_text_clears_dirty) {
  host_->insert_char("a");
  EXPECT_TRUE(host_->is_dirty());
  host_->load_text("brand new", swg::eol::lf);
  EXPECT_FALSE(host_->is_dirty());
}

TEST_F(host_doc_ops, clear_clears_dirty) {
  host_->insert_char("a");
  host_->clear();
  EXPECT_FALSE(host_->is_dirty());
}

TEST_F(host_doc_ops, undo_keeps_dirty_until_explicit_clear) {
  host_->insert_char("a");
  host_->undo();
  // Even though the doc is empty again, we still consider it dirty so the
  // caller doesn't lose the prompt-to-save behavior after an undo cycle.
  EXPECT_TRUE(host_->is_dirty());
}

// Line / column ------------------------------------------------------------

TEST_F(host_doc_ops, pos_to_linecol_empty_doc) {
  auto p = host_->pos_to_linecol(0);
  EXPECT_EQ(p.line, 1);
  EXPECT_EQ(p.column, 1);
}

TEST_F(host_doc_ops, pos_to_linecol_single_line) {
  host_->load_text("hello", swg::eol::lf);
  EXPECT_EQ(host_->pos_to_linecol(0).column, 1);
  EXPECT_EQ(host_->pos_to_linecol(0).line, 1);
  EXPECT_EQ(host_->pos_to_linecol(5).column, 6);
}

TEST_F(host_doc_ops, pos_to_linecol_multi_line_lf) {
  host_->load_text("ab\ncd\nef", swg::eol::lf);
  // 'a' on line 1 col 1, '\n' boundary -> 'c' is line 2 col 1.
  EXPECT_EQ(host_->pos_to_linecol(0).line, 1);
  EXPECT_EQ(host_->pos_to_linecol(2).line, 1);
  EXPECT_EQ(host_->pos_to_linecol(3).line, 2);
  EXPECT_EQ(host_->pos_to_linecol(3).column, 1);
  EXPECT_EQ(host_->pos_to_linecol(5).line, 2);
  EXPECT_EQ(host_->pos_to_linecol(6).line, 3);
}

TEST_F(host_doc_ops, pos_to_linecol_multibyte_column) {
  // "é" (UTF-8: c3 a9) followed by "x".
  host_->load_text("\xc3\xa9x", swg::eol::lf);
  // After the 2-byte 'é', column should be 2 (codepoint count).
  EXPECT_EQ(host_->pos_to_linecol(2).column, 2);
  EXPECT_EQ(host_->pos_to_linecol(3).column, 3);
}

}  // namespace ut::host_doc_ops_ut
