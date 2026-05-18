// std
#include <format>
#include <optional>
// windows
#include "win.hpp"
// glad
#include <glad/glad.h>
// wgl
#include <GL/wgl.h>
// wil
#include <wil/resource.h>
#include <wil/result_macros.h>
// swg
#include <plaindoc.hpp>

namespace {

const double default_font_size = 12.0;
const std::string default_font_path = "C:\\Windows\\Fonts\\Arial.ttf";

class Sedit : public swg::host {
  Sedit(HWND hwnd, std::string fontpath, double fontsize)
      : hwnd_(hwnd), doc_(this, std::move(fontpath), fontsize, swg::eol::crlf) {
    double ratio = GetDpiForWindow(hwnd_) / 96.0;
    caretPosX_ = 4 * ratio;
    caretPosY_ = 2 * ratio;
    caretWidth_ = 1 * ratio;
    caretHeight_ = 24 * ratio;
    InitializeGraphics();
  }

 public:
  ~Sedit() {
    if (glrc_) {
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(glrc_);
    }
  }
  static bool Initialize() {
    WNDCLASSEX wcex{
        .cbSize = sizeof(WNDCLASSEX),
        .style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_OWNDC,
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
  void on_invalidate(swg::rect rc) override {
    RECT winrc{rc.x, rc.y, rc.x + rc.w, rc.y + rc.h};
    InvalidateRect(hwnd_, &winrc, FALSE);
  }

  LRESULT OnPaint() {
    PAINTSTRUCT ps;
    {
      auto hdc = wil::BeginPaint(hwnd_, &ps);
    }
    glClearColor(1.f, 1.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    SwapBuffers(hdc_.get());
    return 0;
  }

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
  LRESULT OnSize(int width, int height) {
    glViewport(0, 0, width, height);
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
  void InitializeGraphics() {
    auto hdc = wil::GetDC(hwnd_);
    THROW_LAST_ERROR_IF(!hdc);
    PIXELFORMATDESCRIPTOR pfd{.nSize = sizeof(PIXELFORMATDESCRIPTOR),
                              .nVersion = 1,
                              .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                              .iPixelType = PFD_TYPE_RGBA,
                              .cColorBits = 32,
                              .cDepthBits = 24,
                              .cStencilBits = 8,
                              .iLayerType = PFD_MAIN_PLANE};
    int format = ChoosePixelFormat(hdc.get(), &pfd);
    if (format == 0 || !SetPixelFormat(hdc.get(), format, &pfd)) {
      throw std::runtime_error{"opengl pixel format error"};
    }
    HGLRC tmp = wglCreateContext(hdc.get());
    if (!tmp) {
      throw std::runtime_error{"opengl context creation error"};
    }
    if (!wglMakeCurrent(hdc.get(), tmp)) {
      wglDeleteContext(tmp);
      throw std::runtime_error{"opengl context activation error"};
    }

    if (!gladLoadGL()) {
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(tmp);
      throw std::runtime_error{"opengl function loading error"};
    }
    int attrs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                   3,
                   WGL_CONTEXT_MINOR_VERSION_ARB,
                   3,
                   WGL_CONTEXT_PROFILE_MASK_ARB,
                   WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                   0};
    auto wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!wglCreateContextAttribsARB) {
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(tmp);
      throw std::runtime_error{"wglCreateContextAttribsARB not supported"};
    }
    HGLRC ctx = wglCreateContextAttribsARB(hdc.get(), nullptr, attrs);
    if (!ctx) {
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(tmp);
      throw std::runtime_error{"modern opengl context creation error"};
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(tmp);
    if (!wglMakeCurrent(hdc.get(), ctx)) {
      wglDeleteContext(ctx);
      throw std::runtime_error{"modern opengl context activation error"};
    }

    if (!gladLoadGL()) {
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(ctx);
      throw std::runtime_error{"modern opengl function loading error"};
    }
    glrc_ = ctx;
    hdc_ = std::move(hdc);
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_SIZE:
        return GetThis(hwnd)->OnSize(LOWORD(lparam), HIWORD(lparam));
      case WM_ERASEBKGND:
        return 0;
      case WM_PAINT:
        return GetThis(hwnd)->OnPaint();
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
        delete self;
        return 0;
      }
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
  }
  static Sedit* GetThis(HWND hwnd) {
    return reinterpret_cast<Sedit*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }

 private:
  HWND hwnd_ = nullptr;
  wil::unique_hdc_window hdc_;
  HGLRC glrc_ = nullptr;
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
