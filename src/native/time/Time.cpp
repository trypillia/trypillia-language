#include "Time.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

namespace StdLib {
namespace TimeModule {

static VMValue timeNow(int argCount, VMValue *args) {
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  double seconds =
      std::chrono::duration_cast<std::chrono::duration<double>>(duration)
          .count();
  return seconds;
}

static VMValue timeSleep(int argCount, VMValue *args) {
  if (argCount != 1 || !args[0].isNumber())
    return nullptr;
  double ms = args[0].asNumber();
  std::this_thread::sleep_for(
      std::chrono::milliseconds(static_cast<long long>(ms)));
  return nullptr;
}

static VMValue timeFormat(int argCount, VMValue *args) {
  if (argCount != 2 || !args[0].isNumber() || !args[1].isString())
    return nullptr;
  double timestamp = args[0].asNumber();
  std::string format = args[1].asString()->flatten();

  std::time_t time = static_cast<std::time_t>(timestamp);
  std::tm *tm_info = std::localtime(&time);

  std::ostringstream ss;
  ss << std::put_time(tm_info, format.c_str());
  return ss.str();
}

void registerAll(VM *vm) {
  currentVM = vm;
  auto timeClass = new ObjClass("Time");

  timeClass->statics["now"] = new ObjNative("now", 0, timeNow);
  timeClass->statics["sleep"] = new ObjNative("sleep", 1, timeSleep);
  timeClass->statics["format"] = new ObjNative("format", 2, timeFormat);

  vm->globals["Time"] = timeClass;
}

void registerSymbols(SymbolTable *scope) {
  Symbol sym;
  sym.name = "Time";
  sym.type = "class";
  sym.isConst = true;
  scope->define(sym);
}
} // namespace TimeModule
} // namespace StdLib
