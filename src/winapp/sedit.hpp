#pragma once
#include "win.hpp"

constexpr const TCHAR* SeditWindowClass = TEXT("SEditWindowClass");

enum class SeditMessage : UINT {
  Begin = WM_APP + 2000,
  SetDoc,
};
