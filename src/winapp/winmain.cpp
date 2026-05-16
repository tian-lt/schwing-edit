// windows
#include <Windows.h>
#include <dwmapi.h>
// wil
#include <wil/resource.h>
#include <wil/result_macros.h>
// swg
#include "resource.hpp"
// app
#include "res.h"

#pragma comment(lib, "Dwmapi.lib")

namespace {
static_assert(std::is_same_v<TCHAR, wchar_t>);

class MainWindow {
 public:
  static ATOM Initailize() {
    const HINSTANCE hinst = GetModuleHandle(nullptr);
    WNDCLASSEX wcex{
        .cbSize = sizeof(WNDCLASSEX),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WndProc,
        .hInstance = hinst,
        .hIcon = (HICON)LoadImage(hinst, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 32, 32,
                                  LR_DEFAULTCOLOR),
        .hCursor = LoadCursor(nullptr, IDC_ARROW),
        .hbrBackground = CreateSolidBrush(RGB(0, 0, 0)),
        .lpszClassName = TEXT("MainWindowClass"),
        .hIconSm = (HICON)LoadImage(hinst, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 16, 16,
                                    LR_DEFAULTCOLOR),
    };
    ATOM atom = RegisterClassEx(&wcex);
    THROW_LAST_ERROR_IF(atom == 0);
    return atom;
  }
  MainWindow(HINSTANCE hinst, int cmdShow) {
    wil::unique_hwnd hwnd{CreateWindowEx(
        0, TEXT("MainWindowClass"), TEXT("Schwing Edit"), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hinst, this)};
    THROW_LAST_ERROR_IF(!hwnd.is_valid());
    RECT rc;
    THROW_IF_WIN32_BOOL_FALSE(GetClientRect(hwnd.get(), &rc));
    double dpiRatio = GetDpiForWindow(hwnd.get()) / 96.0;
    editHwnd_ = wil::unique_hwnd{
        CreateWindowEx(0, TEXT("SEditWindowClass"), nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,
                       30 * dpiRatio, rc.right - rc.left, rc.bottom - rc.top - 30 * dpiRatio,
                       hwnd.get(), nullptr, hinst, nullptr)};
    THROW_LAST_ERROR_IF(!editHwnd_.is_valid());
    SetFocus(editHwnd_.get());
    {  // enable mica
      DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
      DwmSetWindowAttribute(hwnd.get(), DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                            sizeof(DWM_SYSTEMBACKDROP_TYPE));
      MARGINS margins = {0, 0, (int)(30 * dpiRatio), 0};
      DwmExtendFrameIntoClientArea(hwnd.get(), &margins);
    }
    ShowWindow(hwnd.get(), cmdShow);
    hwnd_ = hwnd.release();
  }

 private:
  LRESULT OnSize() {
    RECT rc;
    if (!GetClientRect(hwnd_, &rc)) {
      return 0;  // ignore transient error
    }
    double dpiRatio = GetDpiForWindow(hwnd_) / 96.0;
    SetWindowPos(editHwnd_.get(), nullptr, 0, 30 * dpiRatio, rc.right - rc.left,
                 rc.bottom - rc.top - 30 * dpiRatio, SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
  }
  LRESULT OnSetFocus() {
    SetFocus(editHwnd_.get());
    return 0;
  }
  LRESULT OnDestroy() {
    PostQuitMessage(0);
    return 0;
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_SIZE:
        return GetThis(hwnd)->OnSize();
      case WM_SETFOCUS:
        return GetThis(hwnd)->OnSetFocus();
      case WM_CREATE: {
        auto info = reinterpret_cast<LPCREATESTRUCT>(lparam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(info->lpCreateParams));
        return 0;
      }
      case WM_DESTROY:
        return GetThis(hwnd)->OnDestroy();
      case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<LPMINMAXINFO>(lparam);
        double dpiRatio = GetDpiForWindow(hwnd) / 96.0;
        int minWidth = (int)(480 * dpiRatio);
        int minHeight = (int)(200 * dpiRatio);
        info->ptMinTrackSize.x = minWidth;
        info->ptMinTrackSize.y = minHeight;
        return 0;
      }
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
  }
  static MainWindow* GetThis(HWND hwnd) {
    return reinterpret_cast<MainWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }

 private:
  HWND hwnd_ = nullptr;
  wil::unique_hwnd editHwnd_;
};

const ATOM MainWndInit = MainWindow::Initailize();

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
  swg::initialize();
  THROW_IF_WIN32_BOOL_FALSE(
      SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
  MainWindow mainWnd{hInstance, nCmdShow};
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  swg::uninitialize();
  return 0;
}
