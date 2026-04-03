#include <Windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

#define COL_RESET   "\033[0m"
#define COL_DIM     "\033[2m"
#define COL_RED     "\033[38;2;200;50;50m"
#define COL_RED_B   "\033[38;2;220;70;70m"
#define COL_GRAY    "\033[38;2;130;130;140m"
#define COL_WHITE   "\033[38;2;220;220;228m"
#define COL_DARK    "\033[38;2;60;60;68m"
#define COL_GREEN   "\033[38;2;80;200;100m"
#define COL_YELLOW  "\033[38;2;220;180;60m"

static void EnableVT()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleTitleA("TXC Injector");
}

static void SetConsoleSize()
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    SMALL_RECT rect = { 0, 0, 79, 29 };
    SetConsoleWindowInfo(h, TRUE, &rect);
    COORD size = { 80, 30 };
    SetConsoleScreenBufferSize(h, size);
}

static void HideCursor()
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci{ 1, FALSE };
    SetConsoleCursorInfo(h, &ci);
}

static void ClearScreen()
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h, &csbi);
    DWORD cells = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD written;
    COORD origin = { 0, 0 };
    FillConsoleOutputCharacterA(h, ' ', cells, origin, &written);
    FillConsoleOutputAttribute(h, csbi.wAttributes, cells, origin, &written);
    SetConsoleCursorPosition(h, origin);
}

static void MoveCursor(int x, int y)
{
    COORD c = { (SHORT)x, (SHORT)y };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

static void PrintCentered(int y, const std::string& text, const char* color = COL_WHITE)
{
    int visLen = 0;
    bool inEsc = false;
    for (char c : text) {
        if (c == '\033') { inEsc = true; continue; }
        if (inEsc) { if (c == 'm') inEsc = false; continue; }
        visLen++;
    }
    int x = max(0, (80 - visLen) / 2);
    MoveCursor(x, y);
    printf("%s%s%s", color, text.c_str(), COL_RESET);
}

static void DrawLine(int y, char ch = '-', const char* color = COL_DARK)
{
    MoveCursor(0, y);
    printf("%s", color);
    for (int i = 0; i < 80; i++) putchar(ch);
    printf("%s", COL_RESET);
}

static void DrawBorder()
{
    printf(COL_DARK);
    MoveCursor(0, 0);  putchar('+'); for(int i=0;i<78;i++) putchar('-'); putchar('+');
    MoveCursor(0, 29); putchar('+'); for(int i=0;i<78;i++) putchar('-'); putchar('+');
    for (int y = 1; y < 29; y++) {
        MoveCursor(0, y);  putchar('|');
        MoveCursor(79, y); putchar('|');
    }
    printf(COL_RESET);
}

static void DrawHeader()
{
    DrawBorder();

    PrintCentered(2, "  ______  _  _   ____  ", COL_RED_B);
    PrintCentered(3, " |_   _|  \\ \\/\\  / ___|", COL_RED_B);
    PrintCentered(4, "   | |    \\  /  | |    ", COL_RED);
    PrintCentered(5, "   | |    /  \\  | |___ ", COL_RED);
    PrintCentered(6, "   |_|   /_/\\_\\  \\____|", COL_RED);

    DrawLine(8, '-', COL_DARK);
    PrintCentered(9, "REFLECTIVE DLL INJECTOR", COL_GRAY);
    DrawLine(10, '-', COL_DARK);
}
static void StatusLine(int y, const char* tag, const char* tagColor, const std::string& msg, const char* msgColor = COL_WHITE)
{
    MoveCursor(3, y);
    printf("%s[%s]%s  %s%s%s", tagColor, tag, COL_RESET, msgColor, msg.c_str(), COL_RESET);
}

static void ClearRegion(int y1, int y2)
{
    for (int y = y1; y <= y2; y++) {
        MoveCursor(1, y);
        for (int i = 0; i < 78; i++) putchar(' ');
    }
}

static void DrawProgressBar(int y, float pct, int width = 50)
{
    int filled = (int)(pct * width);
    MoveCursor((80 - width - 4) / 2, y);
    printf(COL_DARK "[" COL_RESET);
    for (int i = 0; i < width; i++) {
        if (i < filled)
            printf(COL_RED "=" COL_RESET);
        else
            printf(COL_DARK "-" COL_RESET);
    }
    printf(COL_DARK "]" COL_RESET);
    MoveCursor((80 - width - 4) / 2 + width + 2, y);
    printf(COL_GRAY " %3d%%" COL_RESET, (int)(pct * 100));
}

static void AnimateProgress(const char* label, int y)
{
    const char* stages[] = {
        "Reading payload  ",
        "Parsing PE       ",
        "Opening process  ",
        "Allocating memory",
        "Writing payload  ",
        "Protecting memory",
        "Spawning thread  ",
        "Done             ",
    };
    int stageCount = 8;

    for (int s = 0; s < stageCount; s++) {
        ClearRegion(y, y + 2);
        MoveCursor(3, y);
        printf(COL_GRAY "%-20s" COL_RESET, stages[s]);
        DrawProgressBar(y + 1, (float)(s + 1) / stageCount);
        Sleep(s < stageCount - 1 ? 120 : 60);
    }
}

static std::string WinError()
{
    DWORD id = GetLastError();
    if (!id) return "";
    LPSTR buf = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buf, 0, NULL);
    std::string msg(buf ? buf : "Unknown error");
    LocalFree(buf);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == '.'))
        msg.pop_back();
    return msg;
}

static DWORD FindPID(const std::wstring& name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static ULONG_PTR RVAToOffset(PIMAGE_DOS_HEADER base, DWORD rva)
{
    auto* nth = (PIMAGE_NT_HEADERS64)((LPBYTE)base + base->e_lfanew);
    WORD optSize = nth->FileHeader.SizeOfOptionalHeader;
    DWORD numSec = nth->FileHeader.NumberOfSections;
    auto* sec = (PIMAGE_SECTION_HEADER)((LPBYTE)&nth->OptionalHeader + optSize);

    for (DWORD i = 0; i < numSec; i++, sec++) {
        if (rva >= sec->VirtualAddress && rva < sec->VirtualAddress + sec->Misc.VirtualSize)
            return (ULONG_PTR)base + (rva - sec->VirtualAddress) + sec->PointerToRawData;
    }
    return 0;
}

static DWORD FindReflectiveOffset(PIMAGE_DOS_HEADER base)
{
    auto* nth = (PIMAGE_NT_HEADERS64)((LPBYTE)base + base->e_lfanew);
    DWORD expRVA = nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!expRVA) return 0;

    auto* expDir = (PIMAGE_EXPORT_DIRECTORY)RVAToOffset(base, expRVA);
    if (!expDir) return 0;

    auto* names   = (DWORD*)RVAToOffset(base, expDir->AddressOfNames);
    auto* ords    = (WORD*) RVAToOffset(base, expDir->AddressOfNameOrdinals);
    auto* funcs   = (DWORD*)RVAToOffset(base, expDir->AddressOfFunctions);

    for (DWORD i = 0; i < expDir->NumberOfNames; i++) {
        auto* fname = (char*)RVAToOffset(base, names[i]);
        if (fname && strcmp(fname, "ReflectiveLoader") == 0) {
            DWORD fnRVA = funcs[ords[i]];
            return (DWORD)(RVAToOffset(base, fnRVA) - (ULONG_PTR)base);
        }
    }
    return 0;
}

enum InjectResult {
    INJ_OK,
    INJ_ERR_DLL_NOT_FOUND,
    INJ_ERR_DLL_READ,
    INJ_ERR_NO_REFLECTIVE_EXPORT,
    INJ_ERR_PROCESS_NOT_FOUND,
    INJ_ERR_OPEN_PROCESS,
    INJ_ERR_ALLOC,
    INJ_ERR_WRITE,
    INJ_ERR_PROTECT,
    INJ_ERR_THREAD,
};

static InjectResult Inject(const std::string& dllPath, const std::wstring& procName, std::string& errDetail)
{
    HANDLE hFile = CreateFileA(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        errDetail = WinError();
        return INJ_ERR_DLL_NOT_FOUND;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    auto* dllBuf = (PIMAGE_DOS_HEADER)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, fileSize);

    DWORD read = 0;
    if (!dllBuf || !ReadFile(hFile, dllBuf, fileSize, &read, NULL) || read != fileSize) {
        errDetail = WinError();
        CloseHandle(hFile);
        if (dllBuf) HeapFree(GetProcessHeap(), 0, dllBuf);
        return INJ_ERR_DLL_READ;
    }
    CloseHandle(hFile);

    DWORD reflOffset = FindReflectiveOffset(dllBuf);
    if (!reflOffset) {
        HeapFree(GetProcessHeap(), 0, dllBuf);
        errDetail = "No ReflectiveLoader export found in DLL";
        return INJ_ERR_NO_REFLECTIVE_EXPORT;
    }

    DWORD pid = FindPID(procName);
    if (!pid) {
        HeapFree(GetProcessHeap(), 0, dllBuf);
        errDetail = "Process not running";
        return INJ_ERR_PROCESS_NOT_FOUND;
    }

    HANDLE hProc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!hProc) {
        errDetail = WinError();
        HeapFree(GetProcessHeap(), 0, dllBuf);
        return INJ_ERR_OPEN_PROCESS;
    }

    LPVOID remote = VirtualAllocEx(hProc, NULL, fileSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        errDetail = WinError();
        CloseHandle(hProc);
        HeapFree(GetProcessHeap(), 0, dllBuf);
        return INJ_ERR_ALLOC;
    }

    if (!WriteProcessMemory(hProc, remote, dllBuf, fileSize, NULL)) {
        errDetail = WinError();
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        HeapFree(GetProcessHeap(), 0, dllBuf);
        return INJ_ERR_WRITE;
    }

    DWORD oldProt = 0;
    if (!VirtualProtectEx(hProc, remote, fileSize, PAGE_EXECUTE_READ, &oldProt)) {
        errDetail = WinError();
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        HeapFree(GetProcessHeap(), 0, dllBuf);
        return INJ_ERR_PROTECT;
    }

    auto lpStart = (LPTHREAD_START_ROUTINE)((LPBYTE)remote + reflOffset);
    HANDLE hThread = CreateRemoteThread(hProc, NULL, 0, lpStart, NULL, 0, NULL);
    if (!hThread) {
        errDetail = WinError();
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        HeapFree(GetProcessHeap(), 0, dllBuf);
        return INJ_ERR_THREAD;
    }

    WaitForSingleObject(hThread, 3000);
    CloseHandle(hThread);
    CloseHandle(hProc);
    HeapFree(GetProcessHeap(), 0, dllBuf);
    return INJ_OK;
}

static std::string InputLine(int x, int y, int maxWidth)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci{ 12, TRUE };
    SetConsoleCursorInfo(hOut, &ci);

    MoveCursor(x, y);
    printf(COL_WHITE);

    std::string result;
    char buf[256] = {};
    DWORD read = 0;
    ReadConsoleA(h, buf, sizeof(buf) - 1, &read, NULL);
    result = buf;
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();

    ci.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &ci);
    SetConsoleMode(h, mode);
    printf(COL_RESET);
    return result;
}

static bool PromptYesNo(int y)
{
    MoveCursor(3, y);
    printf(COL_GRAY "Try again? " COL_DARK "[" COL_WHITE "Y" COL_DARK "/" COL_GRAY "N" COL_DARK "] " COL_RESET);

    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, ENABLE_PROCESSED_INPUT);

    char c = 0;
    DWORD read;
    while (true) {
        ReadConsoleA(h, &c, 1, &read, NULL);
        c = (char)tolower((unsigned char)c);
        if (c == 'y' || c == 'n' || c == '\r') break;
    }
    SetConsoleMode(h, mode);
    return c == 'y' || c == '\r';
}

int main()
{
    EnableVT();
    SetConsoleSize();
    HideCursor();
    ClearScreen();
    DrawHeader();

    std::string dllPath;
    std::string procName;

    while (true) {
        ClearRegion(13, 28);

        StatusLine(13, "INFO", COL_GRAY, "Enter the path to the DLL you want to inject", COL_GRAY);

        MoveCursor(3, 15);
        printf(COL_DARK "DLL Path " COL_RESET);
        MoveCursor(3, 16);
        printf(COL_DARK);
        for (int i = 0; i < 74; i++) putchar('-');
        printf(COL_RESET);

        MoveCursor(3, 17);
        printf(COL_GRAY "> " COL_RESET);
        dllPath = InputLine(5, 17, 72);

        if (dllPath.empty()) {
            ClearRegion(19, 21);
            StatusLine(19, " !! ", COL_RED, "DLL path cannot be empty", COL_RED);
            Sleep(1200);
            continue;
        }

        DWORD attr = GetFileAttributesA(dllPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            ClearRegion(19, 21);
            StatusLine(19, " !! ", COL_RED, "File not found: " + dllPath, COL_RED);
            Sleep(1500);
            continue;
        }

        ClearRegion(19, 21);
        StatusLine(19, " OK ", COL_GREEN, dllPath, COL_WHITE);
        Sleep(300);
        break;
    }

    while (true) {
        ClearRegion(21, 28);

        StatusLine(21, "INFO", COL_GRAY, "Enter the target process name  (e.g. javaw.exe)", COL_GRAY);

        MoveCursor(3, 23);
        printf(COL_DARK "Process " COL_RESET);
        MoveCursor(3, 24);
        printf(COL_DARK);
        for (int i = 0; i < 74; i++) putchar('-');
        printf(COL_RESET);

        MoveCursor(3, 25);
        printf(COL_GRAY "> " COL_RESET);
        procName = InputLine(5, 25, 72);

        if (procName.empty()) {
            ClearRegion(27, 28);
            StatusLine(27, " !! ", COL_RED, "Process name cannot be empty", COL_RED);
            Sleep(1200);
            continue;
        }

        std::wstring wProc(procName.begin(), procName.end());
        DWORD pid = FindPID(wProc);

        if (!pid) {
            ClearRegion(27, 28);
            StatusLine(27, " !! ", COL_RED, "Process not found: " + procName, COL_RED);
            if (!PromptYesNo(28)) { ClearScreen(); return 0; }
            continue;
        }

        ClearRegion(27, 28);
        std::string pidStr = "PID " + std::to_string(pid) + "  \xe2\x80\x94  " + procName;
        StatusLine(27, " OK ", COL_GREEN, pidStr, COL_WHITE);
        Sleep(400);
        break;
    }

    ClearRegion(13, 28);

    StatusLine(14, "....", COL_GRAY, "Injecting", COL_GRAY);

    AnimateProgress("Injecting", 16);

    std::string errDetail;
    std::wstring wProc(procName.begin(), procName.end());
    InjectResult res = Inject(dllPath, wProc, errDetail);

    ClearRegion(13, 28);

    if (res == INJ_OK) {
        MoveCursor(1, 14);
        printf(COL_DARK); for (int i = 0; i < 78; i++) putchar('='); printf(COL_RESET);
        PrintCentered(16, "INJECTION SUCCESSFUL", COL_GREEN);
        MoveCursor(3, 18);
        printf(COL_GRAY "Process  " COL_WHITE "%s" COL_RESET, procName.c_str());
        MoveCursor(3, 19);
        printf(COL_GRAY "DLL      " COL_DARK "%s" COL_RESET, dllPath.c_str());
        MoveCursor(1, 21);
        printf(COL_DARK); for (int i = 0; i < 78; i++) putchar('='); printf(COL_RESET);
    } else {
        const char* errLabel = "INJECTION FAILED";
        const char* reason = "";
        switch (res) {
        case INJ_ERR_DLL_NOT_FOUND:          reason = "DLL file not found"; break;
        case INJ_ERR_DLL_READ:               reason = "Failed to read DLL"; break;
        case INJ_ERR_NO_REFLECTIVE_EXPORT:   reason = "No ReflectiveLoader export in DLL"; break;
        case INJ_ERR_PROCESS_NOT_FOUND:      reason = "Target process not found"; break;
        case INJ_ERR_OPEN_PROCESS:           reason = "Could not open process (run as admin?)"; break;
        case INJ_ERR_ALLOC:                  reason = "Remote memory allocation failed"; break;
        case INJ_ERR_WRITE:                  reason = "WriteProcessMemory failed"; break;
        case INJ_ERR_PROTECT:                reason = "VirtualProtectEx failed"; break;
        case INJ_ERR_THREAD:                 reason = "CreateRemoteThread failed"; break;
        default:                             reason = "Unknown error"; break;
        }

        MoveCursor(1, 14);
        printf(COL_RED); for (int i = 0; i < 78; i++) putchar('-'); printf(COL_RESET);
        PrintCentered(16, errLabel, COL_RED);
        MoveCursor(3, 18);
        printf(COL_GRAY "Reason   " COL_RED_B "%s" COL_RESET, reason);
        if (!errDetail.empty()) {
            MoveCursor(3, 19);
            printf(COL_GRAY "Detail   " COL_GRAY "%s" COL_RESET, errDetail.c_str());
        }
        MoveCursor(1, 21);
        printf(COL_RED); for (int i = 0; i < 78; i++) putchar('-'); printf(COL_RESET);
    }

    MoveCursor(3, 27);
    printf(COL_GRAY "Press any key to exit..." COL_RESET);
    MoveCursor(0, 29);

    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, ENABLE_PROCESSED_INPUT);
    char c; DWORD r;
    ReadConsoleA(h, &c, 1, &r, NULL);
    SetConsoleMode(h, mode);

    ClearScreen();
    return 0;
}
