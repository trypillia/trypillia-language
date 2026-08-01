#include "Math.h"

#include <cmath>
#include <cstdlib>

namespace StdLib {
namespace Math {

static VMValue mathSin(int argCount, VMValue* args) {
  if (argCount != 1 || !args[0].isNumber()) return nullptr;
  return std::sin(args[0].asNumber());
}

static VMValue mathCos(int argCount, VMValue* args) {
  if (argCount != 1 || !args[0].isNumber()) return nullptr;
  return std::cos(args[0].asNumber());
}

static VMValue mathRandom(int argCount, VMValue* args) {
  return (double)rand() / RAND_MAX;
}

void registerAll(VM* vm) {
  auto mathClass = new ObjClass("Math");
  mathClass->statics["sin"] = new ObjNative("sin", 1, mathSin);
  mathClass->statics["cos"] = new ObjNative("cos", 1, mathCos);
  mathClass->statics["random"] = new ObjNative("random", 0, mathRandom);
  mathClass->statics["PI"] = 3.14159265358979323846;
  vm->globals["Math"] = mathClass;
}

void registerSymbols(SymbolTable* scope) {
  Symbol sym;
  sym.name = "Math";
  sym.type = "class";
  sym.isConst = true;
  scope->define(sym);
}

}  // namespace Math
}  // namespace StdLib
