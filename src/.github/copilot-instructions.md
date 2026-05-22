# Copilot Instructions — schwing-edit

This document captures the goals, design principles, and disciplines that any
contributor (human or AI) should follow when working in this repository. Keep
changes consistent with what is described below; if you must deviate, update
this file in the same change.

## 0. Hard requirements (non-negotiable)

These rules are load-bearing. A change that violates any of them must not be
merged; if a requested change appears to require violating one, push back
before implementing it.

> **Current priority: make everything work on Windows first.** The active
> target — the only one being built, run, and validated today — is the Win32
> desktop app in `src/winapp/`. Other operating systems (Linux, macOS, …)
> are **not** a priority right now: do not spend effort on Linux/macOS host
> implementations, packaging, CI, or porting unless explicitly asked. Hard
> requirement #1 (cross-platform, lightweight core) is about keeping the
> *door open* — `src/edit/` must stay free of Windows-only constructs so a
> future port is feasible — but it does **not** mean shipping non-Windows
> functionality now. When the two pull in opposite directions, ship the
> Windows feature and keep the core clean.

1. **The core layers must be cross-platform and lightweight.** Anything
   under `src/edit/` (and any future shared core) must compile and behave
   correctly on non-Windows toolchains in principle — no `<Windows.h>`,
   no Win32 types, no WIL, no DWM, no WGL, no `#pragma comment(lib, ...)`,
   no MSVC-only language extensions. Keep the dependency surface small
   and the runtime overhead minimal (no hidden allocations on hot paths,
   no heavyweight frameworks, no RTTI/exceptions abuse beyond the
   existing throw-on-precondition-violation pattern).
2. **`src/edit` must provide the full set of basic editing capabilities.**
   The core library owns, at minimum: the document model (piece table +
   line index + `plaindoc`/`host`), memory management for buffers and
   foreign resources, script analysis, font resolution, glyph shaping
   (via HarfBuzz), text layout analysis, viewport clipping, and windowed
   text layout. New editing capabilities of this kind belong in
   `src/edit/`, never in `src/winapp/`.
3. **Only `src/winapp` targets Windows.** All Win32 / WIL / DWM / WGL /
   Direct* code lives in `src/winapp/` and nowhere else. The `winapp`
   subdirectory is already gated by `if (MSVC)` in
   `src/CMakeLists.txt`; keep that gate, and do not introduce any other
   Windows-only target outside `src/winapp/`.
4. **All targets must be managed by CMake.** Every library, executable,
   resource, and test target is declared through `CMakeLists.txt` files
   under `src/` and configured via `src/CMakePresets.json`. Do not
   introduce parallel build systems (MSBuild project files, Makefiles,
   Ninja files checked in by hand, shell build scripts, etc.). New
   targets must be added as `add_library` / `add_executable` in the
   appropriate `CMakeLists.txt` and exposed via `schwing::<name>` ALIAS
   targets, consistent with `schwing::edit` and `schwing::app`.
5. **All dependencies must be managed by vcpkg.** Every third-party
   library is declared in `src/vcpkg.json` (manifest mode) and consumed
   through `find_package(...)` in CMake. Do not vendor third-party
   sources, do not use `FetchContent`, do not add git submodules for
   dependencies, and do not rely on system-installed copies outside of
   vcpkg. Windows-only dependencies must use the `"platform": "windows"`
   guard, as `wil` does today.
6. **Every API in `src/edit` must have corresponding test cases in
   `src/ut`.** Each public function, method, and observable behavior of
   the `schwing::edit` library is covered by GoogleTest cases under
   `src/ut/`. When you add or change an editor-core API, you add or
   update its tests in the same change — following the
   `TestWithParam` + `INSTANTIATE_TEST_CASE_P` data-driven pattern
   described in §6. Untested public APIs in `src/edit/` are treated as
   bugs.
7. **`swg::host` (in `src/edit`) is *the* platform customization point.**
   It is the seam that lets each platform plug its native capabilities
   (window system, input, rendering surface, caret, clipboard, IME,
   timers, etc.) into the schwing-edit core. Every platform-specific
   integration **must derive from `swg::host`** and implement its
   abstract operations (e.g. `on_invalidate(rect)`), exactly as
   `winapp/sedit.cpp`'s `Sedit` class does today. Do not bypass
   `swg::host` by reaching into `plaindoc` / `piecetable` /
   `linetable` directly from platform code, and do not add
   platform-specific virtuals or `#ifdef`s to other core types — extend
   `swg::host` instead so the customization stays in one well-defined
   place.

## 1. What this project is

`schwing-edit` (CMake project name: `schwing`) is a **high-performance,
Windows-native text editor** written from scratch in modern C++. It is split
into:

- `src/edit/` — `schwing::edit`, a platform-agnostic editor core (piece table,
  line index, document, font engine, FreeType/HarfBuzz resources). This is the
  reusable library.
- `src/winapp/` — `schwing::app`, the Win32 desktop application that hosts the
  editor core. It owns the top-level window, the child edit window, and the
  WGL/OpenGL rendering context.
- `src/ut/` — GoogleTest-based unit tests (`ut`) for the editor core.

The `winapp/` target is only added on MSVC; the `edit/` library and `ut/`
tests are intended to remain portable to other compilers in principle, though
the active build target today is MSVC + Windows.

## 2. Build, test, lint

Everything lives under `src/`. Out-of-tree builds go to `src/b/<preset>/`
(already in `.gitignore`).

Standard commands (verified):

```pwsh
# from src/
cmake --preset vs-dbg                          # configure
cmake --build b\vs-dbg --config Debug          # build
ctest --test-dir b\vs-dbg -C Debug             # run all tests
```

- Toolchain: vcpkg in **manifest mode** (`src/vcpkg.json`); `VCPKG_ROOT` env
  var must point at a vcpkg checkout. Triplet: `x64-windows`.
- Generator: `Visual Studio 18 2026`, x64, Debug.
- C++ standard: **C++23, required** (`CMAKE_CXX_STANDARD 23`,
  `CMAKE_CXX_STANDARD_REQUIRED ON`). Do not weaken this.
- Warnings are errors. MSVC: `/W4 /WX`. Other compilers: `-Wall -Wextra
  -Wpedantic -Werror`. Fix warnings; do not suppress them globally.
- Tests are auto-discovered with `gtest_discover_tests`; new GoogleTest cases
  show up in `ctest` without further wiring.
- Formatting: `.clang-format` is Google base, `ColumnLimit: 100`. Run
  clang-format on touched files; do not reformat unrelated code.

## 3. Dependencies

All third-party dependencies are managed by vcpkg via `src/vcpkg.json`:

- `freetype` — font rasterization.
- `harfbuzz` — complex-script text shaping.
- `glad` — OpenGL function loader (core 3.3 used).
- `gtest` — unit tests.
- `wil` (Windows-only) — Win32 RAII wrappers (`wil::unique_hwnd`,
  `wil::unique_hdc_window`, `wil::GetDC`, `wil::BeginPaint`, …) and error
  macros (`THROW_LAST_ERROR_IF`, `THROW_IF_WIN32_BOOL_FALSE`, `THROW_WIN32_IF`).

Win32 + DWM are linked directly (`#pragma comment(lib, "Dwmapi.lib")`). The
app uses the OpenGL ICD via `WGL`, bootstrapped through a dummy window so
that `wglCreateContextAttribsARB` / `wglChoosePixelFormatARB` can be used to
request a modern 3.3 core context.

Add new dependencies through `vcpkg.json` only — never vendor sources or use
`FetchContent`.

## 4. Source layout and module responsibilities

```
src/
├── CMakeLists.txt          # top-level: edit (always), winapp (MSVC), ut (BUILD_TESTING)
├── CMakePresets.json       # only preset: vs-dbg
├── vcpkg.json              # manifest deps
├── .clang-format           # Google, 100 cols
├── edit/                   # schwing::edit (PUBLIC include dir)
│   ├── piecetable.{hpp,cpp}  # piece-table text buffer
│   ├── linetable.{hpp,cpp}   # line index over a piecetable, EOL detection
│   ├── plaindoc.{hpp,cpp}    # document + host interface
│   ├── fontengine.{hpp,cpp}  # FreeType face + HarfBuzz font wrapper
│   └── resource.{hpp,cpp}    # FT_Library lifetime, deleters, error helpers
├── winapp/                 # schwing::app (WIN32 exe)
│   ├── win.hpp               # canonical Windows.h include (WIN32_LEAN_AND_MEAN, NOMINMAX)
│   ├── winmain.cpp           # wWinMain, MainWindow (mica, DPI)
│   ├── sedit.cpp             # Sedit child window: WGL bootstrap, host impl, input
│   ├── res.{h,rc.in}         # icon + VERSIONINFO templated from CMake
│   └── app_icon.ico
└── ut/                     # GoogleTest target `ut`
    ├── piecetable_tests.cpp
    └── linetable_tests.cpp
```

### Editor core architecture (`src/edit/`)

- **`swg::piecetable`** — classic piece table. Holds an immutable
  `initbuf_` (the original `string_view`) and a growing `addbuf_` string;
  `piecelist_` is a `vector<piece>` of `{offset, length, is_original}`.
  Supports `insert`, `erase`, `get`, `get_to(span)`, `length`. Insertions
  coalesce with the previous add-buffer piece when adjacent; mid-piece
  insertions and erases split pieces. Out-of-range positions throw
  `std::out_of_range`.
- **`swg::linetable`** — line index built on top of a `piecetable`. Each
  entry is `{beg, length}` where `length` includes the terminator. Knows
  three terminators (`eol::lf | crlf | cr`); `rebuild` and `insert` return
  `true` when more than one terminator kind is observed (mixed EOL).
  `line_at_pos` is a `ranges::upper_bound` over `beg`.
- **`swg::plaindoc` + `swg::host`** — `plaindoc` owns the `piecetable`,
  `linetable`, font path/size, EOL mode, and a `host*`. `host` is an
  abstract interface implemented by the UI; the editor calls back into
  `host::on_invalidate(rect)`. `host` also exposes high-level input ops
  (`insert_char`, `erase_char`, `delete_char`, `linefeed`) that operate on
  an internal `inspos_` and respect the document's EOL mode.
- **`swg::fontengine`** — wraps one `(fontpath, fontsize)` pair as a
  FreeType face + HarfBuzz font. Uses `swg::unique_ft_face` /
  `swg::unique_hb_font` with custom deleters defined in `swg::details`.
- **`swg::resource`** — owns the process-wide `FT_Library`
  (`swg::initialize()` / `swg::uninitialize()` called from `wWinMain`).
  Provides `check_fterror(FT_Error)` and `check_ptr(void*, const char*)`
  helpers that throw `std::runtime_error{std::format(...)}` on failure.

### Windows app architecture (`src/winapp/`)

- All Win32 headers come through `win.hpp`, which defines
  `WIN32_LEAN_AND_MEAN` and `NOMINMAX` before `<Windows.h>`. Do not include
  `<Windows.h>` directly.
- `UNICODE` and `_UNICODE` are defined for the `app` target;
  `static_assert(std::is_same_v<TCHAR, wchar_t>)` enforces this in code.
- `MainWindow` (`winmain.cpp`) is the top-level frame: mica backdrop via
  `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE, DWMSBT_MAINWINDOW)`,
  per-monitor V2 DPI awareness, a 30 dip title strip, and one child window
  of class `SEditWindowClass` filling the rest.
- `Sedit` (`sedit.cpp`) is the editor child window. It:
  - Implements `swg::host::on_invalidate` by calling `InvalidateRect`.
  - Bootstraps a modern WGL context: dummy `CS_OWNDC` window → legacy
    context → load `wglGetExtensionsStringARB`,
    `wglChoosePixelFormatARB`, `wglCreateContextAttribsARB` → request an
    OpenGL 3.3 **core** context → `gladLoadGL()`. Falls back gracefully if
    extensions are missing.
  - Assembles UTF-16 surrogate pairs from `WM_CHAR` and converts to UTF-8
    via `WideCharToMultiByte(CP_UTF8, ...)` before handing bytes to the
    editor. The editor's internal encoding is **UTF-8**.
  - Owns the caret (`CreateCaret` / `SetCaretPos` / `ShowCaret`).
- Resources (`res.rc.in`) are configured by CMake; `VERSION_MAJOR` /
  `_MINOR` / `_PATCH` come from the top-level `project(... VERSION ...)`.

## 5. Coding conventions and disciplines

These are observed throughout the codebase. Follow them in new code.

### Language and style

- **C++23 is fair game.** The code uses `std::out_ptr`, `std::format`,
  `std::ranges`, `std::unreachable()`, `0uz` size literals, templated
  lambdas (`[&]<class T>(const T&)`), `std::span`, and designated
  initializers freely. Prefer the modern form.
- Headers use `#pragma once`. No include guards.
- Group includes and label the groups with comments, in this order:

  ```cpp
  // std
  #include <vector>
  // deps
  #include <freetype/freetype.h>
  // windows           (in winapp only)
  #include "win.hpp"
  // wil               (in winapp only)
  #include <wil/result_macros.h>
  // gl / glad         (when applicable)
  #include <glad/glad.h>
  // swg / schwing
  #include "piecetable.hpp"
  // app               (in winapp only)
  #include "res.h"
  ```
- Use trailing-`_` for private member variables (`piecelist_`, `inspos_`,
  `hwnd_`, …). Local variables and function parameters are plain
  `snake_case`. Types are `snake_case` too (`piecetable`, `linetable`,
  `plaindoc`, `fontengine`, `host`). Win32-flavored types in `winapp` may
  use `PascalCase` (`MainWindow`, `Sedit`) to match the Win32 idiom.
- Comments are sparse and explain *why*, not *what*. Don't add
  obvious-from-code comments.

### Namespaces

- All library code lives in `namespace swg`.
- Implementation-detail helpers (custom deleters, etc.) live in
  `namespace swg::details`.
- Tests live in `namespace swg::ut::<module>_ut` (e.g.
  `swg::ut::piecetable_ut`, `swg::ut::linetable_ut`), with local test-only
  types in an anonymous namespace inside.

### Hidden-implementation pattern (no heap pimpl)

The codebase uses a distinctive "static-helpers on a forward-declared
`impl` struct" pattern instead of allocated pimpl. The class header
forward-declares a private `struct impl;`, and the `.cpp` defines it with
only `static` methods that take a pointer to the outer object:

```cpp
// header
class piecetable {
  struct impl;
  // ...
};

// cpp
struct piecetable::impl {
  static auto find_piece(const piecetable* self, size_t pos) { /* ... */ }
  static void copy_out(const piecetable* self, size_t pos, size_t length, char* out) { /* ... */ }
};
```

This keeps private helpers out of the header without any extra allocation
or indirection. Use this pattern when you need non-trivial private helpers.

### Resource management

- **RAII everything.** Foreign C resources are wrapped in
  `std::unique_ptr` with a custom deleter declared in `swg::details`
  (e.g. `unique_ft_face`, `unique_hb_font`, `unique_hglrc`,
  `unique_wgl_bootstrap_context`). When adding a new C resource, add its
  deleter the same way.
- Win32 handles use `wil::unique_*` (e.g. `wil::unique_hwnd`,
  `wil::unique_hdc_window`). Use `wil::GetDC` / `wil::BeginPaint` rather
  than raw `GetDC` / `BeginPaint` whenever possible.
- Global FreeType state is owned by `swg::resource`
  (`swg::initialize()` / `swg::uninitialize()` bracketing `wWinMain`).

### Error handling

- The editor core throws `std::out_of_range` for invalid positions /
  lengths and `std::runtime_error` for foreign-API failures.
- Use the existing helpers: `swg::check_fterror(FT_Error)` for FreeType
  return codes, `swg::check_ptr(void*, const char*)` for nullable pointers
  returned from C APIs.
- In `winapp`, prefer WIL error macros (`THROW_LAST_ERROR_IF`,
  `THROW_IF_WIN32_BOOL_FALSE`, `THROW_WIN32_IF`) for Win32 calls.
- Do not introduce silent error paths; if a precondition cannot hold,
  throw. `std::unreachable()` is used for genuinely unreachable switch
  arms.

### Text encoding and EOL

- The document buffer is **UTF-8** end to end. UTF-16 input from Windows
  must be transcoded before entering the editor.
- UTF-8 char boundaries are detected by the continuation-byte mask
  `(byte & 0xC0) != 0x80`. See `host::erase_char` for the canonical
  back-scan loop; reuse that pattern, do not reinvent it.
- EOL is a per-document choice (`eol::lf | crlf | cr`). The `linetable`
  reports mixed EOL via a `bool` return from `rebuild` / `insert`; honor
  that signal (`plaindoc::mixeol_`).

## 6. Testing discipline

- All editor-core changes need GoogleTest coverage in `src/ut/`.
- The standard test shape is **data-driven, parameterized tests**:

  ```cpp
  struct test_case { /* inputs + expected outputs */ };
  struct linetable_tests : ::testing::TestWithParam<test_case> {};
  TEST_P(linetable_tests, run) { /* drive the SUT from GetParam() */ }
  INSTANTIATE_TEST_CASE_P(group_name, linetable_tests, ::testing::Values(
      test_case{ /* ... */ },
      // ...
  ));
  ```

  Add new scenarios as additional `test_case` entries; only add a new
  fixture when you genuinely need different setup.
- When a test needs access to private state, expose it via `FRIEND_TEST`
  **guarded by `#ifdef SWGUT`** in the header. The `ut` target defines
  `SWGUT` (see `src/ut/CMakeLists.txt`); production builds do not.
  Forward-declare the test fixture in `namespace swg::ut::<module>_ut`
  inside the same `#ifdef SWGUT` block. See `linetable.hpp` for the
  canonical example.
- Each `test_case` line in `INSTANTIATE_TEST_CASE_P` has a one-line
  comment describing the scenario. Keep this up when adding cases.
- Use designated initializers in test data; it makes intent obvious.

## 7. Things to keep in mind when changing code

- **Do not weaken warnings, the C++ standard, or `Werror`.** If a new
  warning is legitimate, fix the code; if it is genuinely spurious, scope
  the suppression as narrowly as possible (`#pragma warning(push/pop)` for
  a single declaration, etc.).
- **Keep the editor core free of Win32.** Anything that depends on
  `<Windows.h>`, WGL, WIL, or DWM belongs in `winapp/`. The `edit/`
  library must remain a pure C++ + FreeType + HarfBuzz + OpenGL-loader
  module.
- **No allocations in hot inner loops.** `piecetable::impl::copy_out` and
  `linetable::rebuild` deliberately reuse fixed-size buffers (256 B) and
  walk pieces without per-byte allocation. Preserve that.
- **Coalesce when you can.** Both `piecetable::insert` and
  `linetable::insert` carefully coalesce/extend instead of always
  splitting; new editing primitives should follow suit.
- **Mind the build directory.** Build output goes to `src/b/<preset>/`
  and is git-ignored. Do not commit anything under `src/b/`.
- **Resource versioning is derived from CMake.** Bump
  `project(schwing VERSION X.Y.Z ...)` in `src/CMakeLists.txt` for
  releases; `res.rc.in` picks it up automatically.
- **Windows-only features stay behind `if (MSVC)`** in CMake (the
  `winapp` subdirectory is gated this way today). Don't unconditionally
  add Win32-only code to the top-level build.
