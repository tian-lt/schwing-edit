#pragma once
// std
#include <memory>
#include <string>
// swg
#include "../edit/plaindoc.hpp"
#include "win.hpp"

namespace swg::winapp {

class Sedit : public swg::host {
 public:
  static ATOM Register(HINSTANCE hinst);
  static HWND Create(HINSTANCE hinst, HWND parent, int id);

  // Win32 wndproc dispatch
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

  // host overrides
  void on_invalidate() override;
  void on_doc_changed(const swg::damage& d) override;
  void on_caret_moved() override;

  // file commands
  bool open_file(const wchar_t* path);
  bool save_file(const wchar_t* path);

 private:
  Sedit() = default;
  ~Sedit() override;

  // message handlers
  LRESULT on_create(HWND hwnd, LPCREATESTRUCT cs);
  void on_size(int w, int h);
  void on_paint();
  void on_set_focus();
  void on_kill_focus();
  void on_lbutton_down(int x, int y, WPARAM mods);
  void on_mouse_move(int x, int y, WPARAM mods);
  void on_lbutton_up(int x, int y, WPARAM mods);
  void on_mouse_wheel(int delta, WPARAM mods);
  void on_char(wchar_t ch);
  bool on_key_down(WPARAM vk, LPARAM lparam);
  void on_destroy();

  // helpers
  void ensure_font();
  void recompute_metrics();
  void update_scrollbars();
  void scroll_to_caret();
  void invalidate_all();
  void place_caret();
  std::size_t hit_test(int x, int y) const;
  // pixel coord of pos within current viewport (returns (x, y) in client px).
  POINT pos_to_xy(std::size_t pos) const;
  // convert a single visual line range to UTF-16 for ExtTextOutW
  std::wstring line_utf16(std::size_t line_idx) const;

  HWND hwnd_ = nullptr;
  HFONT font_ = nullptr;
  int line_h_ = 16;
  int char_w_ = 8;          // approx ascii advance for column estimates
  int ascent_ = 12;
  int client_w_ = 0;
  int client_h_ = 0;
  int top_line_ = 0;
  int left_col_px_ = 0;     // horizontal pixel scroll
  bool selecting_ = false;

  swg::plaindoc doc_;
  // backing storage for load_view (mmap or owned buffer)
  std::string owned_bytes_;
};

}  // namespace swg::winapp
