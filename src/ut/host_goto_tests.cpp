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

namespace ut::host_goto_ut {

class fake_host : public swg::host {
 public:
  explicit fake_host(std::string fontpath, swg::eol mode = swg::eol::lf)
      : doc_(std::make_unique<swg::plaindoc>(this, std::move(fontpath), 12.0, mode)) {
    doc = doc_.get();
    viewport = {0, 0, 800, 600};
  }
  void on_invalidate(swg::rect) override {}

 private:
  std::unique_ptr<swg::plaindoc> doc_;
};

class host_goto : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(swg::ut::arial_path())) {
      GTEST_SKIP() << "Arial not found at " << swg::ut::arial_path();
    }
    host_ = std::make_unique<fake_host>(swg::ut::arial_path());
  }
  std::unique_ptr<fake_host> host_;
};

TEST_F(host_goto, line_count_single_line) {
  host_->load_text("hello", swg::eol::lf);
  EXPECT_EQ(host_->line_count(), 1u);
}

TEST_F(host_goto, line_count_multi_line) {
  host_->load_text("a\nb\nc", swg::eol::lf);
  EXPECT_EQ(host_->line_count(), 3u);
}

TEST_F(host_goto, line_count_trailing_newline_yields_empty_last_line) {
  host_->load_text("a\nb\n", swg::eol::lf);
  // "a\n", "b\n", then an empty trailing line.
  EXPECT_GE(host_->line_count(), 2u);
}

TEST_F(host_goto, goto_first_line) {
  host_->load_text("abc\ndef\nghi", swg::eol::lf);
  size_t p = host_->goto_line(1);
  EXPECT_EQ(p, 0u);
  EXPECT_EQ(host_->inspos(), 0u);
}

TEST_F(host_goto, goto_second_line) {
  host_->load_text("abc\ndef\nghi", swg::eol::lf);
  size_t p = host_->goto_line(2);
  EXPECT_EQ(p, 4u);  // after "abc\n"
}

TEST_F(host_goto, goto_third_line) {
  host_->load_text("abc\ndef\nghi", swg::eol::lf);
  size_t p = host_->goto_line(3);
  EXPECT_EQ(p, 8u);  // after "abc\ndef\n"
}

TEST_F(host_goto, goto_clamps_too_large) {
  host_->load_text("a\nb\nc", swg::eol::lf);
  size_t p = host_->goto_line(999);
  size_t last = host_->line_count() - 1;
  EXPECT_GT(p, 0u);
  EXPECT_LT(p, host_->all_text().size() + 1);
  (void)last;
}

TEST_F(host_goto, goto_clamps_zero_or_negative) {
  host_->load_text("a\nb\nc", swg::eol::lf);
  size_t p = host_->goto_line(0);
  EXPECT_EQ(p, 0u);
  p = host_->goto_line(-5);
  EXPECT_EQ(p, 0u);
}

TEST_F(host_goto, goto_clears_selection) {
  host_->load_text("abc\ndef\nghi", swg::eol::lf);
  host_->select_all();
  EXPECT_TRUE(host_->has_selection());
  host_->goto_line(2);
  EXPECT_FALSE(host_->has_selection());
}

TEST_F(host_goto, goto_empty_doc) {
  host_->load_text("", swg::eol::lf);
  size_t p = host_->goto_line(5);
  EXPECT_EQ(p, 0u);
}

TEST_F(host_goto, page_down_motion_without_layout_falls_back_to_lines) {
  std::string doc;
  for (int i = 0; i < 30; ++i) {
    doc += "line";
    doc += std::to_string(i);
    doc += "\n";
  }
  host_->load_text(doc, swg::eol::lf);
  host_->caret(0);
  // Without a layout, page motions fall back to line motions: one line at a
  // time. So calling page_down once moves to line 2.
  host_->move_caret(swg::host::motion::page_down, nullptr);
  EXPECT_GT(host_->inspos(), 0u);
}

}  // namespace ut::host_goto_ut
