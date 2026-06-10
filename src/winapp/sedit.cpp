// std
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
// swg
#include "sedit.hpp"

#include <commdlg.h>
#include <windowsx.h>

namespace swg::winapp {

namespace {
constexpr wchar_t kClassName[] = L"SchwingEditChild";
constexpr int kTabCols = 4;

std::wstring utf8_to_utf16(std::string_view s) {
  if (s.empty()) return {};
  const int needed = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                           nullptr, 0);
  if (needed <= 0) return {};
  std::wstring out(static_cast<std::size_t>(needed), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
  return out;
}

std::string utf16_to_utf8(std::wstring_view s) {
  if (s.empty()) return {};
  const int needed = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                           nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return {};
  std::string out(static_cast<std::size_t>(needed), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed,
                        nullptr, nullptr);
  return out;
}

// Replace tab characters with kTabCols spaces for display only. Returns a wstring
// suitable for ExtTextOutW. (MVP: avoids needing to call SetTextCharacterExtraEx.)
std::wstring expand_tabs(std::wstring_view in) {
  std::wstring out;
  out.reserve(in.size());
  int col = 0;
  for (auto c : in) {
    if (c == L'\t') {
      const int n = kTabCols - (col % kTabCols);
      for (int i = 0; i < n; ++i) out.push_back(L' ');
      col += n;
    } else if (c == L'\r' || c == L'\n') {
      // skip
    } else {
      out.push_back(c);
      ++col;
    }
  }
  return out;
}

}  // namespace

Sedit::~Sedit() {
  if (font_) {
    ::DeleteObject(font_);
    font_ = nullptr;
  }
}

ATOM Sedit::Register(HINSTANCE hinst) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.lpfnWndProc = &Sedit::WndProc;
  wc.cbWndExtra = sizeof(Sedit*);
  wc.hInstance = hinst;
  wc.hCursor = ::LoadCursor(nullptr, IDC_IBEAM);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kClassName;
  return ::RegisterClassExW(&wc);
}

HWND Sedit::Create(HINSTANCE hinst, HWND parent, int id) {
  return ::CreateWindowExW(0, kClassName, L"",
                           WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                           0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           hinst, nullptr);
}

LRESULT CALLBACK Sedit::WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  Sedit* self = reinterpret_cast<Sedit*>(::GetWindowLongPtrW(hwnd, 0));
  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<LPCREATESTRUCTW>(l);
      self = new Sedit();
      self->hwnd_ = hwnd;
      ::SetWindowLongPtrW(hwnd, 0, reinterpret_cast<LONG_PTR>(self));
      self->doc_.attach_host(self);
      (void)cs;
      return ::DefWindowProcW(hwnd, msg, w, l);
    }
    case WM_NCDESTROY: {
      if (self) {
        self->doc_.attach_host(nullptr);
        delete self;
        ::SetWindowLongPtrW(hwnd, 0, 0);
      }
      return 0;
    }
  }
  if (!self) return ::DefWindowProcW(hwnd, msg, w, l);
  switch (msg) {
    case WM_CREATE:
      return self->on_create(hwnd, reinterpret_cast<LPCREATESTRUCT>(l));
    case WM_SIZE:
      self->on_size(LOWORD(l), HIWORD(l));
      return 0;
    case WM_PAINT:
      self->on_paint();
      return 0;
    case WM_ERASEBKGND:
      return 1;  // we paint the whole client area in WM_PAINT
    case WM_SETFOCUS:
      self->on_set_focus();
      return 0;
    case WM_KILLFOCUS:
      self->on_kill_focus();
      return 0;
    case WM_LBUTTONDOWN:
      self->on_lbutton_down(GET_X_LPARAM(l), GET_Y_LPARAM(l), w);
      return 0;
    case WM_MOUSEMOVE:
      self->on_mouse_move(GET_X_LPARAM(l), GET_Y_LPARAM(l), w);
      return 0;
    case WM_LBUTTONUP:
      self->on_lbutton_up(GET_X_LPARAM(l), GET_Y_LPARAM(l), w);
      return 0;
    case WM_MOUSEWHEEL:
      self->on_mouse_wheel(GET_WHEEL_DELTA_WPARAM(w), w);
      return 0;
    case WM_CHAR:
      self->on_char(static_cast<wchar_t>(w));
      return 0;
    case WM_KEYDOWN:
      if (self->on_key_down(w, l)) return 0;
      break;
    case WM_VSCROLL: {
      SCROLLINFO si{};
      si.cbSize = sizeof(si);
      si.fMask = SIF_ALL;
      ::GetScrollInfo(hwnd, SB_VERT, &si);
      int newpos = si.nPos;
      const WORD code = LOWORD(w);
      switch (code) {
        case SB_LINEUP: newpos -= 1; break;
        case SB_LINEDOWN: newpos += 1; break;
        case SB_PAGEUP: newpos -= static_cast<int>(si.nPage); break;
        case SB_PAGEDOWN: newpos += static_cast<int>(si.nPage); break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: newpos = si.nTrackPos; break;
        case SB_TOP: newpos = si.nMin; break;
        case SB_BOTTOM: newpos = si.nMax; break;
      }
      newpos = std::clamp(newpos, 0,
                          static_cast<int>(self->doc_.lines().line_count()) - 1);
      if (newpos != self->top_line_) {
        self->top_line_ = newpos;
        si.fMask = SIF_POS;
        si.nPos = newpos;
        ::SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        self->place_caret();
      }
      return 0;
    }
    case WM_HSCROLL: {
      SCROLLINFO si{};
      si.cbSize = sizeof(si);
      si.fMask = SIF_ALL;
      ::GetScrollInfo(hwnd, SB_HORZ, &si);
      int newpos = si.nPos;
      const WORD code = LOWORD(w);
      switch (code) {
        case SB_LINELEFT: newpos -= self->char_w_; break;
        case SB_LINERIGHT: newpos += self->char_w_; break;
        case SB_PAGELEFT: newpos -= static_cast<int>(si.nPage); break;
        case SB_PAGERIGHT: newpos += static_cast<int>(si.nPage); break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: newpos = si.nTrackPos; break;
      }
      newpos = std::max(0, std::min(newpos, si.nMax));
      if (newpos != self->left_col_px_) {
        self->left_col_px_ = newpos;
        si.fMask = SIF_POS;
        si.nPos = newpos;
        ::SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        self->place_caret();
      }
      return 0;
    }
    case WM_DESTROY:
      self->on_destroy();
      return 0;
  }
  return ::DefWindowProcW(hwnd, msg, w, l);
}

LRESULT Sedit::on_create(HWND, LPCREATESTRUCT) {
  ensure_font();
  recompute_metrics();
  update_scrollbars();
  return 0;
}

void Sedit::ensure_font() {
  if (font_) return;
  LOGFONTW lf{};
  lf.lfHeight = -16;
  lf.lfWeight = FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
  lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  lf.lfQuality = CLEARTYPE_QUALITY;
  lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
  wcscpy_s(lf.lfFaceName, L"Consolas");
  font_ = ::CreateFontIndirectW(&lf);
  if (!font_) {
    wcscpy_s(lf.lfFaceName, L"Courier New");
    font_ = ::CreateFontIndirectW(&lf);
  }
}

void Sedit::recompute_metrics() {
  HDC dc = ::GetDC(hwnd_);
  HFONT old = static_cast<HFONT>(::SelectObject(dc, font_));
  TEXTMETRICW tm{};
  ::GetTextMetricsW(dc, &tm);
  line_h_ = tm.tmHeight + tm.tmExternalLeading;
  ascent_ = tm.tmAscent;
  // Average char width is fine for monospace fonts.
  char_w_ = tm.tmAveCharWidth > 0 ? tm.tmAveCharWidth : 8;
  ::SelectObject(dc, old);
  ::ReleaseDC(hwnd_, dc);
}

void Sedit::on_size(int w, int h) {
  client_w_ = w;
  client_h_ = h;
  update_scrollbars();
  place_caret();
}

void Sedit::update_scrollbars() {
  const std::size_t lc = doc_.lines().line_count();
  const int visible_lines = std::max(1, client_h_ / std::max(1, line_h_));
  {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = lc > 0 ? static_cast<int>(lc - 1) : 0;
    si.nPage = static_cast<UINT>(visible_lines);
    si.nPos = std::clamp(top_line_, si.nMin, si.nMax);
    top_line_ = si.nPos;
    ::SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
  }
  {
    // Approximation: assume longest visible width = client_w_ * 4 to allow
    // horizontal scrolling; a precise max-line-width calc is deferred.
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, client_w_ * 4);
    si.nPage = static_cast<UINT>(std::max(1, client_w_));
    si.nPos = std::clamp(left_col_px_, si.nMin, si.nMax);
    left_col_px_ = si.nPos;
    ::SetScrollInfo(hwnd_, SB_HORZ, &si, TRUE);
  }
}

std::wstring Sedit::line_utf16(std::size_t line_idx) const {
  return expand_tabs(utf8_to_utf16(doc_.get_line_text(line_idx)));
}

void Sedit::on_paint() {
  PAINTSTRUCT ps{};
  HDC dc = ::BeginPaint(hwnd_, &ps);

  // Double-buffer using a memory DC to avoid flicker.
  HDC mem = ::CreateCompatibleDC(dc);
  HBITMAP bmp = ::CreateCompatibleBitmap(dc, client_w_, client_h_);
  HBITMAP oldbmp = static_cast<HBITMAP>(::SelectObject(mem, bmp));
  HFONT oldfont = static_cast<HFONT>(::SelectObject(mem, font_));
  ::SetBkMode(mem, OPAQUE);

  RECT full{0, 0, client_w_, client_h_};
  HBRUSH bg = ::GetSysColorBrush(COLOR_WINDOW);
  ::FillRect(mem, &full, bg);
  ::SetTextColor(mem, ::GetSysColor(COLOR_WINDOWTEXT));
  ::SetBkColor(mem, ::GetSysColor(COLOR_WINDOW));

  const std::size_t lc = doc_.lines().line_count();
  const int visible_lines = (client_h_ + line_h_ - 1) / std::max(1, line_h_);

  const auto [sel_lo, sel_hi] = doc_.selection_range();
  const bool has_sel = sel_lo != sel_hi;

  for (int i = 0; i < visible_lines; ++i) {
    const std::size_t li = static_cast<std::size_t>(top_line_) + static_cast<std::size_t>(i);
    if (li >= lc) break;
    const int y = i * line_h_;

    const auto l = doc_.lines().at(li);
    const std::size_t line_text_len = (l.length > l.eol_bytes) ? (l.length - l.eol_bytes) : 0;

    // Draw selection rect first.
    if (has_sel) {
      const std::size_t line_beg = l.beg;
      const std::size_t line_end = l.beg + line_text_len;
      if (sel_hi > line_beg && sel_lo < line_end + 1 /* include EOL slot */) {
        std::size_t s = std::max(sel_lo, line_beg);
        std::size_t e = std::min(sel_hi, line_end);
        // Compute pixel x for s and e by measuring the line prefix in UTF-16.
        auto utf16_line = utf8_to_utf16(doc_.get_line_text(li));
        // Map byte offsets within the line (utf-8) to wide-char counts by
        // re-converting prefixes (acceptable for visible lines; ASCII fast).
        auto x_for_byte = [&](std::size_t off_bytes) -> int {
          off_bytes = std::min(off_bytes, line_text_len);
          if (off_bytes == 0) return -left_col_px_;
          auto prefix_utf16 =
              expand_tabs(utf8_to_utf16(std::string_view(doc_.get_line_text(li))
                                            .substr(0, off_bytes)));
          SIZE sz{};
          ::GetTextExtentPoint32W(mem, prefix_utf16.c_str(),
                                  static_cast<int>(prefix_utf16.size()), &sz);
          return sz.cx - left_col_px_;
        };
        int x_s = x_for_byte(s - line_beg);
        int x_e = x_for_byte(e - line_beg);
        if (sel_hi > line_end) {
          // Selection extends past end of line — paint EOL slot too.
          x_e = std::max(x_e + char_w_, x_e);
        }
        RECT r{x_s, y, x_e, y + line_h_};
        HBRUSH selbr = ::GetSysColorBrush(COLOR_HIGHLIGHT);
        ::FillRect(mem, &r, selbr);
      }
    }

    // Draw the line text.
    auto wline = line_utf16(li);
    if (!wline.empty()) {
      // Highlight foreground inside selection by drawing twice with rgn? For
      // simplicity, draw single color and let selection rect provide contrast.
      ::ExtTextOutW(mem, -left_col_px_, y, ETO_CLIPPED, &full, wline.c_str(),
                    static_cast<UINT>(wline.size()), nullptr);
    }
  }

  ::BitBlt(dc, 0, 0, client_w_, client_h_, mem, 0, 0, SRCCOPY);

  ::SelectObject(mem, oldfont);
  ::SelectObject(mem, oldbmp);
  ::DeleteObject(bmp);
  ::DeleteDC(mem);
  ::EndPaint(hwnd_, &ps);
}

void Sedit::on_set_focus() {
  ::CreateCaret(hwnd_, nullptr, 2, line_h_);
  place_caret();
  ::ShowCaret(hwnd_);
}

void Sedit::on_kill_focus() {
  ::HideCaret(hwnd_);
  ::DestroyCaret();
}

POINT Sedit::pos_to_xy(std::size_t pos) const {
  POINT p{0, 0};
  const auto [li, col] = doc_.lines().line_col(pos);
  const int y = (static_cast<int>(li) - top_line_) * line_h_;
  // measure prefix
  HDC dc = ::GetDC(hwnd_);
  HFONT old = static_cast<HFONT>(::SelectObject(dc, font_));
  auto prefix =
      expand_tabs(utf8_to_utf16(std::string_view(doc_.get_line_text(li)).substr(0, col)));
  SIZE sz{};
  ::GetTextExtentPoint32W(dc, prefix.c_str(), static_cast<int>(prefix.size()), &sz);
  ::SelectObject(dc, old);
  ::ReleaseDC(hwnd_, dc);
  p.x = sz.cx - left_col_px_;
  p.y = y;
  return p;
}

void Sedit::place_caret() {
  if (::GetFocus() != hwnd_) return;
  const auto p = pos_to_xy(doc_.caret());
  ::SetCaretPos(p.x, p.y);
}

void Sedit::on_lbutton_down(int x, int y, WPARAM mods) {
  ::SetFocus(hwnd_);
  ::SetCapture(hwnd_);
  selecting_ = true;
  const bool extend = (mods & MK_SHIFT) != 0;
  const std::size_t pos = hit_test(x, y);
  doc_.set_caret(pos, extend);
  place_caret();
  ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void Sedit::on_mouse_move(int x, int y, WPARAM /*mods*/) {
  if (!selecting_) return;
  const std::size_t pos = hit_test(x, y);
  doc_.set_caret(pos, true);
  place_caret();
  ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void Sedit::on_lbutton_up(int /*x*/, int /*y*/, WPARAM /*mods*/) {
  if (selecting_) {
    selecting_ = false;
    ::ReleaseCapture();
  }
}

void Sedit::on_mouse_wheel(int delta, WPARAM /*mods*/) {
  const int lines = -(delta / WHEEL_DELTA) * 3;
  int newtop = std::clamp(top_line_ + lines, 0,
                          static_cast<int>(doc_.lines().line_count()) - 1);
  if (newtop != top_line_) {
    top_line_ = newtop;
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    si.nPos = newtop;
    ::SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    place_caret();
  }
}

std::size_t Sedit::hit_test(int x, int y) const {
  const int row = y / std::max(1, line_h_);
  const std::size_t lc = doc_.lines().line_count();
  std::size_t li = std::min(lc - 1, static_cast<std::size_t>(std::max(0, top_line_ + row)));
  const auto line = doc_.lines().at(li);
  const std::size_t text_len = (line.length > line.eol_bytes) ? (line.length - line.eol_bytes) : 0;
  if (text_len == 0) return line.beg;
  auto utf8_line = doc_.get_line_text(li);
  // Find the byte column whose prefix's pixel width is closest to (x + left_col_px_).
  const int target = std::max(0, x + left_col_px_);
  HDC dc = ::GetDC(hwnd_);
  HFONT old = static_cast<HFONT>(::SelectObject(dc, font_));
  std::size_t best = 0;
  int best_diff = std::numeric_limits<int>::max();
  for (std::size_t i = 0; i <= utf8_line.size(); ++i) {
    if (i > 0 && i < utf8_line.size()) {
      // Skip UTF-8 continuation bytes.
      if ((static_cast<unsigned char>(utf8_line[i]) & 0xC0) == 0x80) continue;
    }
    auto prefix = expand_tabs(utf8_to_utf16(std::string_view(utf8_line).substr(0, i)));
    SIZE sz{};
    ::GetTextExtentPoint32W(dc, prefix.c_str(), static_cast<int>(prefix.size()), &sz);
    const int diff = std::abs(sz.cx - target);
    if (diff < best_diff) {
      best_diff = diff;
      best = i;
    }
    if (sz.cx > target + char_w_) break;  // we've overshot
  }
  ::SelectObject(dc, old);
  ::ReleaseDC(hwnd_, dc);
  return line.beg + best;
}

void Sedit::on_char(wchar_t ch) {
  // Reject ASCII control chars except those mapped by WM_KEYDOWN.
  if (ch < 0x20 || ch == 0x7F) return;
  wchar_t buf[2] = {ch, 0};
  auto utf8 = utf16_to_utf8(buf);
  doc_.insert_text(utf8);
  scroll_to_caret();
}

bool Sedit::on_key_down(WPARAM vk, LPARAM /*lp*/) {
  const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
  using motion = swg::plaindoc::motion;
  switch (vk) {
    case VK_LEFT:
      doc_.move_caret(ctrl ? motion::left_word : motion::left_char, shift);
      scroll_to_caret();
      return true;
    case VK_RIGHT:
      doc_.move_caret(ctrl ? motion::right_word : motion::right_char, shift);
      scroll_to_caret();
      return true;
    case VK_UP:
      doc_.move_caret(motion::line_up, shift);
      scroll_to_caret();
      return true;
    case VK_DOWN:
      doc_.move_caret(motion::line_down, shift);
      scroll_to_caret();
      return true;
    case VK_HOME:
      doc_.move_caret(ctrl ? motion::doc_home : motion::line_home, shift);
      scroll_to_caret();
      return true;
    case VK_END:
      doc_.move_caret(ctrl ? motion::doc_end : motion::line_end, shift);
      scroll_to_caret();
      return true;
    case VK_PRIOR: {  // page up
      const int n = std::max(1, client_h_ / std::max(1, line_h_));
      for (int i = 0; i < n; ++i) doc_.move_caret(motion::line_up, shift);
      scroll_to_caret();
      return true;
    }
    case VK_NEXT: {  // page down
      const int n = std::max(1, client_h_ / std::max(1, line_h_));
      for (int i = 0; i < n; ++i) doc_.move_caret(motion::line_down, shift);
      scroll_to_caret();
      return true;
    }
    case VK_BACK:
      doc_.backspace();
      scroll_to_caret();
      return true;
    case VK_DELETE:
      doc_.delete_forward();
      scroll_to_caret();
      return true;
    case VK_TAB:
      doc_.insert_text("\t");
      scroll_to_caret();
      return true;
    case VK_RETURN:
      doc_.newline();
      scroll_to_caret();
      return true;
    case 'A':
      if (ctrl) {
        doc_.select_all();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
      }
      break;
    case 'S':
      if (ctrl) {
        // Save dialog
        wchar_t path[MAX_PATH] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.txt\0\0";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (::GetSaveFileNameW(&ofn)) save_file(path);
        return true;
      }
      break;
    case 'O':
      if (ctrl) {
        wchar_t path[MAX_PATH] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.txt\0\0";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (::GetOpenFileNameW(&ofn)) open_file(path);
        return true;
      }
      break;
  }
  return false;
}

void Sedit::scroll_to_caret() {
  const auto [li, col] = doc_.lines().line_col(doc_.caret());
  const int line_idx = static_cast<int>(li);
  bool changed = false;
  const int visible_lines = std::max(1, client_h_ / std::max(1, line_h_));
  if (line_idx < top_line_) {
    top_line_ = line_idx;
    changed = true;
  } else if (line_idx >= top_line_ + visible_lines) {
    top_line_ = line_idx - visible_lines + 1;
    changed = true;
  }
  if (changed) {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    si.nPos = top_line_;
    ::SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
  }
  // Horizontal scroll to caret
  HDC dc = ::GetDC(hwnd_);
  HFONT old = static_cast<HFONT>(::SelectObject(dc, font_));
  auto prefix =
      expand_tabs(utf8_to_utf16(std::string_view(doc_.get_line_text(li)).substr(0, col)));
  SIZE sz{};
  ::GetTextExtentPoint32W(dc, prefix.c_str(), static_cast<int>(prefix.size()), &sz);
  ::SelectObject(dc, old);
  ::ReleaseDC(hwnd_, dc);
  const int caret_x = sz.cx;
  if (caret_x - left_col_px_ < 0) {
    left_col_px_ = std::max(0, caret_x - 16);
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    si.nPos = left_col_px_;
    ::SetScrollInfo(hwnd_, SB_HORZ, &si, TRUE);
    changed = true;
  } else if (caret_x - left_col_px_ >= client_w_) {
    left_col_px_ = caret_x - client_w_ + 16;
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    si.nPos = left_col_px_;
    ::SetScrollInfo(hwnd_, SB_HORZ, &si, TRUE);
    changed = true;
  }
  if (changed) ::InvalidateRect(hwnd_, nullptr, FALSE);
  place_caret();
}

void Sedit::on_invalidate() {
  if (hwnd_) ::InvalidateRect(hwnd_, nullptr, FALSE);
  update_scrollbars();
}

void Sedit::on_doc_changed(const swg::damage&) {
  update_scrollbars();
  ::InvalidateRect(hwnd_, nullptr, FALSE);
  place_caret();
}

void Sedit::on_caret_moved() { place_caret(); }

void Sedit::on_destroy() {}

bool Sedit::open_file(const wchar_t* path) {
  HANDLE h = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  if (!::GetFileSizeEx(h, &sz)) {
    ::CloseHandle(h);
    return false;
  }
  owned_bytes_.assign(static_cast<std::size_t>(sz.QuadPart), '\0');
  DWORD total_read = 0;
  while (total_read < owned_bytes_.size()) {
    DWORD chunk = static_cast<DWORD>(
        std::min<std::size_t>(owned_bytes_.size() - total_read, 1u << 22));
    DWORD got = 0;
    if (!::ReadFile(h, owned_bytes_.data() + total_read, chunk, &got, nullptr) || got == 0)
      break;
    total_read += got;
  }
  ::CloseHandle(h);
  owned_bytes_.resize(total_read);
  doc_.load_view(owned_bytes_);
  top_line_ = 0;
  left_col_px_ = 0;
  update_scrollbars();
  ::InvalidateRect(hwnd_, nullptr, FALSE);
  place_caret();
  return true;
}

bool Sedit::save_file(const wchar_t* path) {
  HANDLE h = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  auto bytes = doc_.buffer().str();
  DWORD written = 0;
  ::WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
  ::CloseHandle(h);
  // After save, materialize so we no longer reference any mmap.
  doc_.materialize();
  return written == bytes.size();
}

void Sedit::invalidate_all() {
  if (hwnd_) ::InvalidateRect(hwnd_, nullptr, FALSE);
}

}  // namespace swg::winapp
