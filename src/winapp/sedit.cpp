// windows
#include <Windows.h>
// wil
#include <wil/resource.h>
#include <wil/result_macros.h>
// edit
#include <plaindoc.hpp>
// app
#include "sedit.hpp"

namespace {

class Sedit {
  Sedit(HWND hwnd) : hwnd_(hwnd) {}

 public:
  static bool Initialize() {
    WNDCLASSEX wcex{
        .cbSize = sizeof(WNDCLASSEX),
        .style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
        .lpfnWndProc = WndProc,
        .hInstance = GetModuleHandle(nullptr),
        .hCursor = LoadCursor(nullptr, IDC_IBEAM),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = TEXT("SEditWindowClass"),
    };
    ATOM atom = RegisterClassEx(&wcex);
    THROW_LAST_ERROR_IF(atom == 0);
    return true;
  }

 private:
  LRESULT OnChar() { return 0; }
  LRESULT OnSetFocus() {
    double ratio = GetDpiForWindow(hwnd_) / 96.0;
    CreateCaret(hwnd_, nullptr, 1 * ratio, 24 * ratio);
    SetCaretPos(4 * ratio, 2 * ratio);
    ShowCaret(hwnd_);
    return 0;
  }
  LRESULT OnKillFocus() {
    HideCaret(hwnd_);
    DestroyCaret();
    return 0;
  }
  LRESULT OnDestroy() {
    PostQuitMessage(0);
    return 0;
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;
      case WM_CHAR:
        return GetThis(hwnd)->OnChar();
      case WM_SETFOCUS:
        return GetThis(hwnd)->OnSetFocus();
      case WM_KILLFOCUS:
        return GetThis(hwnd)->OnKillFocus();
      case WM_CREATE:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new Sedit(hwnd)));
        return 0;
      case WM_DESTROY: {
        auto self = GetThis(hwnd);
        auto res = self->OnDestroy();
        delete self;
        return res;
      }
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
  }
  static Sedit* GetThis(HWND hwnd) {
    return reinterpret_cast<Sedit*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }

 private:
  HWND hwnd_;
};

ATOM SeditWndInit = Sedit::Initialize();

}  // namespace
