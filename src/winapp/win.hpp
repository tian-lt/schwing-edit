#pragma once

// windows header
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef NOMINMAX
#undef WIN32_LEAN_AND_MEAN

#ifdef __cplusplus
inline constexpr int ID_EDIT = 100;
inline constexpr int IDM_FILE_NEW = 40001;
inline constexpr int IDM_FILE_OPEN = 40002;
inline constexpr int IDM_FILE_SAVE = 40003;
inline constexpr int IDM_FILE_SAVE_AS = 40004;
inline constexpr int IDM_FILE_PAGE_SETUP = 40005;
inline constexpr int IDM_FILE_PRINT = 40006;
inline constexpr int IDM_FILE_EXIT = 40007;
inline constexpr int IDM_EDIT_UNDO = 40101;
inline constexpr int IDM_EDIT_CUT = 40102;
inline constexpr int IDM_EDIT_COPY = 40103;
inline constexpr int IDM_EDIT_PASTE = 40104;
inline constexpr int IDM_EDIT_DELETE = 40105;
inline constexpr int IDM_EDIT_SELECT_ALL = 40106;
inline constexpr int IDM_EDIT_TIME_DATE = 40107;
inline constexpr int IDM_SEARCH_FIND = 40201;
inline constexpr int IDM_SEARCH_FIND_NEXT = 40202;
inline constexpr int IDM_SEARCH_REPLACE = 40203;
inline constexpr int IDM_SEARCH_GO_TO = 40204;
inline constexpr int IDM_FORMAT_WORD_WRAP = 40301;
inline constexpr int IDM_FORMAT_FONT = 40302;
inline constexpr int IDM_VIEW_STATUS_BAR = 40401;
inline constexpr int IDM_HELP_ABOUT = 40501;
inline constexpr int SEDIT_SET_WORD_WRAP = WM_APP + 1;
#endif
