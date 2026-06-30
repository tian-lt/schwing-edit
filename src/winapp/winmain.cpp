// std
#include <filesystem>
#include <memory>
// windows
#include <shobjidl.h>

#include "win.hpp"
// wil
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result_macros.h>
// swg
#include <plaindoc.hpp>
#include <resource.hpp>
// app
#include "res.h"
#include "sedit.hpp"

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
        .lpszMenuName = MAKEINTRESOURCE(IDR_MAINMENU),
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
    editHwnd_ = wil::unique_hwnd{CreateWindowEx(
        0, SeditWindowClass, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL, 0, 0,
        rc.right - rc.left, rc.bottom - rc.top, hwnd.get(), nullptr, hinst, nullptr)};
    THROW_LAST_ERROR_IF(!editHwnd_.is_valid());
    doc_ = std::make_unique<swg::plaindoc>(12.0, swg::eol::crlf);
    SendMessage(editHwnd_.get(), std::to_underlying(SeditMessage::SetDoc), 0,
                reinterpret_cast<LPARAM>(doc_.get()));
    SetFocus(editHwnd_.get());
    ShowWindow(hwnd.get(), cmdShow);
    hwnd_ = hwnd.release();
  }
  HWND Handle() const noexcept { return hwnd_; }

 private:
  LRESULT OnSize() {
    RECT rc;
    if (!GetClientRect(hwnd_, &rc)) {
      return 0;  // ignore transient error
    }
    SetWindowPos(editHwnd_.get(), nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
  }
  LRESULT OnSetFocus() {
    SetFocus(editHwnd_.get());
    return 0;
  }
  LRESULT OnDestroy() {
    doc_.reset();
    PostQuitMessage(0);
    return 0;
  }
  LRESULT OnCommand(int id) {
    switch (id) {
      case IDM_FILE_OPEN:
        OpenFile();
        break;
      case IDM_FILE_SAVE:
        break;
      case IDM_FILE_SAVEAS:
        break;
      case IDM_FILE_EXIT:
        PostMessage(hwnd_, WM_CLOSE, 0, 0);
        break;
    }
    return 0;
  }
  LRESULT OnClose() {
    DestroyWindow(hwnd_);
    return 0;
  }

  void OpenFile() {
    // TODO: check unsaved changes
    auto dialog = wil::CoCreateInstance<IFileOpenDialog>(CLSID_FileOpenDialog);
    HRESULT hr = dialog->Show(hwnd_);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
      return;
    }
    THROW_IF_FAILED(hr);
    wil::com_ptr<IShellItem> item;
    THROW_IF_FAILED(dialog->GetResult(&item));
    wil::unique_cotaskmem_string path;
    THROW_IF_FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
    doc_ = std::make_unique<swg::plaindoc>(12, swg::eol::crlf, std::filesystem::path{path.get()});
    SendMessage(editHwnd_.get(), std::to_underlying(SeditMessage::SetDoc), 0,
                reinterpret_cast<LPARAM>(doc_.get()));
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_COMMAND:
        return GetThis(hwnd)->OnCommand(LOWORD(wparam));
      case WM_SIZE:
        return GetThis(hwnd)->OnSize();
      case WM_SETFOCUS:
        return GetThis(hwnd)->OnSetFocus();
      case WM_CLOSE:
        return GetThis(hwnd)->OnDestroy();
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
  std::unique_ptr<swg::plaindoc> doc_;
};

const ATOM MainWndInit = MainWindow::Initailize();

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
  swg::initialize();
  auto coInit = wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
  THROW_IF_WIN32_BOOL_FALSE(
      SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
  MainWindow mainWnd{hInstance, nCmdShow};
  wil::unique_haccel accel{LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_MAINACCEL))};
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (TranslateAccelerator(mainWnd.Handle(), accel.get(), &msg)) {
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  swg::uninitialize();
  return 0;
}
