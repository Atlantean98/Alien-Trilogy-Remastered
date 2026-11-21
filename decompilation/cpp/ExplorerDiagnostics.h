#pragma once
// Session logs and crash reports for players to attach to bug reports.
//
// Every normal run gets its own folder, logs/<date>_<time>/ beside GameData:
//   session.log  version, system, renderer, level/mode changes, a state snapshot every 30 s, errors, how the run ended
//   crash.txt    on a crash: what it was, where (module + offset), the call stack, what the game was doing, recent events
//   crash.dmp    a small minidump for the developer (it can contain folder paths, so attaching it is optional)
//   gpu-backend.log / movie-playback.log / runtime-errors.log copies, when the run made them
// Sessions with a crash or an error are kept; clean ones are pruned to the newest 30. The Windows user folder and user
// name are replaced with placeholders in the text logs, so they can be posted publicly.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <intrin.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>
#include "ExplorerVersion.h"

namespace ExplorerDiagnostics {
inline std::filesystem::path root, folder;
inline HANDLE logFile = INVALID_HANDLE_VALUE;
inline CRITICAL_SECTION lock;
inline bool active = false, hadError = false;
inline char context[1024] = "Starting up";
inline char recent[24][512]{};          // the last events, repeated in crash.txt
inline unsigned recentNext = 0;
inline volatile LONG crashing = 0;
inline std::string profileSlash, profileForward, userName;
inline FILETIME started{};   // logs older than this run are not copied into its folder

inline std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}
inline void replaceAll(std::string& text, const std::string& what, const char* with) {
    if (what.size() < 3) return;
    for (size_t at = 0;;) {   // case-insensitive (Windows paths)
        auto it = std::search(text.begin() + at, text.end(), what.begin(), what.end(),
                              [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
        if (it == text.end()) return;
        at = size_t(it - text.begin());
        text.replace(at, what.size(), with);
        at += std::strlen(with);
    }
}
inline std::string scrub(std::string text) {   // keep the player's identity out of logs they may post
    replaceAll(text, profileSlash, "%USERPROFILE%");
    replaceAll(text, profileForward, "%USERPROFILE%");
    if (userName.size() >= 3) replaceAll(text, userName, "<user>");
    return text;
}
inline void write(HANDLE file, const std::string& text) {
    if (file == INVALID_HANDLE_VALUE) return;
    auto clean = scrub(text);
    DWORD written = 0;
    WriteFile(file, clean.data(), DWORD(clean.size()), &written, nullptr);
    FlushFileBuffers(file);
}
inline std::string stamp() {
    SYSTEMTIME t{}; GetLocalTime(&t);
    char text[32]; snprintf(text, sizeof text, "%02u:%02u:%02u.%03u", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return text;
}
inline void event(const std::string& text) {
    if (!active) return;
    EnterCriticalSection(&lock);
    auto line = stamp() + "  " + text;
    snprintf(recent[recentNext++ % 24], 512, "%s", line.c_str());
    write(logFile, line + "\r\n");
    LeaveCriticalSection(&lock);
}
inline void error(const std::string& text) {
    if (!active) return;
    if (!hadError) { hadError = true; std::FILE* f = _wfopen((folder / L"had-errors").c_str(), L"w"); if (f) std::fclose(f); }
    event("ERROR: " + text);
}
// The game's current state (level, mode, position...), shown with any crash; logged when it changes the level or mode.
inline void note(const std::string& state, const std::string& where) {
    if (!active) return;
    static std::string lastWhere; static ULONGLONG lastSnapshot = 0;
    snprintf(context, sizeof context, "%s", (where + " | " + state).c_str());
    const auto now = GetTickCount64();
    if (where != lastWhere) { event("Now: " + where); lastWhere = where; lastSnapshot = now; }
    else if (now - lastSnapshot >= 30000) { event("State: " + where + " | " + state); lastSnapshot = now; }
}

inline std::string systemSummary() {
    std::string out;
    char line[512];
    using GetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW os{sizeof os};
    if (auto f = reinterpret_cast<GetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) f(&os);
    snprintf(line, sizeof line, "Windows: %lu.%lu build %lu\r\n", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber); out += line;
    int info[4]{}; char brand[49]{};
    __cpuid(info, 0x80000000);
    if (unsigned(info[0]) >= 0x80000004) for (int i = 0; i < 3; ++i) { __cpuid(info, 0x80000002 + i); std::memcpy(brand + i * 16, info, 16); }
    SYSTEM_INFO si{}; GetNativeSystemInfo(&si);
    snprintf(line, sizeof line, "CPU: %s (%lu logical cores)\r\n", brand, si.dwNumberOfProcessors); out += line;
    MEMORYSTATUSEX memory{sizeof memory}; GlobalMemoryStatusEx(&memory);
    snprintf(line, sizeof line, "RAM: %.1f GB\r\n", memory.ullTotalPhys / 1073741824.0); out += line;
    DISPLAY_DEVICEW device{sizeof device};
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); ++i)
        if (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) { out += "GPU: " + utf8(device.DeviceString) + "\r\n"; break; }
    snprintf(line, sizeof line, "Display: %dx%d\r\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)); out += line;
    return out;
}

// The call stack as module+offset per frame (the developer maps offsets back to code with the matching build's symbols).
inline std::string stack(CONTEXT context) {
    std::string out;
    HMODULE help = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!help) return "  (stack unavailable)\r\n";
    using Init = BOOL(WINAPI*)(HANDLE, PCSTR, BOOL);
    using Walk = BOOL(WINAPI*)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64, PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64);
    auto init = reinterpret_cast<Init>(GetProcAddress(help, "SymInitialize"));
    auto walk = reinterpret_cast<Walk>(GetProcAddress(help, "StackWalk64"));
    auto table = reinterpret_cast<PFUNCTION_TABLE_ACCESS_ROUTINE64>(GetProcAddress(help, "SymFunctionTableAccess64"));
    auto base = reinterpret_cast<PGET_MODULE_BASE_ROUTINE64>(GetProcAddress(help, "SymGetModuleBase64"));
    if (!init || !walk || !table || !base) return "  (stack unavailable)\r\n";
    init(GetCurrentProcess(), nullptr, TRUE);
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip; frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp; frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 48 && walk(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &frame, &context, nullptr, table, base, nullptr); ++i) {
        const auto pc = frame.AddrPC.Offset;
        if (!pc) break;
        HMODULE module = nullptr; wchar_t path[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(pc), &module))
            GetModuleFileNameW(module, path, MAX_PATH);
        char line[512];
        snprintf(line, sizeof line, "  #%02d  %s+0x%llX\r\n", i, utf8(std::filesystem::path(path).filename().wstring()).c_str(),
                 (unsigned long long)(pc - reinterpret_cast<uintptr_t>(module)));
        out += line;
    }
    return out.empty() ? "  (no frames)\r\n" : out;
}
inline const char* exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case 0xC0000409: return "stack buffer overrun / fast fail";
    case 0xC0000374: return "heap corruption";
    case 0xE06D7363: return "C++ exception";
    default: return "exception";
    }
}
inline void copyLogs() {
    for (auto name : {L"gpu-backend.log", L"movie-playback.log", L"runtime-errors.log"}) {
        auto from = root / L"decompilation/reports" / name;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExW(from.c_str(), GetFileExInfoStandard, &data) && CompareFileTime(&data.ftLastWriteTime, &started) >= 0)
            CopyFileW(from.c_str(), (folder / name).c_str(), FALSE);
    }
}
// Writes crash.txt (+ crash.dmp). `why` describes the failure; `pointers` is null for aborts and unhandled C++ errors.
inline void report(const std::string& why, EXCEPTION_POINTERS* pointers) {
    if (!active || InterlockedExchange(&crashing, 1)) return;
    auto path = folder / L"crash.txt";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::string text = "Alien Trilogy: Remastered crash report\r\n======================================\r\n";
    text += "Version: " + std::string(TRILOGY_VERSION_TEXT) + "\r\nTime: " + stamp() + "\r\nWhat happened: " + why + "\r\n";
    CONTEXT context{};
    if (pointers && pointers->ExceptionRecord) {
        const auto record = pointers->ExceptionRecord;
        char line[512];
        snprintf(line, sizeof line, "Exception: 0x%08lX (%s)\r\n", record->ExceptionCode, exceptionName(record->ExceptionCode)); text += line;
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            snprintf(line, sizeof line, "Access: %s address 0x%llX\r\n", record->ExceptionInformation[0] == 1 ? "writing" : record->ExceptionInformation[0] == 8 ? "executing" : "reading",
                     (unsigned long long)record->ExceptionInformation[1]);
            text += line;
        }
        HMODULE module = nullptr; wchar_t name[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(record->ExceptionAddress), &module))
            GetModuleFileNameW(module, name, MAX_PATH);
        snprintf(line, sizeof line, "Where: %s+0x%llX\r\n", utf8(std::filesystem::path(name).filename().wstring()).c_str(),
                 (unsigned long long)(reinterpret_cast<uintptr_t>(record->ExceptionAddress) - reinterpret_cast<uintptr_t>(module)));
        text += line;
        if (pointers->ContextRecord) context = *pointers->ContextRecord;
    } else {
        RtlCaptureContext(&context);
    }
    text += "Game state: " + std::string(ExplorerDiagnostics::context) + "\r\n\r\nCall stack:\r\n" + stack(context);
    text += "\r\nSystem:\r\n" + systemSummary() + "\r\nLast events:\r\n";
    for (unsigned i = 0; i < 24; ++i) { const auto& line = recent[(recentNext + i) % 24]; if (line[0]) text += std::string("  ") + line + "\r\n"; }
    write(file, text);
    // a small minidump; attaching it to a bug report is optional (it can hold folder paths)
    bool dumped = false;
    if (HMODULE help = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        using Dump = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
        if (auto dump = reinterpret_cast<Dump>(GetProcAddress(help, "MiniDumpWriteDump"))) {
            HANDLE out = CreateFileW((folder / L"crash.dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (out != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), pointers, FALSE};
                dumped = dump(GetCurrentProcess(), GetCurrentProcessId(), out, MiniDumpNormal, pointers ? &info : nullptr, nullptr, nullptr) != 0;
                CloseHandle(out);
            }
        }
    }
    write(file, dumped ? "\r\nMinidump: crash.dmp\r\n" : "\r\nMinidump: unavailable\r\n");
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    write(logFile, stamp() + "  CRASH: " + why + " - see crash.txt\r\n");
    copyLogs();
}
inline LONG WINAPI unhandled(EXCEPTION_POINTERS* pointers) {
    report("the game crashed", pointers);
    return EXCEPTION_EXECUTE_HANDLER;
}
inline void aborted(int) {
    report("the game stopped itself (abort)", nullptr);
    _exit(3);
}
inline void terminated() {
    std::string why = "an error nothing in the game handled";
    if (auto current = std::current_exception()) {
        try { std::rethrow_exception(current); }
        catch (const std::exception& e) { why += ": "; why += e.what(); }
        catch (...) {}
    }
    report(why, nullptr);
    _exit(3);
}
inline void invalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    report("invalid parameter passed to the C runtime", nullptr);
    _exit(3);
}
inline void purecall() { report("pure virtual function call", nullptr); _exit(3); }

inline void prune() {   // keep every session with a crash or an error; clean ones: the newest 30
    std::error_code e;
    std::vector<std::filesystem::path> clean;
    for (const auto& entry : std::filesystem::directory_iterator(root / L"logs", e))
        if (entry.is_directory(e) && entry.path() != folder && !std::filesystem::exists(entry.path() / L"crash.txt", e) &&
            !std::filesystem::exists(entry.path() / L"had-errors", e))
            clean.push_back(entry.path());
    std::sort(clean.begin(), clean.end());
    for (size_t i = 0; i + 30 < clean.size(); ++i) std::filesystem::remove_all(clean[i], e);
}

// Call once at startup, for normal play (self-tests and tools skip it).
inline void start(const std::filesystem::path& gameRoot, const std::wstring& commandLine) {
    root = gameRoot;
    wchar_t profile[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) {
        profileSlash = utf8(profile);
        profileForward = profileSlash; std::replace(profileForward.begin(), profileForward.end(), '\\', '/');
    }
    wchar_t name[256]{}; DWORD size = 256;
    if (GetUserNameW(name, &size)) userName = utf8(name);
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t session[64];
    swprintf(session, 64, L"%04u-%02u-%02u_%02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    GetSystemTimeAsFileTime(&started);
    std::error_code e;
    std::filesystem::create_directories(root / L"logs", e);
    folder = root / L"logs" / session;
    for (unsigned n = 2; !CreateDirectoryW(folder.c_str(), nullptr); ++n) {   // two runs in the same second get their own folders
        if (GetLastError() != ERROR_ALREADY_EXISTS || n > 99) return;
        folder = root / L"logs" / (std::wstring(session) + L"_" + std::to_wstring(n));
    }
    logFile = CreateFileW((folder / L"session.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logFile == INVALID_HANDLE_VALUE) return;
    InitializeCriticalSection(&lock);
    active = true;
    ULONG reserve = 65536; SetThreadStackGuarantee(&reserve);   // room to report a stack overflow
    SetUnhandledExceptionFilter(unhandled);
    std::set_terminate(terminated);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);   // abort() raises SIGABRT (reported) instead of a silent fast fail
    std::signal(SIGABRT, aborted);
    _set_invalid_parameter_handler(invalidParameter);
    _set_purecall_handler(purecall);
    write(logFile, std::string("Alien Trilogy: Remastered v") + TRILOGY_VERSION_TEXT + " - session log\r\n" +
                   "Started: " + std::to_string(t.wYear) + "-" + std::to_string(t.wMonth) + "-" + std::to_string(t.wDay) + " " + stamp() + "\r\n" +
                   "Command line: " + utf8(commandLine) + "\r\n" + systemSummary() + "\r\n");
    prune();
}
// Call when the game closes normally (or after a handled fatal error).
inline void finish(int code) {
    if (!active) return;
    event(code ? "Closed with an error (exit code " + std::to_string(code) + ")" : "Closed normally");
    copyLogs();
}
}  // namespace ExplorerDiagnostics
