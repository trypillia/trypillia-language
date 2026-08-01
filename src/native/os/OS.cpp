#include "OS.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../StdLib.h"

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

namespace StdLib
{
namespace OSModule
{

std::vector<std::string> commandLineArgs;

static VMValue osGetEnv(int argCount, VMValue *args)
{
    if (argCount != 1 || !args[0].isString())
        return nullptr;
    const char *val = std::getenv(args[0].asString()->flatten().c_str());
    return val ? makeResultOk(currentVM, std::string(val)) : makeResultErr(currentVM, "Not found");
}

static VMValue osCwd(int argCount, VMValue *args)
{
    try
    {
        return makeResultOk(currentVM, std::filesystem::current_path().string());
    }
    catch (...)
    {
        return makeResultErr(currentVM, "Error");
    }
}

static VMValue osExec(int argCount, VMValue *args)
{
    if (argCount != 1 || !args[0].isString())
        return nullptr;
    std::string cmd = args[0].asString()->flatten();
    std::string result;
    std::array<char, 128> buffer;

    FILE *pipe = POPEN(cmd.c_str(), "r");
    if (!pipe)
        return makeResultErr(currentVM, "Failed");

    while (fgets(buffer.data(), buffer.size(), pipe))
        result += buffer.data();
    PCLOSE(pipe);

    return makeResultOk(currentVM, result);
}

static VMValue osExit(int argCount, VMValue *args)
{
    int code = (argCount == 1 && args[0].isNumber()) ? (int)args[0].asNumber() : 0;
    std::exit(code);
    return nullptr;
}

static VMValue osArgs(int argCount, VMValue *args)
{
    std::vector<VMValue> list;
    for (const auto &a : commandLineArgs)
        list.push_back(a);
    return new ObjList(list);
}

void registerAll(VM *vm)
{
    currentVM = vm;
    auto cls = new ObjClass("OS");
    cls->statics["getEnv"] = new ObjNative("getEnv", 1, osGetEnv);
    cls->statics["cwd"] = new ObjNative("cwd", 0, osCwd);
    cls->statics["exec"] = new ObjNative("exec", 1, osExec);
    cls->statics["exit"] = new ObjNative("exit", -1, osExit);
    cls->statics["args"] = new ObjNative("args", 0, osArgs);
    vm->globals["OS"] = cls;
}

void registerSymbols(SymbolTable *scope)
{
    Symbol sym;
    sym.name = "OS";
    sym.type = "class";
    sym.isConst = true;
    scope->define(sym);
}
} // namespace OSModule
} // namespace StdLib
