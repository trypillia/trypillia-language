#include "Terminal.h"

#include <signal.h>

#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace StdLib {
namespace TerminalModule {

#ifdef _WIN32
static DWORD orig_in, orig_out;
#else
static struct termios orig_termios;
#endif
static bool inRaw = false;

static void restore() {
  if (!inRaw) return;
#ifdef _WIN32
  SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), orig_in);
  SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), orig_out);
#else
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
#endif
  inRaw = false;
}

static VMValue terminalEnableRawMode(int argCount, VMValue* args) {
  if (inRaw) return nullptr;
#ifdef _WIN32
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  GetConsoleMode(hIn, &orig_in);
  GetConsoleMode(hOut, &orig_out);
  SetConsoleMode(hIn, orig_in & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));
  SetConsoleMode(hOut, orig_out | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
  tcgetattr(STDIN_FILENO, &orig_termios);
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
  inRaw = true;
  atexit(restore);
  return nullptr;
}

static VMValue terminalReadChar(int argCount, VMValue* args) {
#ifdef _WIN32
  INPUT_RECORD ir;
  DWORD n;
  while (ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &ir, 1, &n) &&
         n > 0) {
    if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
      char c = ir.Event.KeyEvent.uChar.AsciiChar;
      if (c) return std::string(1, c);
    }
  }
#else
  char c;
  if (read(STDIN_FILENO, &c, 1) == 1) return std::string(1, c);
#endif
  return nullptr;
}

static VMValue terminalWrite(int argCount, VMValue* args) {
  if (argCount == 1 && args[0].isString()) {
    std::cout << args[0].asString()->flatten() << std::flush;
  }
  return nullptr;
}

static VMValue terminalColor(int argCount, VMValue* args) {
  if (argCount == 1 && args[0].isString()) {
    std::string color = args[0].asString()->flatten();
    std::string code = "";
    if (color == "black")
      code = "\033[30m";
    else if (color == "red")
      code = "\033[31m";
    else if (color == "green")
      code = "\033[32m";
    else if (color == "yellow")
      code = "\033[33m";
    else if (color == "blue")
      code = "\033[34m";
    else if (color == "magenta")
      code = "\033[35m";
    else if (color == "cyan")
      code = "\033[36m";
    else if (color == "white")
      code = "\033[37m";
    else if (color == "gray" || color == "grey")
      code = "\033[90m";

    if (!code.empty()) {
      std::cout << code << std::flush;
    }
  }
  return nullptr;
}

static VMValue terminalReset(int argCount, VMValue* args) {
  std::cout << "\033[0m" << std::flush;
  return nullptr;
}

static VMValue terminalClear(int argCount, VMValue* args) {
  std::cout << "\033[2J\033[H" << std::flush;
  return nullptr;
}

static VMValue terminalSetCursor(int argCount, VMValue* args) {
  if (argCount == 2 && args[0].isNumber() && args[1].isNumber()) {
    int x = static_cast<int>(args[0].asNumber());
    int y = static_cast<int>(args[1].asNumber());
    std::cout << "\033[" << y << ";" << x << "H" << std::flush;
  }
  return nullptr;
}

static VMValue terminalGetCursor(int argCount, VMValue* args) {
  auto list = new ObjList(std::vector<VMValue>{});
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    list->elements.push_back(static_cast<double>(csbi.dwCursorPosition.X + 1));
    list->elements.push_back(static_cast<double>(csbi.dwCursorPosition.Y + 1));
  }
#else
  struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  std::cout << "\033[6n" << std::flush;
  std::string response;
  char c;
  while (read(STDIN_FILENO, &c, 1) == 1) {
    if (c == 'R') break;
    response += c;
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

  int y = 1, x = 1;
  size_t bracket = response.find('[');
  size_t semicolon = response.find(';');
  if (bracket != std::string::npos && semicolon != std::string::npos) {
    y = std::stoi(response.substr(bracket + 1, semicolon - bracket - 1));
    x = std::stoi(response.substr(semicolon + 1));
  }
  list->elements.push_back(static_cast<double>(x));
  list->elements.push_back(static_cast<double>(y));
#endif
  return list;
}

void registerAll(VM* vm) {
  currentVM = vm;
  auto cls = new ObjClass("Terminal");
  cls->statics["enableRawMode"] =
      new ObjNative("enableRawMode", 0, terminalEnableRawMode);
  cls->statics["disableRawMode"] =
      new ObjNative("disableRawMode", 0, [](int, VMValue*) {
        restore();
        return (VMValue) nullptr;
      });
  cls->statics["readChar"] = new ObjNative("readChar", 0, terminalReadChar);
  cls->statics["write"] = new ObjNative("write", 1, terminalWrite);
  cls->statics["color"] = new ObjNative("color", 1, terminalColor);
  cls->statics["reset"] = new ObjNative("reset", 0, terminalReset);
  cls->statics["clear"] = new ObjNative("clear", 0, terminalClear);
  cls->statics["setCursor"] = new ObjNative("setCursor", 2, terminalSetCursor);
  cls->statics["getCursor"] = new ObjNative("getCursor", 0, terminalGetCursor);
  vm->globals["Terminal"] = cls;
}

void registerSymbols(SymbolTable* scope) {
  Symbol s;
  s.name = "Terminal";
  s.type = "class";
  s.isConst = true;
  scope->define(s);
}
}  // namespace TerminalModule
}  // namespace StdLib
