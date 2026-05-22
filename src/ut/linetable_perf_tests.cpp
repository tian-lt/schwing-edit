// std
#include <chrono>
#include <string>
// gtest
#include <gtest/gtest.h>
// swg
#include "linetable.hpp"
#include "piecetable.hpp"

namespace swg::ut::linetable_perf_ut {

// Sentinel that exercises the incremental linetable::erase path against a
// large document. Before the incremental erase landed, `plaindoc::erase`
// fell back to `linetable::rebuild`, which is O(N) over the entire piece
// table on every call — typing or deleting in a 100k-line file ground to a
// halt. With the incremental erase, every operation is O(local span +
// trailing line count), so this whole test runs in tens of milliseconds.
//
// The threshold is intentionally generous (2s on Debug) so it acts as a
// regression sentinel rather than a tight timing assertion that would flake
// on busy CI machines.
TEST(linetable_perf_tests, many_erases_on_large_doc_finishes_quickly) {
  // Build a synthetic 100k-line document: each line is "abc\n" (4 bytes).
  // Total document size: 400 KB.
  constexpr size_t kLines = 100'000;
  std::string content;
  content.reserve(kLines * 4);
  for (size_t i = 0; i < kLines; ++i) {
    content += "abc\n";
  }

  piecetable ptable{content};
  linetable table{eol::lf};
  table.rebuild(ptable, eol::lf);
  ASSERT_EQ(table.line_count(), kLines);

  // Erase one byte near the top of the document, 1000 times in a row. Each
  // erase shifts the trailing ~100k lines' `.beg` values; before the fix it
  // also ran a full O(400KB) scan via rebuild, dominating the wall clock.
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  for (int i = 0; i < 1000; ++i) {
    ptable.erase(0, 1);
    table.erase(ptable, 0, 1);
  }
  const auto dt = clock::now() - t0;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(dt).count();

  // Sanity: 250 of those 1000 erases removed entire "abc\n" lines (every 4th
  // erase deletes the line's terminator). Line count should have dropped by
  // exactly 250.
  EXPECT_EQ(table.line_count(), kLines - 250);
  // Perf sentinel: 1000 single-byte erases on a 100k-line doc must finish
  // well under 2 seconds even on a Debug build. With the old O(N) rebuild
  // path this used to take double-digit seconds.
  EXPECT_LT(ms, 2000) << "linetable::erase regressed: 1000 single-byte erases"
                         " on a 100k-line doc took " << ms << " ms";
}

// Same sentinel but for `piecetable::length()` — used to be an O(piece-count)
// scan over `piecelist_`. After caching the running total it should be a
// trivial field read; we issue a few hundred thousand calls to confirm.
TEST(linetable_perf_tests, piecetable_length_is_constant_time) {
  piecetable p{};
  // Make ~5000 pieces by alternating inserts at the start and end.
  for (int i = 0; i < 2500; ++i) {
    p.insert(0, "x");
    p.insert(p.length(), "y");
  }
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  size_t sum = 0;
  for (int i = 0; i < 1'000'000; ++i) {
    sum += p.length();
  }
  const auto dt = clock::now() - t0;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(dt).count();
  EXPECT_GT(sum, 0u);  // keep the optimizer honest
  EXPECT_LT(ms, 500) << "piecetable::length() regressed: 1M reads took " << ms << " ms";
}

// Realistic scenario: open a ~1 MB document (~25k lines) and type 200 single
// characters near the middle. This exercises the full hot loop —
// piecetable::insert + linetable::insert + the post-edit length and
// line-lookup queries that the host calls on every keystroke. Acts as a
// regression sentinel for the typing/backspace path on "large" files.
TEST(linetable_perf_tests, typing_in_large_doc_is_fast) {
  // Build a ~1 MB doc: 25,000 lines of "Lorem ipsum dolor sit amet\n" (27 bytes).
  constexpr size_t kLines = 25'000;
  std::string content;
  content.reserve(kLines * 27);
  for (size_t i = 0; i < kLines; ++i) {
    content += "Lorem ipsum dolor sit amet\n";
  }

  piecetable ptable{content};
  linetable table{eol::lf};
  table.rebuild(ptable, eol::lf);

  // Type 200 chars near the middle of the document.
  const size_t mid = content.size() / 2;
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  for (int i = 0; i < 200; ++i) {
    const size_t pos = mid + static_cast<size_t>(i);
    ptable.insert(pos, "x");
    table.insert(pos, "x");
    // Simulate the host-side query bursts after each keystroke.
    (void)ptable.length();
    (void)table.line_at_pos(pos + 1);
  }
  const auto dt = clock::now() - t0;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(dt).count();
  // 200 keystrokes must fit comfortably under 1 second even on a Debug
  // build (≈5ms per keystroke worst-case); Release tends to be ~10× faster.
  EXPECT_LT(ms, 1000) << "typing in 1 MB doc regressed: 200 chars took " << ms << " ms";
}

}  // namespace swg::ut::linetable_perf_ut
