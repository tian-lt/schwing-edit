#pragma once

// OpenGL-backed text caret. Replaces the Win32 `CreateCaret` / `SetCaretPos`
// family with a tiny child window that owns its own WGL context and renders
// a solid coloured quad via `glClear` + `SwapBuffers`. The blink phase is
// driven by `WM_TIMER` using the system's `GetCaretBlinkTime()`.
//
// The caret window is created lazily on the first `Show()` so we do not pay
// for the WGL bootstrap until the editor actually gets focus. The hosting
// editor must register `WS_CLIPCHILDREN` so its BitBlt does not paint over
// the caret region.

#include "win.hpp"

#include <cstdint>

namespace swg::winapp {

class GlCaret {
 public:
  GlCaret() = default;
  ~GlCaret();

  GlCaret(const GlCaret&) = delete;
  GlCaret& operator=(const GlCaret&) = delete;

  // Lazily create the caret child window + GL context the first time the
  // caret is shown. Subsequent calls are cheap (just `ShowWindow`).
  void Show(HWND parent);

  // Hide the caret. Keeps the GL context and window alive so the next
  // Show() is free.
  void Hide();

  // Move/resize the caret. Idempotent — only calls `SetWindowPos` when the
  // rect actually changes. Coordinates are in the parent's client space.
  void Place(int x, int y, int width, int height);

  // Update the caret's fill colour (0x00RRGGBB). Triggers a redraw if the
  // caret is currently visible.
  void SetColor(uint32_t rgb);

  // Tear down the caret window and GL context. Called from the parent
  // editor's `WM_DESTROY`.
  void Destroy();

  bool is_visible() const { return shown_; }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
  void EnsureCreated(HWND parent);
  bool InitGL();
  void TeardownGL();
  void Render();

  HWND hwnd_ = nullptr;
  HDC hdc_ = nullptr;
  HGLRC hglrc_ = nullptr;
  int last_x_ = -1, last_y_ = -1, last_w_ = 0, last_h_ = 0;
  bool shown_ = false;        // logical visibility from Show/Hide.
  bool blink_on_ = true;      // current phase of the blink timer.
  uint32_t color_ = 0;        // 0x00RRGGBB.
};

}  // namespace swg::winapp
