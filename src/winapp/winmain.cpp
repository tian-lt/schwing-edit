// std
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <fstream>
#include <format>
#include <iterator>
#include <string>
#include <vector>
// windows
#include <dwmapi.h>

#include "win.hpp"
#include <commdlg.h>
#include <shellapi.h>
// wil
#include <wil/resource.h>
#include <wil/result_macros.h>
// swg
#include "resource.hpp"
// app
#include "res.h"

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")

namespace {
static_assert(std::is_same_v<TCHAR, wchar_t>);
const UINT find_replace_msg = RegisterWindowMessageW(FINDMSGSTRINGW);

enum class text_encoding { utf8, utf8_bom, utf16_le, utf16_be };

wil::unique_hmenu CreateMainMenu() {
  wil::unique_hmenu menu{CreateMenu()};
  THROW_LAST_ERROR_IF(!menu);
  wil::unique_hmenu file{CreatePopupMenu()};
  THROW_LAST_ERROR_IF(!file);
  AppendMenu(file.get(), MF_STRING, IDM_FILE_NEW, L"&New\tCtrl+N");
  AppendMenu(file.get(), MF_STRING, IDM_FILE_OPEN, L"&Open...\tCtrl+O");
  AppendMenu(file.get(), MF_STRING, IDM_FILE_SAVE, L"&Save\tCtrl+S");
  AppendMenu(file.get(), MF_STRING, IDM_FILE_SAVE_AS, L"Save &As...");
  AppendMenu(file.get(), MF_SEPARATOR, 0, nullptr);
  AppendMenu(file.get(), MF_STRING, IDM_FILE_PAGE_SETUP, L"Page Set&up...");
  AppendMenu(file.get(), MF_STRING, IDM_FILE_PRINT, L"&Print...\tCtrl+P");
  AppendMenu(file.get(), MF_SEPARATOR, 0, nullptr);
  AppendMenu(file.get(), MF_STRING, IDM_FILE_EXIT, L"E&xit");
  AppendMenu(menu.get(), MF_POPUP, reinterpret_cast<UINT_PTR>(file.release()), L"&File");
  wil::unique_hmenu edit{CreatePopupMenu()};
  THROW_LAST_ERROR_IF(!edit);
  AppendMenu(edit.get(), MF_STRING, IDM_EDIT_UNDO, L"&Undo\tCtrl+Z");
  AppendMenu(edit.get(), MF_SEPARATOR, 0, nullptr);
  AppendMenu(edit.get(), MF_STRING, IDM_EDIT_CUT, L"Cu&t\tCtrl+X");
  AppendMenu(edit.get(), MF_STRING, IDM_EDIT_COPY, L"&Copy\tCtrl+C");
  AppendMenu(edit.get(), MF_STRING, IDM_EDIT_PASTE, L"&Paste\tCtrl+V");
  AppendMenu(edit.get(), MF_STRING, IDM_EDIT_DELETE, L"De&lete\tDel");
  AppendMenu(edit.get(), MF_SEPARATOR, 0, nullptr);
  AppendMenu(edit.get(), MF_STRING, IDM_EDIT_SELECT_ALL, L"Select &All\tCtrl+A");
  AppendMenu(edit.get(), MF_STRING, IDM_EDIT_TIME_DATE, L"Time/&Date\tF5");
  AppendMenu(menu.get(), MF_POPUP, reinterpret_cast<UINT_PTR>(edit.release()), L"&Edit");
  wil::unique_hmenu search{CreatePopupMenu()};
  THROW_LAST_ERROR_IF(!search);
  AppendMenu(search.get(), MF_STRING, IDM_SEARCH_FIND, L"&Find...\tCtrl+F");
  AppendMenu(search.get(), MF_STRING, IDM_SEARCH_FIND_NEXT, L"Find &Next\tF3");
  AppendMenu(search.get(), MF_STRING, IDM_SEARCH_REPLACE, L"&Replace...\tCtrl+H");
  AppendMenu(search.get(), MF_STRING, IDM_SEARCH_GO_TO, L"&Go To...\tCtrl+G");
  AppendMenu(menu.get(), MF_POPUP, reinterpret_cast<UINT_PTR>(search.release()), L"&Search");
  wil::unique_hmenu format{CreatePopupMenu()};
  THROW_LAST_ERROR_IF(!format);
  AppendMenu(format.get(), MF_STRING | MF_CHECKED, IDM_FORMAT_WORD_WRAP, L"&Word Wrap");
  AppendMenu(format.get(), MF_STRING, IDM_FORMAT_FONT, L"&Font...");
  AppendMenu(menu.get(), MF_POPUP, reinterpret_cast<UINT_PTR>(format.release()), L"F&ormat");
  wil::unique_hmenu view{CreatePopupMenu()};
  THROW_LAST_ERROR_IF(!view);
  AppendMenu(view.get(), MF_STRING | MF_CHECKED, IDM_VIEW_STATUS_BAR, L"&Status Bar");
  AppendMenu(menu.get(), MF_POPUP, reinterpret_cast<UINT_PTR>(view.release()), L"&View");
  wil::unique_hmenu help{CreatePopupMenu()};
  THROW_LAST_ERROR_IF(!help);
  AppendMenu(help.get(), MF_STRING, IDM_HELP_ABOUT, L"&About Schwing Edit");
  AppendMenu(menu.get(), MF_POPUP, reinterpret_cast<UINT_PTR>(help.release()), L"&Help");
  return menu;
}

class MainWindow {
 public:
  static ATOM Initailize() {
    const HINSTANCE hinst = GetModuleHandle(nullptr);
    WNDCLASSEX wcex{
        .cbSize = sizeof(WNDCLASSEX),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WndProc,
        .hInstance = hinst,
        .hIcon = (HICON)LoadImage(hinst, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 32, 32,
                                  LR_DEFAULTCOLOR),
        .hCursor = LoadCursor(nullptr, IDC_ARROW),
        .hbrBackground = CreateSolidBrush(RGB(0, 0, 0)),
        .lpszClassName = TEXT("MainWindowClass"),
        .hIconSm = (HICON)LoadImage(hinst, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 16, 16,
                                    LR_DEFAULTCOLOR),
    };
    ATOM atom = RegisterClassEx(&wcex);
    THROW_LAST_ERROR_IF(atom == 0);
    return atom;
  }
  MainWindow(HINSTANCE hinst, int cmdShow) {
    auto menu = CreateMainMenu();
    wil::unique_hwnd hwnd{CreateWindowEx(
        0, TEXT("MainWindowClass"), TEXT("Schwing Edit"), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, menu.get(), hinst, this)};
    THROW_LAST_ERROR_IF(!hwnd.is_valid());
    menu.release();
    RECT rc;
    THROW_IF_WIN32_BOOL_FALSE(GetClientRect(hwnd.get(), &rc));
    double dpiRatio = GetDpiForWindow(hwnd.get()) / 96.0;
    DWORD edit_style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
                       ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL;
    editHwnd_ = wil::unique_hwnd{CreateWindowEx(
        WS_EX_CLIENTEDGE, L"EDIT", nullptr, edit_style, 0, 30 * dpiRatio, rc.right - rc.left,
        rc.bottom - rc.top - 52 * dpiRatio, hwnd.get(),
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT)), hinst, nullptr)};
    THROW_LAST_ERROR_IF(!editHwnd_.is_valid());
    auto default_font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    GetObjectW(default_font, sizeof(logFont_), &logFont_);
    SendMessageW(editHwnd_.get(), WM_SETFONT, reinterpret_cast<WPARAM>(default_font), TRUE);
    SendMessageW(editHwnd_.get(), EM_SETLIMITTEXT, 0, 0);
    statusHwnd_ = wil::unique_hwnd{CreateWindowEx(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE, 0,
                                                  rc.bottom - 22 * dpiRatio, rc.right - rc.left,
                                                  22 * dpiRatio, hwnd.get(), nullptr, hinst, nullptr)};
    THROW_LAST_ERROR_IF(!statusHwnd_.is_valid());
    SetFocus(editHwnd_.get());
    {  // enable mica
      DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
      DwmSetWindowAttribute(hwnd.get(), DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                            sizeof(DWM_SYSTEMBACKDROP_TYPE));
      MARGINS margins = {0, 0, (int)(30 * dpiRatio), 0};
      DwmExtendFrameIntoClientArea(hwnd.get(), &margins);
    }
    ShowWindow(hwnd.get(), cmdShow);
    hwnd_ = hwnd.release();
    UpdateTitle();
    UpdateStatus();
  }
  HWND hwnd() const { return hwnd_; }
  void OpenPath(std::wstring path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
      MessageBoxW(hwnd_, L"Could not open the file.", L"Schwing Edit", MB_OK | MB_ICONERROR);
      return;
    }
    std::string bytes{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    text_encoding encoding = text_encoding::utf8;
    std::wstring text;
    if (bytes.starts_with("\xEF\xBB\xBF")) {
      encoding = text_encoding::utf8_bom;
      bytes.erase(0, 3);
    } else if (bytes.starts_with("\xFF\xFE")) {
      encoding = text_encoding::utf16_le;
      bytes.erase(0, 2);
      if (bytes.size() % sizeof(wchar_t) != 0) {
        MessageBoxW(hwnd_, L"Could not decode the UTF-16 file.", L"Schwing Edit",
                    MB_OK | MB_ICONERROR);
        return;
      }
      text.resize(bytes.size() / sizeof(wchar_t));
      std::memcpy(text.data(), bytes.data(), bytes.size());
    } else if (bytes.starts_with("\xFE\xFF")) {
      encoding = text_encoding::utf16_be;
      bytes.erase(0, 2);
      if (bytes.size() % sizeof(wchar_t) != 0) {
        MessageBoxW(hwnd_, L"Could not decode the UTF-16 file.", L"Schwing Edit",
                    MB_OK | MB_ICONERROR);
        return;
      }
      text.resize(bytes.size() / sizeof(wchar_t));
      for (size_t i = 0; i < text.size(); ++i) {
        auto hi = static_cast<unsigned char>(bytes[i * 2]);
        auto lo = static_cast<unsigned char>(bytes[i * 2 + 1]);
        text[i] = static_cast<wchar_t>((hi << 8) | lo);
      }
    }
    if (text.empty() && !bytes.empty()) {
      int len =
          MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
      if (len <= 0) {
        MessageBoxW(hwnd_, L"Could not decode the file as UTF-8.", L"Schwing Edit",
                    MB_OK | MB_ICONERROR);
        return;
      }
      text.resize(static_cast<size_t>(len));
      MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(),
                          len);
    }
    encoding_ = encoding;
    path_ = std::move(path);
    SetEditText(text);
    SetFocus(editHwnd_.get());
  }

 private:
  std::wstring GetEditText() const {
    const int length = GetWindowTextLengthW(editHwnd_.get());
    std::wstring text(static_cast<size_t>(length + 1), L'\0');
    GetWindowTextW(editHwnd_.get(), text.data(), static_cast<int>(text.size()));
    text.resize(static_cast<size_t>(length));
    return text;
  }
  void SetEditText(const std::wstring& text) {
    SetWindowTextW(editHwnd_.get(), text.c_str());
    dirty_ = false;
    UpdateTitle();
  }
  void UpdateTitle() {
    std::wstring title = path_.empty() ? L"Untitled" : path_;
    if (dirty_) {
      title.insert(title.begin(), L'*');
    }
    title += L" - Schwing Edit";
    SetWindowTextW(hwnd_, title.c_str());
  }
  void UpdateStatus() {
    if (!statusHwnd_) {
      return;
    }
    DWORD start = 0;
    DWORD end = 0;
    SendMessageW(editHwnd_.get(), EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                 reinterpret_cast<LPARAM>(&end));
    auto text = GetEditText();
    size_t line = 1;
    size_t col = 1;
    for (size_t i = 0; i < std::min<size_t>(end, text.size()); ++i) {
      if (text[i] == L'\r') {
        if (i + 1 < text.size() && text[i + 1] == L'\n') {
          ++i;
        }
        ++line;
        col = 1;
      } else if (text[i] == L'\n') {
        ++line;
        col = 1;
      } else {
        ++col;
      }
    }
    SetWindowTextW(statusHwnd_.get(), std::format(L"  Ln {}, Col {}", line, col).c_str());
  }
  bool ConfirmDiscard() {
    if (!dirty_) {
      return true;
    }
    int result = MessageBoxW(hwnd_, L"Do you want to save changes?", L"Schwing Edit",
                             MB_YESNOCANCEL | MB_ICONWARNING);
    if (result == IDCANCEL) {
      return false;
    }
    if (result == IDYES) {
      return Save();
    }
    return true;
  }
  std::wstring PickOpenPath() {
    std::wstring path(MAX_PATH, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Text Documents (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path.data();
    ofn.nMaxFile = static_cast<DWORD>(path.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) {
      return {};
    }
    path.resize(wcslen(path.c_str()));
    return path;
  }
  std::wstring PickSavePath() {
    std::wstring path = path_.empty() ? std::wstring(MAX_PATH, L'\0') : path_;
    path.resize(MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Text Documents (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path.data();
    ofn.nMaxFile = static_cast<DWORD>(path.size());
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) {
      return {};
    }
    path.resize(wcslen(path.c_str()));
    return path;
  }
  void NewFile() {
    if (!ConfirmDiscard()) {
      return;
    }
    path_.clear();
    encoding_ = text_encoding::utf8;
    SetEditText(L"");
    SetFocus(editHwnd_.get());
  }
  void Open() {
    if (!ConfirmDiscard()) {
      return;
    }
    auto path = PickOpenPath();
    if (path.empty()) {
      return;
    }
    OpenPath(std::move(path));
  }
  bool Save() {
    if (path_.empty()) {
      path_ = PickSavePath();
      if (path_.empty()) {
        return false;
      }
    }
    auto text = GetEditText();
    std::string bytes;
    if (encoding_ == text_encoding::utf16_le || encoding_ == text_encoding::utf16_be) {
      bytes.reserve(2 + text.size() * sizeof(wchar_t));
      bytes.push_back(encoding_ == text_encoding::utf16_le ? static_cast<char>(0xFF)
                                                           : static_cast<char>(0xFE));
      bytes.push_back(encoding_ == text_encoding::utf16_le ? static_cast<char>(0xFE)
                                                           : static_cast<char>(0xFF));
      for (wchar_t ch : text) {
        if (encoding_ == text_encoding::utf16_le) {
          bytes.push_back(static_cast<char>(ch & 0xFF));
          bytes.push_back(static_cast<char>((ch >> 8) & 0xFF));
        } else {
          bytes.push_back(static_cast<char>((ch >> 8) & 0xFF));
          bytes.push_back(static_cast<char>(ch & 0xFF));
        }
      }
    } else {
      if (encoding_ == text_encoding::utf8_bom) {
        bytes.append("\xEF\xBB\xBF", 3);
      }
      int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                    nullptr, 0, nullptr, nullptr);
      THROW_LAST_ERROR_IF(len < 0);
      auto old_size = bytes.size();
      bytes.resize(old_size + static_cast<size_t>(len));
      if (len > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                            bytes.data() + old_size, len, nullptr, nullptr);
      }
    }
    std::ofstream file{path_, std::ios::binary | std::ios::trunc};
    if (!file) {
      MessageBoxW(hwnd_, L"Could not save the file.", L"Schwing Edit", MB_OK | MB_ICONERROR);
      return false;
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    dirty_ = false;
    UpdateTitle();
    return true;
  }
  void SaveAs() {
    auto old_path = path_;
    path_ = PickSavePath();
    if (path_.empty()) {
      path_ = old_path;
      return;
    }
    Save();
  }
  void PageSetup() {
    PAGESETUPDLGW psd{};
    psd.lStructSize = sizeof(psd);
    psd.hwndOwner = hwnd_;
    psd.Flags = PSD_MARGINS | PSD_INTHOUSANDTHSOFINCHES;
    PageSetupDlgW(&psd);
  }
  void Print() {
    PRINTDLGW pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = hwnd_;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS;
    if (PrintDlgW(&pd) && pd.hDC) {
      DeleteDC(pd.hDC);
    }
  }
  void ChooseEditorFont() {
    LOGFONTW lf = logFont_;
    CHOOSEFONTW cf{};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwnd_;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
    if (!ChooseFontW(&cf)) {
      return;
    }
    wil::unique_hfont font{CreateFontIndirectW(&lf)};
    if (!font) {
      return;
    }
    logFont_ = lf;
    editorFont_ = std::move(font);
    SendMessageW(editHwnd_.get(), WM_SETFONT, reinterpret_cast<WPARAM>(editorFont_.get()), TRUE);
  }
  void InsertTimeDate() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t date[128]{};
    wchar_t time[128]{};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date,
                    static_cast<int>(std::size(date)), nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, time,
                    static_cast<int>(std::size(time)));
    std::wstring value = std::format(L"{} {}", time, date);
    SendMessageW(editHwnd_.get(), WM_CHAR, value[0], 0);
    for (size_t i = 1; i < value.size(); ++i) {
      SendMessageW(editHwnd_.get(), WM_CHAR, value[i], 0);
    }
  }
  DWORD SelectionEnd() const {
    DWORD start = 0;
    DWORD end = 0;
    SendMessageW(editHwnd_.get(), EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                 reinterpret_cast<LPARAM>(&end));
    return end;
  }
  bool IsWordBoundary(const std::wstring& text, size_t pos) const {
    if (pos >= text.size()) {
      return true;
    }
    return !std::iswalnum(text[pos]) && text[pos] != L'_';
  }
  bool FindNext(bool down = true) {
    if (findText_[0] == L'\0') {
      ShowFindDialog();
      return false;
    }
    std::wstring haystack = GetEditText();
    std::wstring needle = findText_;
    std::wstring searchable = haystack;
    if ((findReplace_.Flags & FR_MATCHCASE) == 0) {
      std::ranges::transform(searchable, searchable.begin(),
                             [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
      std::ranges::transform(needle, needle.begin(),
                             [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    }
    size_t start = down ? SelectionEnd() : 0;
    size_t found = std::wstring::npos;
    while (true) {
      found = down ? searchable.find(needle, start) : searchable.rfind(needle, SelectionEnd());
      if (found == std::wstring::npos) {
        break;
      }
      if ((findReplace_.Flags & FR_WHOLEWORD) == 0 ||
          ((found == 0 || IsWordBoundary(searchable, found - 1)) &&
           IsWordBoundary(searchable, found + needle.size()))) {
        SendMessageW(editHwnd_.get(), EM_SETSEL, found, found + needle.size());
        SetFocus(editHwnd_.get());
        return true;
      }
      start = found + 1;
    }
    MessageBoxW(hwnd_, std::format(L"Cannot find \"{}\"", findText_).c_str(), L"Schwing Edit",
                MB_OK | MB_ICONINFORMATION);
    return false;
  }
  void ReplaceSelection() {
    SendMessageW(editHwnd_.get(), WM_CLEAR, 0, 0);
    for (const wchar_t* p = replaceText_; *p; ++p) {
      SendMessageW(editHwnd_.get(), WM_CHAR, *p, 0);
    }
  }
  void ReplaceCurrent() {
    DWORD start = 0;
    DWORD end = 0;
    SendMessageW(editHwnd_.get(), EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                 reinterpret_cast<LPARAM>(&end));
    auto text = GetEditText();
    std::wstring selected = start < end && end <= text.size() ? text.substr(start, end - start) : L"";
    std::wstring needle = findText_;
    if ((findReplace_.Flags & FR_MATCHCASE) == 0) {
      std::ranges::transform(selected, selected.begin(),
                             [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
      std::ranges::transform(needle, needle.begin(),
                             [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    }
    if (selected == needle) {
      ReplaceSelection();
    }
  }
  void ReplaceAll() {
    SendMessageW(editHwnd_.get(), EM_SETSEL, 0, 0);
    size_t replacements = 0;
    while (FindNext(true)) {
      ReplaceSelection();
      ++replacements;
    }
    if (replacements == 0) {
      MessageBoxW(hwnd_, std::format(L"Cannot find \"{}\"", findText_).c_str(), L"Schwing Edit",
                  MB_OK | MB_ICONINFORMATION);
    }
  }
  void ShowFindDialog() {
    findReplace_ = {};
    findReplace_.lStructSize = sizeof(findReplace_);
    findReplace_.hwndOwner = hwnd_;
    findReplace_.Flags = FR_DOWN;
    findReplace_.lpstrFindWhat = findText_;
    findReplace_.wFindWhatLen = static_cast<WORD>(std::size(findText_));
    findDialog_ = FindTextW(&findReplace_);
  }
  void ShowReplaceDialog() {
    findReplace_ = {};
    findReplace_.lStructSize = sizeof(findReplace_);
    findReplace_.hwndOwner = hwnd_;
    findReplace_.Flags = FR_DOWN;
    findReplace_.lpstrFindWhat = findText_;
    findReplace_.wFindWhatLen = static_cast<WORD>(std::size(findText_));
    findReplace_.lpstrReplaceWith = replaceText_;
    findReplace_.wReplaceWithLen = static_cast<WORD>(std::size(replaceText_));
    findDialog_ = ReplaceTextW(&findReplace_);
  }
  LRESULT OnFindReplace(LPARAM lparam) {
    auto fr = reinterpret_cast<LPFINDREPLACEW>(lparam);
    findReplace_.Flags = fr->Flags;
    if (fr->Flags & FR_DIALOGTERM) {
      findDialog_ = nullptr;
      return 0;
    }
    if (fr->Flags & FR_FINDNEXT) {
      FindNext((fr->Flags & FR_DOWN) != 0);
    } else if (fr->Flags & FR_REPLACE) {
      ReplaceCurrent();
      FindNext((fr->Flags & FR_DOWN) != 0);
    } else if (fr->Flags & FR_REPLACEALL) {
      ReplaceAll();
    }
    return 0;
  }
  LRESULT OnCommand(WPARAM wparam, LPARAM lparam) {
    if (HIWORD(wparam) == EN_CHANGE && reinterpret_cast<HWND>(lparam) == editHwnd_.get()) {
      dirty_ = true;
      UpdateTitle();
      UpdateStatus();
      return 0;
    }
    switch (LOWORD(wparam)) {
      case IDM_FILE_NEW:
        NewFile();
        return 0;
      case IDM_FILE_OPEN:
        Open();
        return 0;
      case IDM_FILE_SAVE:
        Save();
        return 0;
      case IDM_FILE_SAVE_AS:
        SaveAs();
        return 0;
      case IDM_FILE_PAGE_SETUP:
        PageSetup();
        return 0;
      case IDM_FILE_PRINT:
        Print();
        return 0;
      case IDM_FILE_EXIT:
        SendMessage(hwnd_, WM_CLOSE, 0, 0);
        return 0;
      case IDM_EDIT_UNDO:
        SendMessage(editHwnd_.get(), WM_UNDO, 0, 0);
        return 0;
      case IDM_EDIT_CUT:
        SendMessage(editHwnd_.get(), WM_CUT, 0, 0);
        return 0;
      case IDM_EDIT_COPY:
        SendMessage(editHwnd_.get(), WM_COPY, 0, 0);
        return 0;
      case IDM_EDIT_PASTE:
        SendMessage(editHwnd_.get(), WM_PASTE, 0, 0);
        return 0;
      case IDM_EDIT_DELETE:
        SendMessage(editHwnd_.get(), WM_CLEAR, 0, 0);
        return 0;
      case IDM_EDIT_SELECT_ALL:
        SendMessage(editHwnd_.get(), EM_SETSEL, 0, -1);
        return 0;
      case IDM_EDIT_TIME_DATE:
        InsertTimeDate();
        return 0;
      case IDM_SEARCH_FIND:
        ShowFindDialog();
        return 0;
      case IDM_SEARCH_FIND_NEXT:
        FindNext(true);
        return 0;
      case IDM_SEARCH_REPLACE:
        ShowReplaceDialog();
        return 0;
      case IDM_SEARCH_GO_TO:
        MessageBoxW(hwnd_, L"Go To is not available until line navigation is implemented.",
                    L"Schwing Edit", MB_OK | MB_ICONINFORMATION);
        return 0;
      case IDM_FORMAT_WORD_WRAP:
        wordWrap_ = !wordWrap_;
        CheckMenuItem(GetMenu(hwnd_), IDM_FORMAT_WORD_WRAP,
                      MF_BYCOMMAND | (wordWrap_ ? MF_CHECKED : MF_UNCHECKED));
        {
          auto style = GetWindowLongPtrW(editHwnd_.get(), GWL_STYLE);
          if (wordWrap_) {
            style &= ~(WS_HSCROLL | ES_AUTOHSCROLL);
          } else {
            style |= WS_HSCROLL | ES_AUTOHSCROLL;
          }
          SetWindowLongPtrW(editHwnd_.get(), GWL_STYLE, style);
          SetWindowPos(editHwnd_.get(), nullptr, 0, 0, 0, 0,
                       SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
          InvalidateRect(editHwnd_.get(), nullptr, TRUE);
        }
        return 0;
      case IDM_FORMAT_FONT:
        ChooseEditorFont();
        return 0;
      case IDM_VIEW_STATUS_BAR:
        statusVisible_ = !statusVisible_;
        CheckMenuItem(GetMenu(hwnd_), IDM_VIEW_STATUS_BAR,
                      MF_BYCOMMAND | (statusVisible_ ? MF_CHECKED : MF_UNCHECKED));
        ShowWindow(statusHwnd_.get(), statusVisible_ ? SW_SHOW : SW_HIDE);
        OnSize();
        return 0;
      case IDM_HELP_ABOUT:
        MessageBoxW(hwnd_, L"Schwing Edit\nA native Windows text editor.", L"About Schwing Edit",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    return 0;
  }
  LRESULT OnSize() {
    RECT rc;
    if (!GetClientRect(hwnd_, &rc)) {
      return 0;  // ignore transient error
    }
    double dpiRatio = GetDpiForWindow(hwnd_) / 96.0;
    int status_height = statusVisible_ ? static_cast<int>(22 * dpiRatio) : 0;
    SetWindowPos(editHwnd_.get(), nullptr, 0, 30 * dpiRatio, rc.right - rc.left,
                 rc.bottom - rc.top - 30 * dpiRatio - status_height, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(statusHwnd_.get(), nullptr, 0, rc.bottom - status_height, rc.right - rc.left,
                 status_height, SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
  }
  LRESULT OnSetFocus() {
    SetFocus(editHwnd_.get());
    return 0;
  }
  LRESULT OnDestroy() {
    PostQuitMessage(0);
    return 0;
  }
  LRESULT OnClose() {
    if (ConfirmDiscard()) {
      DestroyWindow(hwnd_);
    }
    return 0;
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == find_replace_msg) {
      return GetThis(hwnd)->OnFindReplace(lparam);
    }
    switch (msg) {
      case WM_SIZE:
        return GetThis(hwnd)->OnSize();
      case WM_SETFOCUS:
        return GetThis(hwnd)->OnSetFocus();
      case WM_COMMAND:
        return GetThis(hwnd)->OnCommand(wparam, lparam);
      case WM_CREATE: {
        auto info = reinterpret_cast<LPCREATESTRUCT>(lparam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(info->lpCreateParams));
        return 0;
      }
      case WM_DESTROY:
        return GetThis(hwnd)->OnDestroy();
      case WM_CLOSE:
        return GetThis(hwnd)->OnClose();
      case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<LPMINMAXINFO>(lparam);
        double dpiRatio = GetDpiForWindow(hwnd) / 96.0;
        int minWidth = (int)(480 * dpiRatio);
        int minHeight = (int)(200 * dpiRatio);
        info->ptMinTrackSize.x = minWidth;
        info->ptMinTrackSize.y = minHeight;
        return 0;
      }
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
  }
  static MainWindow* GetThis(HWND hwnd) {
    return reinterpret_cast<MainWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }

 private:
  HWND hwnd_ = nullptr;
  wil::unique_hwnd editHwnd_;
  wil::unique_hwnd statusHwnd_;
  wil::unique_hfont editorFont_;
  LOGFONTW logFont_{};
  std::wstring path_;
  text_encoding encoding_ = text_encoding::utf8;
  FINDREPLACEW findReplace_{};
  wchar_t findText_[256]{};
  wchar_t replaceText_[256]{};
  HWND findDialog_ = nullptr;
  bool wordWrap_ = true;
  bool statusVisible_ = true;
  bool dirty_ = false;
};

const ATOM MainWndInit = MainWindow::Initailize();

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
  swg::initialize();
  THROW_IF_WIN32_BOOL_FALSE(
      SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
  MainWindow mainWnd{hInstance, nCmdShow};
  int argc = 0;
  wil::unique_hlocal argv{CommandLineToArgvW(GetCommandLineW(), &argc)};
  if (argv && argc > 1) {
    auto args = static_cast<LPWSTR*>(argv.get());
    mainWnd.OpenPath(args[1]);
  }
  ACCEL accels[] = {{FVIRTKEY | FCONTROL, 'N', IDM_FILE_NEW},
                    {FVIRTKEY | FCONTROL, 'O', IDM_FILE_OPEN},
                    {FVIRTKEY | FCONTROL, 'S', IDM_FILE_SAVE},
                    {FVIRTKEY | FCONTROL, 'P', IDM_FILE_PRINT},
                    {FVIRTKEY | FCONTROL, 'A', IDM_EDIT_SELECT_ALL},
                    {FVIRTKEY | FCONTROL, 'X', IDM_EDIT_CUT},
                    {FVIRTKEY | FCONTROL, 'C', IDM_EDIT_COPY},
                    {FVIRTKEY | FCONTROL, 'V', IDM_EDIT_PASTE},
                    {FVIRTKEY | FCONTROL, 'Z', IDM_EDIT_UNDO},
                    {FVIRTKEY, VK_F5, IDM_EDIT_TIME_DATE},
                    {FVIRTKEY | FCONTROL, 'F', IDM_SEARCH_FIND},
                    {FVIRTKEY, VK_F3, IDM_SEARCH_FIND_NEXT},
                    {FVIRTKEY | FCONTROL, 'H', IDM_SEARCH_REPLACE},
                    {FVIRTKEY | FCONTROL, 'G', IDM_SEARCH_GO_TO}};
  HACCEL accel = CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));
  THROW_LAST_ERROR_IF(!accel);
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (!TranslateAcceleratorW(mainWnd.hwnd(), accel, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  DestroyAcceleratorTable(accel);
  swg::uninitialize();
  return 0;
}
