#include "Map.h"

namespace StdLib {
namespace MapModule {

static VMValue mapKeys(int argCount, VMValue *args) {
  if (argCount != 1 || !args[0].isMap())
    return nullptr;
  auto map = args[0].asMap();
  std::vector<VMValue> keys;
  for (const auto &pair : map->values) {
    keys.push_back(pair.first);
  }
  return new ObjList(keys);
}

static VMValue mapValues(int argCount, VMValue *args) {
  if (argCount != 1 || !args[0].isMap())
    return nullptr;
  auto map = args[0].asMap();
  std::vector<VMValue> values;
  for (const auto &pair : map->values) {
    values.push_back(pair.second);
  }
  return new ObjList(values);
}

static VMValue mapHas(int argCount, VMValue *args) {
  if (argCount != 2 || !args[0].isMap())
    return nullptr;
  auto map = args[0].asMap();
  return map->values.count(args[1]) > 0;
}

static VMValue mapRemove(int argCount, VMValue *args) {
  if (argCount != 2 || !args[0].isMap())
    return nullptr;
  auto map = args[0].asMap();
  if (map->values.count(args[1]) > 0) {
    VMValue val = map->values[args[1]];
    map->values.erase(args[1]);
    return val;
  }
  return nullptr;
}

static VMValue mapSet(int argCount, VMValue *args) {
  if (argCount != 3 || !args[0].isMap())
    return nullptr;
  auto map = args[0].asMap();
  map->values[args[1]] = args[2];
  return args[2];
}

void registerAll(VM *vm) {
  currentVM = vm;
  auto mapClass = new ObjClass("Map");

  mapClass->statics["keys"] = new ObjNative("keys", 1, mapKeys);
  mapClass->statics["values"] = new ObjNative("values", 1, mapValues);
  mapClass->statics["has"] = new ObjNative("has", 2, mapHas);
  mapClass->statics["remove"] = new ObjNative("remove", 2, mapRemove);
  mapClass->statics["set"] = new ObjNative("set", 3, mapSet);

  vm->globals["Map"] = mapClass;
}

void registerSymbols(SymbolTable *scope) {
  Symbol sym;
  sym.name = "Map";
  sym.type = "class";
  sym.isConst = true;
  scope->define(sym);
}

} // namespace MapModule
} // namespace StdLib
