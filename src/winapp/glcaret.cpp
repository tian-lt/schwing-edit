// windows
#include "win.hpp"
#include <GL/gl.h>
// std
#include <climits>
// wil
#include <wil/result_macros.h>
// app
#include "glcaret.hpp"

#pragma comment(lib, "opengl32.lib")

namespace swg::winapp {

namespace {

constexpr wchar_t kClassName[] = L"SeditGlCaretClass";
constexpr UINT_PTR kBlinkTimerId = 0x5347454Cu;  // 'SGEL'

ATOM RegisterCaretClass() {
  WNDCLASSEXW wcex{};
  wcex.cbSize = sizeof(wcex);
  // CS_OWNDC because the WGL context is bound to this window's private DC for
  // the entire lifetime of the caret.
  wcex.style = CS_OWNDC;
  wcex.lpfnWndProc = &GlCaret::WndProc;
  wcex.hInstance = GetModuleHandleW(nullptr);
  // No cursor: parent editor's I-beam shows through the (single-pixel) gap on
  // hit-test edges. Setting `nullptr` here means "do not override".
  wcex.hCursor = nullptr;
  wcex.hbrBackground = nullptr;  // we paint everything via OpenGL.
  wcex.lpszClassName = kClassName;
  return RegisterClassExW(&wcex);
}

ATOM EnsureClass() {
  static const ATOM kAtom = RegisterCaretClass();
  return kAtom;
}

}  // namespace

GlCaret::~GlCaret() { Destroy(); }

void GlCaret::EnsureCreated(HWND parent) {
  if (hwnd_) return;
  EnsureClass();
  // `WS_DISABLED` keeps mouse events from being captured by the caret
  // window — the parent editor stays the hit target for clicks.
  hwnd_ = CreateWindowExW(0, kClassName, nullptr,
                          WS_CHILD | WS_DISABLED,
                          0, 0, 1, 1, parent, nullptr,
                          GetModuleHandleW(nullptr), this);
  if (!hwnd_) return;
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  hdc_ = GetDC(hwnd_);
  if (!hdc_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    return;
  }
  if (!InitGL()) {
    // Hard fall-back: tear down everything if GL refused to come up. The
    // editor will simply have no caret in that case — typing still works.
    if (hdc_) {
      ReleaseDC(hwnd_, hdc_);
      hdc_ = nullptr;
    }
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

bool GlCaret::InitGL() {
  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.iLayerType = PFD_MAIN_PLANE;
  const int pf = ChoosePixelFormat(hdc_, &pfd);
  if (pf == 0) return false;
  if (!SetPixelFormat(hdc_, pf, &pfd)) return false;
  hglrc_ = wglCreateContext(hdc_);
  if (!hglrc_) return false;
  // No need to keep the context current outside of Render() — making it
  // current in every Render call is cheap and lets future code keep its own
  // shared context without clashing.
  return true;
}

void GlCaret::TeardownGL() {
  if (hglrc_) {
    if (wglGetCurrentContext() == hglrc_) wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(hglrc_);
    hglrc_ = nullptr;
  }
}

void GlCaret::Destroy() {
  if (hwnd_) KillTimer(hwnd_, kBlinkTimerId);
  TeardownGL();
  if (hdc_ && hwnd_) {
    ReleaseDC(hwnd_, hdc_);
    hdc_ = nullptr;
  }
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  shown_ = false;
  blink_on_ = true;
  last_x_ = last_y_ = -1;
  last_w_ = last_h_ = 0;
}

void GlCaret::Show(HWND parent) {
  EnsureCreated(parent);
  if (!hwnd_) return;
  shown_ = true;
  blink_on_ = true;
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  // GetCaretBlinkTime can return INFINITE when the user has disabled
  // blinking in accessibility settings — honour that.
  const UINT period = GetCaretBlinkTime();
  if (period != 0 && period != INFINITE) {
    SetTimer(hwnd_, kBlinkTimerId, period, nullptr);
  } else {
    KillTimer(hwnd_, kBlinkTimerId);
  }
  Render();
}

void GlCaret::Hide() {
  if (!hwnd_) return;
  if (shown_) {
    KillTimer(hwnd_, kBlinkTimerId);
    ShowWindow(hwnd_, SW_HIDE);
  }
  shown_ = false;
}

void GlCaret::Place(int x, int y, int width, int height) {
  if (!hwnd_) return;
  if (width <= 0 || height <= 0) return;
  if (x == last_x_ && y == last_y_ && width == last_w_ && height == last_h_) {
    return;
  }
  last_x_ = x;
  last_y_ = y;
  last_w_ = width;
  last_h_ = height;
  // SWP_NOREDRAW: we explicitly re-render below; suppressing the implicit
  // erase avoids a frame of stale pixels during fast typing.
  SetWindowPos(hwnd_, HWND_TOP, x, y, width, height,
               SWP_NOACTIVATE | SWP_NOREDRAW);
  // Reset the blink phase so the caret is visible immediately after a move
  // (matches Win32 caret behaviour during fast typing).
  if (shown_) {
    blink_on_ = true;
    const UINT period = GetCaretBlinkTime();
    if (period != 0 && period != INFINITE) {
      // Restart the timer so the next blink-off is `period` from now, not
      // from some stale base time.
      SetTimer(hwnd_, kBlinkTimerId, period, nullptr);
    }
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    Render();
  }
}

void GlCaret::SetColor(uint32_t rgb) {
  if (rgb == color_) return;
  color_ = rgb;
  if (shown_ && blink_on_) Render();
}

void GlCaret::Render() {
  if (!hglrc_ || !hdc_) return;
  if (!wglMakeCurrent(hdc_, hglrc_)) return;
  const float r = static_cast<float>((color_ >> 16) & 0xFF) / 255.0f;
  const float g = static_cast<float>((color_ >> 8) & 0xFF) / 255.0f;
  const float b = static_cast<float>((color_) & 0xFF) / 255.0f;
  glClearColor(r, g, b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  SwapBuffers(hdc_);
}

LRESULT CALLBACK GlCaret::WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  auto* self =
      reinterpret_cast<GlCaret*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_NCCREATE: {
      auto cs = reinterpret_cast<LPCREATESTRUCT>(l);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      break;
    }
    case WM_ERASEBKGND:
      // We own the pixels via glClear; tell GDI not to touch us.
      return 1;
    case WM_PAINT: {
      if (self) self->Render();
      ValidateRect(hwnd, nullptr);
      return 0;
    }
    case WM_TIMER:
      if (self && w == kBlinkTimerId) {
        self->blink_on_ = !self->blink_on_;
        if (self->blink_on_) {
          ShowWindow(hwnd, SW_SHOWNOACTIVATE);
          self->Render();
        } else {
          ShowWindow(hwnd, SW_HIDE);
        }
      }
      return 0;
  }
  return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace swg::winapp
