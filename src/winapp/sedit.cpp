// std
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
// windows
#include "win.hpp"
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
// wil
#include <wil/resource.h>
#include <wil/result_macros.h>
// app
#include "res.h"
#include "glcaret.hpp"
#include "theme.hpp"
// swg
#include <plaindoc.hpp>

namespace {

const double default_font_size = 12.0;
const std::string default_font_path = "C:\\Windows\\Fonts\\Arial.ttf";

// Initial file path to open when the Sedit window is created (set by
// winmain.cpp when a file is passed on the command line).
std::wstring g_initial_file_path;

// Find/Replace common dialog state. The dialog is modeless and requires the
// FINDREPLACE struct + buffers to live as long as the dialog itself, so we
// keep them as TU-locals.
HWND g_find_dialog = nullptr;
UINT g_find_msg = 0;
FINDREPLACEW g_findrep{};
wchar_t g_find_what[256] = {};
wchar_t g_replace_with[256] = {};
HWND g_find_owner = nullptr;  // child Sedit hwnd that should receive find events.

// Software-composite all shaped glyphs from the R8 atlas onto a 32-bit ARGB
// back-buffer using the standard "lerp(bg, fg, alpha/255)" formula. Reads the
// existing destination pixel as the background so anti-aliased glyphs blend
// over whatever was previously drawn (e.g., selection highlight rectangles).
// Pure CPU so this runs in any environment, including Hyper-V VMs without a
// GPU.
void CompositeAllGlyphs(uint32_t* dst, int dst_w, int dst_h, const uint8_t* atlas,
                        int atlas_w, int atlas_h, const swg::layout_result& layout,
                        uint32_t fg) {
  const uint32_t fg_r = (fg >> 16) & 0xFF;
  const uint32_t fg_g = (fg >> 8) & 0xFF;
  const uint32_t fg_b = fg & 0xFF;
  for (const auto& q : layout.glyphs) {
    const int gw = static_cast<int>(q.w);
    const int gh = static_cast<int>(q.h);
    if (gw <= 0 || gh <= 0) continue;
    const int gx = static_cast<int>(q.x);
    const int gy = static_cast<int>(q.y);
    const int au0 = static_cast<int>(q.u0 * atlas_w + 0.5f);
    const int av0 = static_cast<int>(q.v0 * atlas_h + 0.5f);
    for (int dy = 0; dy < gh; ++dy) {
      const int yy = gy + dy;
      if (yy < 0 || yy >= dst_h) continue;
      const uint8_t* sr = atlas + (av0 + dy) * atlas_w + au0;
      uint32_t* dr = dst + static_cast<size_t>(yy) * dst_w;
      for (int dx = 0; dx < gw; ++dx) {
        const int xx = gx + dx;
        if (xx < 0 || xx >= dst_w) continue;
        const uint8_t a = sr[dx];
        if (a == 0) continue;
        const uint32_t ia = 255u - a;
        const uint32_t bgp = dr[xx];
        const uint32_t bg_r = (bgp >> 16) & 0xFF;
        const uint32_t bg_g = (bgp >> 8) & 0xFF;
        const uint32_t bg_b = bgp & 0xFF;
        const uint32_t r = (bg_r * ia + fg_r * a) / 255u;
        const uint32_t gn = (bg_g * ia + fg_g * a) / 255u;
        const uint32_t b = (bg_b * ia + fg_b * a) / 255u;
        dr[xx] = (r << 16) | (gn << 8) | b;
      }
    }
  }
}

// Fill an axis-aligned rectangle in the back-buffer with a solid color.
void FillRect32(uint32_t* dst, int dst_w, int dst_h, int x0, int y0, int x1, int y1,
                uint32_t color) {
  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(dst_w, x1);
  y1 = std::min(dst_h, y1);
  for (int y = y0; y < y1; ++y) {
    uint32_t* row = dst + static_cast<size_t>(y) * dst_w;
    for (int x = x0; x < x1; ++x) row[x] = color;
  }
}

// Draw selection rectangles for the given byte range [sel_b, sel_e). Walks the
// `layout` caret anchors, groups them by baseline_y to recover lines, and fills
// one rect per visible line that intersects the selection. When the selection
// extends past a line's last text byte, the rectangle is extended a small
// amount to indicate the newline is selected too (Notepad behavior).
void RenderSelection(uint32_t* dst, int dst_w, int dst_h, const swg::layout_result& layout,
                     size_t sel_b, size_t sel_e, uint32_t color) {
  if (sel_b >= sel_e || layout.carets.empty()) return;
  const int ascent = layout.ascent;
  const int line_h = layout.line_height;
  if (line_h <= 0) return;
  const int nl_extra = line_h / 3;  // extra pixels to indicate trailing newline
  // Group anchors by baseline_y (each line group is a contiguous run).
  size_t i = 0;
  while (i < layout.carets.size()) {
    const float by = layout.carets[i].baseline_y;
    size_t j = i + 1;
    while (j < layout.carets.size() && layout.carets[j].baseline_y == by) ++j;
    // [i, j) are the anchors on this line.
    size_t line_min = layout.carets[i].byte_pos;
    size_t line_max = layout.carets[i].byte_pos;
    float left_text_x = layout.carets[i].x;
    float right_text_x = layout.carets[i].x;
    for (size_t k = i; k < j; ++k) {
      line_min = std::min(line_min, layout.carets[k].byte_pos);
      line_max = std::max(line_max, layout.carets[k].byte_pos);
      left_text_x = std::min(left_text_x, layout.carets[k].x);
      right_text_x = std::max(right_text_x, layout.carets[k].x);
    }
    if (line_max < sel_b || line_min >= sel_e) {
      i = j;
      continue;
    }
    // Find anchors at the clamped selection boundaries on this line.
    float x_lo = left_text_x;
    float x_hi = right_text_x;
    if (sel_b > line_min) {
      x_lo = right_text_x;  // worst-case fallback (overlaps fully)
      for (size_t k = i; k < j; ++k) {
        if (layout.carets[k].byte_pos == sel_b) {
          x_lo = layout.carets[k].x;
          break;
        }
      }
    }
    if (sel_e < line_max) {
      x_hi = left_text_x;
      for (size_t k = i; k < j; ++k) {
        if (layout.carets[k].byte_pos == sel_e) {
          x_hi = layout.carets[k].x;
          break;
        }
      }
    } else if (sel_e > line_max) {
      // Selection wraps over the end of this line — extend a bit.
      x_hi = right_text_x + static_cast<float>(nl_extra);
    }
    const int y0 = static_cast<int>(by) - ascent;
    const int y1 = static_cast<int>(by) + (line_h - ascent);
    FillRect32(dst, dst_w, dst_h, static_cast<int>(x_lo), y0, static_cast<int>(x_hi), y1,
               color);
    i = j;
  }
}

// Read the entire contents of a file as raw bytes.
std::optional<std::vector<uint8_t>> ReadFileBytes(const std::wstring& path);
bool WriteFileBytes(const std::wstring& path, std::string_view bytes);

// UTF-8 string → wide string for Win32 APIs.
std::wstring Utf8ToWide(std::string_view s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

// Wide string → UTF-8 for the document.
std::string WideToUtf8(std::wstring_view w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                              nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                      nullptr);
  return s;
}

// Read the entire contents of a file as raw bytes.
std::optional<std::vector<uint8_t>> ReadFileBytes(const std::wstring& path) {
  std::ifstream f(std::filesystem::path(path), std::ios::binary);
  if (!f) return std::nullopt;
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
  return buf;
}

bool WriteFileBytes(const std::wstring& path, std::string_view bytes) {
  std::ofstream f(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return f.good();
}

// Decode raw file bytes into UTF-8. Detects BOMs for UTF-8 / UTF-16 LE / BE.
// For files with no BOM, treats the bytes as UTF-8 verbatim.
std::string DecodeToUtf8(const std::vector<uint8_t>& bytes) {
  // UTF-8 BOM
  if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
    return std::string(reinterpret_cast<const char*>(bytes.data() + 3), bytes.size() - 3);
  }
  // UTF-16 LE BOM
  if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
    const wchar_t* wp = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
    size_t wn = (bytes.size() - 2) / sizeof(wchar_t);
    return WideToUtf8({wp, wn});
  }
  // UTF-16 BE BOM
  if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
    std::wstring w((bytes.size() - 2) / 2, L'\0');
    for (size_t i = 0; i + 1 < bytes.size() - 2; i += 2) {
      w[i / 2] = static_cast<wchar_t>((bytes[2 + i] << 8) | bytes[2 + i + 1]);
    }
    return WideToUtf8(w);
  }
  // No BOM — assume UTF-8.
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// Detect the dominant end-of-line style in a UTF-8 buffer.
swg::eol DetectEol(std::string_view text) {
  size_t crlf = 0, cr = 0, lf = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') {
        ++crlf;
        ++i;
      } else {
        ++cr;
      }
    } else if (c == '\n') {
      ++lf;
    }
  }
  if (crlf >= cr && crlf >= lf) return swg::eol::crlf;
  if (lf >= cr) return swg::eol::lf;
  return swg::eol::cr;
}

// Convert an arbitrary UTF-8 string's line terminators to the document's EOL
// mode (Notepad behavior on paste). Recognises CRLF, CR, and LF in the source.
std::string NormalizeEol(std::string_view src, swg::eol mode) {
  std::string out;
  out.reserve(src.size());
  const char* eol_str = "\r\n";
  size_t eol_len = 2;
  switch (mode) {
    case swg::eol::lf: eol_str = "\n"; eol_len = 1; break;
    case swg::eol::cr: eol_str = "\r"; eol_len = 1; break;
    case swg::eol::crlf: eol_str = "\r\n"; eol_len = 2; break;
    default: break;
  }
  for (size_t i = 0; i < src.size(); ++i) {
    char c = src[i];
    if (c == '\r') {
      out.append(eol_str, eol_len);
      if (i + 1 < src.size() && src[i + 1] == '\n') ++i;
    } else if (c == '\n') {
      out.append(eol_str, eol_len);
    } else {
      out.push_back(c);
    }
  }
  return out;
}

class Sedit : public swg::host {
 public:
  Sedit(HWND hwnd, std::string fontpath, double fontsize)
      : hwnd_(hwnd), doc_(this, std::move(fontpath), fontsize, swg::eol::crlf) {
    doc = &doc_;
    current_dpi_ = GetDpiForWindow(hwnd_);
    if (current_dpi_ <= 0) current_dpi_ = 96;
    const double ratio = current_dpi_ / 96.0;
    caret_width_px_ = std::max(1, static_cast<int>(1 * ratio));
    line_height_px_ = static_cast<int>(24 * ratio);
    // Anchor the zoom ladder's "100%" to whatever pixel size the editor was
    // constructed with, interpreted as logical 96-DPI pixels. ZoomedFontSize
    // scales this back up to the current DPI for the renderer.
    if (fontsize > 0.0) zoom_base_font_size_ = fontsize;
    // Re-rasterize at the DPI-corrected pixel size so the initial paint
    // doesn't render at the 96-DPI baseline on a high-DPI display.
    doc_.reset(std::nullopt, std::nullopt, ZoomedFontSize());
  }

  ~Sedit() {
    caret_.Destroy();
    if (mem_dc_) {
      if (old_bitmap_) SelectObject(mem_dc_, old_bitmap_);
      DeleteDC(mem_dc_);
    }
    if (backbuffer_) DeleteObject(backbuffer_);
  }

  static bool Initialize() {
    WNDCLASSEX wcex{
        .cbSize = sizeof(WNDCLASSEX),
        .style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_OWNDC,
        .lpfnWndProc = WndProc,
        .hInstance = GetModuleHandle(nullptr),
        .hCursor = LoadCursor(nullptr, IDC_IBEAM),
        .hbrBackground = nullptr,
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
    // Keep the title bar's dirty marker in sync with the document. Update only
    // when the visible state changes to avoid spurious SetWindowText calls.
    if (is_dirty() != last_title_dirty_) {
      last_title_dirty_ = is_dirty();
      UpdateTitle();
    }
  }

  void EnsureBackBuffer(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (backbuffer_ && bb_w_ == w && bb_h_ == h) return;
    if (mem_dc_) {
      if (old_bitmap_) SelectObject(mem_dc_, old_bitmap_);
      DeleteDC(mem_dc_);
      mem_dc_ = nullptr;
      old_bitmap_ = nullptr;
    }
    if (backbuffer_) {
      DeleteObject(backbuffer_);
      backbuffer_ = nullptr;
    }
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    backbuffer_ = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!backbuffer_) {
      THROW_LAST_ERROR();
    }
    bb_pixels_ = static_cast<uint32_t*>(bits);
    bb_w_ = w;
    bb_h_ = h;
    mem_dc_ = CreateCompatibleDC(nullptr);
    old_bitmap_ = SelectObject(mem_dc_, backbuffer_);
  }

  LRESULT OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    if (viewport.w <= 0 || viewport.h <= 0) {
      EndPaint(hwnd_, &ps);
      return 0;
    }
    EnsureBackBuffer(viewport.w, viewport.h);

    // Pull the theme palette once per paint. Cheap registry read; nothing in
    // the hot typing path depends on it.
    const auto palette = swg::winapp::theme::current_palette();

    // Clear back-buffer to the editor background (theme-aware).
    std::fill_n(bb_pixels_, static_cast<size_t>(bb_w_) * bb_h_, palette.editor_bg);

    // Compute the layout — pure CPU work in src/edit/.
    auto layout =
        render({.x = 0, .y = 0, .w = viewport.w, .h = viewport.h}, scroll_y_, scroll_x_);
    // Sync cached metrics from the freshly computed layout. line_height_px_
    // must be set before UpdateScrollBars so the V scrollbar gets the right
    // page size on the first paint.
    if (layout.line_height > 0) line_height_px_ = layout.line_height;
    if (layout.content_width > content_width_) {
      content_width_ = layout.content_width;
    }

    // If a zoom or font change just invalidated the layout, bring the caret
    // back into view now that we have a fresh line_height_px_. We do the
    // vertical scroll here so the horizontal pass below sees a layout that
    // actually contains the caret's row.
    if (pending_ensure_caret_visible_) {
      pending_ensure_caret_visible_ = false;
      const int prev_scroll_y = scroll_y_;
      EnsureCaretVisible();
      if (scroll_y_ != prev_scroll_y) {
        layout =
            render({.x = 0, .y = 0, .w = viewport.w, .h = viewport.h}, scroll_y_, scroll_x_);
        if (layout.content_width > content_width_) {
          content_width_ = layout.content_width;
        }
      }
    }

    // Adjust horizontal scroll to keep the caret in view, then re-render once
    // if scroll_x_ changed. Doing this here (instead of via an extra render in
    // EnsureCaretVisible) means we never run two full layouts per keystroke
    // for the common case where the caret remains horizontally visible.
    if (AdjustScrollXForCaret(layout)) {
      layout =
          render({.x = 0, .y = 0, .w = viewport.w, .h = viewport.h}, scroll_y_, scroll_x_);
      if (layout.content_width > content_width_) {
        content_width_ = layout.content_width;
      }
    }

    UpdateScrollBars();

    // Draw selection highlight rectangles first, behind text.
    if (has_selection()) {
      const auto [sb, se] = selection_range();
      RenderSelection(bb_pixels_, bb_w_, bb_h_, layout, sb, se, palette.selection_bg);
    }

    // Composite every shaped glyph using the document's CPU-side atlas.
    const auto& atlas = doc_.atlas();
    CompositeAllGlyphs(bb_pixels_, bb_w_, bb_h_, atlas.bitmap().data(), atlas.width(),
                       atlas.height(), layout, /*fg=*/palette.editor_fg);

    // Move the OpenGL caret child window to its new position BEFORE BitBlt so
    // that WS_CLIPCHILDREN excludes the new rect from the BitBlt destination.
    // Without this ordering the old caret rect would keep stale pixels.
    PlaceCaret(layout);

    BitBlt(hdc, 0, 0, bb_w_, bb_h_, mem_dc_, 0, 0, SRCCOPY);
    EndPaint(hwnd_, &ps);
    return 0;
  }

  LRESULT OnChar(wchar_t uchar) {
    // Reject all ASCII control characters. Backspace, Tab, Enter, etc. are
    // handled in OnKeyDown via VK_BACK/VK_TAB/VK_RETURN — letting them through
    // here would cause Ctrl+letter shortcuts whose ASCII control equivalent
    // happens to be \b (0x08, Ctrl+H), \t (0x09, Ctrl+I), \n (0x0A, Ctrl+J),
    // or \r (0x0D, Ctrl+M) to inadvertently mutate the document on top of
    // running the shortcut's command.
    if (uchar < 0x20 || uchar == 0x7F) return 0;
    if (auto res = DigestChar(uchar); res.has_value()) {
      insert_char(*res);
      EnsureCaretVisible();
#ifdef _DEBUG
      auto s = doc_.get(0, doc_.length());
      int l = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
      std::wstring wstr(static_cast<size_t>(l), 0);
      MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), wstr.data(),
                          static_cast<int>(wstr.size()));
      OutputDebugStringW(std::format(L"{}\n", wstr).c_str());
#endif
    }
    return 0;
  }
  LRESULT OnSize(int width, int height) {
    viewport.w = width;
    viewport.h = height;
    UpdateScrollBars();
    return 0;
  }

  // Re-rasterize everything that depends on monitor DPI. Called from
  // WM_DPICHANGED_BEFOREPARENT (per-monitor DPI awareness V2) when the
  // window's monitor changes scale factor. The font size baseline is stored
  // DPI-agnostic (in 96-DPI pixels) so we just pick up the new dpi and call
  // ApplyZoom() to push the new physical pixel size into fontengine.
  LRESULT OnDpiChanged(int new_dpi) {
    if (new_dpi <= 0) new_dpi = 96;
    if (new_dpi == current_dpi_) return 0;
    const double ratio = new_dpi / 96.0;
    current_dpi_ = new_dpi;
    caret_width_px_ = std::max(1, static_cast<int>(1 * ratio));
    // Force OnPaint to recompute line height from the new layout; the old
    // value is wrong for the new DPI.
    line_height_px_ = 0;
    // Re-rasterize at the new DPI's pixel size. ApplyZoom() also flags
    // pending_ensure_caret_visible_ so the caret stays in view.
    ApplyZoom();
    return 0;
  }

  LRESULT OnSetFocus() {
    if (line_height_px_ <= 0) {
      line_height_px_ = static_cast<int>(24 * GetDpiForWindow(hwnd_) / 96.0);
    }
    caret_.SetColor(swg::winapp::theme::current_palette().editor_fg);
    caret_.Show(hwnd_);
    return 0;
  }
  LRESULT OnKillFocus() {
    caret_.Hide();
    return 0;
  }

  void PlaceCaret(const swg::layout_result& layout) {
    if (layout.line_height > 0 && layout.line_height != line_height_px_) {
      line_height_px_ = layout.line_height;
    }
    const size_t pos = inspos();
    const swg::caret_anchor* exact = nullptr;
    const swg::caret_anchor* best_le = nullptr;
    for (const auto& a : layout.carets) {
      if (a.byte_pos == pos) {
        exact = &a;
        break;
      }
      if (a.byte_pos < pos && (!best_le || a.byte_pos > best_le->byte_pos)) {
        best_le = &a;
      }
    }
    const swg::caret_anchor* chosen = exact ? exact : best_le;
    if (!chosen && !layout.carets.empty()) {
      chosen = &layout.carets.front();
    }
    if (chosen) {
      const int x = static_cast<int>(chosen->x);
      const int y = static_cast<int>(chosen->baseline_y) - layout.ascent;
      caret_.Place(x, y, caret_width_px_, line_height_px_);
      // Keep the palette in sync so theme switches recolour the caret on
      // the next paint without an explicit handler.
      caret_.SetColor(swg::winapp::theme::current_palette().editor_fg);
    }
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

  // Scroll so that the current caret line is within the visible viewport.
  // Horizontal-scroll-into-view is handled in OnPaint (where we already have
  // a layout) — keeping the keystroke path doing exactly zero extra layouts.
  void EnsureCaretVisible() {
    if (viewport.h <= 0 || line_height_px_ <= 0) {
      UpdateScrollBars();
      return;
    }
    const auto& lt = doc_.lines();
    size_t li = (lt.line_count() == 0) ? 0 : lt.line_at_pos(inspos());
    const int padding_y = 2;
    const int caret_top = padding_y + static_cast<int>(li) * line_height_px_;
    const int caret_bot = caret_top + line_height_px_;
    if (caret_top - scroll_y_ < padding_y) {
      scroll_y_ = caret_top - padding_y;
    } else if (caret_bot - scroll_y_ > viewport.h) {
      scroll_y_ = caret_bot - viewport.h;
    }
    scroll_y_ = std::max(0, scroll_y_);
    UpdateStatusBar();
    UpdateScrollBars();
  }

  // Given a fresh layout (computed with the current scroll_y_/scroll_x_),
  // adjust scroll_x_ so the caret is horizontally visible. Returns true iff
  // scroll_x_ changed and the caller needs to re-render. Cheap: just looks
  // up the caret's screen-space x in the layout we already produced.
  bool AdjustScrollXForCaret(const swg::layout_result& layout) {
    if (viewport.w <= 0) return false;
    const size_t pos = inspos();
    const swg::caret_anchor* exact = nullptr;
    for (const auto& a : layout.carets) {
      if (a.byte_pos == pos) {
        exact = &a;
        break;
      }
    }
    if (!exact) return false;  // caret off-screen vertically; nothing to do
    const int padding_x = 4;
    const int caret_w = std::max(1, caret_width_px_);
    // The carets in `layout` are already in screen space; convert back to
    // document space by adding scroll_x_.
    const int caret_doc_x = static_cast<int>(exact->x) + scroll_x_;
    int new_scroll_x = scroll_x_;
    if (caret_doc_x - new_scroll_x < padding_x) {
      new_scroll_x = std::max(0, caret_doc_x - padding_x);
    } else if (caret_doc_x + caret_w - new_scroll_x > viewport.w) {
      new_scroll_x = caret_doc_x + caret_w - viewport.w;
    }
    new_scroll_x = std::max(0, new_scroll_x);
    if (new_scroll_x == scroll_x_) return false;
    scroll_x_ = new_scroll_x;
    return true;
  }

  // Configure both scrollbars based on the current viewport, document height,
  // and the most recent content_width measurement. Call after viewport size
  // changes, scroll changes, and document mutations.
  void UpdateScrollBars() {
    if (viewport.h > 0 && line_height_px_ > 0) {
      const size_t n_lines = std::max<size_t>(1, doc_.lines().line_count());
      const int doc_h = static_cast<int>(n_lines) * line_height_px_ + 4;
      SCROLLINFO si{};
      si.cbSize = sizeof(si);
      si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
      si.nMin = 0;
      si.nMax = std::max(0, doc_h - 1);
      si.nPage = static_cast<UINT>(std::max(1, viewport.h));
      scroll_y_ = std::clamp(scroll_y_, 0, std::max(0, doc_h - viewport.h));
      si.nPos = scroll_y_;
      SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
    }
    if (viewport.w > 0) {
      // content_width_ is the widest visible line. Make sure the H scrollbar
      // can always reach at least scroll_x_ + viewport.w so we don't jitter
      // when the caret is on a long off-screen line.
      const int doc_w = std::max({content_width_, scroll_x_ + viewport.w, viewport.w});
      SCROLLINFO si{};
      si.cbSize = sizeof(si);
      si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
      si.nMin = 0;
      si.nMax = std::max(0, doc_w - 1);
      si.nPage = static_cast<UINT>(std::max(1, viewport.w));
      scroll_x_ = std::clamp(scroll_x_, 0, std::max(0, doc_w - viewport.w));
      si.nPos = scroll_x_;
      SetScrollInfo(hwnd_, SB_HORZ, &si, TRUE);
    }
  }

  // Push the current line/column + EOL + encoding to the status bar control.
  void UpdateStatusBar() {
    if (!status_hwnd_) return;
    swg::docpos lc = pos_to_linecol(inspos());
    const wchar_t* eol_label = L"CRLF";
    switch (doc_.eol_mode()) {
      case swg::eol::lf:   eol_label = L"LF";   break;
      case swg::eol::cr:   eol_label = L"CR";   break;
      case swg::eol::crlf: eol_label = L"CRLF"; break;
    }
    std::wstring text =
        std::format(L"Ln {}, Col {}    |    {}%    |    {}    |    UTF-8",
                    lc.line, lc.column, ZoomPercent(), eol_label);
    SetWindowTextW(status_hwnd_, text.c_str());
  }

  // Dispatch a menu command from the main window (forwarded via WM_COMMAND).
  // Returns true if handled.
  bool OnCommand(WORD cmd) {
    switch (cmd) {
      case IDM_FILE_NEW:        DoNew();    return true;
      case IDM_FILE_OPEN:       DoOpen();   return true;
      case IDM_FILE_SAVE:       DoSave();   return true;
      case IDM_FILE_SAVE_AS:    DoSaveAs(); return true;
      case IDM_EDIT_UNDO:
        undo();
        EnsureCaretVisible();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
      case IDM_EDIT_REDO:
        redo();
        EnsureCaretVisible();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
      case IDM_EDIT_CUT:        DoCut();    return true;
      case IDM_EDIT_COPY:       DoCopy();   return true;
      case IDM_EDIT_PASTE:      DoPaste();  return true;
      case IDM_EDIT_DELETE:
        delete_char();
        EnsureCaretVisible();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
      case IDM_EDIT_SELECT_ALL: SelectAll(); return true;
      case IDM_EDIT_FIND:       OpenFindDialog(false); return true;
      case IDM_EDIT_REPLACE:    OpenFindDialog(true);  return true;
      case IDM_EDIT_FIND_NEXT:  FindNextRepeat(); return true;
      case IDM_EDIT_GOTO:       DoGoTo(); return true;
      case IDM_EDIT_TIME_DATE:
        InsertDateTime();
        EnsureCaretVisible();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
      case IDM_FORMAT_FONT:     DoChooseFont(); return true;
      case IDM_VIEW_ZOOM_IN:    ZoomIn(); return true;
      case IDM_VIEW_ZOOM_OUT:   ZoomOut(); return true;
      case IDM_VIEW_ZOOM_RESET: ZoomReset(); return true;
      case IDM_HELP_ABOUT:      DoAbout(); return true;
    }
    return false;
  }

  // Translate VK key codes to caret motions or editing commands. Returns true
  // if the message was consumed.
  bool OnKeyDown(WPARAM vkey) {
    using motion = swg::host::motion;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    // Ctrl-letter combinations are dispatched as commands; the corresponding
    // WM_CHAR is swallowed by the Sedit window because we never call
    // TranslateMessage selectively, so handle the verbs here directly.
    if (ctrl && !shift) {
      switch (vkey) {
        case 'A': SelectAll(); return true;
        case 'C': DoCopy(); return true;
        case 'X': DoCut(); return true;
        case 'V': DoPaste(); return true;
        case 'N': DoNew(); return true;
        case 'O': DoOpen(); return true;
        case 'S': DoSave(); return true;
        case 'F': OpenFindDialog(false); return true;
        case 'H': OpenFindDialog(true); return true;
        case 'G': DoGoTo(); return true;
        // Ctrl + '=' (the unshifted '+' key) and Ctrl + Numpad-Plus -> zoom in.
        // Most keyboards send VK_OEM_PLUS for '=' / '+', so we honour it
        // regardless of the shift state to match common editor behaviour.
        case VK_OEM_PLUS:
        case VK_ADD:
          ZoomIn();
          return true;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
          ZoomOut();
          return true;
        case '0':
        case VK_NUMPAD0:
          ZoomReset();
          return true;
        case 'Z':
          undo();
          EnsureCaretVisible();
          InvalidateRect(hwnd_, nullptr, FALSE);
          return true;
        case 'Y':
          redo();
          EnsureCaretVisible();
          InvalidateRect(hwnd_, nullptr, FALSE);
          return true;
      }
    }
    if (ctrl && shift) {
      switch (vkey) {
        case 'S': DoSaveAs(); return true;
        // Ctrl+Shift+'=' is how '+' is typed on US keyboards; treat the
        // shifted variant as zoom-in too so the menu accelerator label
        // ("Ctrl++") is honest.
        case VK_OEM_PLUS:
        case VK_ADD:
          ZoomIn();
          return true;
        case 'Z':
          redo();  // Ctrl+Shift+Z is an alternate redo binding.
          EnsureCaretVisible();
          InvalidateRect(hwnd_, nullptr, FALSE);
          return true;
      }
    }

    std::optional<motion> m;
    switch (vkey) {
      case VK_F3:
        FindNextRepeat();
        return true;
      case VK_BACK:
        erase_char();
        EnsureCaretVisible();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
      case VK_RETURN:
        linefeed();
        EnsureCaretVisible();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
      case VK_TAB:
        insert_char("\t");
        EnsureCaretVisible();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
      case VK_LEFT:  m = motion::char_left;  break;
      case VK_RIGHT: m = motion::char_right; break;
      case VK_UP:    m = motion::line_up;    break;
      case VK_DOWN:  m = motion::line_down;  break;
      case VK_HOME:  m = ctrl ? motion::doc_home : motion::line_home; break;
      case VK_END:   m = ctrl ? motion::doc_end : motion::line_end;   break;
      case VK_PRIOR: m = motion::page_up;   break;
      case VK_NEXT:  m = motion::page_down; break;
      case VK_DELETE:
        delete_char();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
      case VK_F5:
        InsertDateTime();
        EnsureCaretVisible();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
      default:
        return false;
    }
    if (!m) return false;
    auto layout =
        render({.x = 0, .y = 0, .w = viewport.w, .h = viewport.h}, scroll_y_, scroll_x_);
    if (shift) {
      shift_move_caret(*m, &layout);
    } else {
      move_caret(*m, &layout);
    }
    EnsureCaretVisible();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
  }

  void SelectAll() {
    select_all();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  // Win32 clipboard glue. The document carries UTF-8; the clipboard carries
  // UTF-16 via CF_UNICODETEXT.
  void DoCopy() {
    if (!has_selection()) return;
    const std::string text = selected_text();
    if (text.empty()) return;
    SetClipboardText(text);
  }

  void DoCut() {
    if (!has_selection()) return;
    const std::string text = selected_text();
    if (text.empty()) return;
    SetClipboardText(text);
    delete_selection();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void DoPaste() {
    std::string text;
    if (!GetClipboardText(text)) return;
    if (text.empty()) {
      // Still collapse the selection per Notepad behavior.
      paste({});
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    std::string normalized = NormalizeEol(text, doc_.eol_mode());
    paste(normalized);
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void SetClipboardText(const std::string& utf8) {
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    std::wstring w = Utf8ToWide(utf8);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (h) {
      void* p = GlobalLock(h);
      if (p) {
        std::memcpy(p, w.data(), w.size() * sizeof(wchar_t));
        static_cast<wchar_t*>(p)[w.size()] = L'\0';
        GlobalUnlock(h);
        if (!SetClipboardData(CF_UNICODETEXT, h)) {
          GlobalFree(h);
        }
      } else {
        GlobalFree(h);
      }
    }
    CloseClipboard();
  }

  bool GetClipboardText(std::string& out) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
    if (!OpenClipboard(hwnd_)) return false;
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT); h != nullptr) {
      if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h)); p != nullptr) {
        size_t n = std::wcslen(p);
        out = WideToUtf8({p, n});
        GlobalUnlock(h);
        ok = true;
      }
    }
    CloseClipboard();
    return ok;
  }

  // File I/O. The document holds UTF-8 internally; on load we BOM-sniff
  // UTF-16 and convert; on save we always write UTF-8 without BOM.

 public:
  // Open `path` as the current document, replacing any existing content.
  // Called either via Ctrl+O or by the command-line argument plumbing.
  bool OpenFile(const std::wstring& path) {
    auto bytes = ReadFileBytes(path);
    if (!bytes.has_value()) {
      MessageBoxW(hwnd_, L"Failed to open file.", L"Schwing Edit",
                  MB_OK | MB_ICONWARNING);
      return false;
    }
    std::string utf8 = DecodeToUtf8(*bytes);
    swg::eol detected = DetectEol(utf8);
    std::string normalized = NormalizeEol(utf8, detected);
    load_text(normalized, detected);
    scroll_y_ = 0;
    scroll_x_ = 0;
    content_width_ = 0;
    file_path_ = path;
    clear_dirty();
    UpdateTitle();
    UpdateScrollBars();
    return true;
  }

  // Prompt the user when there are unsaved changes. Returns true if the caller
  // may proceed (user picked Save or Don't Save, or there were no changes);
  // returns false if Cancel was picked.
  bool ConfirmDiscardChanges() {
    if (!is_dirty()) return true;
    std::wstring name = file_path_.empty()
                            ? std::wstring(L"Untitled")
                            : std::filesystem::path(file_path_).filename().wstring();
    std::wstring msg = L"Do you want to save changes to " + name + L"?";
    int r = MessageBoxW(hwnd_, msg.c_str(), L"Schwing Edit",
                        MB_YESNOCANCEL | MB_ICONWARNING);
    if (r == IDCANCEL) return false;
    if (r == IDYES) {
      DoSave();
      // If the save was cancelled (user picked Cancel in Save As), the file is
      // still dirty -- treat that as cancelling the original action too.
      return !is_dirty();
    }
    // IDNO -- discard changes.
    return true;
  }

 private:
  void DoNew() {
    if (!ConfirmDiscardChanges()) return;
    load_text({}, doc_.eol_mode());
    scroll_y_ = 0;
    scroll_x_ = 0;
    content_width_ = 0;
    file_path_.clear();
    clear_dirty();
    UpdateTitle();
    UpdateScrollBars();
  }

  void DoOpen() {
    if (!ConfirmDiscardChanges()) return;
    OPENFILENAMEW ofn{};
    wchar_t buf[MAX_PATH] = L"";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter =
        L"Text documents (*.txt)\0*.txt\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"txt";
    if (GetOpenFileNameW(&ofn)) {
      OpenFile(buf);
    }
  }

  void DoSave() {
    if (file_path_.empty()) {
      DoSaveAs();
      return;
    }
    if (WriteCurrentTo(file_path_)) {
      clear_dirty();
      UpdateTitle();
    }
  }

  void DoSaveAs() {
    OPENFILENAMEW ofn{};
    wchar_t buf[MAX_PATH] = L"";
    if (!file_path_.empty()) {
      std::wcsncpy(buf, file_path_.c_str(), MAX_PATH - 1);
    }
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter =
        L"Text documents (*.txt)\0*.txt\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"txt";
    if (GetSaveFileNameW(&ofn)) {
      file_path_ = buf;
      if (WriteCurrentTo(file_path_)) {
        clear_dirty();
        UpdateTitle();
      }
    }
  }

  bool WriteCurrentTo(const std::wstring& path) {
    std::string utf8 = all_text();
    if (!WriteFileBytes(path, utf8)) {
      MessageBoxW(hwnd_, L"Failed to save file.", L"Schwing Edit",
                  MB_OK | MB_ICONWARNING);
      return false;
    }
    return true;
  }

  // Reflects the current filename and dirty state in the top-level window's
  // title bar. A leading "* " indicates unsaved changes.
  void UpdateTitle() {
    HWND top = GetAncestor(hwnd_, GA_ROOT);
    if (!top) return;
    std::wstring title;
    std::wstring name = file_path_.empty()
                            ? std::wstring(L"Untitled")
                            : std::filesystem::path(file_path_).filename().wstring();
    if (is_dirty()) {
      title = L"*" + name + L" - Schwing Edit";
    } else {
      title = name + L" - Schwing Edit";
    }
    SetWindowTextW(top, title.c_str());
  }

  LRESULT OnLButtonDown(int x, int y) {
    SetFocus(hwnd_);
    auto layout =
        render({.x = 0, .y = 0, .w = viewport.w, .h = viewport.h}, scroll_y_, scroll_x_);
    const size_t pos = swg::host::hit_test(layout, static_cast<float>(x),
                                           static_cast<float>(y) + layout.ascent / 2.0f);
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (shift) {
      // Extend or start a selection toward the click.
      if (!selection_anchor().has_value()) {
        set_selection_anchor(inspos());
      }
      caret_keep_anchor(pos);
    } else {
      caret(pos);
      // Begin a potential drag selection.
      set_selection_anchor(pos);
      drag_active_ = true;
      SetCapture(hwnd_);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateStatusBar();
    return 0;
  }

  LRESULT OnMouseMove(int x, int y, WPARAM wparam) {
    if (!drag_active_ || (wparam & MK_LBUTTON) == 0) return 0;
    auto layout =
        render({.x = 0, .y = 0, .w = viewport.w, .h = viewport.h}, scroll_y_, scroll_x_);
    const size_t pos = swg::host::hit_test(layout, static_cast<float>(x),
                                           static_cast<float>(y) + layout.ascent / 2.0f);
    caret_keep_anchor(pos);
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateStatusBar();
    return 0;
  }

  LRESULT OnLButtonUp(int /*x*/, int /*y*/) {
    if (drag_active_) {
      ReleaseCapture();
      drag_active_ = false;
      // If anchor and caret ended up at the same byte, collapse the selection
      // so a plain click doesn't leave a phantom anchor.
      if (selection_anchor().has_value() && *selection_anchor() == inspos()) {
        set_selection_anchor(std::nullopt);
      }
    }
    return 0;
  }

  // Move the caret without clearing the selection anchor. Used by mouse-drag.
  void caret_keep_anchor(size_t pos) {
    auto a = selection_anchor();
    caret(pos);
    set_selection_anchor(a);
  }

  LRESULT OnMouseWheel(int delta, WPARAM keys) {
    // Ctrl+wheel -> zoom in/out (Notepad parity). Consume the event so the
    // wheel does NOT also scroll the viewport on the same notch.
    if ((keys & MK_CONTROL) != 0) {
      HandleZoomWheel(delta);
      return 0;
    }
    // 3 lines per wheel notch (WHEEL_DELTA = 120). With Shift held the wheel
    // scrolls horizontally instead — matches the convention many editors use.
    if (line_height_px_ <= 0) line_height_px_ = 16;
    const int step_lines = (delta / WHEEL_DELTA) * 3;
    if ((keys & MK_SHIFT) != 0) {
      ApplyHScrollDelta(-step_lines * line_height_px_);
    } else {
      ApplyVScrollDelta(-step_lines * line_height_px_);
    }
    return 0;
  }

  LRESULT OnMouseHWheel(int delta) {
    // 1 character ~ line_height per WHEEL_DELTA notch — close enough.
    if (line_height_px_ <= 0) line_height_px_ = 16;
    const int step_px = (delta / WHEEL_DELTA) * 3 * line_height_px_;
    ApplyHScrollDelta(step_px);
    return 0;
  }

  // Centralized vertical scroll mutation: clamps, repaints, and refreshes the
  // scrollbar. All vertical scroll sources funnel through here.
  void ApplyVScrollDelta(int delta_y) {
    const size_t n_lines = std::max<size_t>(1, doc_.lines().line_count());
    const int doc_h = static_cast<int>(n_lines) * line_height_px_ + 4;
    const int max_scroll = std::max(0, doc_h - viewport.h);
    int new_scroll = std::clamp(scroll_y_ + delta_y, 0, max_scroll);
    if (new_scroll != scroll_y_) {
      scroll_y_ = new_scroll;
      UpdateScrollBars();
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }

  void SetVScroll(int new_pos) {
    ApplyVScrollDelta(new_pos - scroll_y_);
  }

  // Centralized horizontal scroll mutation.
  void ApplyHScrollDelta(int delta_x) {
    const int doc_w = std::max({content_width_, scroll_x_ + viewport.w, viewport.w});
    const int max_scroll = std::max(0, doc_w - viewport.w);
    int new_scroll = std::clamp(scroll_x_ + delta_x, 0, max_scroll);
    if (new_scroll != scroll_x_) {
      scroll_x_ = new_scroll;
      UpdateScrollBars();
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }

  void SetHScroll(int new_pos) {
    ApplyHScrollDelta(new_pos - scroll_x_);
  }

  // Translate a WM_VSCROLL notification into a scroll position update.
  LRESULT OnVScroll(WPARAM wparam) {
    const int code = LOWORD(wparam);
    const int line = std::max(1, line_height_px_);
    const int page = std::max(line, viewport.h - line);
    int target = scroll_y_;
    switch (code) {
      case SB_LINEUP:        target -= line; break;
      case SB_LINEDOWN:      target += line; break;
      case SB_PAGEUP:        target -= page; break;
      case SB_PAGEDOWN:      target += page; break;
      case SB_TOP:           target = 0; break;
      case SB_BOTTOM:        target = INT_MAX; break;
      case SB_THUMBPOSITION:
      case SB_THUMBTRACK: {
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_TRACKPOS;
        if (GetScrollInfo(hwnd_, SB_VERT, &si)) target = si.nTrackPos;
        break;
      }
      default: return 0;
    }
    SetVScroll(target);
    return 0;
  }

  LRESULT OnHScroll(WPARAM wparam) {
    const int code = LOWORD(wparam);
    const int line = std::max(1, line_height_px_);
    const int page = std::max(line, viewport.w - line);
    int target = scroll_x_;
    switch (code) {
      case SB_LINELEFT:      target -= line; break;
      case SB_LINERIGHT:     target += line; break;
      case SB_PAGELEFT:      target -= page; break;
      case SB_PAGERIGHT:     target += page; break;
      case SB_LEFT:          target = 0; break;
      case SB_RIGHT:         target = INT_MAX; break;
      case SB_THUMBPOSITION:
      case SB_THUMBTRACK: {
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_TRACKPOS;
        if (GetScrollInfo(hwnd_, SB_HORZ, &si)) target = si.nTrackPos;
        break;
      }
      default: return 0;
    }
    SetHScroll(target);
    return 0;
  }

  // -------- Find / Replace common dialog plumbing --------

  // Open (or focus) the Win32 stock Find / Replace dialog. Modeless: the
  // dialog posts notifications back via the registered FINDMSGSTRING message,
  // which is dispatched in WndProc.
  void OpenFindDialog(bool replace) {
    if (g_find_msg == 0) {
      g_find_msg = RegisterWindowMessageW(FINDMSGSTRINGW);
    }
    if (g_find_dialog) {
      // A dialog is already up — bring it forward instead of opening another.
      SetForegroundWindow(g_find_dialog);
      return;
    }
    // Seed the search text with the current selection (Notepad parity).
    if (has_selection()) {
      std::string sel = selected_text();
      int wlen = MultiByteToWideChar(CP_UTF8, 0, sel.data(),
                                     static_cast<int>(sel.size()), nullptr, 0);
      if (wlen > 0 && wlen < static_cast<int>(std::size(g_find_what)) - 1) {
        MultiByteToWideChar(CP_UTF8, 0, sel.data(),
                            static_cast<int>(sel.size()), g_find_what, wlen);
        g_find_what[wlen] = 0;
      }
    }
    g_findrep = {};
    g_findrep.lStructSize = sizeof(FINDREPLACEW);
    g_findrep.hwndOwner = hwnd_;
    g_findrep.Flags = FR_DOWN;
    g_findrep.lpstrFindWhat = g_find_what;
    g_findrep.wFindWhatLen = static_cast<WORD>(std::size(g_find_what));
    g_findrep.lpstrReplaceWith = g_replace_with;
    g_findrep.wReplaceWithLen = static_cast<WORD>(std::size(g_replace_with));
    g_find_owner = hwnd_;
    g_find_dialog = replace ? ReplaceTextW(&g_findrep) : FindTextW(&g_findrep);
  }

  // Repeat the most recent find (F3). Falls back to opening the dialog if the
  // user has not yet entered a search term.
  void FindNextRepeat() {
    if (g_find_what[0] == 0) {
      OpenFindDialog(false);
      return;
    }
    // Convert the cached UTF-16 find text to UTF-8.
    int blen = WideCharToMultiByte(CP_UTF8, 0, g_find_what, -1, nullptr, 0,
                                   nullptr, nullptr);
    if (blen <= 1) return;
    std::string needle(blen - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, g_find_what, -1, needle.data(), blen,
                        nullptr, nullptr);
    bool match_case = (g_findrep.Flags & FR_MATCHCASE) != 0;
    bool down = (g_findrep.Flags & FR_DOWN) != 0;
    if (!find_text(needle, match_case, down)) {
      ReportNotFound(needle);
    } else {
      EnsureCaretVisible();
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }

  // Dispatch a FINDMSGSTRING notification from the modeless dialog.
  void HandleFindMsg(LPFINDREPLACEW fr) {
    if (fr->Flags & FR_DIALOGTERM) {
      g_find_dialog = nullptr;
      return;
    }
    int blen = WideCharToMultiByte(CP_UTF8, 0, fr->lpstrFindWhat, -1, nullptr,
                                   0, nullptr, nullptr);
    if (blen <= 1) return;
    std::string needle(blen - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, fr->lpstrFindWhat, -1, needle.data(), blen,
                        nullptr, nullptr);
    std::string repl;
    if ((fr->Flags & (FR_REPLACE | FR_REPLACEALL)) && fr->lpstrReplaceWith) {
      int rlen = WideCharToMultiByte(CP_UTF8, 0, fr->lpstrReplaceWith, -1,
                                     nullptr, 0, nullptr, nullptr);
      if (rlen > 1) {
        repl.assign(rlen - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, fr->lpstrReplaceWith, -1, repl.data(),
                            rlen, nullptr, nullptr);
      }
    }
    const bool match_case = (fr->Flags & FR_MATCHCASE) != 0;
    const bool down = (fr->Flags & FR_DOWN) != 0;
    if (fr->Flags & FR_FINDNEXT) {
      if (!find_text(needle, match_case, down)) ReportNotFound(needle);
    } else if (fr->Flags & FR_REPLACE) {
      if (!replace_text(needle, repl, match_case)) ReportNotFound(needle);
    } else if (fr->Flags & FR_REPLACEALL) {
      size_t n = replace_all(needle, repl, match_case);
      if (n == 0) ReportNotFound(needle);
    }
    EnsureCaretVisible();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void ReportNotFound(const std::string& needle) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, needle.data(),
                                   static_cast<int>(needle.size()), nullptr, 0);
    std::wstring wneedle(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, needle.data(),
                        static_cast<int>(needle.size()), wneedle.data(), wlen);
    std::wstring msg = L"Cannot find \"" + wneedle + L"\"";
    HWND owner = g_find_dialog ? g_find_dialog : hwnd_;
    MessageBoxW(owner, msg.c_str(), L"Schwing Edit",
                MB_OK | MB_ICONINFORMATION);
  }

  // -------- Go To Line --------

  // Dialog proc for the Go To Line dialog (modal). The accepted line number is
  // stored in *result via DWLP_USER and signaled via EndDialog(IDOK).
  static INT_PTR CALLBACK GoToDlgProc(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam) {
    switch (msg) {
      case WM_INITDIALOG: {
        SetWindowLongPtr(hwnd, DWLP_USER, static_cast<LONG_PTR>(lparam));
        // lparam carries the initial line number to display.
        wchar_t buf[32];
        _snwprintf_s(buf, _TRUNCATE, L"%d", static_cast<int>(lparam));
        SetDlgItemTextW(hwnd, IDC_GOTO_EDIT, buf);
        SendDlgItemMessageW(hwnd, IDC_GOTO_EDIT, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(hwnd, IDC_GOTO_EDIT));
        return FALSE;
      }
      case WM_COMMAND:
        switch (LOWORD(wparam)) {
          case IDOK: {
            BOOL ok = FALSE;
            UINT v = GetDlgItemInt(hwnd, IDC_GOTO_EDIT, &ok, TRUE);
            EndDialog(hwnd, ok ? static_cast<INT_PTR>(v) : 0);
            return TRUE;
          }
          case IDCANCEL:
            EndDialog(hwnd, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
  }

  void DoGoTo() {
    int current_line = pos_to_linecol(inspos()).line;
    INT_PTR r = DialogBoxParamW(GetModuleHandleW(nullptr),
                                MAKEINTRESOURCEW(IDD_GOTO), hwnd_,
                                &Sedit::GoToDlgProc,
                                static_cast<LPARAM>(current_line));
    if (r <= 0) return;
    goto_line(static_cast<int>(r));
    EnsureCaretVisible();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  // -------- Font picker --------

  void DoChooseFont() {
    LOGFONTW lf{};
    // Seed with a sensible default font: family = Arial, size = the editor's
    // current *physical* pixel size (zoom_base_font_size_ is logical 96-DPI
    // pixels, so multiply by current DPI scale). lfHeight is in pixels at the
    // current DPI per the Win32 font dialog contract.
    HDC dc = GetDC(hwnd_);
    const int dpi = current_dpi_ > 0 ? current_dpi_ : 96;
    const double dpi_scale = dpi / 96.0;
    lf.lfHeight = -static_cast<int>(zoom_base_font_size_ * dpi_scale + 0.5);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, L"Arial", _TRUNCATE);
    ReleaseDC(hwnd_, dc);

    CHOOSEFONTW cf{};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwnd_;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL |
               CF_FORCEFONTEXIST;
    if (!ChooseFontW(&cf)) return;

    // Resolve the chosen face name to a .ttf path under C:\Windows\Fonts via
    // the standard Win32 font registry. Falls back to Arial if the lookup fails.
    std::wstring face = lf.lfFaceName;
    std::string path = ResolveFontPath(face);
    if (path.empty()) path = default_font_path;
    // lfHeight is the negative cell height in *pixels at the current DPI*.
    // We store the baseline in *logical 96-DPI pixels* so re-scaling on a DPI
    // change is a single multiplication. ZoomedFontSize() converts back to
    // physical pixels for fontengine.
    const double physical_pixel_size = std::abs(lf.lfHeight);
    const double logical_pixel_size = dpi_scale > 0.0
                                          ? physical_pixel_size / dpi_scale
                                          : physical_pixel_size;
    if (logical_pixel_size > 0.0) zoom_base_font_size_ = logical_pixel_size;
    zoom_idx_ = kDefaultZoomIdx;
    // Hand the *physical* pixel size to fontengine via plaindoc::reset.
    doc_.reset(path, std::nullopt,
               physical_pixel_size > 0.0
                   ? std::optional<double>(physical_pixel_size)
                   : std::nullopt);
    // Glyph metrics change with the font; invalidate cached content width and
    // recompute line height + scrollbars at the next paint.
    content_width_ = 0;
    line_height_px_ = 0;
    pending_ensure_caret_visible_ = true;
    UpdateStatusBar();
    UpdateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  // Look up a TTF/OTF file path for the given installed font face name. Uses
  // the standard "Fonts (Vista+)" registry key; returns an empty string when
  // no match is found.
  static std::string ResolveFontPath(const std::wstring& face_name) {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
      return {};
    }
    std::string out;
    wchar_t name[256];
    wchar_t data[1024];
    DWORD i = 0;
    for (;;) {
      DWORD name_len = static_cast<DWORD>(std::size(name));
      DWORD data_len = sizeof(data);
      DWORD type{};
      LSTATUS s =
          RegEnumValueW(key, i++, name, &name_len, nullptr, &type,
                        reinterpret_cast<LPBYTE>(data), &data_len);
      if (s == ERROR_NO_MORE_ITEMS) break;
      if (s != ERROR_SUCCESS) continue;
      if (type != REG_SZ) continue;
      // Registry value names are "Arial (TrueType)" — match the prefix.
      std::wstring n(name);
      if (n.find(face_name) != 0) continue;
      // Data is either a relative filename under %SystemRoot%\Fonts, or
      // an absolute path.
      std::wstring d(data);
      if (d.find(L':') == std::wstring::npos) {
        wchar_t winroot[MAX_PATH] = L"";
        GetWindowsDirectoryW(winroot, MAX_PATH);
        d = std::wstring(winroot) + L"\\Fonts\\" + d;
      }
      int blen = WideCharToMultiByte(CP_UTF8, 0, d.c_str(), -1, nullptr, 0,
                                     nullptr, nullptr);
      if (blen > 1) {
        out.assign(blen - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, d.c_str(), -1, out.data(), blen,
                            nullptr, nullptr);
      }
      break;
    }
    RegCloseKey(key);
    return out;
  }

  // -------- Zoom (Notepad-parity ladder) --------

  // Notepad's zoom ladder, in percent. Index `kDefaultZoomIdx` is the
  // reference (100%). Ctrl+ +/-/0 and Ctrl+MouseWheel step through this.
  static constexpr int kZoomLadder[] = {10, 25, 50, 75, 100, 110, 125, 150,
                                        175, 200, 250, 300, 400, 500};
  static constexpr int kDefaultZoomIdx = 4;  // 100%
  static constexpr int kZoomLadderLen =
      static_cast<int>(sizeof(kZoomLadder) / sizeof(kZoomLadder[0]));

  int ZoomPercent() const { return kZoomLadder[zoom_idx_]; }

  // Map the current zoom level + DPI to a physical font pixel size. The "100%
  // baseline" is stored in logical 96-DPI pixels; we scale by both the zoom
  // ladder and the current monitor DPI so a "12 px" baseline renders at 12
  // device pixels on a 96-DPI display and at 18 device pixels on a 144-DPI
  // (150%) display — the same physical size in millimetres.
  double ZoomedFontSize() const {
    const double dpi_scale = current_dpi_ > 0 ? current_dpi_ / 96.0 : 1.0;
    // Clamp to >=1 pixel so freetype doesn't get a degenerate cell size.
    double sz = zoom_base_font_size_ * static_cast<double>(ZoomPercent()) / 100.0
                * dpi_scale;
    return sz < 1.0 ? 1.0 : sz;
  }

  // Re-rasterize at the current zoom level by handing a new font size to
  // plaindoc::reset. Invalidates the cached layout metrics so the next paint
  // sees fresh line height / content width, and asks OnPaint to bring the
  // caret back into view (the byte position is preserved but the pixel
  // coordinates change — caret can land off-screen on heavy zoom-in).
  void ApplyZoom() {
    doc_.reset(std::nullopt, std::nullopt, ZoomedFontSize());
    content_width_ = 0;
    line_height_px_ = 0;
    // After zoom-out, the cached horizontal scroll may now point past the
    // shorter content width. Pull it back to zero; OnPaint will scroll
    // forward to the caret if needed.
    scroll_x_ = 0;
    pending_ensure_caret_visible_ = true;
    UpdateStatusBar();
    UpdateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void ZoomIn() {
    if (zoom_idx_ + 1 < kZoomLadderLen) {
      ++zoom_idx_;
      ApplyZoom();
    }
  }

  void ZoomOut() {
    if (zoom_idx_ > 0) {
      --zoom_idx_;
      ApplyZoom();
    }
  }

  void ZoomReset() {
    if (zoom_idx_ != kDefaultZoomIdx) {
      zoom_idx_ = kDefaultZoomIdx;
      ApplyZoom();
    }
  }

  // Ctrl+MouseWheel: step the zoom ladder by one notch per WHEEL_DELTA.
  // Accumulates sub-notch deltas so precision-touchpad scrolls eventually
  // trigger a zoom step. Returns true iff the event was consumed (caller
  // should skip scrolling).
  bool HandleZoomWheel(int delta) {
    if (delta == 0) return false;
    zoom_wheel_accum_ += delta;
    int notches = zoom_wheel_accum_ / WHEEL_DELTA;
    zoom_wheel_accum_ -= notches * WHEEL_DELTA;
    if (notches == 0) return true;  // residual sub-notch wheel; consume.
    const int old_idx = zoom_idx_;
    const int steps = notches > 0 ? notches : -notches;
    for (int i = 0; i < steps; ++i) {
      if (notches > 0) {
        if (zoom_idx_ + 1 >= kZoomLadderLen) break;
        ++zoom_idx_;
      } else {
        if (zoom_idx_ == 0) break;
        --zoom_idx_;
      }
    }
    if (zoom_idx_ != old_idx) ApplyZoom();
    return true;
  }

  // -------- Drag and drop --------

  void OnDropFiles(HDROP drop) {
    wchar_t path[MAX_PATH];
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (n > 0) {
      DragQueryFileW(drop, 0, path, MAX_PATH);
      if (ConfirmDiscardChanges()) {
        OpenFile(path);
      }
    }
    DragFinish(drop);
  }

  // -------- Insert Date/Time (F5) — Notepad parity. --------

  void InsertDateTime() {
    // Standard short-time + short-date in the user's current locale.
    wchar_t timebuf[64] = L"";
    wchar_t datebuf[64] = L"";
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr,
                    timebuf, static_cast<int>(std::size(timebuf)));
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, nullptr, nullptr,
                    datebuf, static_cast<int>(std::size(datebuf)), nullptr);
    std::wstring combined =
        std::wstring(timebuf) + L" " + std::wstring(datebuf);
    int blen = WideCharToMultiByte(CP_UTF8, 0, combined.c_str(),
                                   static_cast<int>(combined.size()), nullptr,
                                   0, nullptr, nullptr);
    if (blen <= 0) return;
    std::string out(static_cast<size_t>(blen), '\0');
    WideCharToMultiByte(CP_UTF8, 0, combined.c_str(),
                        static_cast<int>(combined.size()), out.data(), blen,
                        nullptr, nullptr);
    paste(out);
  }

  // -------- About --------

  void DoAbout() {
    MessageBoxW(hwnd_,
                L"Schwing Edit\n\n"
                L"A blazing-fast cross-platform text editor.\n"
                L"© 2026 Tian Liao",
                L"About Schwing Edit", MB_OK | MB_ICONINFORMATION);
  }

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto self = GetThis(hwnd);
    try {
      // Modeless Find/Replace dialog notifications arrive via a runtime-
      // registered message (FINDMSGSTRING). It is not a compile-time constant
      // so it must be matched before the switch.
      if (g_find_msg != 0 && msg == g_find_msg) {
        if (self) self->HandleFindMsg(reinterpret_cast<LPFINDREPLACEW>(lparam));
        return 0;
      }
      switch (msg) {
        case WM_SIZE:
          if (self) return self->OnSize(LOWORD(lparam), HIWORD(lparam));
          break;
        case WM_ERASEBKGND:
          return 1;
        case WM_PAINT:
          if (self) return self->OnPaint();
          break;
        case WM_LBUTTONDOWN:
          if (self) return self->OnLButtonDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
          SetFocus(hwnd);
          return 0;
        case WM_MOUSEMOVE:
          if (self) return self->OnMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), wparam);
          break;
        case WM_LBUTTONUP:
          if (self) return self->OnLButtonUp(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
          break;
        case WM_MOUSEWHEEL:
          if (self) return self->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam),
                                              GET_KEYSTATE_WPARAM(wparam));
          break;
        case WM_MOUSEHWHEEL:
          if (self) return self->OnMouseHWheel(GET_WHEEL_DELTA_WPARAM(wparam));
          break;
        case WM_VSCROLL:
          if (self) return self->OnVScroll(wparam);
          break;
        case WM_HSCROLL:
          if (self) return self->OnHScroll(wparam);
          break;
        case WM_CAPTURECHANGED:
          if (self) self->drag_active_ = false;
          return 0;
        case WM_KEYDOWN:
          if (self && self->OnKeyDown(wparam)) return 0;
          break;
        case WM_COMMAND:
          if (self && self->OnCommand(LOWORD(wparam))) return 0;
          break;
        case WM_SEDIT_CAN_CLOSE:
          if (self) return self->ConfirmDiscardChanges() ? 1 : 0;
          return 1;
        case WM_SEDIT_SET_STATUS:
          if (self) {
            self->status_hwnd_ = reinterpret_cast<HWND>(wparam);
            self->UpdateStatusBar();
          }
          return 0;
        case WM_CHAR:
          if (self) return self->OnChar(static_cast<wchar_t>(wparam));
          break;
        case WM_SETFOCUS:
          if (self) return self->OnSetFocus();
          break;
        case WM_KILLFOCUS:
          if (self) return self->OnKillFocus();
          break;
        case WM_SETTINGCHANGE: {
          // Forwarded by the main window when the system app-theme changes.
          // Invalidate so the next paint picks up the new palette.
          const wchar_t* what = reinterpret_cast<const wchar_t*>(lparam);
          if (what && lstrcmpiW(what, L"ImmersiveColorSet") == 0) {
            InvalidateRect(hwnd, nullptr, FALSE);
          }
          return 0;
        }
        // Per-monitor-DPI v2: top-level parent gets WM_DPICHANGED; this child
        // window gets WM_DPICHANGED_BEFOREPARENT (and _AFTERPARENT) so it can
        // re-rasterize before the parent resizes us. The new DPI is in the
        // LOWORD of wParam — same convention as the parent's WM_DPICHANGED.
        case WM_DPICHANGED_BEFOREPARENT: {
          if (self) {
            int new_dpi = static_cast<int>(GetDpiForWindow(hwnd));
            if (new_dpi <= 0) new_dpi = 96;
            self->OnDpiChanged(new_dpi);
          }
          return 0;
        }
        case WM_CREATE: {
          auto cs = reinterpret_cast<LPCREATESTRUCT>(lparam);
          auto edit = std::make_unique<Sedit>(hwnd, default_font_path, default_font_size);
          edit->viewport = swg::rect{.x = 0, .y = 0, .w = cs->cx, .h = cs->cy};
          auto* edit_ptr = edit.release();
          SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(edit_ptr));
          DragAcceptFiles(hwnd, TRUE);
          if (!g_initial_file_path.empty()) {
            edit_ptr->OpenFile(g_initial_file_path);
            g_initial_file_path.clear();
          } else {
            edit_ptr->UpdateTitle();
          }
          return 0;
        }
        case WM_DROPFILES:
          if (self) self->OnDropFiles(reinterpret_cast<HDROP>(wparam));
          return 0;
        case WM_DESTROY: {
          if (self) std::default_delete<Sedit>{}(self);
          SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
          return 0;
        }
      }
    } catch (const std::exception& e) {
      std::string what = e.what();
      std::wstring wmsg(what.begin(), what.end());
      OutputDebugStringA("Schwing Edit exception: ");
      OutputDebugStringA(e.what());
      OutputDebugStringA("\n");
      MessageBoxW(hwnd, wmsg.c_str(), L"Schwing Edit - unhandled exception", MB_OK | MB_ICONERROR);
      PostQuitMessage(1);
      return 0;
    } catch (...) {
      MessageBoxW(hwnd, L"unknown exception", L"Schwing Edit - unhandled exception",
                  MB_OK | MB_ICONERROR);
      PostQuitMessage(1);
      return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
  }
  static Sedit* GetThis(HWND hwnd) {
    return reinterpret_cast<Sedit*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }

 private:
  HWND hwnd_ = nullptr;
  swg::plaindoc doc_;
  swg::winapp::GlCaret caret_;
  wchar_t surrogate_[2] = {};
  int caret_width_px_ = 1;
  int line_height_px_ = 0;
  int scroll_y_ = 0;
  int scroll_x_ = 0;
  // Cached document content width (px) from the most recent layout. Updated by
  // every render() call. Used to size the horizontal scrollbar so it tracks the
  // widest line currently in view.
  int content_width_ = 0;
  bool drag_active_ = false;
  int zoom_idx_ = 4;  // 100% — index into kZoomLadder.
  // The "100%" font size in *logical 96-DPI pixels*. Stored DPI-agnostic so
  // moving the window between monitors at different DPIs just re-applies the
  // scale factor in ZoomedFontSize() without losing the user-picked size.
  // Initialized from the constructor's fontsize and overwritten whenever the
  // user picks a new font via Format > Font... .
  double zoom_base_font_size_ = default_font_size;
  // Current monitor DPI for this window. Updated on construction and on
  // WM_DPICHANGED_BEFOREPARENT so renderer + caret + scroll metrics stay in
  // sync with the system DPI.
  int current_dpi_ = 96;
  // Accumulator for Ctrl+MouseWheel deltas so precision touchpads (which
  // emit sub-WHEEL_DELTA increments) still zoom over time.
  int zoom_wheel_accum_ = 0;
  // Set by ApplyZoom (and font picker): on the next OnPaint, after layout
  // has produced fresh line_height_px_ / caret anchors, scroll so the caret
  // is back in view. Cleared by OnPaint after it runs.
  bool pending_ensure_caret_visible_ = false;
  std::wstring file_path_;
  bool last_title_dirty_ = false;
  HWND status_hwnd_ = nullptr;

  // GDI back-buffer for double-buffered software compositing.
  HBITMAP backbuffer_ = nullptr;
  HDC mem_dc_ = nullptr;
  HGDIOBJ old_bitmap_ = nullptr;
  uint32_t* bb_pixels_ = nullptr;
  int bb_w_ = 0;
  int bb_h_ = 0;
};

const ATOM SeditWndInit = Sedit::Initialize();

}  // namespace

namespace swg::winapp {
void SetInitialFilePath(const std::wstring& path) {
  g_initial_file_path = path;
}
// Called from the main message loop so the modeless Find/Replace dialog can
// process Tab / Enter / Esc etc. Returns true when the message was consumed
// by the dialog.
bool IsFindDialogMessage(MSG* msg) {
  if (!g_find_dialog) return false;
  return IsDialogMessageW(g_find_dialog, msg) != FALSE;
}
}  // namespace swg::winapp
