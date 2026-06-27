# Copilot instructions

Schwing Edit is a Windows text editor in C++23 (CMake + vcpkg). See `README.md`
for an overview and build commands.

## Layout

- `src/edit` — core engine library `schwing::edit`.
- `src/winapp` — Win32 GUI app, MSVC only, uses WIL.
- `src/ut` — GoogleTest unit tests.

## Build & test

```sh
cd src
cmake --preset vs-dbg
cmake --build b/vs-dbg
ctest --test-dir b/vs-dbg
```

## Conventions

- Everything lives in namespace `swg`.
- snake_case for types, functions, and variables; private members end with `_`.
- Header/source pairs are `.hpp` / `.cpp`.
- Formatting is clang-format (Google style, 100-column limit); run it before committing.
- The `edit` library builds with `/W4 /WX` — warnings are errors. Keep code clean.
- Prefer RAII wrappers (`unique_resource` in `resource.hpp`) over manual cleanup.
- Use designated initializers and C++23 features where they read clearly.
- Don't write comments in the codebase, except a single-line comment when
  something is super important.

## Tests

- Use GoogleTest; parameterized tests use `INSTANTIATE_TEST_SUITE_P` (not the
  deprecated `INSTANTIATE_TEST_CASE_P`, which fails under `/WX`).
- Test fixture class names are global across translation units, so keep them
  unique (e.g. prefix with the component, like `pt_` or `lt_`).
