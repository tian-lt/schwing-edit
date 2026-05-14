// windows
#include <Windows.h>
// wil
#include <wil/resource.h>
#include <wil/result_macros.h>

namespace {

class MainWindow {
 public:
  static ATOM Initailize() {
    WNDCLASSEX wcex{
        .cbSize = sizeof(WNDCLASSEX),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WndProc,
        .hInstance = GetModuleHandle(nullptr),
        .hCursor = LoadCursor(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW),
        .lpszClassName = TEXT("MainWindowClass"),
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
    ShowWindow(hwnd.get(), cmdShow);
    hwnd_ = hwnd.release();
  }

 private:
  LRESULT OnDestroy() {
    PostQuitMessage(0);
    return 0;
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_CREATE: {
        auto info = reinterpret_cast<LPCREATESTRUCT>(lparam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(info->lpCreateParams));
        return 0;
      }
      case WM_DESTROY:
        return GetThis(hwnd)->OnDestroy();
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

ATOM MainWndInit = MainWindow::Initailize();

}  // namespace

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine,
                   _In_ int nCmdShow) {
  THROW_IF_WIN32_BOOL_FALSE(
      SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
  MainWindow mainWnd{hInstance, nCmdShow};
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}
