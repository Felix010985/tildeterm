#include "edit.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>
#include <conio.h>
#include <windows.h>
#include "globals.h"

// fs::path home_dir = std::filesystem::path(std::getenv("USERPROFILE"));
// fs::path current_dir = home_dir;

static void setCursorPosition(short x, short y) {
    COORD pos = { x, y };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
}

void drawTitleBar(const std::string& filename) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;

    // SetConsoleTextAttribute(hConsole,
    // BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    SetConsoleTextAttribute(hConsole,
    BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | 0);

    std::string line(width, ' ');

    std::string title = " " + filename + " ";
    if (title.size() < line.size())
        line.replace((line.size() - title.size()) / 2, title.size(), title);

    std::cout << line << "\n";

    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}


void openEditor(const std::string& filename) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    std::vector<std::string> lines;
    std::ifstream in(filename);
    std::string temp;
    while (std::getline(in, temp)) lines.push_back(temp);
    in.close();
    if (lines.empty()) lines.push_back("");

    size_t cursorX = 0;
    size_t cursorY = 0;
    bool running = true;

    auto saveFile = [&]() {
        std::ofstream out(filename);
        if (!out) {
            std::cerr << "\ncannot save file.\n";
            return;
        }
        for (auto& l : lines) out << l << "\n";
    };

    auto draw = [&]() {
        std::cout << "\033[?25l\033[H";
        fs::path full_path = current_dir / filename;
        fs::path canon = fs::weakly_canonical(full_path);
        fs::path home  = fs::weakly_canonical(home_dir);

        std::string title_path;

        std::string canon_str = canon.string();
        std::string home_str  = home.string();

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

        drawTitleBar(title_path);
        for (auto& line : lines) {
            std::cout << line << "\033[K\n";
        }
        setCursorPosition(0, 20);
        std::cout << "Ctrl+S: Save | Ctrl+Q: Quit       ";

        setCursorPosition((short)cursorX, (short)(cursorY + 1));
        std::cout << "\033[?25h" << std::flush;
    };

    draw();

    while (running) {
        int c = _getch();

        if (c == 224 || c == 0) {
            int arrow = _getch();
            switch(arrow) {
                case 72: // ↑
                    if (cursorY > 0) cursorY--;
                    if (cursorX > lines[cursorY].size()) cursorX = lines[cursorY].size();
                    break;
                case 80: // ↓
                    if (cursorY + 1 < lines.size()) cursorY++;
                    if (cursorX > lines[cursorY].size()) cursorX = lines[cursorY].size();
                    break;
                case 75: // ←
                    if (cursorX > 0) cursorX--;
                    else if (cursorY > 0) {
                        cursorY--;
                        cursorX = lines[cursorY].size();
                    }
                    break;
                case 77: // →
                    if (cursorX < lines[cursorY].size()) cursorX++;
                    else if (cursorY + 1 < lines.size()) {
                        cursorY++;
                        cursorX = 0;
                    }
                    break;
            }
        }

        else if (c == 17) {
            running = false;
        }
        else if (c == 19) {
            saveFile();
        }
        else if (c == 8) {
            if (cursorX > 0) {
                lines[cursorY].erase(cursorX - 1, 1);
                cursorX--;
            } else if (cursorY > 0) {
                cursorX = lines[cursorY - 1].size();
                lines[cursorY - 1] += lines[cursorY];
                lines.erase(lines.begin() + cursorY);
                cursorY--;
            }
        }
        else if (c == 13) {
            std::string rest = lines[cursorY].substr(cursorX);
            lines[cursorY] = lines[cursorY].substr(0, cursorX);
            lines.insert(lines.begin() + cursorY + 1, rest);
            cursorY++;
            cursorX = 0;
        }
        else if (c >= 32 && c <= 126) {
            lines[cursorY].insert(cursorX, 1, (char)c);
            cursorX++;
        }

        if (cursorY >= lines.size()) cursorY = lines.size() - 1;
        if (cursorX > lines[cursorY].size()) cursorX = lines[cursorY].size();

        draw();
    }

    system("cls");
    std::cout << "quit.\n";
}