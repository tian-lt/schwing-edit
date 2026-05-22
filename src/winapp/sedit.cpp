// std
#include <algorithm>
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

namespace {

const double default_font_size = 12.0;
const std::string default_font_path = "C:\\Windows\\Fonts\\Arial.ttf";
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
  Sedit(HWND hwnd, std::string fontpath, double fontsize)
      : hwnd_(hwnd), doc_(this, std::move(fontpath), fontsize, swg::eol::crlf) {
    doc = &doc_;
    double ratio = GetDpiForWindow(hwnd_) / 96.0;
    caretPosX_ = 4 * ratio;
    caretPosY_ = 2 * ratio;
    caretWidth_ = 1 * ratio;
    caretHeight_ = 24 * ratio;
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
    auto hdc = wil::BeginPaint(hwnd_, &ps);
    glClearColor(1.f, 1.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    render({.x = ps.rcPaint.left,
            .y = ps.rcPaint.top,
            .w = ps.rcPaint.right - ps.rcPaint.left,
             .h = ps.rcPaint.bottom - ps.rcPaint.top});
    SwapBuffers(hdc_.get());
    DrawDocumentText(hdc.get());
    return 0;
  }

  LRESULT OnChar(wchar_t uchar) {
    if (uchar < L' ' && uchar != L'\r' && uchar != L'\n' && uchar != L'\b') {
      return 0;
    }
    if (auto res = DigestChar(uchar); res.has_value()) {
      bool changed = false;
      if (HasSelection() && *res != "\b") {
        SaveUndo();
        DeleteSelection();
        changed = true;
      }
      if (*res == "\r" || *res == "\n") {
        if (!changed) {
          SaveUndo();
        }
        linefeed();
        changed = true;
      } else if (*res == "\b") {
        if (!changed) {
          SaveUndo();
        }
        if (HasSelection()) {
          DeleteSelection();
        } else {
          erase_char();
        }
        changed = true;
      } else {
        if (!changed) {
          SaveUndo();
        }
        insert_char(*res);
        changed = true;
      }
      ClearSelection();
      UpdateCaretFromModel();
      InvalidateRect(hwnd_, nullptr, FALSE);
      if (changed) {
        NotifyChange();
      }
#ifdef _DEBUG
      auto s = doc_.get(0, doc_.length());
      int l = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
      std::wstring wstr((size_t)l, 0);
      MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), wstr.data(),
                          static_cast<int>(wstr.size()));
      OutputDebugStringW(std::format(L"{}\n", wstr).c_str());
#endif
    }
    return 0;
  }
  LRESULT OnKeyDown(WPARAM key, LPARAM /*lparam*/) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool changed = false;
    if (ctrl && key == 'V') {
      SaveUndo();
      if (HasSelection()) {
        DeleteSelection();
      }
      PasteFromClipboard();
      ClearSelection();
      changed = true;
    } else if (ctrl && key == 'A') {
      SelectRange(0, doc_.length());
      UpdateCaretFromModel();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    } else if (ctrl && key == 'Z') {
      Undo();
      return 0;
    } else if (ctrl && key == 'C') {
      CopySelectionToClipboard();
      return 0;
    } else if (ctrl && key == 'X') {
      CopySelectionToClipboard();
      changed = DeleteSelection();
    } else if (ctrl && key == 'N') {
      SendMessage(GetParent(hwnd_), WM_COMMAND, IDM_FILE_NEW, 0);
      return 0;
    } else if (ctrl && key == 'O') {
      SendMessage(GetParent(hwnd_), WM_COMMAND, IDM_FILE_OPEN, 0);
      return 0;
    } else if (ctrl && key == 'S') {
      SendMessage(GetParent(hwnd_), WM_COMMAND, IDM_FILE_SAVE, 0);
      return 0;
    } else {
      switch (key) {
        case VK_DELETE:
          SaveUndo();
          if (HasSelection()) {
            DeleteSelection();
          } else {
            delete_char();
          }
          changed = true;
          break;
        case VK_LEFT:
          ClearSelection();
          move_left();
          break;
        case VK_RIGHT:
          ClearSelection();
          move_right();
          break;
        case VK_HOME:
          ClearSelection();
          caret_pos(0);
          break;
        case VK_END:
          ClearSelection();
          caret_pos(doc_.length());
          break;
        default:
          return 0;
      }
    }
    UpdateCaretFromModel();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (changed) {
      NotifyChange();
    }
    return 0;
  }
  LRESULT OnSize(int width, int height) {
    viewport.w = width;
    viewport.h = height;
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
  std::wstring DocumentTextW() const {
    auto text = doc_.get(0, doc_.length());
    if (text.empty()) {
      return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    THROW_LAST_ERROR_IF(len <= 0);
    std::wstring result(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        static_cast<int>(result.size()));
    return result;
  }
  void DrawDocumentText(HDC hdc) const {
    RECT rc;
    if (!GetClientRect(hwnd_, &rc)) {
      return;
    }
    double ratio = GetDpiForWindow(hwnd_) / 96.0;
    rc.left += static_cast<LONG>(4 * ratio);
    rc.top += static_cast<LONG>(2 * ratio);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));
    auto text = DocumentTextW();
    UINT format = DT_LEFT | DT_TOP | DT_EXPANDTABS | DT_NOPREFIX;
    if (!wordWrap_) {
      format |= DT_NOCLIP;
    }
    DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rc, format);
  }
  void UpdateCaretFromModel() {
    auto prefix = doc_.get(0, caret_pos());
    size_t line = 0;
    size_t column = 0;
    for (size_t i = 0; i < prefix.size(); ++i) {
      if (prefix[i] == '\r') {
        if (i + 1 < prefix.size() && prefix[i + 1] == '\n') {
          ++i;
        }
        ++line;
        column = 0;
      } else if (prefix[i] == '\n') {
        ++line;
        column = 0;
      } else if ((static_cast<unsigned char>(prefix[i]) & 0xC0) != 0x80) {
        ++column;
      }
    }
    double ratio = GetDpiForWindow(hwnd_) / 96.0;
    caretPosX_ = static_cast<int>((4 + column * 8) * ratio);
    caretPosY_ = static_cast<int>((2 + line * 20) * ratio);
    SetCaretPos(caretPosX_, caretPosY_);
  }
  bool HasSelection() const { return selectionStart_ != selectionEnd_; }
  void ClearSelection() {
    selectionStart_ = caret_pos();
    selectionEnd_ = caret_pos();
  }
  void SelectRange(size_t start, size_t end) {
    selectionStart_ = std::min(start, doc_.length());
    selectionEnd_ = std::min(end, doc_.length());
    caret_pos(selectionEnd_);
  }
  std::pair<size_t, size_t> NormalizedSelection() const {
    return std::minmax(selectionStart_, selectionEnd_);
  }
  bool DeleteSelection() {
    if (!HasSelection()) {
      return false;
    }
    auto [start, end] = NormalizedSelection();
    doc_.erase(start, end - start);
    caret_pos(start);
    ClearSelection();
    return true;
  }
  size_t CharIndexToByteOffset(size_t char_index) const {
    auto text = doc_.get(0, doc_.length());
    size_t chars = 0;
    for (size_t i = 0; i < text.size(); ++i) {
      if (chars == char_index) {
        return i;
      }
      if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
        ++chars;
      }
    }
    return text.size();
  }
  size_t ByteOffsetToCharIndex(size_t byte_offset) const {
    auto text = doc_.get(0, std::min(byte_offset, doc_.length()));
    size_t chars = 0;
    for (char c : text) {
      if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
        ++chars;
      }
    }
    return chars;
  }
  void PasteFromClipboard() {
    if (!OpenClipboard(hwnd_)) {
      return;
    }
    auto close_clipboard = wil::scope_exit([] { CloseClipboard(); });
    HGLOBAL clipboard_data = GetClipboardData(CF_UNICODETEXT);
    if (!clipboard_data) {
      return;
    }
    auto text = static_cast<const wchar_t*>(GlobalLock(clipboard_data));
    if (!text) {
      return;
    }
    auto unlock = wil::scope_exit([&] { GlobalUnlock(clipboard_data); });
    int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    THROW_LAST_ERROR_IF(len <= 0);
    std::string utf8(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), len, nullptr, nullptr);
    insert_char(utf8);
  }
  void SaveUndo() {
    undoText_ = DocumentTextW();
    undoCaret_ = ByteOffsetToCharIndex(caret_pos());
    hasUndo_ = true;
  }
  void Undo() {
    if (!hasUndo_) {
      return;
    }
    auto text = std::move(undoText_);
    auto caret = undoCaret_;
    hasUndo_ = false;
    OnSetText(reinterpret_cast<LPARAM>(text.c_str()));
    OnSetSel(caret, caret);
    NotifyChange();
  }
  void CopySelectionToClipboard() {
    if (!HasSelection()) {
      return;
    }
    auto [start, end] = NormalizedSelection();
    auto bytes = doc_.get(start, end - start);
    int len =
        MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    THROW_LAST_ERROR_IF(len <= 0);
    std::wstring text(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), len);
    if (!OpenClipboard(hwnd_)) {
      return;
    }
    auto close_clipboard = wil::scope_exit([] { CloseClipboard(); });
    EmptyClipboard();
    const size_t clipboard_bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, clipboard_bytes);
    if (!memory) {
      return;
    }
    auto data = static_cast<wchar_t*>(GlobalLock(memory));
    if (!data) {
      GlobalFree(memory);
      return;
    }
    std::copy_n(text.c_str(), text.size() + 1, data);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
      GlobalFree(memory);
    }
  }
  void ClearDocument() {
    doc_.erase(0, doc_.length());
    caret_pos(0);
    ClearSelection();
  }
  LRESULT OnGetTextLength() const { return static_cast<LRESULT>(DocumentTextW().size()); }
  LRESULT OnGetText(WPARAM cch, LPARAM buffer) const {
    if (cch == 0 || buffer == 0) {
      return 0;
    }
    auto text = DocumentTextW();
    auto out = reinterpret_cast<wchar_t*>(buffer);
    const size_t count = std::min(static_cast<size_t>(cch - 1), text.size());
    std::copy_n(text.c_str(), count, out);
    out[count] = L'\0';
    return static_cast<LRESULT>(count);
  }
  LRESULT OnSetText(LPARAM text) {
    auto wtext = reinterpret_cast<const wchar_t*>(text);
    doc_.erase(0, doc_.length());
    if (wtext && *wtext) {
      int len = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, nullptr, 0, nullptr, nullptr);
      THROW_LAST_ERROR_IF(len <= 0);
      std::string utf8(static_cast<size_t>(len - 1), '\0');
      WideCharToMultiByte(CP_UTF8, 0, wtext, -1, utf8.data(), len, nullptr, nullptr);
      doc_.insert(0, utf8);
    }
    caret_pos(0);
    ClearSelection();
    UpdateCaretFromModel();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return TRUE;
  }
  LRESULT OnGetSel(WPARAM start, LPARAM end) const {
    auto [selection_start, selection_end] = NormalizedSelection();
    DWORD start_char = static_cast<DWORD>(ByteOffsetToCharIndex(selection_start));
    DWORD end_char = static_cast<DWORD>(ByteOffsetToCharIndex(selection_end));
    if (start) {
      *reinterpret_cast<DWORD*>(start) = start_char;
    }
    if (end) {
      *reinterpret_cast<DWORD*>(end) = end_char;
    }
    return MAKELRESULT(start_char, end_char);
  }
  LRESULT OnSetSel(WPARAM start, LPARAM end) {
    size_t start_char = static_cast<size_t>(start);
    size_t end_char = static_cast<size_t>(end);
    if (static_cast<int>(start) < 0) {
      start_char = doc_.length();
      end_char = doc_.length();
    } else if (static_cast<int>(end) < 0) {
      end_char = start_char;
    }
    SelectRange(CharIndexToByteOffset(start_char), CharIndexToByteOffset(end_char));
    UpdateCaretFromModel();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return 0;
  }
  void NotifyChange() {
    SendMessage(GetParent(hwnd_), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd_), EN_CHANGE),
                reinterpret_cast<LPARAM>(hwnd_));
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
    } // boostrap wgl

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
      case WM_KEYDOWN:
        return GetThis(hwnd)->OnKeyDown(wparam, lparam);
      case WM_GETTEXTLENGTH:
        return GetThis(hwnd)->OnGetTextLength();
      case WM_GETTEXT:
        return GetThis(hwnd)->OnGetText(wparam, lparam);
      case WM_SETTEXT:
        return GetThis(hwnd)->OnSetText(lparam);
      case SEDIT_SET_WORD_WRAP:
        GetThis(hwnd)->wordWrap_ = wparam != 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      case WM_COPY:
        GetThis(hwnd)->CopySelectionToClipboard();
        return 0;
      case WM_CUT:
        GetThis(hwnd)->CopySelectionToClipboard();
        if (GetThis(hwnd)->DeleteSelection()) {
          GetThis(hwnd)->UpdateCaretFromModel();
          InvalidateRect(hwnd, nullptr, FALSE);
          GetThis(hwnd)->NotifyChange();
        }
        return 0;
      case WM_PASTE:
        GetThis(hwnd)->SaveUndo();
        if (GetThis(hwnd)->HasSelection()) {
          GetThis(hwnd)->DeleteSelection();
        }
        GetThis(hwnd)->PasteFromClipboard();
        GetThis(hwnd)->ClearSelection();
        GetThis(hwnd)->UpdateCaretFromModel();
        InvalidateRect(hwnd, nullptr, FALSE);
        GetThis(hwnd)->NotifyChange();
        return 0;
      case WM_CLEAR:
        GetThis(hwnd)->SaveUndo();
        if (GetThis(hwnd)->DeleteSelection()) {
          GetThis(hwnd)->UpdateCaretFromModel();
          InvalidateRect(hwnd, nullptr, FALSE);
          GetThis(hwnd)->NotifyChange();
        }
        return 0;
      case WM_UNDO:
        GetThis(hwnd)->Undo();
        return 0;
      case EM_GETSEL:
        return GetThis(hwnd)->OnGetSel(wparam, lparam);
      case EM_SETSEL:
        return GetThis(hwnd)->OnSetSel(wparam, lparam);
      case WM_SETFOCUS:
        return GetThis(hwnd)->OnSetFocus();
      case WM_KILLFOCUS:
        return GetThis(hwnd)->OnKillFocus();
      case WM_CREATE: {
        auto cs = reinterpret_cast<LPCREATESTRUCT>(lparam);
        auto edit = std::make_unique<Sedit>(hwnd, default_font_path, default_font_size);
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
  swg::plaindoc doc_;
  wchar_t surrogate_[2] = {};
  int caretPosX_ = 0;
  int caretPosY_ = 0;
  int caretWidth_ = 0;
  int caretHeight_ = 0;
  size_t selectionStart_ = 0;
  size_t selectionEnd_ = 0;
  std::wstring undoText_;
  size_t undoCaret_ = 0;
  bool hasUndo_ = false;
  bool wordWrap_ = true;
};

const ATOM SeditWndInit = Sedit::Initialize();

}  // namespace
