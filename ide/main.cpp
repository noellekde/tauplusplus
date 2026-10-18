// tauIDE - a small Win32 IDE for Tau. The window, files and Run command are C++;
// syntax highlighting and the status bar are Tau programs (scripts\*.tau) that
// run on the embedded tau++ interpreter.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WINVER
#define WINVER 0x0600
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

#include "../tauvm.hpp"

// ---- RichEdit pieces that older headers may lack ------------------------------
#ifndef EM_GETTEXTEX
#define EM_GETTEXTEX (WM_USER + 94)
#define EM_GETTEXTLENGTHEX (WM_USER + 95)
#define EM_SETTEXTEX (WM_USER + 97)
#define GT_DEFAULT 0
#define GTL_NUMCHARS 8
#define ST_DEFAULT 0
struct GETTEXTEX_ { DWORD cb; DWORD flags; UINT codepage; LPCSTR lpDefaultChar; LPBOOL lpUsedDefChar; };
struct GETTEXTLENGTHEX_ { DWORD flags; UINT codepage; };
struct SETTEXTEX_ { DWORD flags; UINT codepage; };
#define GETTEXTEX GETTEXTEX_
#define GETTEXTLENGTHEX GETTEXTLENGTHEX_
#define SETTEXTEX SETTEXTEX_
#endif

namespace {

enum {
    IDM_NEW = 101, IDM_OPEN, IDM_SAVE, IDM_SAVEAS, IDM_EXIT,
    IDM_RUN, IDM_RELOAD, IDM_ABOUT
};
const UINT_PTR TIMER_HIGHLIGHT = 1;

// colours (dark theme); index = Paint colour used by highlight.tau
const COLORREF BG = RGB(30, 30, 30);
const COLORREF PALETTE[8] = {
    RGB(212, 212, 212),  // 0 default
    RGB(86, 156, 214),   // 1 keyword
    RGB(106, 153, 85),   // 2 comment
    RGB(206, 145, 120),  // 3 string / char
    RGB(181, 206, 168),  // 4 number
    RGB(197, 134, 192),  // 5 label
    RGB(78, 201, 176),   // 6 register
    RGB(244, 71, 71),    // 7 spare
};

HINSTANCE g_inst;
HWND g_main, g_edit, g_out, g_status;
HFONT g_font;
HBRUSH g_bgBrush;
std::wstring g_path;  // empty = untitled
bool g_dirty = false;
std::wstring g_exeDir;

struct Script {
    std::unique_ptr<tau::Program> program;
    std::string error;
};
Script g_highlight, g_statusScript;

// ---- helpers ------------------------------------------------------------------
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (n == 0) { cp = CP_ACP; flags = 0; n = MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), nullptr, 0); }
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

bool readFile(const std::wstring& path, std::string& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    char buf[8192];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

bool writeFile(const std::wstring& path, const std::string& data) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

std::wstring toCrLf(const std::string& utf8) {
    std::wstring w = utf8ToWide(utf8), r;
    for (size_t i = 0; i < w.size(); ++i) {
        if (w[i] == L'\r') {
            r += L"\r\n";
            if (i + 1 < w.size() && w[i + 1] == L'\n') ++i;
        } else if (w[i] == L'\n') {
            r += L"\r\n";
        } else {
            r += w[i];
        }
    }
    return r;
}

// the editor text; RichEdit separates lines with a single \r
std::wstring editorText() {
    GETTEXTLENGTHEX gl = {GTL_NUMCHARS, 1200};
    LRESULT len = SendMessageW(g_edit, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
    if (len <= 0) return L"";
    std::wstring w((size_t)len + 1, L'\0');
    GETTEXTEX gt = {};
    gt.cb = (DWORD)(w.size() * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    SendMessageW(g_edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)&w[0]);
    size_t z = w.find(L'\0');
    if (z != std::wstring::npos) w.resize(z);
    return w;
}

void setEditorUtf8(const std::string& utf8) {
    SETTEXTEX st = {ST_DEFAULT, CP_UTF8};
    SendMessageW(g_edit, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)utf8.c_str());
}

void setOutput(const std::wstring& text) { SetWindowTextW(g_out, text.c_str()); }

std::wstring withCrLf(const std::string& s) { return toCrLf(s); }

void updateTitle() {
    std::wstring name = g_path.empty() ? L"untitled" : g_path.substr(g_path.find_last_of(L"\\/") + 1);
    std::wstring t = L"tauIDE - " + name + (g_dirty ? L" *" : L"");
    SetWindowTextW(g_main, t.c_str());
    SendMessageW(g_status, SB_SETTEXTW, 1, (LPARAM)(g_path.empty() ? L"untitled" : g_path.c_str()));
}

// ---- scripts ------------------------------------------------------------------
void loadScript(Script& s, const wchar_t* file) {
    s.program.reset();
    s.error.clear();
    std::string src;
    if (!readFile(g_exeDir + L"\\scripts\\" + file, src)) {
        s.error = wideToUtf8(std::wstring(file)) + ": file not found next to the program (scripts folder)";
        return;
    }
    try {
        s.program.reset(new tau::Program(tau::Program::parse(src)));
    } catch (const tau::Error& e) {
        s.error = wideToUtf8(std::wstring(file)) + ": " + e.what();
    }
}

void reloadScripts() {
    loadScript(g_highlight, L"highlight.tau");
    loadScript(g_statusScript, L"status.tau");
}

// Host functions the editor scripts can call
struct EditorHost : tau::Host {
    std::string text;  // one char per UTF-16 unit, '?' for non-ASCII
    int caretLine = 1, caretCol = 1, lineCount = 1;
    std::vector<std::array<int, 3>> paints;

    void call(const std::string& name, const std::vector<tau::Operand>& a, tau::Vm& vm) override {
        struct Entry { const char* n; size_t argc; };
        static const Entry table[] = {{"textlen", 1}, {"charat", 2}, {"caretline", 1},
                                      {"caretcol", 1}, {"linecount", 1}, {"paint", 3}};
        size_t need = (size_t)-1;
        for (const Entry& e : table)
            if (name == e.n) need = e.argc;
        if (need == (size_t)-1) vm.fail("unknown host function '" + name + "'");
        if (a.size() != need) vm.fail("host function '" + name + "' takes " + std::to_string(need) + " argument(s)");
        int32_t n = (int32_t)text.size();
        if (name == "textlen") vm.setReg(a[0], n);
        else if (name == "charat") {
            int32_t i = vm.val(a[1]);
            vm.setReg(a[0], (i >= 0 && i < n) ? (uint8_t)text[(size_t)i] : 0);
        } else if (name == "caretline") vm.setReg(a[0], caretLine);
        else if (name == "caretcol") vm.setReg(a[0], caretCol);
        else if (name == "linecount") vm.setReg(a[0], lineCount);
        else paints.push_back({vm.val(a[0]), vm.val(a[1]), vm.val(a[2])});
    }
};

void fillEditorHost(EditorHost& h, const std::wstring& w) {
    h.text.resize(w.size());
    for (size_t i = 0; i < w.size(); ++i) h.text[i] = w[i] < 128 ? (char)w[i] : '?';
    CHARRANGE sel = {};
    SendMessageW(g_edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    LONG line = (LONG)SendMessageW(g_edit, EM_EXLINEFROMCHAR, 0, sel.cpMin);
    LONG lineStart = (LONG)SendMessageW(g_edit, EM_LINEINDEX, (WPARAM)line, 0);
    h.caretLine = (int)line + 1;
    h.caretCol = (int)(sel.cpMin - lineStart) + 1;
    h.lineCount = (int)SendMessageW(g_edit, EM_GETLINECOUNT, 0, 0);
}

void updateStatus() {
    std::wstring msg;
    if (!g_statusScript.program) {
        msg = L"status.tau: " + utf8ToWide(g_statusScript.error);
    } else {
        EditorHost host;
        fillEditorHost(host, editorText());
        tau::Vm vm(*g_statusScript.program, &host);
        try {
            vm.run();
            msg = utf8ToWide(vm.out);
        } catch (const tau::Error& e) {
            msg = L"status.tau error: " + utf8ToWide(e.what());
        }
    }
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)msg.c_str());
}

void applyHighlight() {
    if (!g_highlight.program) return;
    std::wstring w = editorText();
    EditorHost host;
    fillEditorHost(host, w);
    tau::Vm vm(*g_highlight.program, &host, 100000000L);
    try {
        vm.run();
    } catch (const tau::Error& e) {
        SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)(L"highlight.tau error: " + utf8ToWide(e.what())).c_str());
        return;
    }
    CHARRANGE oldSel = {};
    SendMessageW(g_edit, EM_EXGETSEL, 0, (LPARAM)&oldSel);
    LRESULT oldMask = SendMessageW(g_edit, EM_SETEVENTMASK, 0, 0);
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);

    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR;
    cf.dwEffects = 0;
    CHARRANGE all = {0, -1};
    SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM)&all);
    cf.crTextColor = PALETTE[0];
    SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    LONG total = (LONG)w.size();
    for (const auto& p : host.paints) {
        LONG s = p[0], e = p[0] + p[1];
        if (s < 0 || p[1] <= 0 || s >= total) continue;
        if (e > total) e = total;
        CHARRANGE r = {s, e};
        SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM)&r);
        cf.crTextColor = PALETTE[(p[2] >= 0 && p[2] < 8) ? p[2] : 0];
        SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
    SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM)&oldSel);
    SendMessageW(g_edit, EM_SETEVENTMASK, 0, oldMask);
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_edit, nullptr, TRUE);
}

// ---- file commands ------------------------------------------------------------
bool saveTo(const std::wstring& path) {
    std::wstring w = editorText();
    std::string utf8 = wideToUtf8(toCrLf(wideToUtf8(w)));
    if (!writeFile(path, utf8)) {
        MessageBoxW(g_main, L"Could not write the file.", L"tauIDE", MB_ICONERROR);
        return false;
    }
    g_path = path;
    g_dirty = false;
    updateTitle();
    return true;
}

bool pickFile(bool save, std::wstring& path) {
    wchar_t buf[MAX_PATH] = L"";
    if (save && !g_path.empty()) wcsncpy(buf, g_path.c_str(), MAX_PATH - 1);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = L"Tau programs (*.tau)\0*.tau\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"tau";
    ofn.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) return false;
    path = buf;
    return true;
}

bool doSaveAs() {
    std::wstring p;
    return pickFile(true, p) && saveTo(p);
}

bool doSave() { return g_path.empty() ? doSaveAs() : saveTo(g_path); }

// false = user cancelled
bool confirmDiscard() {
    if (!g_dirty) return true;
    int r = MessageBoxW(g_main, L"Save changes to this file?", L"tauIDE", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return doSave();
    return true;
}

void newFile() {
    if (!confirmDiscard()) return;
    setEditorUtf8("");
    g_path.clear();
    g_dirty = false;
    updateTitle();
    applyHighlight();
    updateStatus();
}

void openFile() {
    if (!confirmDiscard()) return;
    std::wstring p;
    if (!pickFile(false, p)) return;
    std::string data;
    if (!readFile(p, data)) {
        MessageBoxW(g_main, L"Could not read the file.", L"tauIDE", MB_ICONERROR);
        return;
    }
    setEditorUtf8(data);
    g_path = p;
    g_dirty = false;
    updateTitle();
    applyHighlight();
    updateStatus();
}

void runProgram() {
    std::string src = wideToUtf8(editorText());
    for (auto& c : src)
        if (c == '\r') c = '\n';
    std::string result;
    try {
        tau::Program prog = tau::Program::parse(src);
        // programs can read the document through the same Host functions the IDE scripts use
        EditorHost host;
        fillEditorHost(host, editorText());
        for (auto& c : host.text)
            if (c == '\r') c = '\n';
        tau::Vm vm(prog, &host);
        try {
            vm.run();
        } catch (const tau::Error& e) {
            result = vm.out + "\nTau error: " + e.what() + "\n";
            setOutput(withCrLf(result));
            return;
        }
        result = vm.out;
        if (result.empty()) result = "(program finished, no output)\n";
    } catch (const tau::Error& e) {
        result = std::string("Tau error: ") + e.what() + "\n";
    }
    setOutput(withCrLf(result));
}

// ---- window -------------------------------------------------------------------
void layout() {
    RECT rc;
    GetClientRect(g_main, &rc);
    SendMessageW(g_status, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(g_status, &sr);
    int sh = sr.bottom - sr.top;
    int w = rc.right, h = rc.bottom - sh;
    if (h < 0) h = 0;
    int outH = h * 28 / 100;
    int edH = h - outH;
    MoveWindow(g_edit, 0, 0, w, edH, TRUE);
    MoveWindow(g_out, 0, edH, w, outH, TRUE);
    int parts[2] = {w > 420 ? w - 360 : w / 2, -1};
    SendMessageW(g_status, SB_SETPARTS, 2, (LPARAM)parts);
    updateTitle();
    updateStatus();
}

const char* SAMPLE =
    "# hello from tauIDE - press F5 to run\r\n"
    "Text tau 1 \"Hello from Tau!\\n\"\r\n"
    "Set r0 1\r\n"
    "loop:\r\n"
    "Jgt r0 3 end\r\n"
    "Print 1\r\n"
    "Add r0 1\r\n"
    "Jump loop\r\n"
    "end:\r\n"
    "Halt\r\n";

void createControls(HWND hwnd) {
    g_font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                             ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL | ES_WANTRETURN,
                             0, 0, 100, 100, hwnd, (HMENU)1001, g_inst, nullptr);
    g_out = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                            ES_READONLY | ES_AUTOVSCROLL, 0, 0, 100, 100, hwnd, (HMENU)1002, g_inst, nullptr);
    g_status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)1003,
                               g_inst, nullptr);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_out, EM_SETLIMITTEXT, 0, 0);
    SendMessageW(g_out, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_edit, EM_SETBKGNDCOLOR, 0, BG);
    SendMessageW(g_edit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
    cf.crTextColor = PALETTE[0];
    cf.yHeight = 220;
    wcscpy(cf.szFaceName, L"Consolas");
    SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

HMENU buildMenu() {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_NEW, L"&New\tCtrl+N");
    AppendMenuW(file, MF_STRING, IDM_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDM_SAVEAS, L"Save &As...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit");
    HMENU run = CreatePopupMenu();
    AppendMenuW(run, MF_STRING, IDM_RUN, L"&Run\tF5");
    AppendMenuW(run, MF_STRING, IDM_RELOAD, L"Re&load IDE scripts\tCtrl+R");
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&About");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)run, L"&Run");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_main = hwnd;
            createControls(hwnd);
            return 0;
        case WM_SIZE:
            if (g_status) layout();
            return 0;
        case WM_SETFOCUS:
            SetFocus(g_edit);
            return 0;
        case WM_CTLCOLORSTATIC:
            if ((HWND)lp == g_out) {
                SetTextColor((HDC)wp, PALETTE[0]);
                SetBkColor((HDC)wp, BG);
                return (LRESULT)g_bgBrush;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wp) == 1001 && HIWORD(wp) == EN_CHANGE) {
                g_dirty = true;
                updateTitle();
                SetTimer(hwnd, TIMER_HIGHLIGHT, 150, nullptr);
                return 0;
            }
            switch (LOWORD(wp)) {
                case IDM_NEW: newFile(); break;
                case IDM_OPEN: openFile(); break;
                case IDM_SAVE: doSave(); break;
                case IDM_SAVEAS: doSaveAs(); break;
                case IDM_EXIT: SendMessageW(hwnd, WM_CLOSE, 0, 0); break;
                case IDM_RUN: runProgram(); break;
                case IDM_RELOAD:
                    reloadScripts();
                    applyHighlight();
                    updateStatus();
                    {
                        std::string err = g_highlight.error + (g_highlight.error.empty() || g_statusScript.error.empty() ? "" : "\n") + g_statusScript.error;
                        setOutput(err.empty() ? L"IDE scripts reloaded.\r\n" : withCrLf(err));
                    }
                    break;
                case IDM_ABOUT:
                    MessageBoxW(hwnd,
                                L"tauIDE\n\nThe window, files and Run command are C++ (tau++).\n"
                                L"Syntax highlighting and the status bar are Tau programs in the scripts folder.",
                                L"About tauIDE", MB_ICONINFORMATION);
                    break;
            }
            return 0;
        case WM_NOTIFY: {
            NMHDR* h = (NMHDR*)lp;
            if (h->hwndFrom == g_edit && h->code == EN_SELCHANGE) updateStatus();
            return 0;
        }
        case WM_TIMER:
            if (wp == TIMER_HIGHLIGHT) {
                KillTimer(hwnd, TIMER_HIGHLIGHT);
                applyHighlight();
                updateStatus();
            }
            return 0;
        case WM_CLOSE:
            if (confirmDiscard()) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    g_inst = inst;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_exeDir = exe;
    g_exeDir = g_exeDir.substr(0, g_exeDir.find_last_of(L"\\/"));

    if (!LoadLibraryW(L"Msftedit.dll")) {
        MessageBoxW(nullptr, L"Msftedit.dll (RichEdit 5) is required.", L"tauIDE", MB_ICONERROR);
        return 1;
    }
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    g_bgBrush = CreateSolidBrush(BG);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_bgBrush;
    wc.lpszClassName = L"tauIDEWindow";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"tauIDE", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                1000, 700, nullptr, buildMenu(), inst, nullptr);
    ACCEL accel[] = {
        {FCONTROL | FVIRTKEY, 'N', IDM_NEW}, {FCONTROL | FVIRTKEY, 'O', IDM_OPEN},
        {FCONTROL | FVIRTKEY, 'S', IDM_SAVE}, {FVIRTKEY, VK_F5, IDM_RUN},
        {FCONTROL | FVIRTKEY, 'R', IDM_RELOAD},
    };
    HACCEL table = CreateAcceleratorTableW(accel, (int)(sizeof accel / sizeof accel[0]));

    reloadScripts();
    setEditorUtf8(SAMPLE);
    g_dirty = false;
    layout();
    applyHighlight();
    updateStatus();
    std::string err = g_highlight.error + (g_highlight.error.empty() || g_statusScript.error.empty() ? "" : "\n") + g_statusScript.error;
    if (!err.empty()) setOutput(withCrLf(err));

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    SetFocus(g_edit);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(hwnd, table, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return (int)m.wParam;
}
