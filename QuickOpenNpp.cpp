// QuickOpenNpp v0.2 — native Win32 open-document fuzzy switcher for Notepad++.
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
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
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
static const wchar_t CLASS_NAME[] = L"QuickOpenNppPopupClassV2";

struct Document { UINT_PTR bufferId; std::wstring name; std::wstring folder; };
static NppData g_npp{};
static HINSTANCE g_instance = nullptr;
static HWND g_popup = nullptr, g_edit = nullptr, g_list = nullptr;
static WNDPROC g_oldEditProc = nullptr;
static HFONT g_font = nullptr;
static std::vector<Document> g_documents;
static std::vector<int> g_results;
static std::wstring g_iniPath;
static int g_width = DEFAULT_WIDTH, g_height = DEFAULT_HEIGHT;
static int g_x = CW_USEDEFAULT, g_y = CW_USEDEFAULT;
static int g_nameWidth = 230, g_folderWidth = 440;
static bool g_closing = false;

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
static void loadSettings() {
    g_iniPath = configurationFile();
    g_width = std::max(MIN_WIDTH, readInt(L"Width", DEFAULT_WIDTH));
    g_height = std::max(MIN_HEIGHT, readInt(L"Height", DEFAULT_HEIGHT));
    g_nameWidth = std::max(70, readInt(L"NameWidth", 230));
    g_folderWidth = std::max(70, readInt(L"FolderWidth", 440));
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
    std::stable_sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
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
static void layoutChildren(HWND hwnd) {
    RECT rc{}; GetClientRect(hwnd, &rc);
    int width = std::max(1, rc.right - 2 * MARGIN);
    int listY = MARGIN + SEARCH_HEIGHT + LIST_GAP;
    int listHeight = std::max(1, rc.bottom - listY - MARGIN);
    if (g_edit) MoveWindow(g_edit, MARGIN, MARGIN, width, SEARCH_HEIGHT, TRUE);
    if (g_list) MoveWindow(g_list, MARGIN, listY, width, listHeight, TRUE);
}
static LRESULT CALLBACK popupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_popup = hwnd;
        g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE |
               WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 28, hwnd,
               reinterpret_cast<HMENU>(static_cast<INT_PTR>(EDIT_ID)), g_instance, nullptr);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE |
               WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
               0, 0, 100, 100, hwnd,
               reinterpret_cast<HMENU>(static_cast<INT_PTR>(LIST_ID)), g_instance, nullptr);
        if (!g_edit || !g_list) return -1;
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
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
        if (wp != SIZE_MINIMIZED) layoutChildren(hwnd);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize.x = MIN_WIDTH;
        info->ptMinTrackSize.y = MIN_HEIGHT;
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == EDIT_ID && HIWORD(wp) == EN_CHANGE) updateMatches();
        return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lp)->idFrom == LIST_ID) {
            if (reinterpret_cast<NMHDR*>(lp)->code == NM_DBLCLK) { activateSelected(); return 0; }
            if (reinterpret_cast<NMHDR*>(lp)->code == NM_CLICK) { SetFocus(g_edit); return 0; }
        }
        break;
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
        g_popup = g_edit = g_list = nullptr;
        g_oldEditProc = nullptr;
        if (g_font) { DeleteObject(g_font); g_font = nullptr; }
        g_documents.clear();
        g_results.clear();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void __cdecl toggleQuickOpen() {
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
static ShortcutKey g_shortcut{true, true, true, 'S'};
static FuncItem g_commands[] = {{L"Quick Open Documents", toggleQuickOpen, 0, false, &g_shortcut}};
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
