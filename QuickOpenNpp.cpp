// QuickOpenNpp v0.5.0 — native Win32 open-document fuzzy switcher for Notepad++.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <unordered_set>
#include <climits>

struct NppData { HWND _nppHandle; HWND _scintillaMainHandle; HWND _scintillaSecondHandle; };
using PFUNCPLUGINCMD = void (__cdecl *)();
struct ShortcutKey { bool _isCtrl; bool _isAlt; bool _isShift; UCHAR _key; };
struct FuncItem { wchar_t _itemName[64]; PFUNCPLUGINCMD _pFunc; int _cmdID; bool _init2Check; ShortcutKey* _pShKey; };

static constexpr UINT NPPMSG = WM_USER + 1000;
static constexpr UINT NPPM_GETNBOPENFILES = NPPMSG + 7;
static constexpr UINT NPPM_ACTIVATEDOC = NPPMSG + 28;
static constexpr UINT NPPM_GETPLUGINSCONFIGDIR = NPPMSG + 46;
static constexpr UINT NPPM_GETFULLPATHFROMBUFFERID = NPPMSG + 58;
static constexpr UINT NPPM_GETBUFFERIDFROMPOS = NPPMSG + 59;
static constexpr UINT NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR = NPPMSG + 90;
static constexpr UINT NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR = NPPMSG + 91;
static constexpr UINT NPPM_ISDARKMODEENABLED = NPPMSG + 107;
static constexpr UINT NPPM_GETDARKMODECOLORS = NPPMSG + 108;
static constexpr int PRIMARY_VIEW = 1; // GETNBOPENFILES only
static constexpr int SECOND_VIEW = 2;  // GETNBOPENFILES only
static constexpr int EDIT_ID = 101;
static constexpr int LIST_ID = 102;
static constexpr int DEFAULT_WIDTH = 720;
static constexpr int DEFAULT_HEIGHT = 420;
static constexpr int MIN_WIDTH = 420;
static constexpr int MIN_HEIGHT = 220;
static constexpr int MARGIN = 12;
static constexpr int SEARCH_HEIGHT = 28;
static constexpr int LIST_GAP = 10;
static const wchar_t CLASS_NAME[] = L"QuickOpenNppPopupClassV4";

struct Document { UINT_PTR bufferId; std::wstring name; std::wstring folder; };
static NppData g_npp{};
static HINSTANCE g_instance = nullptr;
static HWND g_popup = nullptr, g_edit = nullptr, g_list = nullptr;
static WNDPROC g_oldEditProc = nullptr;
static HFONT g_font = nullptr;
static std::vector<Document> g_documents;
static std::vector<int> g_results;
// -1 = default fuzzy relevance; 0 = File name; 1 = Folder.
static int g_sortColumn = -1;
static bool g_sortAscending = true;
static std::wstring g_iniPath;
static int g_width = DEFAULT_WIDTH, g_height = DEFAULT_HEIGHT;
static int g_x = CW_USEDEFAULT, g_y = CW_USEDEFAULT;
static int g_nameWidth = 230, g_folderWidth = 440;
static bool g_closing = false;
static const wchar_t SETTINGS_CLASS[] = L"QuickOpenNppSettingsClassV3";
static HWND g_settings = nullptr;
enum ThemeMode { FOLLOW_EDITOR = 0, FOLLOW_UI = 1, CUSTOM = 2 };
struct ThemeColors { COLORREF bg, fg, selection, selectionText; };
struct NppDarkColors {
    COLORREF background, softerBackground, hotBackground, pureBackground, errorBackground;
    COLORREF text, darkerText, disabledText, linkText, edge, hotEdge, disabledEdge;
};
static int g_themeMode = FOLLOW_EDITOR;
static ThemeColors g_custom{RGB(18,30,38), RGB(229,237,243), RGB(39,87,118), RGB(255,255,255)};
static ThemeColors g_theme = g_custom;
static HBRUSH g_backgroundBrush = nullptr;
static HWND g_header = nullptr;
static ThemeColors g_draft = g_custom;
static int g_draftMode = FOLLOW_EDITOR;
static HFONT g_settingsFont = nullptr;
static COLORREF g_chooseColors[16]{};
static constexpr int MODE_EDITOR = 201, MODE_UI = 202, MODE_CUSTOM = 203;
static constexpr int COLOR_BG = 211, COLOR_FG = 212, COLOR_SEL = 213, COLOR_SEL_TEXT = 214;
static constexpr int BTN_SAVE = 220, BTN_CANCEL = 221;

static COLORREF blend(COLORREF a, COLORREF b, int bPercent) {
    const int aPercent = 100 - bPercent;
    return RGB((GetRValue(a)*aPercent + GetRValue(b)*bPercent)/100,
               (GetGValue(a)*aPercent + GetGValue(b)*bPercent)/100,
               (GetBValue(a)*aPercent + GetBValue(b)*bPercent)/100);
}
static bool darkColor(COLORREF c) {
    return (GetRValue(c)*299 + GetGValue(c)*587 + GetBValue(c)*114) < 128000;
}
static void refreshTheme() {
    ThemeColors colors = g_custom;
    if (g_themeMode == FOLLOW_EDITOR && g_npp._nppHandle) {
        colors.bg = static_cast<COLORREF>(SendMessageW(g_npp._nppHandle, NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR, 0, 0));
        colors.fg = static_cast<COLORREF>(SendMessageW(g_npp._nppHandle, NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR, 0, 0));
        colors.selection = blend(colors.bg, darkColor(colors.bg) ? RGB(86,148,192) : RGB(61,126,190), darkColor(colors.bg) ? 45 : 25);
        colors.selectionText = colors.fg;
    } else if (g_themeMode == FOLLOW_UI && g_npp._nppHandle) {
        bool dark = SendMessageW(g_npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0;
        if (dark) {
            NppDarkColors npp{};
            if (SendMessageW(g_npp._nppHandle, NPPM_GETDARKMODECOLORS, sizeof(npp), reinterpret_cast<LPARAM>(&npp))) {
                colors = {npp.pureBackground, npp.text, npp.hotBackground, npp.text};
            } else colors = {RGB(32,32,32), RGB(235,235,235), RGB(65,87,105), RGB(255,255,255)};
        } else {
            colors = {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT)};
        }
    }
    g_theme = colors;
    if (g_backgroundBrush) DeleteObject(g_backgroundBrush);
    g_backgroundBrush = CreateSolidBrush(g_theme.bg);
}


static std::wstring lowercase(std::wstring value) {
    for (auto& ch : value) ch = static_cast<wchar_t>(towlower(ch));
    return value;
}
static int fuzzyScore(const std::wstring& filename, const std::wstring& pattern) {
    if (pattern.empty()) return 0;
    const auto name = lowercase(filename), query = lowercase(pattern);
    size_t from = 0;
    int score = 0, previous = -2;
    for (wchar_t ch : query) {
        size_t pos = name.find(ch, from);
        if (pos == std::wstring::npos) return -1;
        score += 10;
        if (static_cast<int>(pos) == previous + 1) score += 8;
        if (pos == 0 || name[pos - 1] == L'_' || name[pos - 1] == L'-' || name[pos - 1] == L'.' ||
            (iswlower(filename[pos - 1]) && iswupper(filename[pos]))) score += 5;
        previous = static_cast<int>(pos);
        from = pos + 1;
    }
    if (name.find(query) != std::wstring::npos) score += 30;
    score -= static_cast<int>(name.size() / 4);
    return std::max(0, score);
}

static std::wstring configurationFile() {
    if (!g_npp._nppHandle) return {};
    LRESULT count = SendMessageW(g_npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, 0, 0);
    if (count <= 0 || count > 32767) return {};
    std::wstring dir(static_cast<size_t>(count) + 1, L'\0');
    if (!SendMessageW(g_npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR,
                      static_cast<WPARAM>(dir.size()), reinterpret_cast<LPARAM>(dir.data()))) return {};
    dir.resize(wcslen(dir.c_str()));
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    return dir + L"QuickOpenNpp.ini";
}
static int readInt(const wchar_t* name, int fallback) {
    if (g_iniPath.empty()) return fallback;
    return static_cast<int>(GetPrivateProfileIntW(L"Window", name, fallback, g_iniPath.c_str()));
}
static void writeInt(const wchar_t* name, int value) {
    if (g_iniPath.empty()) return;
    const std::wstring str = std::to_wstring(value);
    WritePrivateProfileStringW(L"Window", name, str.c_str(), g_iniPath.c_str());
}
static void loadAppearance() {
    if (g_iniPath.empty()) return;
    g_themeMode = static_cast<int>(GetPrivateProfileIntW(L"Appearance", L"Mode", FOLLOW_EDITOR, g_iniPath.c_str()));
    if (g_themeMode < FOLLOW_EDITOR || g_themeMode > CUSTOM) g_themeMode = FOLLOW_EDITOR;
    g_custom.bg = static_cast<COLORREF>(GetPrivateProfileIntW(L"Appearance", L"Background", static_cast<int>(RGB(18,30,38)), g_iniPath.c_str()));
    g_custom.fg = static_cast<COLORREF>(GetPrivateProfileIntW(L"Appearance", L"Text", static_cast<int>(RGB(229,237,243)), g_iniPath.c_str()));
    g_custom.selection = static_cast<COLORREF>(GetPrivateProfileIntW(L"Appearance", L"Selection", static_cast<int>(RGB(39,87,118)), g_iniPath.c_str()));
    g_custom.selectionText = static_cast<COLORREF>(GetPrivateProfileIntW(L"Appearance", L"SelectionText", static_cast<int>(RGB(255,255,255)), g_iniPath.c_str()));
    refreshTheme();
}
static void saveAppearance() {
    if (g_iniPath.empty()) return;
    auto save = [](const wchar_t* key, int value) {
        std::wstring str = std::to_wstring(value);
        WritePrivateProfileStringW(L"Appearance", key, str.c_str(), g_iniPath.c_str());
    };
    save(L"Mode", g_themeMode);
    save(L"Background", static_cast<int>(g_custom.bg));
    save(L"Text", static_cast<int>(g_custom.fg));
    save(L"Selection", static_cast<int>(g_custom.selection));
    save(L"SelectionText", static_cast<int>(g_custom.selectionText));
}
static void loadSettings() {
    g_iniPath = configurationFile();
    loadAppearance();
    g_width = std::max(MIN_WIDTH, readInt(L"Width", DEFAULT_WIDTH));
    g_height = std::max(MIN_HEIGHT, readInt(L"Height", DEFAULT_HEIGHT));
    g_nameWidth = std::max(70, readInt(L"NameWidth", 230));
    g_folderWidth = std::max(70, readInt(L"FolderWidth", 440));
    g_sortColumn = readInt(L"SortColumn", -1);
    if (g_sortColumn < -1 || g_sortColumn > 1) g_sortColumn = -1;
    g_sortAscending = readInt(L"SortAscending", 1) != 0;
    const int missing = INT_MIN;
    g_x = readInt(L"X", missing);
    g_y = readInt(L"Y", missing);
    if (g_x == missing || g_y == missing) g_x = g_y = CW_USEDEFAULT;
}
static void saveSettings(HWND hwnd) {
    if (!hwnd || g_iniPath.empty()) return;
    RECT rect{};
    if (GetWindowRect(hwnd, &rect)) {
        writeInt(L"X", rect.left);
        writeInt(L"Y", rect.top);
        writeInt(L"Width", rect.right - rect.left);
        writeInt(L"Height", rect.bottom - rect.top);
    }
    if (g_list && IsWindow(g_list)) {
        writeInt(L"NameWidth", ListView_GetColumnWidth(g_list, 0));
        writeInt(L"FolderWidth", ListView_GetColumnWidth(g_list, 1));
        writeInt(L"SortColumn", g_sortColumn);
        writeInt(L"SortAscending", g_sortAscending ? 1 : 0);
    }
}

static void loadDocuments() {
    g_documents.clear();
    std::unordered_set<UINT_PTR> seen;
    for (int view = 0; view < 2; ++view) {
        int count = static_cast<int>(SendMessageW(g_npp._nppHandle, NPPM_GETNBOPENFILES, 0,
                                                  view == 0 ? PRIMARY_VIEW : SECOND_VIEW));
        for (int index = 0; index < count; ++index) {
            UINT_PTR buffer = static_cast<UINT_PTR>(SendMessageW(g_npp._nppHandle,
                                               NPPM_GETBUFFERIDFROMPOS, index, view));
            if (!buffer || !seen.insert(buffer).second) continue;
            LRESULT length = SendMessageW(g_npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID,
                                          static_cast<WPARAM>(buffer), 0);
            if (length < 0 || length > 32767) continue;
            std::wstring path(static_cast<size_t>(length) + 1, L'\0');
            SendMessageW(g_npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID,
                         static_cast<WPARAM>(buffer), reinterpret_cast<LPARAM>(path.data()));
            path.resize(wcslen(path.c_str()));
            size_t slash = path.find_last_of(L"\\/");
            std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
            std::wstring folder = slash == std::wstring::npos ? L"" : path.substr(0, slash);
            if (name.empty()) name = L"(unnamed document)";
            g_documents.push_back({buffer, std::move(name), std::move(folder)});
        }
    }
}
static void updateMatches() {
    if (!g_edit || !g_list) return;
    int len = GetWindowTextLengthW(g_edit);
    std::wstring query(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(g_edit, query.data(), len + 1);
    query.resize(static_cast<size_t>(len));
    std::vector<std::pair<int,int>> scores;
    for (int i = 0; i < static_cast<int>(g_documents.size()); ++i) {
        int score = fuzzyScore(g_documents[i].name, query);
        if (score >= 0) scores.emplace_back(i, score);
    }
    // A column click overrides fuzzy relevance ORDER, but never filtering.
    // CompareStringOrdinal gives Unicode case-insensitive ordering without a locale-dependent
    // lexicographical comparison. Preserve the initial tab order for equal values.
    std::stable_sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        if (g_sortColumn < 0) return a.second > b.second;
        const Document& left = g_documents[a.first];
        const Document& right = g_documents[b.first];
        const std::wstring& lhs = g_sortColumn == 0 ? left.name : left.folder;
        const std::wstring& rhs = g_sortColumn == 0 ? right.name : right.folder;
        int cmp = CompareStringOrdinal(lhs.c_str(), -1, rhs.c_str(), -1, TRUE);
        if (cmp != CSTR_EQUAL) {
            return g_sortAscending ? cmp == CSTR_LESS_THAN : cmp == CSTR_GREATER_THAN;
        }
        // Tie-break by the other column, always ascending for a consistent order.
        const std::wstring& otherL = g_sortColumn == 0 ? left.folder : left.name;
        const std::wstring& otherR = g_sortColumn == 0 ? right.folder : right.name;
        int other = CompareStringOrdinal(otherL.c_str(), -1, otherR.c_str(), -1, TRUE);
        return other == CSTR_LESS_THAN;
    });
    g_results.clear();
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_list);
    for (const auto& result : scores) {
        const int docIndex = result.first;
        const auto& doc = g_documents[docIndex];
        const int row = static_cast<int>(g_results.size());
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.pszText = const_cast<wchar_t*>(doc.name.c_str());
        item.lParam = docIndex;
        if (ListView_InsertItem(g_list, &item) >= 0) {
            ListView_SetItemText(g_list, row, 1, const_cast<wchar_t*>(doc.folder.c_str()));
            g_results.push_back(docIndex);
        }
    }
    if (!g_results.empty()) {
        ListView_SetItemState(g_list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_list, 0, FALSE);
    }
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, nullptr, TRUE);
}
static void closePopup(bool restoreFocus) {
    if (!g_popup || g_closing) return;
    g_closing = true;
    HWND popup = g_popup;
    DestroyWindow(popup);
    if (restoreFocus && IsWindow(g_npp._nppHandle)) {
        SetForegroundWindow(g_npp._nppHandle);
        SetFocus(g_npp._nppHandle);
    }
    g_closing = false;
}
static int selectedRow() {
    return g_list ? ListView_GetNextItem(g_list, -1, LVNI_SELECTED) : -1;
}
static void activateSelected() {
    int selected = selectedRow();
    if (selected < 0 || selected >= static_cast<int>(g_results.size())) return;
    const UINT_PTR buffer = g_documents[g_results[selected]].bufferId;
    closePopup(false);
    for (int view = 0; view < 2; ++view) {
        const int count = static_cast<int>(SendMessageW(g_npp._nppHandle, NPPM_GETNBOPENFILES, 0,
                                                        view == 0 ? PRIMARY_VIEW : SECOND_VIEW));
        for (int index = 0; index < count; ++index) {
            const UINT_PTR current = static_cast<UINT_PTR>(SendMessageW(g_npp._nppHandle,
                                                      NPPM_GETBUFFERIDFROMPOS, index, view));
            if (current == buffer) {
                SendMessageW(g_npp._nppHandle, NPPM_ACTIVATEDOC, view, index);
                SetForegroundWindow(g_npp._nppHandle);
                SetFocus(view == 0 ? g_npp._scintillaMainHandle : g_npp._scintillaSecondHandle);
                return;
            }
        }
    }
    SetForegroundWindow(g_npp._nppHandle);
}
static void changeSelection(int delta) {
    int count = static_cast<int>(g_results.size());
    if (!count) return;
    int old = selectedRow();
    int next = std::clamp((old < 0 ? 0 : old) + delta, 0, count - 1);
    if (old >= 0) ListView_SetItemState(g_list, old, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(g_list, next, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(g_list, next, FALSE);
}
static LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_ESCAPE) { closePopup(true); return 0; }
        if (wp == VK_RETURN) { activateSelected(); return 0; }
        if (wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT) {
            const int step = (wp == VK_PRIOR || wp == VK_NEXT) ? 8 : 1;
            changeSelection((wp == VK_UP || wp == VK_PRIOR) ? -step : step);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
    return CallWindowProcW(g_oldEditProc, hwnd, msg, wp, lp);
}
// Theme-aware header painting, while preserving native column resizing.
static COLORREF headerBackground() {
    return blend(g_theme.bg, g_theme.fg, darkColor(g_theme.bg) ? 9 : 5);
}
static COLORREF frameColor() {
    return blend(g_theme.bg, g_theme.fg, darkColor(g_theme.bg) ? 29 : 23);
}
static LRESULT CALLBACK headerSubclass(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                                        UINT_PTR subclassId, DWORD_PTR) {
    if (message == WM_ERASEBKGND) {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        HBRUSH brush = CreateSolidBrush(headerBackground());
        FillRect(reinterpret_cast<HDC>(wp), &rect, brush);
        DeleteObject(brush);
        return 1;
    }
    if (message == WM_PAINT) {
        LRESULT result = DefSubclassProc(hwnd, message, wp, lp);
        // Header's unoccupied area (after the final column) is otherwise system white.
        RECT client{};
        GetClientRect(hwnd, &client);
        int count = Header_GetItemCount(hwnd);
        RECT last{};
        int right = 0;
        if (count > 0 && Header_GetItemRect(hwnd, count - 1, &last)) right = last.right;
        if (right < client.right) {
            HDC dc = GetDC(hwnd);
            if (dc) {
                RECT rest{right, client.top, client.right, client.bottom};
                HBRUSH brush = CreateSolidBrush(headerBackground());
                FillRect(dc, &rest, brush);
                DeleteObject(brush);
                ReleaseDC(hwnd, dc);
            }
        }
        return result;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, headerSubclass, subclassId);
        if (g_header == hwnd) g_header = nullptr;
    }
    return DefSubclassProc(hwnd, message, wp, lp);
}
static LRESULT drawHeader(NMCUSTOMDRAW* draw) {
    if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    const int column = static_cast<int>(draw->dwItemSpec);
    const COLORREF bg = headerBackground();
    const COLORREF line = frameColor();
    HBRUSH backgroundBrush = CreateSolidBrush(bg);
    FillRect(draw->hdc, &draw->rc, backgroundBrush);
    DeleteObject(backgroundBrush);
    HPEN pen = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ oldPen = SelectObject(draw->hdc, pen);
    MoveToEx(draw->hdc, draw->rc.right - 1, draw->rc.top, nullptr);
    LineTo(draw->hdc, draw->rc.right - 1, draw->rc.bottom);
    MoveToEx(draw->hdc, draw->rc.left, draw->rc.bottom - 1, nullptr);
    LineTo(draw->hdc, draw->rc.right, draw->rc.bottom - 1);
    SelectObject(draw->hdc, oldPen);
    DeleteObject(pen);
    wchar_t name[128]{};
    HDITEMW item{};
    item.mask = HDI_TEXT;
    item.pszText = name;
    item.cchTextMax = static_cast<int>(_countof(name));
    if (SendMessageW(g_header, HDM_GETITEMW, static_cast<WPARAM>(column), reinterpret_cast<LPARAM>(&item))) {
        const int saved = SaveDC(draw->hdc);
        SetTextColor(draw->hdc, g_theme.fg);
        SetBkMode(draw->hdc, TRANSPARENT);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(g_header, WM_GETFONT, 0, 0));
        if (font) SelectObject(draw->hdc, font);
        RECT text = draw->rc;
        text.left += 7;
        text.right -= (column == g_sortColumn ? 24 : 7);
        DrawTextW(draw->hdc, name, -1, &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (column == g_sortColumn) {
            // Draw an up/down triangle using the palette, including in custom-color mode.
            // A separate arrow is necessary because header item painting is custom drawn.
            const int centerX = draw->rc.right - 13;
            const int centerY = (draw->rc.top + draw->rc.bottom) / 2;
            POINT triangle[3]{};
            if (g_sortAscending) {
                triangle[0] = {centerX, centerY - 4};
                triangle[1] = {centerX - 5, centerY + 3};
                triangle[2] = {centerX + 5, centerY + 3};
            } else {
                triangle[0] = {centerX - 5, centerY - 3};
                triangle[1] = {centerX + 5, centerY - 3};
                triangle[2] = {centerX, centerY + 4};
            }
            HBRUSH arrowBrush = CreateSolidBrush(g_theme.fg);
            HPEN arrowPen = CreatePen(PS_SOLID, 1, g_theme.fg);
            HGDIOBJ oldBrush = SelectObject(draw->hdc, arrowBrush);
            HGDIOBJ oldArrowPen = SelectObject(draw->hdc, arrowPen);
            Polygon(draw->hdc, triangle, 3);
            SelectObject(draw->hdc, oldArrowPen);
            SelectObject(draw->hdc, oldBrush);
            DeleteObject(arrowPen);
            DeleteObject(arrowBrush);
        }
        RestoreDC(draw->hdc, saved);
    }
    return CDRF_SKIPDEFAULT;
}
static LRESULT drawListRow(NMLVCUSTOMDRAW* draw) {
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    const int row = static_cast<int>(draw->nmcd.dwItemSpec);
    if (row < 0 || row >= static_cast<int>(g_results.size())) return CDRF_DODEFAULT;
    const bool selected = (ListView_GetItemState(g_list, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    HDC dc = draw->nmcd.hdc;
    RECT itemRect{};
    if (!ListView_GetItemRect(g_list, row, &itemRect, LVIR_BOUNDS)) return CDRF_DODEFAULT;
    RECT client{};
    GetClientRect(g_list, &client);
    RECT wholeRow{client.left, itemRect.top, client.right, itemRect.bottom};
    HBRUSH brush = CreateSolidBrush(selected ? g_theme.selection : g_theme.bg);
    FillRect(dc, &wholeRow, brush);
    DeleteObject(brush);
    const Document& doc = g_documents[g_results[row]];
    const std::wstring* fields[] = { &doc.name, &doc.folder };
    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, selected ? g_theme.selectionText : g_theme.fg);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(g_list, WM_GETFONT, 0, 0));
    if (font) SelectObject(dc, font);
    for (int col = 0; col < 2; ++col) {
        RECT cell{};
        if (!ListView_GetSubItemRect(g_list, row, col, LVIR_BOUNDS, &cell)) continue;
        // Subitem 0's BOUNDS may include other columns. Obtain its right edge from header.
        if (col == 0) {
            RECT headerCell{};
            if (g_header && Header_GetItemRect(g_header, 0, &headerCell)) {
                MapWindowPoints(g_header, g_list, reinterpret_cast<POINT*>(&headerCell), 2);
                cell.left = headerCell.left;
                cell.right = headerCell.right;
            }
        }
        const int cellDc = SaveDC(dc);
        IntersectClipRect(dc, cell.left, cell.top, cell.right, cell.bottom);
        cell.left += 6;
        cell.right -= 6;
        DrawTextW(dc, fields[col]->c_str(), -1, &cell,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RestoreDC(dc, cellDc);
    }
    RestoreDC(dc, saved);
    return CDRF_SKIPDEFAULT;
}

static void layoutChildren(HWND hwnd) {
    RECT rc{}; GetClientRect(hwnd, &rc);
    int width = std::max<int>(1, rc.right - 2 * MARGIN);
    int listY = MARGIN + SEARCH_HEIGHT + LIST_GAP;
    int listHeight = std::max<int>(1, rc.bottom - listY - MARGIN);
    if (g_edit) MoveWindow(g_edit, MARGIN, MARGIN, width, SEARCH_HEIGHT, TRUE);
    if (g_list) MoveWindow(g_list, MARGIN, listY, width, listHeight, TRUE);
}
static LRESULT CALLBACK popupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_popup = hwnd;
        g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE |
               WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 28, hwnd,
               reinterpret_cast<HMENU>(static_cast<INT_PTR>(EDIT_ID)), g_instance, nullptr);
        g_list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE |
               WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
               0, 0, 100, 100, hwnd,
               reinterpret_cast<HMENU>(static_cast<INT_PTR>(LIST_ID)), g_instance, nullptr);
        if (!g_edit || !g_list) return -1;
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(g_list, g_theme.bg);
        ListView_SetTextBkColor(g_list, g_theme.bg);
        ListView_SetTextColor(g_list, g_theme.fg);
        SetWindowTheme(g_list, darkColor(g_theme.bg) ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        g_header = ListView_GetHeader(g_list);
        if (g_header) {
            SetWindowTheme(g_header, darkColor(g_theme.bg) ? L"DarkMode_ItemsView" : L"Explorer", nullptr);
            SetWindowSubclass(g_header, headerSubclass, 1, 0);
        }
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        column.fmt = LVCFMT_LEFT;
        column.pszText = const_cast<wchar_t*>(L"File name");
        column.cx = g_nameWidth;
        ListView_InsertColumn(g_list, 0, &column);
        column.pszText = const_cast<wchar_t*>(L"Folder");
        column.cx = g_folderWidth;
        ListView_InsertColumn(g_list, 1, &column);
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
            g_font = CreateFontIndirectW(&metrics.lfMessageFont);
        if (g_font) {
            SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            SendMessageW(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        }
        g_oldEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_edit, GWLP_WNDPROC,
                                                     reinterpret_cast<LONG_PTR>(editProc)));
        layoutChildren(hwnd);
        loadDocuments();
        updateMatches();
        return 0;
    }
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) { layoutChildren(hwnd); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        if (dc) {
            HBRUSH border = CreateSolidBrush(frameColor());
            if (g_edit && IsWindow(g_edit)) {
                RECT rc{}; GetWindowRect(g_edit, &rc);
                MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<POINT*>(&rc), 2);
                InflateRect(&rc, 1, 1);
                FrameRect(dc, &rc, border);
            }
            if (g_list && IsWindow(g_list)) {
                RECT rc{}; GetWindowRect(g_list, &rc);
                MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<POINT*>(&rc), 2);
                InflateRect(&rc, 1, 1);
                FrameRect(dc, &rc, border);
            }
            DeleteObject(border);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize.x = MIN_WIDTH;
        info->ptMinTrackSize.y = MIN_HEIGHT;
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == EDIT_ID && HIWORD(wp) == EN_CHANGE) updateMatches();
        return 0;
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lp) == g_edit) {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, g_theme.fg);
            SetBkColor(dc, g_theme.bg);
            return reinterpret_cast<LRESULT>(g_backgroundBrush);
        }
        break;
    case WM_ERASEBKGND: {
        RECT r{}; GetClientRect(hwnd, &r);
        FillRect(reinterpret_cast<HDC>(wp), &r, g_backgroundBrush);
        return 1;
    }
    case WM_NOTIFY: {
        auto* note = reinterpret_cast<NMHDR*>(lp);
        // The ListView sends LVN_COLUMNCLICK when the user clicks a report header.
        // Do not hook a header mouse message: that would interfere with dragging dividers.
        if (note->hwndFrom == g_list && note->code == LVN_COLUMNCLICK) {
            const int col = reinterpret_cast<NMLISTVIEW*>(lp)->iSubItem;
            if (col == 0 || col == 1) {
                if (g_sortColumn == col) g_sortAscending = !g_sortAscending;
                else { g_sortColumn = col; g_sortAscending = true; }
                writeInt(L"SortColumn", g_sortColumn);
                writeInt(L"SortAscending", g_sortAscending ? 1 : 0);
                updateMatches();
                if (g_header) InvalidateRect(g_header, nullptr, TRUE);
                if (g_edit) SetFocus(g_edit);
            }
            return 0;
        }
        if (g_header && note->hwndFrom == g_header && note->code == NM_CUSTOMDRAW)
            return drawHeader(reinterpret_cast<NMCUSTOMDRAW*>(lp));
        if (note->hwndFrom == g_list && note->code == NM_CUSTOMDRAW)
            return drawListRow(reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
        if (note->hwndFrom == g_list && note->code == NM_DBLCLK) { activateSelected(); return 0; }
        if (note->hwndFrom == g_list && note->code == NM_CLICK) { SetFocus(g_edit); return 0; }
        break;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && !g_closing) {
            // Mouse-click outside dismisses without stealing focus from another app.
            closePopup(false);
            return 0;
        }
        if (LOWORD(wp) != WA_INACTIVE && g_edit) SetFocus(g_edit);
        break;
    case WM_CLOSE:
        closePopup(true);
        return 0;
    case WM_DESTROY:
        saveSettings(hwnd);
        if (g_edit && g_oldEditProc && IsWindow(g_edit))
            SetWindowLongPtrW(g_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_oldEditProc));
        g_popup = g_edit = g_list = g_header = nullptr;
        g_oldEditProc = nullptr;
        if (g_font) { DeleteObject(g_font); g_font = nullptr; }
        g_documents.clear();
        g_results.clear();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void __cdecl toggleQuickOpen() {
    if (g_settings && IsWindow(g_settings)) { SetForegroundWindow(g_settings); return; }
    if (g_popup) { closePopup(true); return; }
    if (!g_npp._nppHandle) return;
    loadSettings();
    RECT host{};
    GetWindowRect(g_npp._nppHandle, &host);
    int x = g_x == CW_USEDEFAULT ? host.left + ((host.right - host.left) - g_width) / 2 : g_x;
    int y = g_y == CW_USEDEFAULT ? host.top + ((host.bottom - host.top) - g_height) / 3 : g_y;
    // If a saved location is no longer visible after changing monitors, recenter.
    RECT proposed{x, y, x + g_width, y + g_height};
    if (!MonitorFromRect(&proposed, MONITOR_DEFAULTTONULL)) {
        x = host.left + ((host.right - host.left) - g_width) / 2;
        y = host.top + ((host.bottom - host.top) - g_height) / 3;
    }
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, CLASS_NAME, L"Quick Open Documents",
              WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
              x, y, g_width, g_height, g_npp._nppHandle, nullptr, g_instance, nullptr);
    if (popup) { ShowWindow(popup, SW_SHOW); SetForegroundWindow(popup); SetFocus(g_edit); }
}

static HWND addControl(HWND parent, const wchar_t* cls, const wchar_t* title, DWORD style,
                       int id, int x, int y, int w, int h) {
    HWND control = CreateWindowExW(0, cls, title, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                          parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
    if (control && g_settingsFont) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_settingsFont), TRUE);
    return control;
}
static void enableColorControls(HWND hwnd) {
    bool enable = g_draftMode == CUSTOM;
    for (int id = COLOR_BG; id <= COLOR_SEL_TEXT; ++id) {
        HWND control = GetDlgItem(hwnd, id);
        if (control) EnableWindow(control, enable);
    }
}
static std::wstring colorLabel(int id, COLORREF color) {
    const wchar_t* name = id == COLOR_BG ? L"Background" : id == COLOR_FG ? L"Text" :
                          id == COLOR_SEL ? L"Selected row" : L"Selected text";
    wchar_t label[100]{};
    swprintf_s(label, L"%s   (#%02X%02X%02X)...", name, GetRValue(color), GetGValue(color), GetBValue(color));
    return label;
}
static COLORREF* draftColor(int id) {
    if (id == COLOR_BG) return &g_draft.bg;
    if (id == COLOR_FG) return &g_draft.fg;
    if (id == COLOR_SEL) return &g_draft.selection;
    if (id == COLOR_SEL_TEXT) return &g_draft.selectionText;
    return nullptr;
}
static LRESULT CALLBACK settingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_settings = hwnd;
        g_draftMode = g_themeMode;
        g_draft = g_custom;
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
            g_settingsFont = CreateFontIndirectW(&metrics.lfMessageFont);
        addControl(hwnd, L"STATIC", L"Choose how QuickOpenNpp should look:", 0, 0, 18, 16, 420, 22);
        addControl(hwnd, L"BUTTON", L"Follow editor theme (active editor colors)", BS_AUTORADIOBUTTON | WS_GROUP,
                   MODE_EDITOR, 20, 49, 415, 24);
        addControl(hwnd, L"BUTTON", L"Follow Notepad++ UI (light / dark mode)", BS_AUTORADIOBUTTON,
                   MODE_UI, 20, 80, 415, 24);
        addControl(hwnd, L"BUTTON", L"Custom colors", BS_AUTORADIOBUTTON,
                   MODE_CUSTOM, 20, 111, 415, 24);
        CheckRadioButton(hwnd, MODE_EDITOR, MODE_CUSTOM, MODE_EDITOR + g_draftMode);
        addControl(hwnd, L"STATIC", L"Custom palette:", 0, 0, 20, 151, 400, 22);
        const int ids[] = {COLOR_BG, COLOR_FG, COLOR_SEL, COLOR_SEL_TEXT};
        for (int i = 0; i < 4; ++i) {
            int id = ids[i];
            const std::wstring label = colorLabel(id, *draftColor(id));
            addControl(hwnd, L"BUTTON", label.c_str(), BS_PUSHBUTTON, id, 20, 179 + i*37, 410, 29);
        }
        addControl(hwnd, L"BUTTON", L"Save", BS_DEFPUSHBUTTON, BTN_SAVE, 250, 344, 85, 30);
        addControl(hwnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON, BTN_CANCEL, 345, 344, 85, 30);
        enableColorControls(hwnd);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= MODE_EDITOR && id <= MODE_CUSTOM && HIWORD(wp) == BN_CLICKED) {
            g_draftMode = id - MODE_EDITOR;
            enableColorControls(hwnd);
            return 0;
        }
        if (id >= COLOR_BG && id <= COLOR_SEL_TEXT && HIWORD(wp) == BN_CLICKED && g_draftMode == CUSTOM) {
            COLORREF* selected = draftColor(id);
            CHOOSECOLORW chooser{sizeof(chooser)};
            chooser.hwndOwner = hwnd;
            chooser.rgbResult = *selected;
            chooser.lpCustColors = g_chooseColors;
            chooser.Flags = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColorW(&chooser)) {
                *selected = chooser.rgbResult;
                std::wstring label = colorLabel(id, *selected);
                SetWindowTextW(GetDlgItem(hwnd, id), label.c_str());
            }
            return 0;
        }
        if (id == BTN_SAVE) {
            g_themeMode = g_draftMode;
            g_custom = g_draft;
            saveAppearance();
            refreshTheme();
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == BTN_CANCEL) { DestroyWindow(hwnd); return 0; }
        break;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        g_settings = nullptr;
        if (g_settingsFont) { DeleteObject(g_settingsFont); g_settingsFont = nullptr; }
        if (IsWindow(g_npp._nppHandle)) SetForegroundWindow(g_npp._nppHandle);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void __cdecl openConfiguration() {
    if (g_settings && IsWindow(g_settings)) { SetForegroundWindow(g_settings); return; }
    if (g_popup) closePopup(true);
    if (!g_npp._nppHandle) return;
    g_iniPath = configurationFile();
    loadAppearance();
    RECT r{}; GetWindowRect(g_npp._nppHandle, &r);
    int x = r.left + ((r.right-r.left)-470)/2;
    int y = r.top + ((r.bottom-r.top)-430)/2;
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, SETTINGS_CLASS, L"QuickOpenNpp Configuration",
              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, x, y, 470, 430,
              g_npp._nppHandle, nullptr, g_instance, nullptr);
    if (hwnd) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
}

static ShortcutKey g_shortcut{true, true, true, 'S'};
static FuncItem g_commands[] = {
    {L"Quick Open Documents", toggleQuickOpen, 0, false, &g_shortcut},
    {L"Configuration...", openConfiguration, 0, false, nullptr}
};
extern "C" __declspec(dllexport) void __cdecl setInfo(NppData data) {
    g_npp = data;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);
    WNDCLASSW wc{};
    wc.lpfnWndProc = popupProc;
    wc.hInstance = g_instance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    wc.lpfnWndProc = settingsProc;
    wc.lpszClassName = SETTINGS_CLASS;
    RegisterClassW(&wc);
}
extern "C" __declspec(dllexport) const wchar_t* __cdecl getName() { return L"QuickOpenNpp"; }
extern "C" __declspec(dllexport) FuncItem* __cdecl getFuncsArray(int* count) {
    *count = static_cast<int>(sizeof(g_commands) / sizeof(g_commands[0]));
    return g_commands;
}
extern "C" __declspec(dllexport) void __cdecl beNotified(void*) {}
extern "C" __declspec(dllexport) LRESULT __cdecl messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
extern "C" __declspec(dllexport) BOOL __cdecl isUnicode() { return TRUE; }
BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_instance = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}