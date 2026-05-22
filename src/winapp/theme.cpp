// std
#include <iterator>
// windows
#include "theme.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Uxtheme.lib")

namespace swg::winapp::theme {

namespace {

// Undocumented uxtheme.dll entry points used by Explorer to drive dark-mode
// menus and common controls. We resolve them lazily and treat absence as a
// no-op — keeps the binary compatible with older Windows builds.
enum class PreferredAppMode : int {
  Default = 0,
  AllowDark = 1,
  ForceDark = 2,
  ForceLight = 3,
};

using SetPreferredAppMode_fn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using FlushMenuThemes_fn = void(WINAPI*)();
using AllowDarkModeForWindow_fn = BOOL(WINAPI*)(HWND, BOOL);

struct uxtheme_api {
  HMODULE module = nullptr;
  SetPreferredAppMode_fn set_preferred = nullptr;
  FlushMenuThemes_fn flush_menus = nullptr;
  AllowDarkModeForWindow_fn allow_for_window = nullptr;
};

const uxtheme_api& get_uxtheme() {
  static const uxtheme_api api = [] {
    uxtheme_api a{};
    a.module = LoadLibraryExW(L"uxtheme.dll", nullptr,
                              LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!a.module) return a;
    a.set_preferred = reinterpret_cast<SetPreferredAppMode_fn>(
        GetProcAddress(a.module, MAKEINTRESOURCEA(135)));
    a.flush_menus = reinterpret_cast<FlushMenuThemes_fn>(
        GetProcAddress(a.module, MAKEINTRESOURCEA(136)));
    a.allow_for_window = reinterpret_cast<AllowDarkModeForWindow_fn>(
        GetProcAddress(a.module, MAKEINTRESOURCEA(133)));
    return a;
  }();
  return api;
}

constexpr palette kLight = {
    .editor_bg = 0x00FFFFFFu,
    .editor_fg = 0x001F1F1Fu,
    .selection_bg = 0x00CCE4F7u,
    .caret_color = 0x00000000u,
    .status_bg = 0x00F3F3F3u,
    .status_fg = 0x001F1F1Fu,
    .dark = false,
};

constexpr palette kDark = {
    .editor_bg = 0x001F1F1Fu,
    .editor_fg = 0x00E6E6E6u,
    .selection_bg = 0x00264F78u,
    .caret_color = 0x00E6E6E6u,
    .status_bg = 0x00202020u,
    .status_fg = 0x00CCCCCCu,
    .dark = true,
};

}  // namespace

bool is_dark_mode() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    0, KEY_READ, &key) != ERROR_SUCCESS) {
    return false;
  }
  DWORD value = 1;  // light default
  DWORD size = sizeof(value);
  DWORD type = 0;
  LONG status = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(&value), &size);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS || type != REG_DWORD) return false;
  return value == 0;
}

palette current_palette() {
  return is_dark_mode() ? kDark : kLight;
}

void initialize_app_dark_mode() {
  const auto& api = get_uxtheme();
  if (api.set_preferred) {
    api.set_preferred(PreferredAppMode::AllowDark);
  }
  if (api.flush_menus) {
    api.flush_menus();
  }
}

void apply_window_theme(HWND hwnd) {
  if (!hwnd) return;
  const palette p = current_palette();
  const BOOL dark = p.dark ? TRUE : FALSE;
  // Immersive dark title bar — supported on Windows 10 1809+ via attribute 19,
  // and Windows 11 via the documented value 20. Try the modern one first.
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  // Fallback for older Win10 1809-1903 builds where the attribute id is 19.
  static constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_PRE_20H1 = 19;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_PRE_20H1, &dark,
                        sizeof(dark));

  // Round the window corners (Win11). DWMWCP_ROUND = 2.
  static constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE_LOCAL = 33;
  DWORD corner = 2;  // DWMWCP_ROUND
  DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_LOCAL, &corner,
                        sizeof(corner));

  // Mica system backdrop (Win11 22H2+). DWMSBT_MAINWINDOW = 2.
  DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
  DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                        sizeof(backdrop));

  // Opt this specific window into the dark non-client palette as well — needed
  // for child controls and dialogs spawned with this hwnd as owner to inherit
  // the dark scrollbar/menu colors.
  const auto& api = get_uxtheme();
  if (api.allow_for_window) {
    api.allow_for_window(hwnd, dark);
  }
}

namespace {

// Subclass proc for the status bar: paints background + text using the active
// theme palette so dark mode actually feels dark. Falls through to the
// default comctl32 handler for every other message, so resize/sizing-grip
// behavior is preserved.
LRESULT CALLBACK StatusSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR /*id*/, DWORD_PTR /*ref*/) {
  switch (msg) {
    case WM_ERASEBKGND: {
      const palette pal = current_palette();
      HDC dc = reinterpret_cast<HDC>(wp);
      RECT rc;
      GetClientRect(hwnd, &rc);
      HBRUSH br = CreateSolidBrush(RGB((pal.status_bg >> 16) & 0xFF,
                                       (pal.status_bg >> 8) & 0xFF,
                                       pal.status_bg & 0xFF));
      FillRect(dc, &rc, br);
      DeleteObject(br);
      return 1;
    }
    case WM_PAINT: {
      const palette pal = current_palette();
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      HBRUSH br = CreateSolidBrush(RGB((pal.status_bg >> 16) & 0xFF,
                                       (pal.status_bg >> 8) & 0xFF,
                                       pal.status_bg & 0xFF));
      FillRect(dc, &rc, br);
      DeleteObject(br);
      // Read the stored window text (set via SetWindowTextW from the editor)
      // and render it ourselves so it picks up the theme foreground color.
      wchar_t text[512];
      int len = GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
      if (len > 0) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB((pal.status_fg >> 16) & 0xFF,
                             (pal.status_fg >> 8) & 0xFF,
                             pal.status_fg & 0xFF));
        // Use the default GUI font (matches comctl32's status text font).
        HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ old = SelectObject(dc, font);
        RECT tr = rc;
        tr.left += 8;
        DrawTextW(dc, text, len, &tr,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_SETTEXT: {
      // Default handler stores the new text; we just need to repaint after.
      LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
      InvalidateRect(hwnd, nullptr, FALSE);
      return r;
    }
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}

}  // namespace

void apply_status_bar_theme(HWND status_hwnd) {
  if (!status_hwnd) return;
  // Disable comctl32's visual-styles theme for the control so our subclassed
  // WM_ERASEBKGND/WM_PAINT fully own the look — otherwise the themed back
  // brush peeks through behind our fill.
  SetWindowTheme(status_hwnd, L"", L"");
  SetWindowSubclass(status_hwnd, StatusSubclassProc, /*id=*/1u, /*ref=*/0u);
}

void refresh_window_theme(HWND hwnd) {
  apply_window_theme(hwnd);
  // Force the title bar to redraw with the new immersive-dark attribute.
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
  const auto& api = get_uxtheme();
  if (api.flush_menus) api.flush_menus();
  // Force menu bar redraw.
  DrawMenuBar(hwnd);
  // Invalidate client area so child windows that depend on the palette
  // (editor, status bar) repaint with the new colors.
  InvalidateRect(hwnd, nullptr, TRUE);
}

}  // namespace swg::winapp::theme
