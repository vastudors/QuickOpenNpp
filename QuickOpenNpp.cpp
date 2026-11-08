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
