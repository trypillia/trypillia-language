#include "Core.h"

#include <time.h>

#include <cctype>
#include <iostream>

namespace StdLib
{
namespace Core
{

// currentVM defined in VM.cpp

static std::string stringify(const VMValue &val, bool inContainer = false)
{
    if (val.isNumber())
    {
        std::string s = std::to_string(val.asNumber());
        s.erase(s.find_last_not_of('0') + 1, std::string::npos);
        if (s.back() == '.')
            s.pop_back();
        if (s.empty())
            return "0";
        return s;
    }
    else if (val.isString())
    {
        if (inContainer)
        {
            return "\"" + val.asString()->flatten() + "\"";
        }
        else
        {
            return val.asString()->flatten();
        }
    }
    else if (val.isBool())
    {
        return val.asBool() ? "true" : "false";
    }
    else if (val.isList())
    {
        std::string s = "[";
        auto list = val.asList();
        for (size_t i = 0; i < list->elements.size(); ++i)
        {
            s += stringify(list->elements[i], true);
            if (i < list->elements.size() - 1)
                s += ", ";
        }
        s += "]";
        return s;
    }
    else if (val.isMap())
    {
        std::string s = "{";
        auto map = val.asMap();
        size_t i = 0;
        for (auto const &[k, v] : map->values)
        {
            s += stringify(k, true) + ": " + stringify(v, true);
            if (i < map->values.size() - 1)
                s += ", ";
            i++;
        }
        s += "}";
        return s;
    }
    else if (val.isClass())
    {
        return "<class " + val.asClass()->name + ">";
    }
    else if (val.isInstance())
    {
        return "<instance of " + val.asInstance()->klass->name + ">";
    }
    else if (val.isBoundMethod())
    {
        auto boundMethod = val.asBoundMethod()->method;
        if (boundMethod.isFunction())
        {
            return "<bound method " + boundMethod.asFunction()->name + ">";
        }
        else
        {
            return "<bound method " + boundMethod.asNative()->name + ">";
        }
    }
    else if (val.isNil())
    {
        return "nil";
    }
    return "unknown";
}

static VMValue printNative(int argCount, VMValue *args)
{
    for (int i = 0; i < argCount; i++)
    {
        std::cout << stringify(args[i]);
        if (i < argCount - 1)
            std::cout << " ";
    }
    std::cout << std::endl;
    return nullptr;
}

static VMValue inputNative(int argCount, VMValue *args)
{
    if (argCount == 1 && args[0].isString())
    {
        std::cout << args[0].asString()->flatten();
    }
    std::string line;
    std::getline(std::cin, line);
    return line;
}

// --- Error ---
static VMValue errorInit(int argCount, VMValue *args)
{
    VMValue receiver = args[-1];
    if (!receiver.isInstance())
        return nullptr;
    auto instance = receiver.asInstance();

    instance->fields["message"] = argCount > 0 ? args[0] : VMValue(std::string("Unknown Error"));
    instance->fields["code"] = argCount > 1 ? args[1] : VMValue(0.0);
    return nullptr;
}

// --- Result ---
static VMValue resultOk(int argCount, VMValue *args)
{
    if (argCount != 1)
        return nullptr;
    auto klass = currentVM->globals["Result"].asClass();
    auto instance = new ObjInstance(klass);
    instance->fields["value"] = args[0];
    instance->fields["isOk"] = true;
    return instance;
}

static VMValue resultErr(int argCount, VMValue *args)
{
    if (argCount != 1)
        return nullptr;
    auto klass = currentVM->globals["Result"].asClass();
    auto instance = new ObjInstance(klass);
    instance->fields["error"] = args[0];
    instance->fields["isOk"] = false;
    return instance;
}

static VMValue resultIsOk(int argCount, VMValue *args)
{
    VMValue receiver = args[-1];
    auto instance = receiver.asInstance();
    return instance->fields["isOk"];
}

static VMValue resultIsErr(int argCount, VMValue *args)
{
    VMValue receiver = args[-1];
    auto instance = receiver.asInstance();
    return !instance->fields["isOk"].asBool();
}

static VMValue resultUnwrap(int argCount, VMValue *args)
{
    VMValue receiver = args[-1];
    auto instance = receiver.asInstance();
    if (!instance->fields["isOk"].asBool())
    {
        std::cerr << "Called unwrap() on an Err value!" << std::endl;
        exit(1); // Panic
    }
    return instance->fields["value"];
}

static VMValue resultUnwrapErr(int argCount, VMValue *args)
{
    VMValue receiver = args[-1];
    auto instance = receiver.asInstance();
    if (instance->fields["isOk"].asBool())
    {
        std::cerr << "Called unwrapErr() on an Ok value!" << std::endl;
        exit(1); // Panic
    }
    return instance->fields["error"];
}

// --- WeakRef ---
static VMValue weakRefInit(int argCount, VMValue *args)
{
    VMValue receiver = args[-1];
    if (!receiver.isInstance())
        return nullptr;
    auto instance = receiver.asInstance();

    if (argCount != 1)
        return nullptr;

    auto weakObj = new ObjWeakRef();
    VMValue val = args[0];

    if (val.isObj())
    {
        weakObj->weakRef = val.asObj();
    }

    instance->fields["_ref"] = VMValue(weakObj);
    return nullptr;
}

static VMValue weakRefLock(int argCount, VMValue *args)
{
    VMValue receiver = args[-1];
    auto instance = receiver.asInstance();
    if (instance->fields.find("_ref") == instance->fields.end())
        return nullptr;

    auto ref = instance->fields["_ref"];
    if (ref.isWeakRef())
    {
        return ref.asWeakRef()->lock();
    }
    return nullptr;
}

void registerAll(VM *vm)
{
    currentVM = vm;
    vm->defineNative("print", -1, printNative);
    vm->defineNative("input", -1, inputNative);

    auto errorClass = new ObjClass("Error");
    errorClass->methods["init"] = new ObjNative("init", -1, errorInit);
    vm->globals["Error"] = errorClass;

    auto resultClass = new ObjClass("Result");
    resultClass->statics["ok"] = new ObjNative("ok", 1, resultOk);
    resultClass->statics["err"] = new ObjNative("err", 1, resultErr);
    resultClass->methods["isOk"] = new ObjNative("isOk", 0, resultIsOk);
    resultClass->methods["isErr"] = new ObjNative("isErr", 0, resultIsErr);
    resultClass->methods["unwrap"] = new ObjNative("unwrap", 0, resultUnwrap);
    resultClass->methods["unwrapErr"] = new ObjNative("unwrapErr", 0, resultUnwrapErr);
    vm->globals["Result"] = resultClass;

    auto weakRefClass = new ObjClass("WeakRef");
    weakRefClass->methods["init"] = new ObjNative("init", 1, weakRefInit);
    weakRefClass->methods["lock"] = new ObjNative("lock", 0, weakRefLock);
    vm->globals["WeakRef"] = weakRefClass;
}

void registerSymbols(SymbolTable *scope)
{
    auto addFunc = [&](const std::string &name) {
        Symbol sym;
        sym.name = name;
        sym.type = "function";
        sym.isConst = true;
        scope->define(sym);
    };
    addFunc("print");
    addFunc("input");

    auto addClass = [&](const std::string &name) {
        Symbol sym;
        sym.name = name;
        sym.type = "class";
        sym.isConst = true;
        scope->define(sym);
    };
    addClass("Error");
    addClass("Result");
    addClass("WeakRef");
}

} // namespace Core
} // namespace StdLib
