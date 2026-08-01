#include "List.h"

#include <algorithm>
#include <string>

namespace StdLib {
namespace ListModule {

static VMValue listLength(int argCount, VMValue* args) {
  if (argCount != 1 || !args[0].isList()) return nullptr;
  return (double)args[0].asList()->elements.size();
}

static VMValue listPush(int argCount, VMValue* args) {
  if (argCount != 2 || !args[0].isList()) return nullptr;
  auto list = args[0].asList();
  list->elements.push_back(args[1]);
  return args[0];
}

static VMValue listPop(int argCount, VMValue* args) {
  if (argCount != 1 || !args[0].isList()) return nullptr;
  auto list = args[0].asList();
  if (list->elements.empty()) return nullptr;
  VMValue last = list->elements.back();
  list->elements.pop_back();
  return last;
}

static VMValue listInsert(int argCount, VMValue* args) {
  if (argCount != 3 || !args[0].isList() || !args[1].isNumber()) return nullptr;
  auto list = args[0].asList();
  int index = args[1].asNumber();
  if (index < 0 || index > list->elements.size()) return nullptr;

  list->elements.insert(list->elements.begin() + index, args[2]);
  return args[0];
}

static VMValue listRemove(int argCount, VMValue* args) {
  if (argCount != 2 || !args[0].isList() || !args[1].isNumber()) return nullptr;
  auto list = args[0].asList();
  int index = args[1].asNumber();
  if (index < 0 || index >= list->elements.size()) return nullptr;

  VMValue removed = list->elements[index];
  list->elements.erase(list->elements.begin() + index);
  return removed;
}

static VMValue listReverse(int argCount, VMValue* args) {
  if (argCount != 1 || !args[0].isList()) return nullptr;
  auto list = args[0].asList();
  std::reverse(list->elements.begin(), list->elements.end());
  return args[0];
}

static VMValue listJoin(int argCount, VMValue* args) {
  if (argCount != 2 || !args[0].isList() || !args[1].isString()) return nullptr;
  auto list = args[0].asList();
  std::string delim = args[1].asString()->flatten();

  std::string result = "";
  for (size_t i = 0; i < list->elements.size(); i++) {
    if (list->elements[i].isString()) {
      result += list->elements[i].asString()->flatten();
    } else if (list->elements[i].isNumber()) {
      result += std::to_string(list->elements[i].asNumber());  // Basic cast
    }
    // More types can be converted...

    if (i < list->elements.size() - 1) {
      result += delim;
    }
  }
  return result;
}

static VMValue listMap(int argCount, VMValue* args) {
  if (argCount != 2 || !args[0].isList()) return nullptr;
  auto list = args[0].asList();
  VMValue closure = args[1];

  std::vector<VMValue> result;
  result.reserve(list->elements.size());

  for (size_t i = 0; i < list->elements.size(); i++) {
    VMValue arg = list->elements[i];
    VMValue res = currentVM->callClosure(closure, 1, &arg);
    result.push_back(res);
  }
  return new ObjList(result);
}

static VMValue listFilter(int argCount, VMValue* args) {
  if (argCount != 2 || !args[0].isList()) return nullptr;
  auto list = args[0].asList();
  VMValue closure = args[1];

  std::vector<VMValue> result;

  for (size_t i = 0; i < list->elements.size(); i++) {
    VMValue arg = list->elements[i];
    VMValue res = currentVM->callClosure(closure, 1, &arg);
    bool isTruthy = false;
    if (res.isBool()) {
      isTruthy = res.asBool();
    } else if (!res.isNil()) {
      isTruthy = true;
    }
    if (isTruthy) {
      result.push_back(arg);
    }
  }
  return new ObjList(result);
}

static VMValue listForEach(int argCount, VMValue* args) {
  if (argCount != 2 || !args[0].isList()) return nullptr;
  auto list = args[0].asList();
  VMValue closure = args[1];

  for (size_t i = 0; i < list->elements.size(); i++) {
    VMValue arg = list->elements[i];
    currentVM->callClosure(closure, 1, &arg);
  }
  return nullptr;
}

void registerAll(VM* vm) {
  currentVM = vm;
  auto listClass = new ObjClass("List");

  listClass->statics["length"] = new ObjNative("length", 1, listLength);
  listClass->statics["push"] = new ObjNative("push", 2, listPush);
  listClass->statics["pop"] = new ObjNative("pop", 1, listPop);
  listClass->statics["insert"] = new ObjNative("insert", 3, listInsert);
  listClass->statics["remove"] = new ObjNative("remove", 2, listRemove);
  listClass->statics["reverse"] = new ObjNative("reverse", 1, listReverse);
  listClass->statics["join"] = new ObjNative("join", 2, listJoin);
  listClass->statics["map"] = new ObjNative("map", 2, listMap);
  listClass->statics["filter"] = new ObjNative("filter", 2, listFilter);
  listClass->statics["forEach"] = new ObjNative("forEach", 2, listForEach);

  vm->globals["List"] = listClass;
}

void registerSymbols(SymbolTable* scope) {
  Symbol sym;
  sym.name = "List";
  sym.type = "class";
  sym.isConst = true;
  scope->define(sym);
}
}  // namespace ListModule
}  // namespace StdLib
