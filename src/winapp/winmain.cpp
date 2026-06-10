// std
#include <string>
// swg
#include "res.h"
#include "sedit.hpp"
#include "win.hpp"

#include <windowsx.h>

namespace {

constexpr wchar_t kMainClass[] = L"SchwingEditMain";
constexpr int kSeditId = 100;

HWND g_sedit = nullptr;

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  switch (msg) {
    case WM_CREATE: {
      HINSTANCE hinst = reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
      swg::winapp::Sedit::Register(hinst);
      g_sedit = swg::winapp::Sedit::Create(hinst, hwnd, kSeditId);
      return 0;
    }
    case WM_SETFOCUS:
      if (g_sedit) ::SetFocus(g_sedit);
      return 0;
    case WM_SIZE:
      if (g_sedit) {
        ::MoveWindow(g_sedit, 0, 0, LOWORD(l), HIWORD(l), TRUE);
      }
      return 0;
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;
  }
  return ::DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE hinst, _In_opt_ HINSTANCE, _In_ LPWSTR cmdline,
                      _In_ int show) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &MainProc;
  wc.hInstance = hinst;
  wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kMainClass;
  wc.hIcon = ::LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APP_ICON));
  if (!wc.hIcon) wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
  wc.hIconSm = wc.hIcon;
  if (!::RegisterClassExW(&wc)) return 1;

  HWND main = ::CreateWindowExW(0, kMainClass, L"Schwing Edit",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                CW_USEDEFAULT, 1000, 700, nullptr, nullptr, hinst, nullptr);
  if (!main) return 1;
  ::ShowWindow(main, show);
  ::UpdateWindow(main);

  // If a path was passed on the command line, open it.
  if (cmdline && *cmdline) {
    std::wstring path(cmdline);
    // Strip surrounding quotes if any.
    if (!path.empty() && path.front() == L'"') {
      auto end = path.find(L'"', 1);
      if (end != std::wstring::npos) path = path.substr(1, end - 1);
    }
    if (!path.empty()) {
      auto* self = reinterpret_cast<swg::winapp::Sedit*>(::GetWindowLongPtrW(g_sedit, 0));
      if (self) self->open_file(path.c_str());
    }
  }

  MSG msg{};
  while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
    ::TranslateMessage(&msg);
    ::DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}
