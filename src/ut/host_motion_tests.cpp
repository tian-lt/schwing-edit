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

namespace ut::host_motion_ut {

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

class host_motion : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(swg::ut::arial_path())) {
      GTEST_SKIP() << "Arial not found at " << swg::ut::arial_path();
    }
    host_ = std::make_unique<fake_host>(swg::ut::arial_path());
  }

  std::unique_ptr<fake_host> host_;
};

TEST_F(host_motion, char_right_steps_one_codepoint) {
  host_->insert_char("a");
  host_->insert_char("b");
  host_->insert_char("c");
  host_->caret(0);
  host_->move_caret(swg::host::motion::char_right);
  EXPECT_EQ(host_->inspos(), 1u);
  host_->move_caret(swg::host::motion::char_right);
  EXPECT_EQ(host_->inspos(), 2u);
}

TEST_F(host_motion, char_left_steps_one_codepoint) {
  host_->insert_char("a");
  host_->insert_char("b");
  host_->insert_char("c");
  EXPECT_EQ(host_->inspos(), 3u);
  host_->move_caret(swg::host::motion::char_left);
  EXPECT_EQ(host_->inspos(), 2u);
  host_->move_caret(swg::host::motion::char_left);
  EXPECT_EQ(host_->inspos(), 1u);
}

TEST_F(host_motion, char_right_treats_crlf_as_unit) {
  host_->insert_char("a");
  // Manually emit \r\n by directly inserting (linefeed() would honor doc EOL).
  host_->document()->insert(host_->inspos(), "\r\n");
  host_->caret(host_->inspos() + 2);
  host_->insert_char("b");
  host_->caret(1);  // just after 'a', before \r
  host_->move_caret(swg::host::motion::char_right);
  // Should land just past \r\n, before 'b'.
  EXPECT_EQ(host_->inspos(), 3u);
}

TEST_F(host_motion, char_left_treats_crlf_as_unit) {
  host_->insert_char("a");
  host_->document()->insert(host_->inspos(), "\r\n");
  host_->caret(host_->inspos() + 2);
  host_->insert_char("b");
  host_->caret(3);  // just before 'b', past \r\n
  host_->move_caret(swg::host::motion::char_left);
  EXPECT_EQ(host_->inspos(), 1u);
}

TEST_F(host_motion, char_left_at_zero_is_noop) {
  host_->insert_char("a");
  host_->caret(0);
  host_->move_caret(swg::host::motion::char_left);
  EXPECT_EQ(host_->inspos(), 0u);
}

TEST_F(host_motion, char_right_at_end_is_noop) {
  host_->insert_char("x");
  EXPECT_EQ(host_->inspos(), 1u);
  host_->move_caret(swg::host::motion::char_right);
  EXPECT_EQ(host_->inspos(), 1u);
}

TEST_F(host_motion, line_home_goes_to_line_start) {
  host_->insert_char("a");
  host_->insert_char("b");
  host_->insert_char("c");
  host_->linefeed();
  host_->insert_char("d");
  host_->insert_char("e");
  // Caret is on line 2 at byte 5 ("ab c\nde", caret after 'e').
  host_->move_caret(swg::host::motion::line_home);
  // Line 2 starts at byte 4 (after the '\n').
  EXPECT_EQ(host_->inspos(), 4u);
}

TEST_F(host_motion, line_end_goes_before_eol) {
  host_->insert_char("a");
  host_->insert_char("b");
  host_->linefeed();
  host_->insert_char("c");
  host_->caret(0);
  host_->move_caret(swg::host::motion::line_end);
  // First line is "ab\n" — end is at byte 2 (before \n).
  EXPECT_EQ(host_->inspos(), 2u);
}

TEST_F(host_motion, doc_home_doc_end) {
  host_->insert_char("a");
  host_->insert_char("b");
  host_->insert_char("c");
  host_->move_caret(swg::host::motion::doc_home);
  EXPECT_EQ(host_->inspos(), 0u);
  host_->move_caret(swg::host::motion::doc_end);
  EXPECT_EQ(host_->inspos(), 3u);
}

TEST_F(host_motion, line_down_then_line_up_with_layout) {
  host_->insert_char("a");
  host_->insert_char("b");
  host_->insert_char("c");
  host_->linefeed();
  host_->insert_char("d");
  host_->insert_char("e");
  host_->insert_char("f");

  host_->caret(1);  // between 'a' and 'b' on line 1
  auto layout = host_->render({0, 0, 800, 600});
  host_->move_caret(swg::host::motion::line_down, &layout);
  // We should now be roughly between 'd' and 'e' on line 2.
  EXPECT_EQ(host_->inspos(), 5u);

  layout = host_->render({0, 0, 800, 600});
  host_->move_caret(swg::host::motion::line_up, &layout);
  EXPECT_EQ(host_->inspos(), 1u);
}

TEST_F(host_motion, hit_test_picks_nearest_caret) {
  host_->insert_char("a");
  host_->insert_char("b");
  host_->insert_char("c");
  auto layout = host_->render({0, 0, 800, 600});
  ASSERT_FALSE(layout.carets.empty());
  // Click way out to the right: should snap to the last caret.
  size_t pos = swg::host::hit_test(layout, 10000.0f, layout.carets.front().baseline_y);
  EXPECT_EQ(pos, 3u);
  // Click at origin: should snap to the first caret.
  pos = swg::host::hit_test(layout, 0.0f, layout.carets.front().baseline_y);
  EXPECT_EQ(pos, 0u);
}

}  // namespace ut::host_motion_ut
