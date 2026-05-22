#pragma once

// Lightweight Win11 visual-theme helper for the editor: detects the system's
// light/dark preference, exposes a tiny color palette, and centralizes the
// undocumented uxtheme calls needed to make Win32 menus pick up dark mode.
//
// Win32 only — no XAML Islands, no WinAppSDK. Designed to be a no-op fallback
// on older Windows builds: if a DWM/uxtheme call isn't available, the title
// bar simply stays light and the editor renders with light colors.

// windows
#include "win.hpp"

namespace swg::winapp::theme {

struct palette {
  // Editor area
  uint32_t editor_bg;     // 0x00RRGGBB, fed straight into the GDI back-buffer.
  uint32_t editor_fg;
  uint32_t selection_bg;
  uint32_t caret_color;
  // Status bar
  uint32_t status_bg;
  uint32_t status_fg;
  // True when the active theme is dark.
  bool dark;
};

// Read the user's Windows app-theme preference. Returns true when "Choose your
// mode" is set to Dark. Falls back to false (light) on older Windows builds or
// if the registry key can't be read.
bool is_dark_mode();

// Return the active palette based on the current system theme.
palette current_palette();

// One-time process setup: tell uxtheme that the app is dark-mode aware so the
// menu bar, scroll bars, common controls etc. pick up dark colors. Must be
// called before the first window is created. Safe to call on any Windows
// build — falls back to a no-op when the undocumented entry points are absent.
void initialize_app_dark_mode();

// Apply the current theme to a top-level window: immersive dark title bar,
// rounded corners (Win11), and a Mica system backdrop. Safe to call on
// pre-Win11 builds — the unsupported DWM attributes silently fail.
void apply_window_theme(HWND hwnd);

// Subclass the comctl32 STATUSCLASSNAME instance with theme-aware paint
// handlers, so it picks up the dark palette when in dark mode. Idempotent —
// safe to call once after the status bar is created.
void apply_status_bar_theme(HWND status_hwnd);

// Re-evaluate the current system theme and re-apply window-level visuals
// (dark title bar, menu redraw). Call from WM_SETTINGCHANGE handlers.
void refresh_window_theme(HWND hwnd);

}  // namespace swg::winapp::theme
