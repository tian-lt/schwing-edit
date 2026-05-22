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

namespace ut::host_undo_ut {

class fake_host : public swg::host {
 public:
  explicit fake_host(std::string fontpath, swg::eol mode = swg::eol::lf)
      : doc_(std::make_unique<swg::plaindoc>(this, std::move(fontpath), 12.0, mode)) {
    doc = doc_.get();
    viewport = {0, 0, 800, 600};
  }
  void on_invalidate(swg::rect) override {}
  swg::plaindoc* document() { return doc_.get(); }

 private:
  std::unique_ptr<swg::plaindoc> doc_;
};

class host_undo : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(swg::ut::arial_path())) {
      GTEST_SKIP() << "Arial not found at " << swg::ut::arial_path();
    }
    host_ = std::make_unique<fake_host>(swg::ut::arial_path());
  }
  std::unique_ptr<fake_host> host_;
};

TEST_F(host_undo, undo_empty_stack_is_noop) {
  EXPECT_FALSE(host_->can_undo());
  host_->undo();
  EXPECT_EQ(host_->all_text(), "");
}

TEST_F(host_undo, single_insert_then_undo) {
  host_->insert_char("a");
  EXPECT_EQ(host_->all_text(), "a");
  EXPECT_TRUE(host_->can_undo());
  host_->undo();
  EXPECT_EQ(host_->all_text(), "");
  EXPECT_EQ(host_->inspos(), 0u);
}

TEST_F(host_undo, consecutive_insertions_merge_into_one_step) {
  host_->insert_char("a");
  host_->insert_char("b");
  host_->insert_char("c");
  EXPECT_EQ(host_->all_text(), "abc");
  host_->undo();
  EXPECT_EQ(host_->all_text(), "");
  EXPECT_FALSE(host_->can_undo());
}

TEST_F(host_undo, caret_motion_breaks_merge_chain) {
  host_->insert_char("a");
  host_->insert_char("b");
  // Move caret without editing.
  host_->move_caret(swg::host::motion::char_left);
  host_->insert_char("c");
  // Doc is now "acb" (c inserted before b)
  EXPECT_EQ(host_->all_text(), "acb");
  host_->undo();  // undo "c" insert
  EXPECT_EQ(host_->all_text(), "ab");
  host_->undo();  // undo "ab" merged insert
  EXPECT_EQ(host_->all_text(), "");
}

TEST_F(host_undo, redo_reapplies_last_undone) {
  host_->insert_char("a");
  host_->insert_char("b");
  host_->undo();
  EXPECT_EQ(host_->all_text(), "");
  EXPECT_TRUE(host_->can_redo());
  host_->redo();
  EXPECT_EQ(host_->all_text(), "ab");
  EXPECT_EQ(host_->inspos(), 2u);
}

TEST_F(host_undo, new_edit_clears_redo_stack) {
  host_->insert_char("a");
  host_->undo();
  EXPECT_TRUE(host_->can_redo());
  host_->insert_char("b");
  EXPECT_FALSE(host_->can_redo());
  EXPECT_EQ(host_->all_text(), "b");
}

TEST_F(host_undo, undo_linefeed) {
  host_->insert_char("a");
  host_->linefeed();
  host_->insert_char("b");
  EXPECT_EQ(host_->all_text(), "a\nb");
  host_->undo();  // undo "b"
  EXPECT_EQ(host_->all_text(), "a\n");
  host_->undo();  // undo linefeed
  EXPECT_EQ(host_->all_text(), "a");
  host_->undo();  // undo "a"
  EXPECT_EQ(host_->all_text(), "");
}

TEST_F(host_undo, undo_backspace) {
  host_->insert_char("a");
  host_->insert_char("b");
  host_->erase_char();  // removes "b"
  EXPECT_EQ(host_->all_text(), "a");
  host_->undo();
  EXPECT_EQ(host_->all_text(), "ab");
  EXPECT_EQ(host_->inspos(), 2u);
}

TEST_F(host_undo, undo_delete_key) {
  host_->insert_char("a");
  host_->insert_char("b");
  host_->caret(0);
  host_->delete_char();  // removes "a"
  EXPECT_EQ(host_->all_text(), "b");
  host_->undo();
  EXPECT_EQ(host_->all_text(), "ab");
  EXPECT_EQ(host_->inspos(), 0u);
}

TEST_F(host_undo, undo_delete_selection) {
  host_->document()->insert(0, "abcdef");
  host_->caret(1);
  host_->set_selection_anchor(4);
  EXPECT_EQ(host_->selected_text(), "bcd");
  host_->delete_selection();
  EXPECT_EQ(host_->all_text(), "aef");
  host_->undo();
  EXPECT_EQ(host_->all_text(), "abcdef");
  // After undo, the original caret/anchor are restored.
  EXPECT_EQ(host_->inspos(), 1u);
  EXPECT_TRUE(host_->selection_anchor().has_value());
  EXPECT_EQ(host_->selection_anchor().value(), 4u);
}

TEST_F(host_undo, undo_replace_via_typing_over_selection) {
  host_->document()->insert(0, "abcdef");
  host_->caret(1);
  host_->set_selection_anchor(4);
  host_->insert_char("X");
  EXPECT_EQ(host_->all_text(), "aXef");
  host_->undo();
  EXPECT_EQ(host_->all_text(), "abcdef");
  EXPECT_EQ(host_->inspos(), 1u);
}

TEST_F(host_undo, undo_paste) {
  host_->document()->insert(0, "abc");
  host_->caret(2);
  host_->paste("XYZ");
  EXPECT_EQ(host_->all_text(), "abXYZc");
  host_->undo();
  EXPECT_EQ(host_->all_text(), "abc");
  EXPECT_EQ(host_->inspos(), 2u);
}

TEST_F(host_undo, redo_paste) {
  host_->document()->insert(0, "abc");
  host_->caret(2);
  host_->paste("XYZ");
  host_->undo();
  host_->redo();
  EXPECT_EQ(host_->all_text(), "abXYZc");
}

TEST_F(host_undo, clear_resets_stacks) {
  host_->insert_char("a");
  EXPECT_TRUE(host_->can_undo());
  host_->clear();
  EXPECT_FALSE(host_->can_undo());
  EXPECT_FALSE(host_->can_redo());
}

TEST_F(host_undo, load_text_resets_stacks) {
  host_->insert_char("a");
  host_->load_text("new content", swg::eol::lf);
  EXPECT_FALSE(host_->can_undo());
  EXPECT_FALSE(host_->can_redo());
}

TEST_F(host_undo, multiple_undos_walk_history) {
  host_->insert_char("a");
  host_->insert_char("b");
  // break merge
  host_->caret(host_->inspos());
  host_->insert_char("c");
  host_->insert_char("d");
  EXPECT_EQ(host_->all_text(), "abcd");
  host_->undo();  // undo "cd"
  EXPECT_EQ(host_->all_text(), "ab");
  host_->undo();  // undo "ab"
  EXPECT_EQ(host_->all_text(), "");
}

}  // namespace ut::host_undo_ut
