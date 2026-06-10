# Copilot Instructions — schwing-edit

This document captures the goals, design principles, and disciplines that any
contributor (human or AI) should follow when working in this repository. Keep
changes consistent with what is described below; if you must deviate, update
this file in the same change.

## 0. Hard requirements (non-negotiable)

> **Current priority: make everything work on Windows first.** The active
> target — the only one being built, run, and validated today — is the Win32
> desktop app in `src/winapp/`. Other operating systems are not a priority
> right now: do not spend effort on Linux/macOS host implementations,
> packaging, CI, or porting unless explicitly asked. Hard requirement #1
> (cross-platform, lightweight core) is about keeping the *door open* —
> `src/edit/` must stay free of Windows-only constructs so a future port is
> feasible — but it does **not** mean shipping non-Windows functionality now.

1. **The core layers must be cross-platform and lightweight.** Anything
   under `src/edit/` (and any future shared core) must compile and behave
   correctly on non-Windows toolchains in principle — no `<Windows.h>`,
   no Win32 types, no WIL, no DWM, no MSVC-only language extensions.
   Keep dependencies small and runtime overhead minimal (no hidden
   allocations on hot paths).
2. **`src/edit` owns the editor model.** The core library owns: the
   piece-table buffer, the line index, the document (`plaindoc`) and the
   `host` interface that platforms implement. Future expansions
   (font resolution, glyph shaping, text layout) also belong in
   `src/edit/`, never in `src/winapp/`.
3. **Only `src/winapp` targets Windows.** All Win32 / WIL / DWM code lives
   in `src/winapp/` and nowhere else. The `winapp` subdirectory is gated
   by `if (MSVC)` in `src/CMakeLists.txt`; keep that gate.
4. **All targets must be managed by CMake.** Every library, executable,
   resource, and test target is declared through `CMakeLists.txt` files
   under `src/` and configured via `src/CMakePresets.json`. Do not
   introduce parallel build systems. New targets are exposed via
   `schwing::<name>` ALIAS targets, consistent with `schwing::edit` and
   `schwing::app`.
5. **All dependencies must be managed by vcpkg.** Every third-party
   library is declared in `src/vcpkg.json` (manifest mode) and consumed
   through `find_package(...)` in CMake. Do not vendor sources, do not
   use `FetchContent`, do not use submodules. Windows-only dependencies
   use the `"platform": "windows"` guard.
6. **Every API in `src/edit` must have corresponding test cases in
   `src/ut`.** Each public function, method, and observable behavior is
   covered by GoogleTest cases under `src/ut/`. When you add or change
   an editor-core API, you add or update its tests in the same change.
7. **`swg::host` (in `src/edit`) is *the* platform customization point.**
   Every platform-specific integration **must derive from `swg::host`**
   and implement its abstract operations (e.g. `on_invalidate(rect)`,
   `on_doc_changed(damage)`), exactly as `winapp/sedit.cpp`'s `Sedit`
   class does. Do not bypass `swg::host` by reaching into `plaindoc` /
   `piecetable` / `linetable` directly from platform code, and do not
   add platform-specific virtuals or `#ifdef`s to other core types —
   extend `swg::host` instead.

## 1. What this project is

`schwing-edit` (CMake project name: `schwing`) is a high-performance,
Windows-native plaintext editor written from scratch in modern C++.

- `src/edit/` — `schwing::edit`, a platform-agnostic editor core
  (piece table, line index, document, host interface).
- `src/winapp/` — `schwing::app`, the Win32 desktop application that
  hosts the editor core. Owns the top-level window, the child edit
  window, GDI rendering, and file I/O.
- `src/ut/` — GoogleTest-based unit tests for the editor core.

The `winapp/` target is only added on MSVC.

### Current scope (MVP) and deferred work

The MVP currently shipped here intentionally trades feature breadth for
correctness and speed on the *core data path*:

- **Rendering**: GDI `ExtTextOutW` on a per-line, viewport-only basis.
  Glyph atlas / FreeType / HarfBuzz / OpenGL are deferred — when added,
  they belong in `src/edit/` (font + atlas + shaper + layout) with the
  platform doing pure compositing. No Win32 types may appear in `edit/`.
- **Editing**: insert, backspace, delete-forward, arrow / page / home /
  end motion, mouse positioning, selection (shift-arrow + mouse drag),
  cut/copy/paste with the system clipboard, basic find. Undo/redo and
  goto-line are deferred.
- **File I/O**: open via `mio::mmap_source` → `host::load_view(...)`
  (zero-copy); save by materializing the piecetable to a freshly
  written file. UTF-8 in, UTF-8 out. UTF-16 sources are decoded once
  on load.

When you add a deferred feature, update this file in the same change.

## 2. Build, test, lint

Everything lives under `src/`. Out-of-tree builds go to `src/b/<preset>/`
(git-ignored).

```pwsh
# from src/
cmake --preset vs-dbg                          # configure
cmake --build b\vs-dbg --config Debug          # build
ctest --test-dir b\vs-dbg -C Debug             # run all tests
```

- Toolchain: vcpkg manifest mode; `VCPKG_ROOT` must point at a vcpkg
  checkout. Triplet: `x64-windows`.
- Generator: `Visual Studio 18 2026`, x64, Debug.
- C++ standard: **C++23, required**. Do not weaken.
- Warnings are errors. MSVC: `/W4 /WX`. Other compilers:
  `-Wall -Wextra -Wpedantic -Werror`.
- Tests are auto-discovered with `gtest_discover_tests`.
- Formatting: `.clang-format` is Google base, `ColumnLimit: 100`.

## 3. Dependencies

Managed by vcpkg via `src/vcpkg.json`:

- `gtest` — unit tests.
- `mio` — header-only memory mapping. The mmap'd `string_view` is
  handed straight to `piecetable::initbuf_` so large documents load
  without a per-byte copy.
- `wil` (Windows-only) — Win32 RAII wrappers and error macros.

Win32 + GDI are linked directly through the default platform libs.

## 4. Source layout

```
src/
├── CMakeLists.txt          # top-level: edit (always), winapp (MSVC), ut (BUILD_TESTING)
├── CMakePresets.json       # only preset: vs-dbg
├── vcpkg.json              # manifest deps
├── .clang-format           # Google, 100 cols
├── edit/                   # schwing::edit (PUBLIC include dir)
│   ├── piecetable.{hpp,cpp}  # piece-table text buffer
│   ├── linetable.{hpp,cpp}   # line index over a piecetable
│   ├── plaindoc.{hpp,cpp}    # document + host interface
│   └── CMakeLists.txt
├── winapp/                 # schwing::app (WIN32 exe) — gated by `if (MSVC)`
│   ├── win.hpp               # canonical Windows.h include
│   ├── winmain.cpp           # wWinMain, MainWindow
│   ├── sedit.cpp             # Sedit child window: GDI render, host impl, mmap I/O
│   ├── res.h, res.rc.in      # icon + VERSIONINFO templated from CMake
│   └── CMakeLists.txt
└── ut/
    ├── piecetable_tests.cpp
    ├── linetable_tests.cpp
    ├── plaindoc_tests.cpp
    └── CMakeLists.txt
```

### Editor core architecture (`src/edit/`)

- **`swg::piecetable`** — classic piece table. Holds an immutable
  `initbuf_` (the original `string_view`) and a growing `addbuf_`
  string; `piecelist_` is a `vector<piece>` of `{offset, length,
  is_original}`. Supports `insert`, `erase`, `get`, `get_to(span)`,
  `length`. Insertions coalesce with the previous add-buffer piece
  when adjacent; mid-piece insertions and erases split pieces.
  Out-of-range positions throw `std::out_of_range`.
- **`swg::linetable`** — line index built on top of a `piecetable`.
  Each entry is `{beg, length}` where `length` includes the
  terminator. Knows three terminators (`eol::lf | crlf | cr`);
  `rebuild` and `insert`/`erase` keep the index consistent by rescanning
  only the affected window. `line_at_pos` is `ranges::upper_bound` over
  `beg`. Reports mixed-EOL via a `bool` flag.
- **`swg::plaindoc` + `swg::host`** — `plaindoc` owns the
  `piecetable`, `linetable`, EOL mode, caret + selection, and a
  `host*`. `host` is the abstract platform customization point;
  the editor calls back via `host::on_invalidate(rect)` and
  `host::on_doc_changed(damage)`. `host` exposes high-level input
  operations (`insert_text`, `backspace`, `delete_forward`,
  `move_caret`, `select_to`, …) and document I/O hooks
  (`load_text(utf8_copy, eol)` for owned bytes, `load_view(utf8_view,
  eol)` for non-owning bytes — caller MUST keep storage alive until
  the next `load_text`/`load_view`/`materialize`/destruction).

### Windows app architecture (`src/winapp/`)

- All Win32 headers come through `win.hpp`, which defines
  `WIN32_LEAN_AND_MEAN` and `NOMINMAX` before `<Windows.h>`. Do not
  include `<Windows.h>` directly.
- `UNICODE` and `_UNICODE` are defined for the `app` target.
- `MainWindow` (`winmain.cpp`) is the top-level frame. One child window
  of class `SEditWindowClass` fills the client area.
- `Sedit` (`sedit.cpp`) is the editor child window:
  - Implements `swg::host::on_invalidate` by calling `InvalidateRect`,
    and `on_doc_changed(damage)` by invalidating only the affected lines.
  - Renders via GDI `ExtTextOutW` on a per-visible-line basis. UTF-8
    bytes from the document are transcoded to UTF-16 only for the
    visible window. No full-buffer transcode.
  - Uses native `CreateCaret`/`SetCaretPos`/`ShowCaret`.
  - Memory-maps the open file with `mio::mmap_source`. For UTF-8
    bytes, the mmap'd `string_view` is handed to `host::load_view(...)`
    so the editor reads them through the piecetable's `initbuf_`
    without any copy. For UTF-16 the host falls back to `load_text(...)`
    after a one-time owned decode. Before any save, the host calls
    `plaindoc::materialize()` to copy `initbuf_` into `addbuf_` and
    release the mmap so the file is not locked.
  - Assembles UTF-16 surrogate pairs from `WM_CHAR` and converts to
    UTF-8 via `WideCharToMultiByte(CP_UTF8, ...)`.

## 5. Coding conventions

- **C++23 is fair game.** `std::format`, `std::ranges`, `std::span`,
  `std::unreachable()`, `0uz` literals, designated initializers,
  templated lambdas.
- Headers use `#pragma once`. No include guards.
- Group includes and label the groups with comments:

  ```cpp
  // std
  #include <vector>
  // deps
  #include <mio/mmap.hpp>
  // windows           (winapp only)
  #include "win.hpp"
  // wil               (winapp only)
  #include <wil/result_macros.h>
  // swg / schwing
  #include "piecetable.hpp"
  // app               (winapp only)
  #include "res.h"
  ```
- Trailing-`_` for private member variables (`piecelist_`, `inspos_`,
  `hwnd_`). Local variables and function parameters are plain
  `snake_case`. Types are `snake_case` too (`piecetable`, `linetable`,
  `plaindoc`, `host`). Win32-flavored types in `winapp` may use
  `PascalCase` (`MainWindow`, `Sedit`).
- Comments explain *why*, not *what*.

### Namespaces

- All library code lives in `namespace swg`.
- Implementation-detail helpers live in `namespace swg::details`.
- Tests live in `namespace swg::ut::<module>_ut`.

### Hidden-implementation pattern

Use the "static-helpers on a forward-declared `impl` struct" pattern
instead of allocated pimpl. The header forward-declares
`struct impl;`, and the `.cpp` defines it with only `static` methods
that take a pointer to the outer object.

### Resource management

- RAII everything.
- Foreign C resources wrap in `std::unique_ptr` with custom deleters
  declared in `swg::details`.
- Win32 handles use `wil::unique_*` and `wil::GetDC` / `wil::BeginPaint`.

### Error handling

- The core throws `std::out_of_range` for invalid positions / lengths
  and `std::runtime_error` for foreign-API failures.
- In `winapp`, prefer WIL error macros (`THROW_LAST_ERROR_IF`,
  `THROW_IF_WIN32_BOOL_FALSE`, `THROW_WIN32_IF`).

### Text encoding and EOL

- The document buffer is **UTF-8** end to end. UTF-16 input from
  Windows must be transcoded before entering the editor.
- UTF-8 char boundaries are detected by the continuation-byte mask
  `(byte & 0xC0) != 0x80`.
- EOL is a per-document choice (`eol::lf | crlf | cr`).

## 6. Testing discipline

- All editor-core changes need GoogleTest coverage in `src/ut/`.
- Standard test shape is **data-driven, parameterized tests**:

  ```cpp
  struct test_case { /* inputs + expected outputs */ };
  struct linetable_tests : ::testing::TestWithParam<test_case> {};
  TEST_P(linetable_tests, run) { /* drive the SUT from GetParam() */ }
  INSTANTIATE_TEST_CASE_P(group_name, linetable_tests, ::testing::Values(
      test_case{ /* ... */ },
      // ...
  ));
  ```

- When a test needs private state, expose via `FRIEND_TEST` guarded by
  `#ifdef SWGUT`. The `ut` target defines `SWGUT`.
- Each `test_case` line in `INSTANTIATE_TEST_CASE_P` has a one-line
  comment describing the scenario.

## 7. Things to keep in mind

- **Do not weaken warnings, the C++ standard, or `Werror`.**
- **Keep the editor core free of Win32.** Anything that depends on
  `<Windows.h>`, WIL, or DWM belongs in `winapp/`.
- **No allocations in hot inner loops.** `piecetable::copy_out` and
  `linetable::rebuild` reuse fixed-size scratch buffers; preserve that.
- **Coalesce when you can.** `piecetable::insert` and `linetable::insert`
  carefully coalesce/extend instead of always splitting; follow suit.
- **Mind the build directory.** Build output goes to `src/b/<preset>/`
  and is git-ignored.
- **Resource versioning is derived from CMake.** Bump
  `project(schwing VERSION X.Y.Z ...)` for releases; `res.rc.in` picks
  it up automatically.
- **Windows-only features stay behind `if (MSVC)`** in CMake.
