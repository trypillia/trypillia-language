#include "Regex.h"

#include <regex>

namespace StdLib
{
namespace RegexModule
{

static VMValue regexTest(int argCount, VMValue *args)
{
    if (argCount != 2 || !args[0].isString() || !args[1].isString())
    {
        return nullptr;
    }
    std::string pattern = args[0].asString()->flatten();
    std::string text = args[1].asString()->flatten();

    try
    {
        std::regex re(pattern);
        return std::regex_search(text, re);
    }
    catch (const std::regex_error &)
    {
        return nullptr;
    }
}

static VMValue regexMatch(int argCount, VMValue *args)
{
    if (argCount != 2 || !args[0].isString() || !args[1].isString())
    {
        return nullptr;
    }
    std::string pattern = args[0].asString()->flatten();
    std::string text = args[1].asString()->flatten();

    try
    {
        std::regex re(pattern);
        std::smatch match;
        if (std::regex_search(text, match, re))
        {
            std::vector<VMValue> results;
            for (size_t i = 0; i < match.size(); ++i)
            {
                results.push_back(match.str(i));
            }
            return new ObjList(results);
        }
    }
    catch (const std::regex_error &)
    {
        return nullptr;
    }
    return nullptr;
}

static VMValue regexReplace(int argCount, VMValue *args)
{
    if (argCount != 3 || !args[0].isString() || !args[1].isString() || !args[2].isString())
    {
        return nullptr;
    }
    std::string pattern = args[0].asString()->flatten();
    std::string text = args[1].asString()->flatten();
    std::string replacement = args[2].asString()->flatten();

    try
    {
        std::regex re(pattern);
        return std::regex_replace(text, re, replacement);
    }
    catch (const std::regex_error &)
    {
        return nullptr;
    }
}

void registerAll(VM *vm)
{
    currentVM = vm;
    auto regexClass = new ObjClass("Regex");

    regexClass->statics["test"] = new ObjNative("test", 2, regexTest);
    regexClass->statics["match"] = new ObjNative("match", 2, regexMatch);
    regexClass->statics["replace"] = new ObjNative("replace", 3, regexReplace);

    vm->globals["Regex"] = regexClass;
}

void registerSymbols(SymbolTable *scope)
{
    Symbol sym;
    sym.name = "Regex";
    sym.type = "class";
    sym.isConst = true;
    scope->define(sym);
}

} // namespace RegexModule
} // namespace StdLib
