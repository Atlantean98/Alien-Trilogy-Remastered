// Alien Trilogy: Remastered - the packaged game's launcher.
// Play stays locked until the player's own disc (BIN/CUE, NTSC-U SLUS-00007) has been imported into game/GameData:
// the launcher runs the bundled Python importer (game/tools/package/package_import.py), shows its progress, then
// starts the game with the chosen renderer. Settings and saves stay inside the game folder.
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "ExplorerVersion.h"
namespace fs = std::filesystem;

namespace {
HINSTANCE instance{};
HWND window{}, chooseButton{}, playButton{}, readmeButton{}, logsButton{}, rendererBox{}, statusBox{}, progressBar{};
HFONT font{}, small{}, heading{};
HBRUSH background{};
fs::path folder, game;
PROCESS_INFORMATION importer{}, running{};
enum { Choose = 1, Play, Readme, Renderer, Logs };
constexpr const wchar_t* RendererNames[] = {L"DirectX 11", L"DirectX 12", L"Vulkan", L"Software"};
constexpr const wchar_t* RendererFlags[] = {L"dx11", L"dx12", L"vulkan", L"software"};

std::wstring wide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
    return out;
}
std::string read(const fs::path& path) {   // shared so the importer can replace the file while we poll it
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string out; char buffer[4096]; DWORD count = 0;
    while (ReadFile(file, buffer, sizeof buffer, &count, nullptr) && count) out.append(buffer, count);
    CloseHandle(file);
    return out;
}
std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\""; unsigned slashes = 0;
    for (auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\'); out += c; slashes = 0;
    }
    return out.append(slashes * 2, L'\\') + L"\"";
}
bool imported() { return fs::exists(game / "GameData/package-ready.txt") && fs::exists(game / "GameData/PS1/CD/SLUS_000.07"); }
bool hasMovies() { std::error_code e; return fs::exists(game / "GameData/PS1/Movies/INTRO.fmv", e); }
unsigned renderer() { auto i = SendMessageW(rendererBox, CB_GETCURSEL, 0, 0); return i >= 0 && i < 4 ? unsigned(i) : 0; }
void status(const std::wstring& text, int percent) { SetWindowTextW(statusBox, text.c_str()); SendMessageW(progressBar, PBM_SETPOS, percent, 0); }
void controls() {
    const bool idle = !importer.hProcess && !running.hProcess;
    EnableWindow(chooseButton, idle); EnableWindow(rendererBox, idle); EnableWindow(playButton, idle && imported());
    SetWindowTextW(chooseButton, imported() ? L"RE-IMPORT DISC" : L"SELECT DISC (.CUE)");
}
void idleStatus() {
    if (imported())
        status(hasMovies() ? L"Your disc is imported. Choose a renderer, then PLAY."
                           : L"Your disc is imported (without the cinematics - FFmpeg was not installed). Choose a renderer, then PLAY.", 100);
    else
        status(L"To play, select your own disc image: Alien Trilogy for PlayStation, NTSC-U (USA), SLUS-00007, dumped as BIN/CUE.\r\n"
               L"Choose its .cue file, or drop it onto this window. See README.md for help.", 0);
}
// The game writes one folder per run under game\logs (session.log, and crash.txt after a crash).
fs::path latestLog() {
    fs::path newest; std::error_code e;
    for (const auto& entry : fs::directory_iterator(game / "logs", e))
        if (entry.is_directory(e) && (newest.empty() || entry.path().filename() > newest.filename())) newest = entry.path();
    return newest;
}
void openFolder(const fs::path& path) {
    std::error_code e; fs::create_directories(path, e);
    ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
void saveRenderer() { std::ofstream(game / "launcher.cfg") << renderer() << "\n"; }
unsigned loadRenderer() { unsigned i = 0; std::ifstream(game / "launcher.cfg") >> i; return i < 4 ? i : 0; }

bool start(const fs::path& exe, const std::vector<std::wstring>& args, PROCESS_INFORMATION& info, const fs::path& log) {
    std::wstring command = quote(exe.wstring());
    for (auto& a : args) command += L" " + quote(a);
    STARTUPINFOW startup{}; startup.cb = sizeof startup;
    HANDLE out = INVALID_HANDLE_VALUE, in = INVALID_HANDLE_VALUE;
    const bool logged = !log.empty();
    if (logged) {
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        out = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
        in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        startup.dwFlags = STARTF_USESTDHANDLES; startup.hStdOutput = startup.hStdError = out; startup.hStdInput = in;
    }
    const bool ok = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, logged, logged ? CREATE_NO_WINDOW : 0, nullptr,
                                   exe.parent_path().c_str(), &startup, &info) != 0;
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
    if (ok) { CloseHandle(info.hThread); info.hThread = nullptr; }
    return ok;
}
void import(const fs::path& image) {
    if (importer.hProcess || running.hProcess) return;
    auto ext = image.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (ext != L".cue" && ext != L".bin") {
        MessageBoxW(window, L"Choose the disc image's .cue file. Alien Trilogy needs a BIN/CUE dump of your own NTSC-U disc (ISO images lose the CD music).", TrilogyTitle, MB_OK | MB_ICONWARNING);
        return;
    }
    std::error_code ignored;
    fs::remove(game / "GameData/package-ready.txt", ignored);
    if (!start(game / "tools/python/python.exe", {L"-I", (game / "tools/package/package_import.py").wstring(), L"--disc", image.wstring()}, importer, game / "import.log")) {
        MessageBoxW(window, L"The bundled importer could not start. Extract the whole package to a folder you can write to, then try again.", TrilogyTitle, MB_OK | MB_ICONERROR);
        return;
    }
    status(L"Checking your disc image...", 1);
    controls();
}
void choose() {
    wchar_t selected[32768]{};
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof dialog; dialog.hwndOwner = window;
    dialog.lpstrFilter = L"Disc image (*.cue)\0*.cue\0Disc image track (*.bin)\0*.bin\0\0";
    dialog.lpstrFile = selected; dialog.nMaxFile = 32768;
    dialog.lpstrTitle = L"Select your Alien Trilogy (PlayStation, NTSC-U, SLUS-00007) .cue file";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) import(selected);
}
void play() {
    if (!imported() || importer.hProcess || running.hProcess) return;
    saveRenderer();
    if (!start(game / "decompilation/cpp/build/recovered_world_explorer.exe", {L"--renderer", RendererFlags[renderer()]}, running, {})) {
        MessageBoxW(window, L"The game could not start. Make sure the whole package was extracted.", TrilogyTitle, MB_OK | MB_ICONERROR);
        return;
    }
    status(L"Playing...", 100); controls(); ShowWindow(window, SW_MINIMIZE);
}
void tick() {
    if (importer.hProcess) {
        auto text = read(game / "import_progress.txt");
        if (auto end = text.find('\n'); end != std::string::npos) {
            int percent = 0; try { percent = std::clamp(std::stoi(text.substr(0, end)), 0, 100); } catch (...) {}
            status(wide(text.substr(end + 1)), percent);
        }
        DWORD code = STILL_ACTIVE;
        if (GetExitCodeProcess(importer.hProcess, &code) && code != STILL_ACTIVE) {
            CloseHandle(importer.hProcess); importer = {};
            controls();
            if (code == 0 && imported()) {
                idleStatus();
                if (!hasMovies()) MessageBoxW(window, L"Your disc is imported and the game is ready.\n\nThe cinematics were skipped because FFmpeg is not installed. To add them, install FFmpeg (see README.md) and choose RE-IMPORT DISC.", TrilogyTitle, MB_OK | MB_ICONINFORMATION);
            } else {
                auto error = wide(read(game / "import_error.txt"));
                if (error.empty()) error = L"The import did not finish. See game\\import.log for details.";
                status(L"Import failed: " + error, 0);
                MessageBoxW(window, error.c_str(), L"Disc import failed", MB_OK | MB_ICONERROR);
            }
        }
    }
    if (running.hProcess) {
        DWORD code = STILL_ACTIVE;
        if (GetExitCodeProcess(running.hProcess, &code) && code != STILL_ACTIVE) {
            CloseHandle(running.hProcess); running = {};
            controls(); ShowWindow(window, SW_RESTORE); SetForegroundWindow(window);
            if (code) {
                const auto log = latestLog();
                const bool crashed = !log.empty() && fs::exists(log / "crash.txt");
                status(L"The game closed with an error. Its log is in game\\logs\\" + log.filename().wstring() +
                       L" - attach it to a bug report on the project's GitHub Issues page.", 100);
                std::wstring text = std::wstring(crashed ? L"The game crashed." : L"The game closed with an error.") +
                    L"\n\nA report was saved in:\ngame\\logs\\" + log.filename().wstring() +
                    L"\n\nTo help get it fixed, open a bug report on the project's GitHub Issues page and attach session.log" +
                    (crashed ? L" and crash.txt" : L"") + L" from that folder.\n\nOpen the folder now?";
                if (!log.empty() && MessageBoxW(window, text.c_str(), TrilogyTitle, MB_YESNO | MB_ICONWARNING) == IDYES) openFolder(log);
            } else idleStatus();
        }
    }
}
void layout() {
    RECT r{}; GetClientRect(window, &r);
    const int w = r.right, h = r.bottom;
    MoveWindow(chooseButton, 32, 150, 220, 44, TRUE);
    MoveWindow(rendererBox, 268, 158, 150, 200, TRUE);
    MoveWindow(playButton, 434, 150, 140, 44, TRUE);
    MoveWindow(readmeButton, w - 32 - 110, 150, 110, 44, TRUE);
    MoveWindow(logsButton, w - 32 - 110 - 12 - 130, 150, 130, 44, TRUE);
    MoveWindow(progressBar, 32, 214, w - 64, 16, TRUE);
    MoveWindow(statusBox, 32, 246, w - 64, std::max(60, h - 300), TRUE);
    InvalidateRect(window, nullptr, TRUE);
}
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE: {
        window = hwnd;
        chooseButton = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, HMENU(Choose), instance, nullptr);
        playButton = CreateWindowW(L"BUTTON", L"PLAY", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd, HMENU(Play), instance, nullptr);
        readmeButton = CreateWindowW(L"BUTTON", L"README", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, HMENU(Readme), instance, nullptr);
        logsButton = CreateWindowW(L"BUTTON", L"OPEN LOGS", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, HMENU(Logs), instance, nullptr);
        rendererBox = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, hwnd, HMENU(Renderer), instance, nullptr);
        for (auto name : RendererNames) SendMessageW(rendererBox, CB_ADDSTRING, 0, LPARAM(name));
        SendMessageW(rendererBox, CB_SETCURSEL, loadRenderer(), 0);
        progressBar = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, instance, nullptr);
        SendMessageW(progressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        statusBox = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY, 0, 0, 0, 0, hwnd, nullptr, instance, nullptr);
        for (auto c : {chooseButton, playButton, readmeButton, logsButton, rendererBox, statusBox}) SendMessageW(c, WM_SETFONT, WPARAM(font), TRUE);
        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, 1, 200, nullptr);
        controls(); idleStatus();
        return 0;
    }
    case WM_SIZE: layout(); return 0;
    case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize = {880, 420}; return 0;
    case WM_COMMAND:
        if (HIWORD(w) == BN_CLICKED) {
            if (LOWORD(w) == Choose) choose();
            if (LOWORD(w) == Play) play();
            if (LOWORD(w) == Readme) ShellExecuteW(hwnd, L"open", (folder / "README.md").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (LOWORD(w) == Logs) openFolder(game / "logs");
        }
        if (LOWORD(w) == Renderer && HIWORD(w) == CBN_SELCHANGE) saveRenderer();
        return 0;
    case WM_DROPFILES: {
        auto drop = HDROP(w); wchar_t path[32768]{};
        if (DragQueryFileW(drop, 0, path, 32768)) import(path);
        DragFinish(drop);
        return 0;
    }
    case WM_TIMER: tick(); return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT:
        SetTextColor(HDC(w), RGB(170, 224, 180)); SetBkColor(HDC(w), RGB(10, 16, 13)); return LRESULT(background);
    case WM_ERASEBKGND: { RECT r{}; GetClientRect(hwnd, &r); FillRect(HDC(w), &r, background); return 1; }
    case WM_PAINT: {
        PAINTSTRUCT paint{}; auto dc = BeginPaint(hwnd, &paint); RECT r{}; GetClientRect(hwnd, &r);
        SetBkMode(dc, TRANSPARENT);
        auto old = SelectObject(dc, heading); SetTextColor(dc, RGB(65, 238, 95));
        RECT title{32, 26, r.right - 32, 66}; DrawTextW(dc, L"ALIEN TRILOGY: REMASTERED", -1, &title, DT_LEFT | DT_SINGLELINE);
        SelectObject(dc, font); SetTextColor(dc, RGB(184, 207, 187));
        RECT sub{32, 72, r.right - 32, 96}; std::wstring line = std::wstring(TrilogyVersion) + L"   |   Native PC port   |   Beta";
        DrawTextW(dc, line.c_str(), -1, &sub, DT_LEFT | DT_SINGLELINE);
        SelectObject(dc, small); SetTextColor(dc, RGB(120, 150, 125));
        RECT need{32, 100, r.right - 32, 140};
        DrawTextW(dc, L"Requires your own copy: Alien Trilogy for PlayStation, NTSC-U (USA), SLUS-00007, as a BIN/CUE disc image. No game files are included.", -1, &need, DT_LEFT | DT_WORDBREAK);
        RECT foot{32, r.bottom - 40, r.right - 32, r.bottom - 10};
        DrawTextW(dc, L"By Atlantean / NobleElite. Unofficial fan project; Alien Trilogy belongs to its respective owners.", -1, &foot, DT_LEFT | DT_SINGLELINE);
        SelectObject(dc, old); EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_CLOSE:
        if (importer.hProcess && MessageBoxW(hwnd, L"The disc import is still running. Stop it and close?", TrilogyTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        if (importer.hProcess) TerminateProcess(importer.hProcess, 1);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: KillTimer(hwnd, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE app, HINSTANCE, PWSTR, int show) {
    instance = app;
    SetProcessDPIAware();
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
    folder = fs::path(exe).parent_path(); game = folder / "game";
    if (!fs::exists(game / "decompilation/cpp/build/recovered_world_explorer.exe")) {
        MessageBoxW(nullptr, L"The game folder is missing. Extract the whole zip (not just this file) and run the launcher from there.", TrilogyTitle, MB_OK | MB_ICONERROR);
        return 1;
    }
    INITCOMMONCONTROLSEX common{sizeof common, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common);
    background = CreateSolidBrush(RGB(10, 16, 13));
    font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    small = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    heading = CreateFontW(-28, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    WNDCLASSW wc{}; wc.hInstance = app; wc.lpfnWndProc = procedure; wc.lpszClassName = L"AlienTrilogyRemasteredLauncher";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(app, MAKEINTRESOURCEW(1)); if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = background;
    RegisterClassW(&wc);
    std::wstring title = std::wstring(TrilogyTitle) + L" - Launcher";
    if (!CreateWindowW(wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 460, nullptr, nullptr, app, nullptr)) return 1;
    ShowWindow(window, show); UpdateWindow(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return int(msg.wParam);
}
