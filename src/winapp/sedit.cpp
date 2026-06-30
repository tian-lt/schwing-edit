// std
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
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
// app
#include "sedit.hpp"

namespace {

const wchar_t wgl_dummy_window_class[] = L"SEditWglDummyWindowClass";

PIXELFORMATDESCRIPTOR OpenGLPixelFormatDescriptor() {
  return PIXELFORMATDESCRIPTOR{
      .nSize = sizeof(PIXELFORMATDESCRIPTOR),
      .nVersion = 1,
      .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
      .iPixelType = PFD_TYPE_RGBA,
      .cColorBits = 32,
      .cDepthBits = 24,
      .cStencilBits = 8,
      .iLayerType = PFD_MAIN_PLANE};
}

void SetLegacyOpenGLPixelFormat(HDC hdc) {
  auto pfd = OpenGLPixelFormatDescriptor();
  int format = ChoosePixelFormat(hdc, &pfd);
  if (format == 0 || !SetPixelFormat(hdc, format, &pfd)) {
    throw std::runtime_error{"opengl pixel format error"};
  }
}

template <typename T>
T LoadWglProc(const char* name) {
  auto proc = wglGetProcAddress(name);
  auto value = reinterpret_cast<std::intptr_t>(proc);
  if (value == 0 || value == 1 || value == 2 || value == 3 || value == -1) {
    return nullptr;
  }
  return reinterpret_cast<T>(proc);
}

bool HasWglExtension(PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB, HDC hdc,
                     std::string_view extension) {
  if (!wglGetExtensionsStringARB) {
    return false;
  }

  const char* extensions = wglGetExtensionsStringARB(hdc);
  if (!extensions) {
    return false;
  }

  std::string_view extension_list{extensions};
  size_t pos = 0;
  while ((pos = extension_list.find(extension, pos)) != std::string_view::npos) {
    const bool starts_token = pos == 0 || extension_list[pos - 1] == ' ';
    const size_t end = pos + extension.length();
    const bool ends_token = end == extension_list.length() || extension_list[end] == ' ';
    if (starts_token && ends_token) {
      return true;
    }
    pos = end;
  }
  return false;
}

void RegisterWglDummyWindowClass() {
  WNDCLASSEX wcex{.cbSize = sizeof(WNDCLASSEX),
                  .style = CS_OWNDC,
                  .lpfnWndProc = DefWindowProc,
                  .hInstance = GetModuleHandle(nullptr),
                  .lpszClassName = wgl_dummy_window_class};
  ATOM atom = RegisterClassEx(&wcex);
  if (atom == 0) {
    DWORD error = GetLastError();
    THROW_WIN32_IF(error, error != ERROR_CLASS_ALREADY_EXISTS);
  }
}

struct wgl_bootstrap_context;

struct hglrc_deleter {
  void operator()(HGLRC glrc) { wglDeleteContext(glrc); }
};

struct wgl_bootstrap_context_deleter {
  void operator()(wgl_bootstrap_context* context);
};

using unique_hglrc = std::unique_ptr<std::remove_pointer_t<HGLRC>, hglrc_deleter>;

struct wgl_bootstrap_context {
  wil::unique_hwnd hwnd;
  wil::unique_hdc_window hdc;
  unique_hglrc glrc;
};

using unique_wgl_bootstrap_context =
    std::unique_ptr<wgl_bootstrap_context, wgl_bootstrap_context_deleter>;

void wgl_bootstrap_context_deleter::operator()(wgl_bootstrap_context* context) {
  wglMakeCurrent(nullptr, nullptr);
  delete context;
}

unique_wgl_bootstrap_context CreateWglBootstrapContext() {
  RegisterWglDummyWindowClass();

  unique_wgl_bootstrap_context context{new wgl_bootstrap_context};
  context->hwnd.reset(CreateWindowEx(0, wgl_dummy_window_class, L"", WS_OVERLAPPED, 0, 0, 1, 1,
                                     nullptr, nullptr, GetModuleHandle(nullptr), nullptr));
  THROW_LAST_ERROR_IF(!context->hwnd);

  context->hdc = wil::GetDC(context->hwnd.get());
  THROW_LAST_ERROR_IF(!context->hdc);

  SetLegacyOpenGLPixelFormat(context->hdc.get());
  context->glrc.reset(wglCreateContext(context->hdc.get()));
  if (!context->glrc) {
    throw std::runtime_error{"opengl bootstrap context creation error"};
  }
  if (!wglMakeCurrent(context->hdc.get(), context->glrc.get())) {
    throw std::runtime_error{"opengl bootstrap context activation error"};
  }
  return context;
}

class Sedit : public swg::host {
 public:
  explicit Sedit(HWND hwnd) : hwnd_(hwnd) {
    dpi = GetDpiForWindow(hwnd_);
    InitializeGraphics();
  }

  ~Sedit() {
    if (glrc_) {
      wglMakeCurrent(nullptr, nullptr);
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
        .lpszClassName = SeditWindowClass,
    };
    ATOM atom = RegisterClassEx(&wcex);
    THROW_LAST_ERROR_IF(atom == 0);
    return true;
  }

 private:
  void on_invalidate() override { InvalidateRect(hwnd_, nullptr, FALSE); }
  void vscroll(swg::scrollinfo info) override {
    auto styles = GetWindowLongPtr(hwnd_, GWL_STYLE);
    if (info.page < (info.max - info.min) + 1) {
      if (!(styles & WS_VSCROLL)) {
        SetWindowLongPtr(hwnd_, GWL_STYLE, styles | WS_VSCROLL);
      }
    } else {
      if (styles & WS_VSCROLL) {
        SetWindowLongPtr(hwnd_, GWL_STYLE, styles & ~WS_VSCROLL);
      }
    }
  }

  LRESULT OnSet(swg::plaindoc* doc) {
    set(doc);
    return 0;
  }
  LRESULT OnPaint() {
    PAINTSTRUCT ps;
    auto hdc = wil::BeginPaint(hwnd_, &ps);
    glClearColor(1.f, 1.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    render();
    SwapBuffers(hdc_.get());
    return 0;
  }
  LRESULT OnChar(wchar_t uchar) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (ctrl || alt) {
      return 0;
    }
    // drop control characters except the editing keys we handle below
    if ((uchar < 0x20 || uchar == 0x7F) && uchar != L'\b' && uchar != L'\t' && uchar != L'\n' &&
        uchar != L'\r') {
      return 0;
    }

    if (auto res = DigestChar(uchar); res.has_value()) {
      double ratio = GetDpiForWindow(hwnd_) / 96.0;
      if (*res == "\r" || *res == "\n") {
        linefeed();
      } else if (*res == "\b") {
        erase_char();
      } else {
        insert_char(*res);
      }
#ifdef _DEBUG
      if (doc != nullptr) {
        auto s = doc->get(0, doc->length());
        int l = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring wstr((size_t)l, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), wstr.data(),
                            static_cast<int>(wstr.size()));
        OutputDebugStringW(std::format(L"{}\n", wstr).c_str());
      }
#endif
    }
    return 0;
  }
  LRESULT OnSize(int width, int height) {
    viewport.w = width;
    viewport.h = height;
    glViewport(0, 0, width, height);
    return 0;
  }
  LRESULT OnSetFocus() { return 0; }
  LRESULT OnKillFocus() { return 0; }
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

    unique_hglrc ctx;
    {
      auto bootstrap = CreateWglBootstrapContext();
      auto wglGetExtensionsStringARB =
          LoadWglProc<PFNWGLGETEXTENSIONSSTRINGARBPROC>("wglGetExtensionsStringARB");
      auto wglChoosePixelFormatARB =
          LoadWglProc<PFNWGLCHOOSEPIXELFORMATARBPROC>("wglChoosePixelFormatARB");
      auto wglCreateContextAttribsARB =
          LoadWglProc<PFNWGLCREATECONTEXTATTRIBSARBPROC>("wglCreateContextAttribsARB");
      if (HasWglExtension(wglGetExtensionsStringARB, bootstrap->hdc.get(),
                          "WGL_ARB_pixel_format") &&
          wglChoosePixelFormatARB) {
        int pixel_format_attrs[] = {WGL_DRAW_TO_WINDOW_ARB,
                                    TRUE,
                                    WGL_SUPPORT_OPENGL_ARB,
                                    TRUE,
                                    WGL_DOUBLE_BUFFER_ARB,
                                    TRUE,
                                    WGL_ACCELERATION_ARB,
                                    WGL_FULL_ACCELERATION_ARB,
                                    WGL_PIXEL_TYPE_ARB,
                                    WGL_TYPE_RGBA_ARB,
                                    WGL_COLOR_BITS_ARB,
                                    32,
                                    WGL_DEPTH_BITS_ARB,
                                    24,
                                    WGL_STENCIL_BITS_ARB,
                                    8,
                                    0};
        int format = 0;
        UINT format_count = 0;
        if (wglChoosePixelFormatARB(hdc.get(), pixel_format_attrs, nullptr, 1, &format,
                                    &format_count) &&
            format_count > 0) {
          PIXELFORMATDESCRIPTOR pfd{};
          if (DescribePixelFormat(hdc.get(), format, sizeof(pfd), &pfd) == 0 ||
              !SetPixelFormat(hdc.get(), format, &pfd)) {
            throw std::runtime_error{"opengl pixel format error"};
          }
        } else {
          SetLegacyOpenGLPixelFormat(hdc.get());
        }
      } else {
        SetLegacyOpenGLPixelFormat(hdc.get());
      }
      if (HasWglExtension(wglGetExtensionsStringARB, bootstrap->hdc.get(),
                          "WGL_ARB_create_context") &&
          wglCreateContextAttribsARB) {
        int attrs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                       3,
                       WGL_CONTEXT_MINOR_VERSION_ARB,
                       3,
                       WGL_CONTEXT_PROFILE_MASK_ARB,
                       WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                       0};
        ctx.reset(wglCreateContextAttribsARB(hdc.get(), nullptr, attrs));
      }
    }  // boostrap wgl

    if (!ctx) {
      ctx.reset(wglCreateContext(hdc.get()));
      if (!ctx) {
        throw std::runtime_error{"opengl context creation error"};
      }
    }
    if (!wglMakeCurrent(hdc.get(), ctx.get())) {
      throw std::runtime_error{"opengl context activation error"};
    }
    if (!gladLoadGL()) {
      wglMakeCurrent(nullptr, nullptr);
      throw std::runtime_error{"opengl function loading error"};
    }
    glrc_ = std::move(ctx);
    hdc_ = std::move(hdc);
    initialize_graphics();
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case std::to_underlying(SeditMessage::SetDoc):
        return GetThis(hwnd)->OnSet(reinterpret_cast<swg::plaindoc*>(lparam));
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
      case WM_CREATE: {
        auto cs = reinterpret_cast<LPCREATESTRUCT>(lparam);
        auto edit = std::make_unique<Sedit>(hwnd);
        edit->viewport = swg::rect{.x = 0, .y = 0, .w = cs->cx, .h = cs->cy};
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(edit.release()));
        return 0;
      }
      case WM_DESTROY: {
        auto self = GetThis(hwnd);
        std::default_delete<Sedit>{}(self);
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
  unique_hglrc glrc_;
  wchar_t surrogate_[2] = {};
};

const ATOM SeditWndInit = Sedit::Initialize();

}  // namespace
