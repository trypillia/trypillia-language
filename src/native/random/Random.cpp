#include "Random.h"
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace StdLib {
namespace RandomModule {

// Use a thread-local random engine for thread safety and performance
static std::mt19937 &getEngine() {
  static thread_local std::random_device rd;
  static thread_local std::mt19937 engine(rd());
  return engine;
}

static VMValue randomInt(int argCount, VMValue *args) {
  if (argCount != 2 || !args[0].isNumber() || !args[1].isNumber())
    return nullptr;
  int min = static_cast<int>(args[0].asNumber());
  int max = static_cast<int>(args[1].asNumber());

  if (min > max) {
    int temp = min;
    min = max;
    max = temp;
  }

  std::uniform_int_distribution<int> dist(min, max);
  return static_cast<double>(dist(getEngine()));
}

static VMValue randomFloat(int argCount, VMValue *args) {
  if (argCount != 2 || !args[0].isNumber() || !args[1].isNumber())
    return nullptr;
  double min = args[0].asNumber();
  double max = args[1].asNumber();

  if (min > max) {
    double temp = min;
    min = max;
    max = temp;
  }

  std::uniform_real_distribution<double> dist(min, max);
  return dist(getEngine());
}

static VMValue randomChoice(int argCount, VMValue *args) {
  if (argCount != 1 || !args[0].isList())
    return nullptr;
  auto list = args[0].asList();

  if (list->elements.empty())
    return nullptr;

  std::uniform_int_distribution<int> dist(
      0, static_cast<int>(list->elements.size() - 1));
  return list->elements[static_cast<size_t>(dist(getEngine()))];
}

static VMValue randomUuid(int argCount, VMValue *args) {
  std::uniform_int_distribution<int> hexDist(0, 15);
  std::uniform_int_distribution<int> variantDist(8, 11);

  const char *hexChars = "0123456789abcdef";
  std::string uuid = "00000000-0000-4000-0000-000000000000";

  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      // uuid[i] is already '-'
      continue;
    } else if (i == 14) {
      // uuid[i] is already '4' (Version 4)
      continue;
    } else if (i == 19) {
      // variant must be 8, 9, a, or b
      uuid[i] = hexChars[variantDist(getEngine())];
    } else {
      uuid[i] = hexChars[hexDist(getEngine())];
    }
  }

  return uuid;
}

void registerAll(VM *vm) {
  currentVM = vm;
  auto randomClass = new ObjClass("Random");

  randomClass->statics["int"] = new ObjNative("int", 2, randomInt);
  randomClass->statics["float"] = new ObjNative("float", 2, randomFloat);
  randomClass->statics["choice"] = new ObjNative("choice", 1, randomChoice);
  randomClass->statics["uuid"] = new ObjNative("uuid", 0, randomUuid);

  vm->globals["Random"] = randomClass;
}

void registerSymbols(SymbolTable *scope) {
  Symbol sym;
  sym.name = "Random";
  sym.type = "class";
  sym.isConst = true;
  scope->define(sym);
}
} // namespace RandomModule
} // namespace StdLib
