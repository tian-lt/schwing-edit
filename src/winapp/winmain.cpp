// windows
#include <Windows.h>
// wil
#include <wil/resource.h>
#include <wil/result_macros.h>

namespace {

class MainWindow {
 public:
  static void Initailize() {
    WNDCLASSEX wcex{
        .cbSize = sizeof(WNDCLASSEX),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WndProc,
        .lpszClassName = TEXT("MainWindowClass"),
    };
    THROW_LAST_ERROR_IF(RegisterClassEx(&wcex) == 0);
  }
  MainWindow(HINSTANCE hinst, int cmdShow) {
    wil::unique_hwnd hwnd{CreateWindowEx(
        0, TEXT("MainWindowClass"), TEXT("Schwing Edit"), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hinst, this)};
    THROW_LAST_ERROR_IF(!hwnd.is_valid());
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
  HWND hwnd_;
};

}  // namespace

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine,
                   _In_ int nCmdShow) {
  MainWindow::Initailize();
  MainWindow mainWnd{hInstance, nCmdShow};
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}
