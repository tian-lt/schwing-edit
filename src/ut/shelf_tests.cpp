// std
#include <memory>
#include <utility>

// gtest
#include <gtest/gtest.h>

// schwing
#include "shelf.hpp"

namespace swg::ut {

TEST(shelf_tests, basic) {
  shelfset<int, int> s{20, 30};
  EXPECT_EQ(s.try_get_view(0), nullptr);
  EXPECT_EQ(s.try_put(0, 0, 10, 10), nullptr);
  s.add_shelf();
  EXPECT_NE(s.try_put(0, 0, 10, 10), nullptr);
  auto cell = s.try_get_view(0);
  EXPECT_NE(cell, nullptr);
}

TEST(shelf_tests, get_view_returns_nullptr_for_missing_key) {
  shelfset<int, int> s{20, 30};
  EXPECT_EQ(s.try_get_view(42), nullptr);
  s.add_shelf();
  EXPECT_EQ(s.try_get_view(42), nullptr);
}

TEST(shelf_tests, put_without_any_shelf_returns_nullptr) {
  shelfset<int, int> s{20, 30};
  EXPECT_EQ(s.try_put(1, 7, 5, 5), nullptr);
  EXPECT_EQ(s.try_get_view(1), nullptr);
}

TEST(shelf_tests, put_rejects_item_wider_than_set) {
  shelfset<int, int> s{20, 30};
  s.add_shelf();
  // w + padding (1) must fit in width_
  EXPECT_EQ(s.try_put(1, 0, 20, 5), nullptr);
  EXPECT_EQ(s.try_put(2, 0, 21, 5), nullptr);
}

TEST(shelf_tests, put_rejects_item_taller_than_set) {
  shelfset<int, int> s{20, 30};
  s.add_shelf();
  EXPECT_EQ(s.try_put(1, 0, 5, 30), nullptr);
  EXPECT_EQ(s.try_put(2, 0, 5, 31), nullptr);
}

TEST(shelf_tests, first_put_is_placed_at_origin) {
  shelfset<int, int> s{20, 30};
  s.add_shelf();
  auto* c = s.try_put(1, 99, 5, 10);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->shelf_index, 0u);
  EXPECT_EQ(c->x, 0u);
  EXPECT_EQ(c->y, 0u);
  EXPECT_EQ(c->w, 5u);
  EXPECT_EQ(c->h, 10u);
  EXPECT_EQ(c->payload, 99);
}

TEST(shelf_tests, get_view_returns_same_cell_as_put) {
  shelfset<int, int> s{20, 30};
  s.add_shelf();
  auto* p = s.try_put(7, 123, 5, 5);
  ASSERT_NE(p, nullptr);
  const auto* v = s.try_get_view(7);
  EXPECT_EQ(v, p);
  EXPECT_EQ(v->payload, 123);
}

TEST(shelf_tests, second_put_in_same_row_advances_x_by_w_plus_padding) {
  shelfset<int, int> s{20, 30};
  s.add_shelf();
  auto* c1 = s.try_put(1, 1, 5, 10);
  auto* c2 = s.try_put(2, 2, 4, 10);
  ASSERT_NE(c1, nullptr);
  ASSERT_NE(c2, nullptr);
  EXPECT_EQ(c1->shelf_index, 0u);
  EXPECT_EQ(c1->x, 0u);
  EXPECT_EQ(c1->y, 0u);
  EXPECT_EQ(c2->shelf_index, 0u);
  EXPECT_EQ(c2->x, 6u);  // 5 + padding(1)
  EXPECT_EQ(c2->y, 0u);
}

TEST(shelf_tests, row_height_tolerance_reuses_existing_row) {
  // existing row h=10; new item h such that h<=10<=h*3/2  -> e.g. h=7 (7<=10<=10)
  shelfset<int, int> s{30, 50};
  s.add_shelf();
  auto* c1 = s.try_put(1, 1, 5, 10);
  auto* c2 = s.try_put(2, 2, 5, 7);
  ASSERT_NE(c1, nullptr);
  ASSERT_NE(c2, nullptr);
  EXPECT_EQ(c2->y, 0u);
  EXPECT_EQ(c2->x, 6u);
}

TEST(shelf_tests, item_taller_than_row_creates_new_row) {
  shelfset<int, int> s{30, 50};
  s.add_shelf();
  auto* c1 = s.try_put(1, 1, 5, 5);
  auto* c2 = s.try_put(2, 2, 5, 10);  // 10 > 5 -> doesn't fit row
  ASSERT_NE(c1, nullptr);
  ASSERT_NE(c2, nullptr);
  EXPECT_EQ(c2->x, 0u);
  EXPECT_EQ(c2->y, 6u);  // 5 + padding(1)
}

TEST(shelf_tests, item_much_shorter_than_row_creates_new_row) {
  // row h=10, new h=4: 10 <= 4*3/2(=6)? no -> new row
  shelfset<int, int> s{30, 50};
  s.add_shelf();
  auto* c1 = s.try_put(1, 1, 5, 10);
  auto* c2 = s.try_put(2, 2, 5, 4);
  ASSERT_NE(c1, nullptr);
  ASSERT_NE(c2, nullptr);
  EXPECT_EQ(c2->x, 0u);
  EXPECT_EQ(c2->y, 11u);  // 10 + padding(1)
}

TEST(shelf_tests, full_row_creates_new_row_in_same_shelf) {
  // width=20, padding=1. After first put of w=10: cursor_x=11.
  // Second put of w=10: 11+10+1=22 > 20 -> doesn't fit row -> new row.
  shelfset<int, int> s{20, 50};
  s.add_shelf();
  auto* c1 = s.try_put(1, 1, 10, 5);
  auto* c2 = s.try_put(2, 2, 10, 5);
  ASSERT_NE(c1, nullptr);
  ASSERT_NE(c2, nullptr);
  EXPECT_EQ(c1->x, 0u);
  EXPECT_EQ(c1->y, 0u);
  EXPECT_EQ(c2->x, 0u);
  EXPECT_EQ(c2->y, 6u);
}

TEST(shelf_tests, item_placed_in_earlier_row_uses_that_rows_y) {
  // Regression: original bug used s.cursor_y (past the last row) instead of r.y
  // when placing into an existing row.
  shelfset<int, int> s{30, 50};
  s.add_shelf();
  auto* c1 = s.try_put(1, 1, 5, 10);  // r0 at y=0
  auto* c2 = s.try_put(
      2, 2, 5,
      3);  // can't reuse r0 (3 < 10*2/3 territory; r0.h=10, 10 > 3*3/2=4) -> new row r1 at y=11
  auto* c3 = s.try_put(3, 3, 5, 10);  // should reuse r0 since h matches; expected y=0
  ASSERT_NE(c1, nullptr);
  ASSERT_NE(c2, nullptr);
  ASSERT_NE(c3, nullptr);
  EXPECT_EQ(c1->shelf_index, 0u);
  EXPECT_EQ(c2->shelf_index, 0u);
  EXPECT_EQ(c2->y, 11u);
  EXPECT_EQ(c3->shelf_index, 0u);
  EXPECT_EQ(c3->y, 0u);
  EXPECT_EQ(c3->x, 6u);
}

TEST(shelf_tests, new_row_advances_cursor_x_so_next_put_does_not_overlap) {
  // Regression: original bug left a freshly created row with cursor_x=0,
  // causing the next put into that row to overlap the first item.
  shelfset<int, int> s{30, 50};
  s.add_shelf();
  auto* c1 = s.try_put(1, 1, 5, 5);   // r0 at y=0
  auto* c2 = s.try_put(2, 2, 5, 10);  // new row r1 at y=6, x=0
  auto* c3 = s.try_put(3, 3, 4, 10);  // reuse r1; must NOT overlap c2
  ASSERT_NE(c2, nullptr);
  ASSERT_NE(c3, nullptr);
  EXPECT_EQ(c2->x, 0u);
  EXPECT_EQ(c2->y, 6u);
  EXPECT_EQ(c3->y, 6u);
  EXPECT_EQ(c3->x, 6u);  // 5 + padding(1)
}

TEST(shelf_tests, falls_through_to_next_shelf_when_current_is_full) {
  // width=11, height=12; items 10x5 (padding=1 -> needs width>=11).
  // shelf0: r0 at y=0 (cursor_x=11, full); r1 at y=6 (6+5+1=12 <= 12, fits);
  //         third item: no row fits (both full), and 12+5+1=18 > 12 -> next shelf.
  shelfset<int, int> s{11, 12};
  s.add_shelf();
  s.add_shelf();
  auto* c1 = s.try_put(1, 1, 10, 5);
  auto* c2 = s.try_put(2, 2, 10, 5);
  auto* c3 = s.try_put(3, 3, 10, 5);
  ASSERT_NE(c1, nullptr);
  ASSERT_NE(c2, nullptr);
  ASSERT_NE(c3, nullptr);
  EXPECT_EQ(c1->shelf_index, 0u);
  EXPECT_EQ(c1->y, 0u);
  EXPECT_EQ(c2->shelf_index, 0u);
  EXPECT_EQ(c2->y, 6u);
  // c3 lands in the freshly-added second shelf, at y=0 of that shelf
  EXPECT_EQ(c3->shelf_index, 1u);
  EXPECT_EQ(c3->y, 0u);
  EXPECT_EQ(c3->x, 0u);
}

TEST(shelf_tests, returns_nullptr_when_all_shelves_are_full) {
  shelfset<int, int> s{11, 12};
  s.add_shelf();
  auto* c1 = s.try_put(1, 1, 10, 5);
  auto* c2 = s.try_put(2, 2, 10, 5);
  auto* c3 = s.try_put(3, 3, 10, 5);
  ASSERT_NE(c1, nullptr);
  ASSERT_NE(c2, nullptr);
  EXPECT_EQ(c3, nullptr);
  EXPECT_EQ(s.try_get_view(3), nullptr);
}

TEST(shelf_tests, supports_move_only_payload) {
  shelfset<int, std::unique_ptr<int>> s{20, 30};
  s.add_shelf();
  auto* c = s.try_put(1, std::make_unique<int>(42), 5, 5);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->shelf_index, 0u);
  ASSERT_NE(c->payload, nullptr);
  EXPECT_EQ(*c->payload, 42);
  const auto* v = s.try_get_view(1);
  ASSERT_NE(v, nullptr);
  ASSERT_NE(v->payload, nullptr);
  EXPECT_EQ(*v->payload, 42);
}

TEST(shelf_tests, shelf_index_spans_multiple_shelves) {
  // 3 shelves; each can hold exactly one 5x5 item (height 6 = 5+padding)
  shelfset<int, int> s{10, 6};
  s.add_shelf();
  s.add_shelf();
  s.add_shelf();
  auto* c0 = s.try_put(0, 0, 5, 5);
  auto* c1 = s.try_put(1, 1, 5, 5);
  auto* c2 = s.try_put(2, 2, 5, 5);
  ASSERT_NE(c0, nullptr);
  ASSERT_NE(c1, nullptr);
  ASSERT_NE(c2, nullptr);
  EXPECT_EQ(c0->shelf_index, 0u);
  EXPECT_EQ(c1->shelf_index, 1u);
  EXPECT_EQ(c2->shelf_index, 2u);
  // verify via get_view as well
  EXPECT_EQ(s.try_get_view(0)->shelf_index, 0u);
  EXPECT_EQ(s.try_get_view(1)->shelf_index, 1u);
  EXPECT_EQ(s.try_get_view(2)->shelf_index, 2u);
}

}  // namespace swg::ut
