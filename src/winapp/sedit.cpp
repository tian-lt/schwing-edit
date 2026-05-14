// std
#include <optional>
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
  Sedit(HWND hwnd) : hwnd_(hwnd) {
    double ratio = GetDpiForWindow(hwnd_) / 96.0;
    caretPosX_ = 4 * ratio;
    caretPosY_ = 2 * ratio;
    caretWidth_ = 1 * ratio;
    caretHeight_ = 24 * ratio;
  }

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
  LRESULT OnChar(char u8char) {
    if (auto res = DigestChar(u8char); res.has_value()) {
      doc_.insert(insPos_, *res);
      insPos_ += res->size();
      double ratio = GetDpiForWindow(hwnd_) / 96.0;
      caretPosX_ += 8;
      SetCaretPos(caretPosX_, caretPosY_);
    }
    return 0;
  }
  LRESULT OnSetFocus() {
    CreateCaret(hwnd_, nullptr, caretWidth_, caretHeight_);
    SetCaretPos(caretPosX_, caretPosY_);
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
  std::optional<std::string> DigestChar(char u8char) {
    chbuf_ += u8char;
    bool cont = (static_cast<unsigned char>(u8char) & 0xC0) == 0x80;
    if (cont) {
      return std::nullopt;
    } else {
      return std::exchange(chbuf_, {});
    }
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;
      case WM_CHAR:
        return GetThis(hwnd)->OnChar(static_cast<char>(wparam));
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
  HWND hwnd_ = nullptr;
  size_t insPos_ = 0;
  std::string chbuf_;
  swg::plaindoc doc_;
  int caretPosX_ = 0;
  int caretPosY_ = 0;
  int caretWidth_ = 0;
  int caretHeight_ = 0;
};

ATOM SeditWndInit = Sedit::Initialize();

}  // namespace
