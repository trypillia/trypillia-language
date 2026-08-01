#include "String.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace StdLib {
namespace StringModule {

static VMValue stringLength(int argCount, VMValue *args) {
  if (argCount != 1 || !args[0].isString())
    return nullptr;
  return static_cast<double>(args[0].asString()->flatten().length());
}

static VMValue stringSubstring(int argCount, VMValue *args) {
  if (argCount != 3 || !args[0].isString() || !args[1].isNumber() ||
      !args[2].isNumber())
    return nullptr;

  std::string str = args[0].asString()->flatten();
  int start = static_cast<int>(args[1].asNumber());
  int length = static_cast<int>(args[2].asNumber());

  if (start < 0 || start >= static_cast<int>(str.length()))
    return std::string("");
  return str.substr(static_cast<size_t>(start), static_cast<size_t>(length));
}

static VMValue stringToUpper(int argCount, VMValue *args) {
  if (argCount != 1 || !args[0].isString())
    return nullptr;
  std::string str = args[0].asString()->flatten();
  for (char &c : str)
    c = std::toupper(c);
  return str;
}

static VMValue stringToLower(int argCount, VMValue *args) {
  if (argCount != 1 || !args[0].isString())
    return nullptr;
  std::string str = args[0].asString()->flatten();
  for (char &c : str)
    c = std::tolower(c);
  return str;
}

static VMValue stringTrim(int argCount, VMValue *args) {
  if (argCount != 1 || !args[0].isString())
    return nullptr;
  std::string str = args[0].asString()->flatten();

  auto start = str.begin();
  while (start != str.end() && std::isspace(*start)) {
    start++;
  }

  auto end = str.end();
  do {
    end--;
  } while (std::distance(start, end) > 0 && std::isspace(*end));

  return std::string(start, end + 1);
}

static VMValue stringSplit(int argCount, VMValue *args) {
  if (argCount != 2 || !args[0].isString() || !args[1].isString())
    return nullptr;

  std::string str = args[0].asString()->flatten();
  std::string delim = args[1].asString()->flatten();

  auto list = new ObjList(std::vector<VMValue>{});
  size_t start = 0;
  size_t end = str.find(delim);

  while (end != std::string::npos) {
    list->elements.push_back(str.substr(start, end - start));
    start = end + delim.length();
    end = str.find(delim, start);
  }
  list->elements.push_back(str.substr(start));

  return list;
}

static VMValue stringReplace(int argCount, VMValue *args) {
  if (argCount != 3 || !args[0].isString() || !args[1].isString() ||
      !args[2].isString())
    return nullptr;

  std::string str = args[0].asString()->flatten();
  std::string search = args[1].asString()->flatten();
  std::string replace = args[2].asString()->flatten();

  if (search.empty())
    return str;

  size_t pos = 0;
  while ((pos = str.find(search, pos)) != std::string::npos) {
    str.replace(pos, search.length(), replace);
    pos += replace.length();
  }

  return str;
}

static VMValue stringIndexOf(int argCount, VMValue *args) {
  if (argCount != 2 || !args[0].isString() || !args[1].isString())
    return nullptr;
  std::string str = args[0].asString()->flatten();
  std::string search = args[1].asString()->flatten();

  size_t pos = str.find(search);
  if (pos == std::string::npos)
    return (double)-1;
  return (double)pos;
}

static VMValue stringIncludes(int argCount, VMValue *args) {
  if (argCount != 2 || !args[0].isString() || !args[1].isString())
    return nullptr;
  std::string str = args[0].asString()->flatten();
  std::string search = args[1].asString()->flatten();

  return str.find(search) != std::string::npos;
}

static VMValue stringStartsWith(int argCount, VMValue *args) {
  if (argCount != 2 || !args[0].isString() || !args[1].isString())
    return nullptr;
  std::string str = args[0].asString()->flatten();
  std::string prefix = args[1].asString()->flatten();

  return str.rfind(prefix, 0) == 0;
}

static VMValue stringEndsWith(int argCount, VMValue *args) {
  if (argCount != 2 || !args[0].isString() || !args[1].isString())
    return nullptr;
  std::string str = args[0].asString()->flatten();
  std::string suffix = args[1].asString()->flatten();

  if (str.length() >= suffix.length()) {
    return (0 == str.compare(str.length() - suffix.length(), suffix.length(),
                             suffix));
  } else {
    return false;
  }
}

static VMValue stringToNumber(int argCount, VMValue *args) {
  if (argCount != 1 || !args[0].isString())
    return nullptr;
  std::string str = args[0].asString()->flatten();
  try {
    return std::stod(str);
  } catch (...) {
    return nullptr;
  }
}

void registerAll(VM *vm) {
  currentVM = vm;
  auto stringClass = new ObjClass("String");

  stringClass->statics["length"] = new ObjNative("length", 1, stringLength);
  stringClass->statics["substring"] =
      new ObjNative("substring", 3, stringSubstring);
  stringClass->statics["toUpper"] = new ObjNative("toUpper", 1, stringToUpper);
  stringClass->statics["toLower"] = new ObjNative("toLower", 1, stringToLower);
  stringClass->statics["trim"] = new ObjNative("trim", 1, stringTrim);
  stringClass->statics["split"] = new ObjNative("split", 2, stringSplit);
  stringClass->statics["replace"] = new ObjNative("replace", 3, stringReplace);
  stringClass->statics["indexOf"] = new ObjNative("indexOf", 2, stringIndexOf);
  stringClass->statics["includes"] =
      new ObjNative("includes", 2, stringIncludes);
  stringClass->statics["startsWith"] =
      new ObjNative("startsWith", 2, stringStartsWith);
  stringClass->statics["endsWith"] =
      new ObjNative("endsWith", 2, stringEndsWith);
  stringClass->statics["toNumber"] =
      new ObjNative("toNumber", 1, stringToNumber);

  vm->globals["String"] = stringClass;
}

void registerSymbols(SymbolTable *scope) {
  Symbol sym;
  sym.name = "String";
  sym.type = "class";
  sym.isConst = true;
  scope->define(sym);
}
} // namespace StringModule
} // namespace StdLib
