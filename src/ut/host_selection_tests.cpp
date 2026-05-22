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

namespace ut::host_selection_ut {

class fake_host : public swg::host {
 public:
  explicit fake_host(std::string fontpath)
      : doc_(std::make_unique<swg::plaindoc>(this, std::move(fontpath), 12.0,
                                             swg::eol::lf)) {
    doc = doc_.get();
    viewport = {0, 0, 800, 600};
  }
  void on_invalidate(swg::rect) override {}
  swg::plaindoc* document() { return doc_.get(); }

 private:
  std::unique_ptr<swg::plaindoc> doc_;
};

class host_selection : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(swg::ut::arial_path())) {
      GTEST_SKIP() << "Arial not found at " << swg::ut::arial_path();
    }
    host_ = std::make_unique<fake_host>(swg::ut::arial_path());
  }
  std::unique_ptr<fake_host> host_;
};

TEST_F(host_selection, no_selection_by_default) {
  host_->document()->insert(0, "hello");
  EXPECT_FALSE(host_->has_selection());
  EXPECT_FALSE(host_->selection_anchor().has_value());
  EXPECT_EQ(host_->selected_text(), "");
  auto [b, e] = host_->selection_range();
  EXPECT_EQ(b, 0u);
  EXPECT_EQ(e, 0u);
}

TEST_F(host_selection, shift_move_caret_starts_anchor_then_extends) {
  host_->document()->insert(0, "hello");
  host_->caret(1);
  host_->shift_move_caret(swg::host::motion::char_right);
  EXPECT_TRUE(host_->has_selection());
  EXPECT_EQ(host_->selection_anchor().value(), 1u);
  EXPECT_EQ(host_->inspos(), 2u);
  EXPECT_EQ(host_->selected_text(), "e");
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->shift_move_caret(swg::host::motion::char_right);
  EXPECT_EQ(host_->selected_text(), "ell");
}

TEST_F(host_selection, shift_move_left_yields_reverse_selection) {
  host_->document()->insert(0, "abcdef");
  host_->caret(4);
  host_->shift_move_caret(swg::host::motion::char_left);
  host_->shift_move_caret(swg::host::motion::char_left);
  EXPECT_TRUE(host_->has_selection());
  EXPECT_EQ(host_->selection_anchor().value(), 4u);
  EXPECT_EQ(host_->inspos(), 2u);
  auto [b, e] = host_->selection_range();
  EXPECT_EQ(b, 2u);
  EXPECT_EQ(e, 4u);
  EXPECT_EQ(host_->selected_text(), "cd");
}

TEST_F(host_selection, move_caret_clears_anchor) {
  host_->document()->insert(0, "abcdef");
  host_->caret(1);
  host_->shift_move_caret(swg::host::motion::char_right);
  EXPECT_TRUE(host_->has_selection());
  host_->move_caret(swg::host::motion::char_right);
  EXPECT_FALSE(host_->has_selection());
  EXPECT_EQ(host_->inspos(), 3u);
}

TEST_F(host_selection, caret_sets_clears_anchor) {
  host_->document()->insert(0, "hello");
  host_->caret(1);
  host_->shift_move_caret(swg::host::motion::char_right);
  ASSERT_TRUE(host_->has_selection());
  host_->caret(0);
  EXPECT_FALSE(host_->has_selection());
}

TEST_F(host_selection, insert_char_replaces_selection) {
  host_->document()->insert(0, "abcdef");
  host_->caret(1);
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->shift_move_caret(swg::host::motion::char_right);
  // selection covers "bc"
  EXPECT_EQ(host_->selected_text(), "bc");
  host_->insert_char("X");
  EXPECT_EQ(host_->document()->get(0, host_->document()->length()), "aXdef");
  EXPECT_EQ(host_->inspos(), 2u);
  EXPECT_FALSE(host_->has_selection());
}

TEST_F(host_selection, linefeed_replaces_selection) {
  host_->document()->insert(0, "abcdef");
  host_->caret(1);
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->linefeed();
  EXPECT_EQ(host_->document()->get(0, host_->document()->length()), "a\ndef");
  EXPECT_EQ(host_->inspos(), 2u);
  EXPECT_FALSE(host_->has_selection());
}

TEST_F(host_selection, erase_char_deletes_selection_when_present) {
  host_->document()->insert(0, "abcdef");
  host_->caret(1);
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->erase_char();
  EXPECT_EQ(host_->document()->get(0, host_->document()->length()), "adef");
  EXPECT_EQ(host_->inspos(), 1u);
  EXPECT_FALSE(host_->has_selection());
}

TEST_F(host_selection, delete_char_deletes_selection_when_present) {
  host_->document()->insert(0, "abcdef");
  host_->caret(1);
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->delete_char();
  EXPECT_EQ(host_->document()->get(0, host_->document()->length()), "adef");
  EXPECT_EQ(host_->inspos(), 1u);
  EXPECT_FALSE(host_->has_selection());
}

TEST_F(host_selection, select_all) {
  host_->document()->insert(0, "abcdef");
  host_->select_all();
  EXPECT_TRUE(host_->has_selection());
  EXPECT_EQ(host_->selection_anchor().value(), 0u);
  EXPECT_EQ(host_->inspos(), 6u);
  EXPECT_EQ(host_->selected_text(), "abcdef");
}

TEST_F(host_selection, select_all_empty_doc) {
  host_->select_all();
  EXPECT_FALSE(host_->has_selection());
  EXPECT_EQ(host_->inspos(), 0u);
}

TEST_F(host_selection, paste_replaces_selection) {
  host_->document()->insert(0, "abcdef");
  host_->caret(1);
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->paste("XYZ");
  EXPECT_EQ(host_->document()->get(0, host_->document()->length()), "aXYZdef");
  EXPECT_EQ(host_->inspos(), 4u);
  EXPECT_FALSE(host_->has_selection());
}

TEST_F(host_selection, paste_with_no_selection_inserts_at_caret) {
  host_->document()->insert(0, "abcdef");
  host_->caret(2);
  host_->paste("XY");
  EXPECT_EQ(host_->document()->get(0, host_->document()->length()), "abXYcdef");
  EXPECT_EQ(host_->inspos(), 4u);
}

TEST_F(host_selection, paste_empty_collapses_selection_only) {
  host_->document()->insert(0, "abcdef");
  host_->caret(1);
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->shift_move_caret(swg::host::motion::char_right);
  host_->paste("");
  EXPECT_EQ(host_->document()->get(0, host_->document()->length()), "adef");
  EXPECT_EQ(host_->inspos(), 1u);
  EXPECT_FALSE(host_->has_selection());
}

TEST_F(host_selection, delete_selection_when_no_selection_is_noop) {
  host_->document()->insert(0, "abc");
  host_->caret(2);
  host_->delete_selection();
  EXPECT_EQ(host_->document()->get(0, host_->document()->length()), "abc");
  EXPECT_EQ(host_->inspos(), 2u);
}

TEST_F(host_selection, anchor_at_same_pos_is_no_selection) {
  host_->document()->insert(0, "abc");
  host_->caret(1);
  host_->set_selection_anchor(1);
  EXPECT_FALSE(host_->has_selection());
  EXPECT_EQ(host_->selected_text(), "");
}

TEST_F(host_selection, set_anchor_then_clear) {
  host_->document()->insert(0, "abc");
  host_->caret(1);
  host_->set_selection_anchor(0);
  EXPECT_TRUE(host_->has_selection());
  EXPECT_EQ(host_->selected_text(), "a");
  host_->set_selection_anchor(std::nullopt);
  EXPECT_FALSE(host_->has_selection());
}

}  // namespace ut::host_selection_ut
