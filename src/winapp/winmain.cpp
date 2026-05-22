// windows
#include "win.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
// std
#include <string>
// wil
#include <wil/resource.h>
#include <wil/result_macros.h>
// swg
#include "resource.hpp"
// app
#include "res.h"

#pragma comment(lib, "Comctl32.lib")
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
    HMENU menu = LoadMenu(hinst, MAKEINTRESOURCE(IDR_MAIN_MENU));
    wil::unique_hwnd hwnd{CreateWindowEx(
        0, TEXT("MainWindowClass"), TEXT("Schwing Edit"), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, menu, hinst, this)};
    THROW_LAST_ERROR_IF(!hwnd.is_valid());
    // Status bar at the bottom; auto-sized via WM_SIZE forwarding.
    statusHwnd_ = wil::unique_hwnd{
        CreateWindowEx(0, STATUSCLASSNAME, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0,
                       0, 0, hwnd.get(), nullptr, hinst, nullptr)};
    THROW_LAST_ERROR_IF(!statusHwnd_.is_valid());
    LayoutStatusBar();
    RECT rc;
    THROW_IF_WIN32_BOOL_FALSE(GetClientRect(hwnd.get(), &rc));
    int statusH = StatusBarHeight();
    editHwnd_ = wil::unique_hwnd{
        CreateWindowEx(0, TEXT("SEditWindowClass"), nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,
                       0, rc.right - rc.left, rc.bottom - rc.top - statusH,
                       hwnd.get(), nullptr, hinst, nullptr)};
    THROW_LAST_ERROR_IF(!editHwnd_.is_valid());
    SetFocus(editHwnd_.get());
    SendMessage(editHwnd_.get(), WM_SEDIT_SET_STATUS,
                reinterpret_cast<WPARAM>(statusHwnd_.get()), 0);
    {  // enable mica
      DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
      DwmSetWindowAttribute(hwnd.get(), DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                            sizeof(DWM_SYSTEMBACKDROP_TYPE));
    }
    ShowWindow(hwnd.get(), cmdShow);
    hwnd_ = hwnd.release();
  }

 private:
  int StatusBarHeight() {
    if (!statusHwnd_) return 0;
    RECT sr{};
    GetWindowRect(statusHwnd_.get(), &sr);
    return sr.bottom - sr.top;
  }
  void LayoutStatusBar() {
    if (statusHwnd_) {
      SendMessage(statusHwnd_.get(), WM_SIZE, 0, 0);
    }
  }
  LRESULT OnSize() {
    RECT rc;
    if (!GetClientRect(hwnd_, &rc)) {
      return 0;  // ignore transient error
    }
    LayoutStatusBar();
    int statusH = StatusBarHeight();
    SetWindowPos(editHwnd_.get(), nullptr, 0, 0, rc.right - rc.left,
                 rc.bottom - rc.top - statusH, SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
  }
  LRESULT OnCommand(WPARAM wparam) {
    const WORD id = LOWORD(wparam);
    if (id == IDM_FILE_EXIT) {
      SendMessage(hwnd_, WM_CLOSE, 0, 0);
      return 0;
    }
    if (id == IDM_HELP_ABOUT) {
      MessageBoxW(hwnd_,
                  L"Schwing Edit\n\nA lightweight, cross-platform text editor.\n\n"
                  L"\u00A9 2026 Tian Liao",
                  L"About Schwing Edit", MB_OK | MB_ICONINFORMATION);
      return 0;
    }
    // Forward all editing commands to the child editor window.
    if (editHwnd_) {
      SendMessage(editHwnd_.get(), WM_COMMAND, wparam, 0);
    }
    return 0;
  }
  LRESULT OnSetFocus() {
    SetFocus(editHwnd_.get());
    return 0;
  }
  LRESULT OnClose() {
    // Ask the editor whether it can close (prompts to save if dirty).
    if (editHwnd_) {
      LRESULT ok = SendMessage(editHwnd_.get(), WM_SEDIT_CAN_CLOSE, 0, 0);
      if (!ok) return 0;  // user cancelled
    }
    DestroyWindow(hwnd_);
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
      case WM_COMMAND:
        return GetThis(hwnd)->OnCommand(wparam);
      case WM_SETFOCUS:
        return GetThis(hwnd)->OnSetFocus();
      case WM_CREATE: {
        auto info = reinterpret_cast<LPCREATESTRUCT>(lparam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(info->lpCreateParams));
        return 0;
      }
      case WM_DESTROY:
        return GetThis(hwnd)->OnDestroy();
      case WM_CLOSE:
        return GetThis(hwnd)->OnClose();
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
  wil::unique_hwnd statusHwnd_;
};

const ATOM MainWndInit = MainWindow::Initailize();

}  // namespace

namespace swg::winapp {
void SetInitialFilePath(const std::wstring& path);
bool IsFindDialogMessage(MSG* msg);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
  swg::initialize();
  // Initialize common controls so STATUSCLASSNAME is registered.
  {
    INITCOMMONCONTROLSEX icc{.dwSize = sizeof(icc), .dwICC = ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
  }
  THROW_IF_WIN32_BOOL_FALSE(
      SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
  // Parse a single optional file argument from the command line.
  if (lpCmdLine && *lpCmdLine) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
    if (argv) {
      if (argc >= 1 && argv[0] && *argv[0]) {
        swg::winapp::SetInitialFilePath(argv[0]);
      }
      LocalFree(argv);
    }
  }
  MainWindow mainWnd{hInstance, nCmdShow};
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    // Let the modeless Find/Replace dialog process navigation keys first.
    if (swg::winapp::IsFindDialogMessage(&msg)) continue;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  swg::uninitialize();
  return 0;
}
