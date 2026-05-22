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

namespace ut::host_find_ut {

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

class host_find : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!std::filesystem::exists(swg::ut::arial_path())) {
      GTEST_SKIP() << "Arial not found at " << swg::ut::arial_path();
    }
    host_ = std::make_unique<fake_host>(swg::ut::arial_path());
  }
  std::unique_ptr<fake_host> host_;
};

TEST_F(host_find, find_simple_match) {
  host_->load_text("hello world", swg::eol::lf);
  EXPECT_TRUE(host_->find_text("world", true, true));
  ASSERT_TRUE(host_->has_selection());
  EXPECT_EQ(host_->selected_text(), "world");
}

TEST_F(host_find, find_returns_false_when_missing) {
  host_->load_text("hello world", swg::eol::lf);
  EXPECT_FALSE(host_->find_text("zzz", true, true));
}

TEST_F(host_find, find_case_insensitive) {
  host_->load_text("Hello WORLD", swg::eol::lf);
  EXPECT_TRUE(host_->find_text("world", false, true));
  EXPECT_EQ(host_->selected_text(), "WORLD");
}

TEST_F(host_find, find_advances_past_current_selection) {
  host_->load_text("abc abc abc", swg::eol::lf);
  // First match at 0.
  EXPECT_TRUE(host_->find_text("abc", true, true));
  EXPECT_EQ(host_->selection_range().first, 0u);
  // Second find should jump to next match at 4.
  EXPECT_TRUE(host_->find_text("abc", true, true));
  EXPECT_EQ(host_->selection_range().first, 4u);
  // Third find -> 8.
  EXPECT_TRUE(host_->find_text("abc", true, true));
  EXPECT_EQ(host_->selection_range().first, 8u);
  // Fourth find should wrap around to 0.
  EXPECT_TRUE(host_->find_text("abc", true, true));
  EXPECT_EQ(host_->selection_range().first, 0u);
}

TEST_F(host_find, find_backwards) {
  host_->load_text("aa bb aa bb", swg::eol::lf);
  host_->caret(host_->all_text().size());  // caret at end
  EXPECT_TRUE(host_->find_text("aa", true, false));
  EXPECT_EQ(host_->selection_range().first, 6u);  // matches second "aa"
  EXPECT_TRUE(host_->find_text("aa", true, false));
  EXPECT_EQ(host_->selection_range().first, 0u);  // matches first "aa"
}

TEST_F(host_find, find_empty_needle_returns_false) {
  host_->load_text("anything", swg::eol::lf);
  EXPECT_FALSE(host_->find_text("", true, true));
}

TEST_F(host_find, replace_text_when_selection_matches) {
  host_->load_text("foo bar foo", swg::eol::lf);
  // First select "foo" via find.
  ASSERT_TRUE(host_->find_text("foo", true, true));
  // Replace current and advance to next.
  EXPECT_TRUE(host_->replace_text("foo", "BAZ", true));
  EXPECT_EQ(host_->all_text(), "BAZ bar foo");
  // After replace_text, we should be positioned at the next match.
  ASSERT_TRUE(host_->has_selection());
  EXPECT_EQ(host_->selected_text(), "foo");
}

TEST_F(host_find, replace_text_no_match_returns_false) {
  host_->load_text("foo bar foo", swg::eol::lf);
  EXPECT_FALSE(host_->replace_text("xyz", "ABC", true));
}

TEST_F(host_find, replace_all_counts_and_substitutes) {
  host_->load_text("aaa bbb aaa ccc aaa", swg::eol::lf);
  size_t n = host_->replace_all("aaa", "Z", true);
  EXPECT_EQ(n, 3u);
  EXPECT_EQ(host_->all_text(), "Z bbb Z ccc Z");
}

TEST_F(host_find, replace_all_case_insensitive) {
  host_->load_text("Abc aBc abC", swg::eol::lf);
  size_t n = host_->replace_all("abc", "X", false);
  EXPECT_EQ(n, 3u);
  EXPECT_EQ(host_->all_text(), "X X X");
}

TEST_F(host_find, replace_all_undo_restores) {
  host_->load_text("aaa aaa aaa", swg::eol::lf);
  host_->replace_all("aaa", "X", true);
  EXPECT_EQ(host_->all_text(), "X X X");
  host_->undo();
  EXPECT_EQ(host_->all_text(), "aaa aaa aaa");
}

}  // namespace ut::host_find_ut
