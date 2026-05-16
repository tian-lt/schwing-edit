// std
#include <format>
#include <optional>
// windows
#include "win.hpp"
// wil
#include <wil/resource.h>
#include <wil/result_macros.h>
// swg
#include <plaindoc.hpp>

namespace {

const double default_font_size = 12.0;
const std::string default_font_path = "C:\\Windows\\Fonts\\Arial.ttf";

class Sedit {
  Sedit(HWND hwnd, std::string fontpath, double fontsize)
      : hwnd_(hwnd), doc_(nullptr, std::move(fontpath), fontsize, swg::eol::crlf) {
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
  LRESULT OnChar(wchar_t uchar) {
    if (auto res = DigestChar(uchar); res.has_value()) {
      double ratio = GetDpiForWindow(hwnd_) / 96.0;
      if (*res == "\r") {
        doc_.insert(insPos_, "\r\n");
        insPos_ += 2;
        caretPosX_ = 4 * ratio;
        caretPosY_ += 24 * ratio;
      } else if (*res == "\b") {
        if (insPos_ == 0 || doc_.length() == 0) {
          return 0;
        } else {
          size_t l = std::min(6uz, insPos_);
          auto s = doc_.get(insPos_ - l, l);
          size_t e = insPos_ - 1;
          for (auto it = s.rbegin(); it != s.rend(); ++it) {
            if ((*it & 0xC0) != 0x80) {
              break;
            }
            --e;
          }
          doc_.erase(e, insPos_ - e);
          insPos_ = e;
          caretPosX_ -= 4 * ratio;
        }
      } else {
        doc_.insert(insPos_, *res);
        insPos_ += res->size();
        caretPosX_ += 4 * ratio;
      }
#ifdef _DEBUG
      auto s = doc_.get(0, doc_.length());
      int l = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
      std::wstring wstr((size_t)l, 0);
      MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), wstr.data(),
                          static_cast<int>(wstr.size()));
      OutputDebugStringW(std::format(L"{}\n", wstr).c_str());
#endif
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
  std::optional<std::string> DigestChar(wchar_t uchar) {
    constexpr int HI = 0, LO = 1;
    if (IS_HIGH_SURROGATE(uchar)) {
      surrogate_[HI] = uchar;
      return std::nullopt;
    } else {
      char mbstr[5];
      surrogate_[LO] = uchar;
      if (surrogate_[HI] == 0) {
        int len = WideCharToMultiByte(CP_UTF8, 0, &surrogate_[LO], 1, mbstr, std::size(mbstr),
                                      nullptr, nullptr);
        THROW_LAST_ERROR_IF(len <= 0);
        return std::string(mbstr, len);
      } else {
        int len = WideCharToMultiByte(CP_UTF8, 0, surrogate_, 2, mbstr, std::size(mbstr), nullptr,
                                      nullptr);
        THROW_LAST_ERROR_IF(len <= 0);
        surrogate_[HI] = 0;
        return std::string(mbstr, len);
      }
    }
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;
      case WM_CHAR:
        return GetThis(hwnd)->OnChar(static_cast<wchar_t>(wparam));
      case WM_SETFOCUS:
        return GetThis(hwnd)->OnSetFocus();
      case WM_KILLFOCUS:
        return GetThis(hwnd)->OnKillFocus();
      case WM_CREATE:
        SetWindowLongPtr(
            hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(new Sedit(hwnd, default_font_path, default_font_size)));
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
  swg::plaindoc doc_;
  wchar_t surrogate_[2] = {};
  int caretPosX_ = 0;
  int caretPosY_ = 0;
  int caretWidth_ = 0;
  int caretHeight_ = 0;
};

const ATOM SeditWndInit = Sedit::Initialize();

}  // namespace
