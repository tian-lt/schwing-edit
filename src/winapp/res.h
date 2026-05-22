#pragma once

#define IDI_APP_ICON 101

#define IDR_MAIN_MENU 200

// Menu command IDs.
#define IDM_FILE_NEW         1001
#define IDM_FILE_OPEN        1002
#define IDM_FILE_SAVE        1003
#define IDM_FILE_SAVE_AS     1004
#define IDM_FILE_EXIT        1099

#define IDM_EDIT_UNDO        1101
#define IDM_EDIT_REDO        1102
#define IDM_EDIT_CUT         1103
#define IDM_EDIT_COPY        1104
#define IDM_EDIT_PASTE       1105
#define IDM_EDIT_DELETE      1106
#define IDM_EDIT_SELECT_ALL  1107
#define IDM_EDIT_FIND        1108
#define IDM_EDIT_FIND_NEXT   1109
#define IDM_EDIT_REPLACE     1110
#define IDM_EDIT_GOTO        1111
#define IDM_EDIT_TIME_DATE   1112

#define IDM_FORMAT_FONT      1201

#define IDM_HELP_ABOUT       1301

// Go To dialog
#define IDD_GOTO             400
#define IDC_GOTO_EDIT        401
#define IDC_GOTO_LABEL       402

// Custom message: top-level window asks the editor child whether it's safe to
// close. The editor presents the save/discard prompt and returns 1 to proceed
// or 0 if the user cancelled.
#define WM_SEDIT_CAN_CLOSE   (WM_USER + 1)
#define WM_SEDIT_SET_STATUS  (WM_USER + 2)  // wparam = HWND of status bar control
