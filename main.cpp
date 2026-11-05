#include <iostream>
#include <string>
#include <filesystem>
#include <windows.h>
#include <vector>
#include <algorithm>
#include <sstream>
#include <shellapi.h>
#include "edit.hpp"
#include <fstream>
#include "globals.h"
#include <dxgi.h>
#pragma comment(lib, "dxgi.lib")
#include <array>
#include <intrin.h>
#include <psapi.h>
#include <unordered_set>
#pragma comment(lib, "advapi32.lib")


namespace fs = std::filesystem;
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

fs::path home_dir = std::filesystem::path(std::getenv("USERPROFILE"));
fs::path current_dir = fs::current_path();

std::wstring string_to_wstring(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &wstr[0], len);
    wstr.resize(len - 1);
    return wstr;
}

bool IsRunAsAdmin() {
    BOOL bIsAdmin = FALSE;
    PSID AdministratorsGroup = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(
        &NtAuthority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &AdministratorsGroup))
    {
        if (!CheckTokenMembership(NULL, AdministratorsGroup, &bIsAdmin)) {
            bIsAdmin = FALSE;
        }
        FreeSid(AdministratorsGroup);
    }

    return bIsAdmin == TRUE;
}

std::string g_AdminStatus = IsRunAsAdmin() ? "#" : "$";

int get_process_count()  {
    DWORD aProcesses[1024], cbNeeded, cProcesses;

    if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded)) {
        return 0;
    }
    cProcesses = cbNeeded / sizeof(DWORD);

    return (int)cProcesses;
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";

    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

BOOL WINAPI CtrlCHandler(DWORD fdwCtrlType) {
    if (fdwCtrlType == CTRL_C_EVENT) return TRUE;
    return FALSE;
}

static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

std::vector<std::string> split(const std::string& input) {
    std::vector<std::string> args;
    std::string arg;
    bool inQuotes = false;

    for (char c : input) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (isspace(c) && !inQuotes) {
            if (!arg.empty()) {
                args.push_back(arg);
                arg.clear();
            }
        } else {
            arg += c;
        }
    }

    if (!arg.empty()) {
        args.push_back(arg);
    }

    return args;
}

// bool request_uac_elevation() {
//     // проверяем, есть ли уже права администратора
//     BOOL isAdmin = FALSE;
//     PSID adminGroup = nullptr;
//     SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
//     if (AllocateAndInitializeSid(&ntAuthority, 2,
//                                  SECURITY_BUILTIN_DOMAIN_RID,
//                                  DOMAIN_ALIAS_RID_ADMINS,
//                                  0, 0, 0, 0, 0, 0,
//                                  &adminGroup)) {
//         CheckTokenMembership(nullptr, adminGroup, &isAdmin);
//         FreeSid(adminGroup);
//                                  }
//     if (isAdmin) {
//         return true;
//     }
//     WCHAR path[MAX_PATH];
//     GetModuleFileNameW(nullptr, path, MAX_PATH);
//
//     SHELLEXECUTEINFOW sei{};
//     sei.cbSize = sizeof(sei);
//     sei.fMask = SEE_MASK_NOCLOSEPROCESS;
//     sei.lpVerb = L"runas";       // запрос UAC
//     sei.lpFile = path;           // сам исполняемый файл
//     sei.lpParameters = L"";      // аргументы, если нужны
//     sei.nShow = SW_SHOWNORMAL;
//
//     if (!ShellExecuteExW(&sei)) return false;
//
//     WaitForSingleObject(sei.hProcess, INFINITE);
//     CloseHandle(sei.hProcess);
//     return true;
// }

bool is_external_command(const std::string& cmd) {
    if (cmd.empty()) return false;

    // преобразуем в wide string для SearchPathW
    int len = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, nullptr, 0);
    std::wstring wcmd(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, &wcmd[0], len);
    wcmd.resize(len - 1); // убираем лишний нуль

    wchar_t buffer[MAX_PATH];
    DWORD result = SearchPathW(
        nullptr,
        wcmd.c_str(),
        L".exe",
        MAX_PATH,
        buffer,
        nullptr
    );

    return result != 0 && result < MAX_PATH;
}

bool find_in_path(const std::wstring& cmd, std::wstring& full_path) {
    wchar_t buffer[MAX_PATH];
    DWORD len = SearchPathW(nullptr, cmd.c_str(), L".exe", MAX_PATH, buffer, nullptr);
    if (len == 0 || len >= MAX_PATH) return false;
    full_path = buffer;
    return true;
}

bool hasAutoRun(const fs::path& home_dir) {
    fs::path autorun = home_dir / "autorun.bat";
    return fs::exists(autorun) && fs::is_regular_file(autorun);
}

void run_external(const std::string& cmdline) {
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::string cwd = current_dir.string();

    if (!CreateProcessA(
            nullptr,
            const_cast<char*>(cmdline.c_str()),
            nullptr, nullptr, FALSE,
            0,
            nullptr, // environment
            cwd.c_str(), // рабочая директория
            &si, &pi))
    {
        // std::cout << "failed to start: " << cmdline << "\n";
        return;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void run_external_bg(const std::string& cmdline) {
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // рабочая директория - current_dir
    std::string cwd = current_dir.string();

    if (!CreateProcessA(
            nullptr,
            const_cast<char*>(cmdline.c_str()),
            nullptr, nullptr, FALSE,
            0,
            nullptr,
            cwd.c_str(),
            &si, &pi))
    {
        // std::cout << "failed to start: " << cmdline << "\n";
        return;
    }
    std::cout << "[BG] "
              << "\033[1;34m" << pi.dwProcessId << "\033[0m" << std::endl;

    // не ждём завершения
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void touchFile(const std::string& filename) {
    std::wstring wpath = (current_dir / utf8_to_wstring(filename)).wstring();
    std::ofstream file(wpath.c_str(), std::ios::app);
    if (!file) {
        std::cerr << "failed to create file: " << filename << "\n";
        return;
    }
    std::cout << "file created: " << filename << "\n";
}

std::string local_to_utf8(const std::string& local) {
    // сначала в wide string (UTF-16)
    int wlen = MultiByteToWideChar(CP_ACP, 0, local.c_str(), -1, nullptr, 0);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, local.c_str(), -1, &wstr[0], wlen);

    // теперь из UTF-16 в UTF-8
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string u8str(u8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &u8str[0], u8len, nullptr, nullptr);

    // удаляем лишний нулевой символ
    u8str.resize(u8len - 1);
    return u8str;
}

std::string getCPUName() {
    std::array<int, 4> cpui;
    char CPUBrandString[0x40] = { 0 };

    __cpuid(cpui.data(), 0x80000000);
    unsigned int nExIds = cpui[0];

    for (unsigned int i = 0x80000000; i <= nExIds; ++i) {
        __cpuid(cpui.data(), i);
        if (i == 0x80000002)
            memcpy(CPUBrandString, cpui.data(), sizeof(cpui));
        else if (i == 0x80000003)
            memcpy(CPUBrandString + 16, cpui.data(), sizeof(cpui));
        else if (i == 0x80000004)
            memcpy(CPUBrandString + 32, cpui.data(), sizeof(cpui));
    }

    return std::string(CPUBrandString);
}

std::vector<std::string> logo = {
    "        88888888888                             ",
    "            888                         ",
    " d888b  d88 888                         ",
    "d888888888P 888   .d88b.  888d888 88888b.d88b.  ",
    "88P  Y888P  888  d8P  Y8b 888P\"   888 \"888 \"88b ",
    "            888  88888888 888     888  888  888 ",
    "            888  Y8b.     888     888  888  888 ",
    "            888   \"Y8888  888     888  888  888 "
};

static void setCursorPosition(short x, short y) {
    COORD pos = { x, y };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
}

void showHelp() {
    std::cout <<
        "TildeTerm built-in commands:\n"
        "\n"
        "  help                show this help\n"
        "  exit                exit tildeterm\n"
        "  c                   clear screen\n"
        "\n"
        "  cd <dir>             change directory\n"
        "  pwd                 print current directory\n"
        "  di    \033[1;33m[-s,-sh]\033[0m      print current disk info\n"
        "  which               locate the file location\n"
        "  ls    \033[1;33m[-l <file>]\033[0m   list directory contents\n"
        "\n"
        "  touch <file>         create empty file\n"
        "  mkdir <dir>          create directory\n"
        "  rmdir <dir>          remove directory\n"
        "  rm <target>          remove file or directory\n"
        "  rn <target> <new>    rename file or directory\n"
        "\n"
        "  cat <file>           print file contents\n"
        "  edit <file>          open built-in editor\n"
        "\n"
        "  sysfetch             show hardware info\n"
        "  osfetch              show OS info\n"
        "  ram                  available RAM in MB\n"
        "\n"
        "  autorun              show autorun help\n"
        "  ./file [args]        run local .exe or .bat (currently hardcoded for ./, may change later)\n"
        "  command [&]          run external command (background with &)\n"
        "\n";
}


void showOSInfo() {
    ULONGLONG ms = GetTickCount64();
    ULONGLONG seconds = ms / 1000;
    ULONGLONG minutes = seconds / 60;
    ULONGLONG hours = minutes / 60;
    ULONGLONG days = hours / 24;
    seconds %= 60; minutes %= 60; hours %= 24;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    short startY = csbi.dwCursorPosition.Y;

    system("ver");
    std::cout << "Hostname\\Username: ";
    system("whoami");
    std::cout << "Processes: " << get_process_count() << std::endl;
    std::cout << "Uptime: ";
    if (days > 0) std::cout << days << "d ";
    std::cout << hours << "h " << minutes << "m " << seconds << "s" << std::endl;

    const int colors[] = {31, 33, 32, 36, 34, 35};
    const int colorCount = 6;

    for (size_t i = 0; i < logo.size(); ++i) {
        setCursorPosition(45, (short)(startY + i + 1));
        for (size_t j = 0; j < logo[i].size(); ++j) {
            int colorIndex = (i + j) % colorCount;
            std::cout << "\033[" << colors[colorIndex] << "m" << logo[i][j];
        }
        std::cout << "\033[0m";
    }

    setCursorPosition(0, (short)(startY + 10));
}


void showPCInfo() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    GlobalMemoryStatusEx(&statex);

    std::cout << "CPU: " << getCPUName() << "\n";
    std::cout << "CPU cores: " << sysInfo.dwNumberOfProcessors << "\n";
    std::cout << "RAM: " << statex.ullTotalPhys / (1024 * 1024) << " MB\n";
    std::cout << "Free RAM: " << statex.ullAvailPhys / (1024 * 1024) << " MB\n";

    IDXGIFactory* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&pFactory)))) {
        IDXGIAdapter* pAdapter = nullptr;
        if (SUCCEEDED(pFactory->EnumAdapters(0, &pAdapter)) && pAdapter != nullptr) {
            DXGI_ADAPTER_DESC desc;
            pAdapter->GetDesc(&desc);

            std::wstring gpuName(desc.Description);
            std::wcout << L"GPU: " << gpuName
                       << L" (" << desc.DedicatedVideoMemory / (1024 * 1024) << L" MB VRAM)\n";

            pAdapter->Release();
        }
        pFactory->Release();
    }
}
// пока не используемая функция конвертации wstring в string
std::string w2s(const std::wstring& w) {
    if (w.empty()) return "";
    int s = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string res(s, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &res[0], s, NULL, NULL);
    return res.c_str();
}
// пополняем коллекцию
std::string w2utf(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, NULL, NULL);
    return str.c_str();
}

std::string format_size(unsigned __int64 bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double size = (double)bytes;
    while (size >= 1024 && i < 4) {
        size /= 1024;
        i++;
    }
    char buf[64];
    sprintf(buf, "%.2f %s", size, units[i]);
    return std::string(buf);
}

void print_drive_info(bool sh, bool s) {
    wchar_t path[MAX_PATH];
    if (GetFullPathNameW(L".", MAX_PATH, path, NULL) == 0) return;

    // Берем букву именно текущего диска
    wchar_t root[] = { path[0], L':', L'\\', L'\0' };

    wchar_t volName[MAX_PATH];
    wchar_t fsName[MAX_PATH];
    DWORD serial = 0, maxLen = 0, flags = 0;

    if (GetVolumeInformationW(root, volName, MAX_PATH, &serial, &maxLen, &flags, fsName, MAX_PATH)) {
        std::cout << "Drive: " << w2s(root) << std::endl;
        std::cout << "Volume Name: " << w2s(volName) << std::endl;
        std::cout << "Filesystem: " << w2s(fsName) << std::endl;
    }

    if (sh || s) {
        ULARGE_INTEGER free, total, available;
        // Передаем root, а не сраный хардкод C:
        if (GetDiskFreeSpaceExW(root, &available, &total, &free)) {
            if (sh) {
                // Твой форматтер для -sh
                std::cout << "Total: " << format_size(total.QuadPart) << std::endl;
                std::cout << "Free:  " << format_size(available.QuadPart) << std::endl;
            } else {
                // Вывод для -s (байты)
                std::cout << "Total Bytes: " << total.QuadPart << std::endl;
                std::cout << "Free Bytes:  " << available.QuadPart << std::endl;
            }
        }
    }
}

void get_space_info(const wchar_t* root, bool human_readable) {
    ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;

    if (GetDiskFreeSpaceExW(root, &freeBytes, &totalBytes, &totalFreeBytes)) {
        if (human_readable) {
            // -sh
            std::cout << "Total: " << format_size(totalBytes.QuadPart) << std::endl;
            std::cout << "Free:  " << format_size(totalFreeBytes.QuadPart) << std::endl;
        } else {
            // -h
            std::cout << "Total Bytes: " << totalBytes.QuadPart << std::endl;
            std::cout << "Free Bytes:  " << totalFreeBytes.QuadPart << std::endl;
        }
    }
}

std::string get_prompt() {
    fs::path canon = fs::weakly_canonical(current_dir);
    fs::path home  = fs::weakly_canonical(home_dir);
    fs::path display;

    auto to_str = [](const fs::path& p) {
        auto u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    };

    std::string canon_str = to_str(canon);
    std::string home_str  = to_str(home);

    if (canon == home) {
        display = "~";
    } else if (canon_str.rfind(home_str + "\\", 0) == 0) {
        display = "~" / canon.lexically_relative(home);
    } else {
        display = canon.filename();
    }

    auto u8 = display.u8string();
    std::string dir_str(u8.begin(), u8.end());
    std::replace(dir_str.begin(), dir_str.end(), '\\', '/');

    return "\x1b[1;32m" + dir_str + "\x1b[0m" + (dir_str.empty() ? "/" : " ") + "\x1b[1m" + g_AdminStatus + "\x1b[0m ";
}

bool is_reserved_command(const std::string& cmd) {
    static const std::unordered_set<std::string> reserved = {
        "help"
    };
    return reserved.contains(cmd);
}


fs::path convert_to_real_path(const std::string& input) {
    if(input == "~") return home_dir;
    if(input.size() >= 3 && input[0]=='/' && input[2]=='/') {
        char drive = std::toupper(input[1]);
        fs::path p = std::string(1, drive) + ":/" + input.substr(3);
        return p;
    }
    return current_dir / fs::u8path(input);
}

bool is_blank(const std::string& s) {
    for (char c : s) {
        if (!isspace(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

int main() {
    // SetConsoleCtrlHandler(NULL, TRUE);
    SetConsoleCtrlHandler(CtrlCHandler, TRUE);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::string cmd;
    if (hasAutoRun(home_dir)) {
        fs::path autorun_file = home_dir / "autorun.bat";

        std::string cmdline = "\"" + autorun_file.string() + "\"";

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        if (!CreateProcessA(
                nullptr,
                const_cast<char*>(cmdline.c_str()),
                nullptr, nullptr, FALSE,
                0,
                nullptr,
                current_dir.string().c_str(),
                &si, &pi))
        {
            return 0;
        } else {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    while(true) {
        std::cout << get_prompt();
        std::getline(std::cin, cmd);
        if (std::cin.fail()) {
            std::cin.clear(); // СБРАСЫВАЕМ флаг ошибки, чтобы cin снова ожил
            continue;         // Пропускаем остаток цикла и идем на новый круг ожидания
        }
        cmd = trim(cmd);
        // if (cmd.substr(0, 5) == "sudo ") {
        //     if (!request_uac_elevation()) {
        //         std::cout << "failed to elevate privileges\n";
        //         continue;
        //     }
        //     cmd = cmd.substr(5);
        // }
        // выделяем имя команды
        std::istringstream iss(cmd);
        std::string cmd_name;
        iss >> cmd_name;
        if (is_blank(cmd)) {
            continue;
        }
        if (is_reserved_command(cmd_name) && !cmd.starts_with("./")) {
            // std::cout << cmd_name << ": disabled in tildeterm\n";
            showHelp();
            continue;
        }
        if (is_external_command(cmd_name)) {
            if (!cmd.empty() && cmd.back() == '&') {
                std::string clean_cmd = cmd.substr(0, cmd.size() - 1);

                while (!clean_cmd.empty() && isspace(clean_cmd.back())) clean_cmd.pop_back();

                run_external_bg(clean_cmd);
            } else {
                run_external(cmd);
            }
        }
        // else if (cmd == "cd") {
        //     fs::path full_path = current_dir;
        //     fs::path canon = fs::weakly_canonical(full_path);
        //     fs::path home  = fs::weakly_canonical(home_dir);
        //
        //     std::string title_path;
        //
        //     std::string canon_str = canon.string();
        //     std::string home_str  = home.string();
        //
        //     if (canon_str == home_str) {
        //         title_path = "~";
        //     }
        //     else if (canon_str.rfind(home_str + "\\", 0) == 0) {
        //         title_path = "~/" + canon_str.substr(home_str.length() + 1);
        //     }
        //     else {
        //         title_path = canon_str;
        //     }
        //     std::replace(title_path.begin(), title_path.end(), '\\', '/');
        //     // std::cout << "usage: cd directory\n";
        //     std::cout << title_path << std::endl;
        // }
        else if (cmd == "cd") {
            current_dir = home_dir;
        }
        else if (cmd == "pwd") {
            std::cout << fs::absolute(current_dir).string() << std::endl;
        }
        else if (cmd == "ram") {
            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);

            MEMORYSTATUSEX statex;
            statex.dwLength = sizeof(statex);
            GlobalMemoryStatusEx(&statex);
            std::cout << statex.ullAvailPhys / (1024 * 1024) << " MB\n";
        }
        else if (cmd.substr(0,3) == "cd ") {
            std::string arg = cmd.substr(3);
            fs::path new_path = convert_to_real_path(arg);
            if(fs::exists(new_path) && fs::is_directory(new_path))
                current_dir = fs::canonical(new_path);
            else
                std::cout << "no such directory.\n";
        }
        else if (cmd == "osfetch") {
            showOSInfo();
        }
        else if (cmd.substr(0, 5) == "mkdir") {
            std::vector<std::string> tokens = split(cmd);
            if (tokens.size() < 2) {
                std::cout << "usage: mkdir foldername\n";
            } else {
                fs::path new_dir = current_dir / fs::u8path(tokens[1]);

                if (!fs::create_directory(new_dir)) {
                    fs::path full_path = current_dir / new_dir.string();
                    fs::path canon = fs::weakly_canonical(full_path);
                    fs::path home  = fs::weakly_canonical(home_dir);

                    std::string title_path;

                    auto u8 = canon.u8string();
                    std::string canon_str(u8.begin(), u8.end());

                    auto u8h = home.u8string();
                    std::string home_str(u8h.begin(), u8h.end());

                    if (canon_str == home_str) {
                        title_path = "~";
                    }
                    else if (canon_str.rfind(home_str + "\\", 0) == 0) {
                        title_path = "~/" + canon_str.substr(home_str.length() + 1);
                    }
                    else {
                        title_path = canon_str;
                    }
                    std::replace(title_path.begin(), title_path.end(), '\\', '/');
                    std::cout << "failed to create directory: " << title_path << "\n";
                }
            }
        }
        else if (cmd.substr(0, 2) == "di") {
            std::vector<std::string> tokens = split(cmd);

            bool sh = false;
            bool s = false;

            if (tokens.size() >= 2) {
                std::string option = tokens[1];
                if (option == "-sh") {
                    sh = true;
                } else if (option == "-s") {
                    s = true;
                } else {
                    std::cout << "unknown option: " << option << "\n";
                    continue;
                }
            }

            print_drive_info(sh, s);
        }

        else if (cmd.substr(0, 5) == "rmdir") {
            std::vector<std::string> tokens = split(cmd);
            if (tokens.size() < 2) {
                std::cout << "usage: rmdir foldername\n";
            } else {
                fs::path target_dir = current_dir / fs::u8path(tokens[1]);

                if (!fs::exists(target_dir) || !fs::is_directory(target_dir)) {
                    std::cout << "directory does not exist: " << tokens[1] << "\n";
                } else {
                    std::error_code ec;
                    if (!fs::remove_all(target_dir, ec)) {
                        fs::path canon = fs::weakly_canonical(target_dir);
                        fs::path home  = fs::weakly_canonical(home_dir);

                        std::string canon_str = canon.string();
                        std::string home_str  = home.string();
                        std::string title_path;

                        if (canon_str == home_str) {
                            title_path = "~";
                        }
                        else if (canon_str.rfind(home_str + "\\", 0) == 0) {
                            title_path = "~/" + canon_str.substr(home_str.length() + 1);
                        }
                        else {
                            title_path = canon_str;
                        }

                        std::replace(title_path.begin(), title_path.end(), '\\', '/');
                        std::cout << "failed to delete directory: " << title_path
                                  << " (" << ec.message() << ")\n";
                    }
                }
            }
        }
        else if (cmd.substr(0, 5) == "touch") {
            std::vector<std::string> tokens;
            size_t pos = 6;
            while (pos < cmd.size()) {
                size_t next = cmd.find(' ', pos);
                if (next == std::string::npos) next = cmd.size();
                tokens.push_back(cmd.substr(pos, next - pos));
                pos = next + 1;
            }

            if (tokens.empty()) {
                std::cout << "usage: touch filename\n";
            } else {
                touchFile(tokens[0]);
            }
        }
        else if (cmd.substr(0, 5) == "which") {
            std::vector<std::string> tokens = split(cmd);
            if (tokens.size() < 2) {
                std::cout << "usage: which filename\n";
                continue;
            }
            wchar_t buffer[MAX_PATH];
            auto wcmd = string_to_wstring(tokens[1]);

            if (SearchPathW(nullptr, wcmd.c_str(), L".exe", MAX_PATH, buffer, nullptr)) {
                std::wstring res = buffer;
                std::replace(res.begin(), res.end(), L'\\', L'/');
                std::wcout << res << std::endl;
            }
        }
        else if (cmd.substr(0, 2) == "rm") {
            std::vector<std::string> tokens = split(cmd);
            if (tokens.size() < 2) {
                std::cout << "usage: rm target\n";
            } else {
                fs::path target = current_dir / fs::u8path(tokens[1]);

                if (!fs::exists(target)) {
                    std::cout << "target does not exist: " << tokens[1] << "\n";
                } else {
                    std::error_code ec;
                    if (fs::remove_all(target, ec) == static_cast<std::uintmax_t>(-1)) {
                        std::string p = fs::weakly_canonical(target).string();
                        std::string h = fs::weakly_canonical(home_dir).string();

                        if (p.rfind(h, 0) == 0) {
                            p = "~" + p.substr(h.length());
                        }
                        std::replace(p.begin(), p.end(), '\\', '/');

                        std::cout << "failed to delete: " << p << " (" << ec.message() << ")\n";
                    }
                }
            }
        }
        else if (cmd.substr(0, 2) == "rn") {
            std::vector<std::string> tokens = split(cmd);
            if (tokens.size() < 3) {
                std::cout << "usage: rn target new_target\n";
            } else {
                fs::path target = current_dir / fs::u8path(tokens[1]);
                fs::path destination = current_dir / fs::u8path(tokens[2]);

                if (!fs::exists(target)) {
                    std::cout << "target does not exist: " << tokens[1] << "\n";
                } else {
                    std::error_code ec;
                    fs::rename(target, destination, ec);

                    if (ec) {
                        std::string p = fs::weakly_canonical(target).string();
                        std::string h = fs::weakly_canonical(home_dir).string();

                        if (p.rfind(h, 0) == 0) {
                            p = "~" + p.substr(h.length());
                        }
                        std::replace(p.begin(), p.end(), '\\', '/');

                        std::cout << "failed to rename: " << p << " (" << ec.message() << ")\n";
                    }
                }
            }
        }
        else if (cmd=="sysfetch") {
            showPCInfo();
        }
        else if (cmd=="autorun") {
            std::cout <<
        "TildeTerm autorun tutorial:\n"
        "\n"
        "  Place a file named 'autorun.bat' in the home (~) directory.\n"
        "  You can do this by writing 'edit autorun.bat' in the ~ directory.\n"
        "\n"
        "  Commands in autorun.bat will execute automatically when TildeTerm launches.\n"
        "\n"
        "  Example autorun.bat content:\n"
        "    color 0A                 set terminal text color (green)\n"
        "    echo Welcome to TildeTerm!\n"
        "    echo Today is %DATE%!\n"
        "\n"
        "  Notes:\n"
        "    - You can execute user scripts using python or powershell.\n"
        "    - You can set the color for the terminal, but it will not work as intended.\n"
        "    - To run background commands, append '&' at the end.\n"
        "\n"
        "  Enjoy a custom startup experience with your own TildeTerm setup!\n";
        }
        else if (cmd.substr(0, 4) == "edit") {
            std::vector<std::string> tokens = split(cmd);
            if (tokens.size() < 2) {
               std::cout << "usage: edit filename\n";
            } else {
                std::string filename = tokens[1];
                fs::path full_path = current_dir / filename;

                std::ifstream in(full_path);
                if (!in) {
                    std::ofstream create(full_path);
                    if (!create) {
                        std::cerr << "cannot create file: " << filename << "\n";
                        continue;
                    }
                   std::cout << "file created: " << filename << "\n";
                }
                in.close();
                system("cls");
                openEditor(full_path.string());
            }
        }
        else if (cmd.substr(0, 3) == "cat") {
            std::vector<std::string> tokens = split(cmd);
            if (tokens.size() < 2) {
                std::cout << "usage: cat filename\n";
            } else {
                std::string filename = tokens[1];
                fs::path full_path = current_dir / filename;

                std::ifstream file(full_path);
                if (fs::exists(full_path) && fs::is_regular_file(full_path)) {
                    auto filesize = fs::file_size(full_path);
                    if (filesize > 8192 * 8192) {
                        std::cout << "error: file too large\n";
                        continue;
                    }
                    if (filesize == 0) {
                        std::cout << "error: file is empty\n";
                        continue;
                    }
                }
                // else if (fs::exists(full_path) && fs::is_regular_file(full_path)) {
                //     auto filesize = fs::file_size(full_path);
                //     if (filesize == 0) {
                //         std::cout << "error: nothing to show\n";
                //         continue;
                //     }
                // }
                if (!file.is_open()) {
                    std::cerr << "cat: " << filename << ": no such file or directory\n";
                } else {
                    std::cout << file.rdbuf() << "";
                    file.close();
                }
            }
        }
        else if (cmd == "c") {
            system("cls");
        }
        else if (cmd.substr(0, 2) == "./") {
            std::vector<std::string> tokens = split(cmd);
            if (tokens.empty()) continue;

            fs::path full_path = current_dir / tokens[0].substr(2);
            std::string cmdline = "\"" + full_path.string() + "\"";
            for (size_t i = 1; i < tokens.size(); i++)
                cmdline += " \"" + tokens[i] + "\""; // кавычки для аргументов с пробелами

            STARTUPINFOA si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};

            // рабочая директория - папка, где лежит файл
            std::string work_dir = full_path.parent_path().string();

            if (!CreateProcessA(
                    nullptr,
                    cmdline.data(),   // ANSI функция принимает char*
                    nullptr,
                    nullptr,
                    FALSE,
                    0,
                    nullptr,          // здесь можно оставить nullptr для environment
                    work_dir.c_str(), // рабочая директория
                    &si,
                    &pi))
            {
                std::cout << "failed to start process.\n";
            }
            else {
                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
        else if (cmd.substr(0, 2) == "ls") {
            std::vector<std::string> tokens = split(cmd);

            if(tokens.size() >= 2 && tokens[1][0] == '-') { // есть опция
                std::string option = tokens[1];
                std::string filename;

                if(option == "-l") {
                    if(tokens.size() == 3) {
                        filename = tokens[2];
                    } else {
                        std::cout << "usage: ls -l filename\n";
                        continue;
                    }

                    fs::path full_path = current_dir / filename;
                    if(!fs::exists(full_path)) {
                        std::cout << "no such file: " << filename << "\n";
                        continue;
                    }

                    auto fstatus = fs::status(full_path);
                    std::string type = fs::is_directory(fstatus) ? "d" : "";
                    auto fsize = fs::is_regular_file(fstatus) ? fs::file_size(full_path) : 0;
                    auto ftime = fs::last_write_time(full_path);
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                    );
                    std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);

                    std::cout << type << "" << fsize << " bytes\n"
                              << std::asctime(std::localtime(&cftime))
                              << filename << "\n";
                } else {
                    std::cout << "unknown option: " << option << "\n";
                }
            } else { // обычный ls
                for(auto& p : fs::directory_iterator(current_dir)) {
                    std::string name = local_to_utf8(p.path().filename().string());
                    CONSOLE_SCREEN_BUFFER_INFO csbi;
                    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
                    WORD saved_color = csbi.wAttributes;

                    if(fs::is_directory(p.status()))
                        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                    else
                        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);

                    std::cout << name << " ";

                    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), saved_color);
                }
                std::cout << "\n";
            }
        }
        else if(cmd == "exit") {
            break;
        }
        else {
            std::cout << cmd << ": command not found\n";
        }
        // return 0;
    }
}
