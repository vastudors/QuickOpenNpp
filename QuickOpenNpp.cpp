// QuickOpenNpp - a tiny native Notepad++ open-document switcher.
// Build using Windows SDK / MSVC; Notepad++ v8.x (Unicode plugin ABI).
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <unordered_set>
#include <climits>

// The relevant declarations from Notepad++'s public plugin interface.
// API reference: https://npp-user-manual.org/docs/plugin-communication/
struct NppData { HWND _nppHandle; HWND _scintillaMainHandle; HWND _scintillaSecondHandle; };
using PFUNCPLUGINCMD = void (__cdecl *)();
struct ShortcutKey { bool _isCtrl; bool _isAlt; bool _isShift; UCHAR _key; };
struct FuncItem { wchar_t _itemName[64]; PFUNCPLUGINCMD _pFunc; int _cmdID; bool _init2Check; ShortcutKey* _pShKey; };

static constexpr UINT NPPMSG = WM_USER + 1000;
static constexpr UINT NPPM_GETNBOPENFILES = NPPMSG + 7;
static constexpr UINT NPPM_ACTIVATEDOC = NPPMSG + 28;
static constexpr UINT NPPM_GETFULLPATHFROMBUFFERID = NPPMSG + 58;
static constexpr UINT NPPM_GETBUFFERIDFROMPOS = NPPMSG + 59;
static constexpr int PRIMARY_VIEW = 1;
static constexpr int SECOND_VIEW = 2;
static constexpr int EDIT_ID = 101;
static constexpr int LIST_ID = 102;
static constexpr int WIDTH = 620;
static constexpr int HEIGHT = 370;
static const wchar_t CLASS_NAME[] = L"QuickOpenNppPopupClass";

struct Document {
    UINT_PTR bufferId;
    int view;
    int tabIndex;
    std::wstring name;
    std::wstring path;
};
static NppData g_npp{};
static HINSTANCE g_instance = nullptr;
static HWND g_popup = nullptr, g_edit = nullptr, g_list = nullptr;
static WNDPROC g_oldEditProc = nullptr;
static HFONT g_font = nullptr;
static std::vector<Document> g_documents;
static std::vector<int> g_results;

static std::wstring lowercase(std::wstring text) {
    for (auto& c : text) c = static_cast<wchar_t>(towlower(c));
    return text;
}

// A subsequence-based fuzzy matcher with contiguous and word-boundary bonuses.
// Returns -1 for no match, or a score >= 0; larger is better.
static int fuzzyScore(const std::wstring& filename, const std::wstring& pattern) {
    if (pattern.empty()) return 0;
    const auto name = lowercase(filename);
    const auto query = lowercase(pattern);
    size_t from = 0;
    int score = 0;
    int previous = -2;
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

static void loadDocuments() {
    g_documents.clear();
    std::unordered_set<UINT_PTR> seen;
    for (int view = 0; view < 2; ++view) {
        int count = static_cast<int>(SendMessageW(g_npp._nppHandle, NPPM_GETNBOPENFILES, 0,
                                                  view == 0 ? PRIMARY_VIEW : SECOND_VIEW));
        for (int i = 0; i < count; ++i) {
            const auto buffer = static_cast<UINT_PTR>(SendMessageW(g_npp._nppHandle,
                                                 NPPM_GETBUFFERIDFROMPOS, i, view));
            if (!buffer || !seen.insert(buffer).second) continue; // dedupe clones
            const LRESULT length = SendMessageW(g_npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID,
                                                 static_cast<WPARAM>(buffer), 0);
            if (length < 0 || length > 32767) continue;
            std::wstring path(static_cast<size_t>(length) + 1, L'\0');
            SendMessageW(g_npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID,
                         static_cast<WPARAM>(buffer), reinterpret_cast<LPARAM>(path.data()));
            path.resize(wcslen(path.c_str()));
            size_t slash = path.find_last_of(L"\\/");
            std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
            if (name.empty()) name = L"(unnamed document)";
            g_documents.push_back({buffer, view, i, std::move(name), std::move(path)});
        }
    }
}

static void updateMatches() {
    if (!g_edit || !g_list) return;
    int len = GetWindowTextLengthW(g_edit);
    std::wstring query(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(g_edit, query.data(), len + 1);
    query.resize(static_cast<size_t>(len));

    std::vector<std::pair<int, int>> scored;
    for (int i = 0; i < static_cast<int>(g_documents.size()); ++i) {
        int score = fuzzyScore(g_documents[i].name, query);
        if (score >= 0) scored.emplace_back(i, score);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    g_results.clear();
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    for (const auto& item : scored) {
        int index = item.first;
        g_results.push_back(index);
        const auto& doc = g_documents[index];
        std::wstring display = doc.name;
        if (doc.path != doc.name) display += L"    —    " + doc.path;
        SendMessageW(g_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
    }
    if (!g_results.empty()) SendMessageW(g_list, LB_SETCURSEL, 0, 0);
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, nullptr, TRUE);
}

static void closePopup() {
    if (g_popup) DestroyWindow(g_popup);
}

static void activateSelected() {
    if (!g_list) return;
    int selected = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
    if (selected < 0 || selected >= static_cast<int>(g_results.size())) return;
    Document doc = g_documents[g_results[selected]];
    closePopup();
    // Tab order may have changed while the popup was visible; resolve by buffer ID.
    for (int view = 0; view < 2; ++view) {
        int count = static_cast<int>(SendMessageW(g_npp._nppHandle, NPPM_GETNBOPENFILES, 0,
                                                  view == 0 ? PRIMARY_VIEW : SECOND_VIEW));
        for (int index = 0; index < count; ++index) {
            UINT_PTR id = static_cast<UINT_PTR>(SendMessageW(g_npp._nppHandle,
                                           NPPM_GETBUFFERIDFROMPOS, index, view));
            if (id == doc.bufferId) {
                SendMessageW(g_npp._nppHandle, NPPM_ACTIVATEDOC, view, index);
                SetForegroundWindow(g_npp._nppHandle);
                return;
            }
        }
    }
}

static LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_ESCAPE) { closePopup(); return 0; }
        if (wp == VK_RETURN) { activateSelected(); return 0; }
        if (wp == VK_DOWN || wp == VK_UP || wp == VK_PRIOR || wp == VK_NEXT) {
            int count = static_cast<int>(g_results.size());
            if (count == 0) return 0;
            int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
            int step = (wp == VK_PRIOR || wp == VK_NEXT) ? 8 : 1;
            sel += (wp == VK_DOWN || wp == VK_NEXT) ? step : -step;
            sel = std::clamp(sel, 0, count - 1);
            SendMessageW(g_list, LB_SETCURSEL, sel, 0);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
    return CallWindowProcW(g_oldEditProc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK popupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_popup = hwnd;
        g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE |
                   WS_TABSTOP | ES_AUTOHSCROLL, 14, 14, WIDTH - 28, 32, hwnd,
                   reinterpret_cast<HMENU>(EDIT_ID), g_instance, nullptr);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE |
                   WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 14, 55, WIDTH - 28,
                   HEIGHT - 70, hwnd, reinterpret_cast<HMENU>(LIST_ID), g_instance, nullptr);
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
            g_font = CreateFontIndirectW(&metrics.lfMessageFont);
        if (g_font) {
            SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            SendMessageW(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        }
        g_oldEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_edit, GWLP_WNDPROC,
                                                    reinterpret_cast<LONG_PTR>(editProc)));
        loadDocuments();
        updateMatches();
        SetFocus(g_edit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == EDIT_ID && HIWORD(wp) == EN_CHANGE) updateMatches();
        if (LOWORD(wp) == LIST_ID && HIWORD(wp) == LBN_DBLCLK) activateSelected();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_DESTROY:
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
    if (g_popup) { closePopup(); return; }
    if (!g_npp._nppHandle) return;
    RECT host{};
    GetWindowRect(g_npp._nppHandle, &host);
    int x = host.left + ((host.right - host.left) - WIDTH) / 2;
    int y = host.top + ((host.bottom - host.top) - HEIGHT) / 3;
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, CLASS_NAME, L"Quick Open Documents",
                                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_BORDER,
                                x, y, WIDTH, HEIGHT, g_npp._nppHandle, nullptr, g_instance, nullptr);
    if (popup) { ShowWindow(popup, SW_SHOWNORMAL); SetForegroundWindow(popup); SetFocus(g_edit); }
}

static ShortcutKey g_shortcut{true, true, true, 'S'};
static FuncItem g_commands[] = {{L"Quick Open Documents", toggleQuickOpen, 0, false, &g_shortcut}};

extern "C" __declspec(dllexport) void __cdecl setInfo(NppData data) {
    g_npp = data;
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
