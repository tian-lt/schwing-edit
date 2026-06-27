# Schwing Edit

A Windows text editor written in C++23.

## Components

- `src/edit` — core editing engine (piece table, line table, Unicode itemizing,
  text shaping, OpenGL glyph-atlas rendering). Built as the `schwing::edit` library.
- `src/winapp` — Win32 GUI application (`app`), MSVC only.
- `src/ut` — GoogleTest unit tests.

## Prerequisites

- CMake 3.20+
- [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set

Dependencies (freetype, harfbuzz, icu, glad, gtest, wil) are restored
automatically by vcpkg in manifest mode.

## Build & test

```sh
cd src
cmake --preset vs-dbg
cmake --build b/vs-dbg
ctest --test-dir b/vs-dbg
```
