#include "FS.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "../StdLib.h"

namespace StdLib
{
namespace FS
{

static void freeFile(void *nativeData)
{
    std::fstream *file = static_cast<std::fstream *>(nativeData);
    if (file)
    {
        if (file->is_open())
            file->close();
        delete file;
    }
}

static VMValue fileOpen(int argCount, VMValue *args)
{
    if (argCount < 1 || argCount > 2 || !args[0].isString())
        return nullptr;
    std::string path = args[0].asString()->flatten();
    std::string mode = "r";
    if (argCount == 2 && args[1].isString())
    {
        mode = args[1].asString()->flatten();
    }

    std::ios_base::openmode fmode = std::ios_base::in;
    if (mode == "w")
        fmode = std::ios_base::out;
    else if (mode == "a")
        fmode = std::ios_base::app;
    else if (mode == "rw")
        fmode = std::ios_base::in | std::ios_base::out;

    std::fstream *file = new std::fstream(path, fmode);
    if (!file->is_open())
    {
        delete file;
        return makeResultErr(currentVM, "Failed to open file: " + path);
    }

    auto fileClass = currentVM->globals["File"].asClass();
    auto instance = new ObjInstance(fileClass);
    instance->nativeData = file;
    instance->freeFn = freeFile;

    return makeResultOk(currentVM, instance);
}

static VMValue fileRead(int argCount, VMValue *args)
{
    if (argCount != 0)
        return nullptr;
    // The implicit "this" receiver is at args[-1] because the VM pushes receiver
    // before args
    VMValue receiver = args[-1];
    if (!receiver.isInstance())
        return nullptr;

    auto instance = receiver.asInstance();
    std::fstream *file = static_cast<std::fstream *>(instance->nativeData);
    if (!file || !file->is_open())
        return makeResultErr(currentVM, "File is not open");

    std::stringstream buffer;
    buffer << file->rdbuf();
    return makeResultOk(currentVM, buffer.str());
}

static VMValue fileWrite(int argCount, VMValue *args)
{
    if (argCount != 1 || !args[0].isString())
        return false;
    VMValue receiver = args[-1];
    if (!receiver.isInstance())
        return false;

    auto instance = receiver.asInstance();
    std::fstream *file = static_cast<std::fstream *>(instance->nativeData);
    if (!file || !file->is_open())
        return makeResultErr(currentVM, "File is not open");

    std::string content = args[0].asString()->flatten();
    *file << content;
    return makeResultOk(currentVM, true);
}

static VMValue fileClose(int argCount, VMValue *args)
{
    if (argCount != 0)
        return false;
    VMValue receiver = args[-1];
    if (!receiver.isInstance())
        return false;

    auto instance = receiver.asInstance();
    std::fstream *file = static_cast<std::fstream *>(instance->nativeData);
    if (file && file->is_open())
    {
        file->close();
    }
    return makeResultOk(currentVM, true);
}

static VMValue fileExists(int argCount, VMValue *args)
{
    if (argCount != 1 || !args[0].isString())
        return false;
    std::string path = args[0].asString()->flatten();
    return std::filesystem::exists(path);
}

static VMValue fileRemove(int argCount, VMValue *args)
{
    if (argCount != 1 || !args[0].isString())
        return false;
    std::string path = args[0].asString()->flatten();
    if (std::filesystem::exists(path))
    {
        return std::filesystem::remove(path);
    }
    return false;
}

void registerAll(VM *vm)
{
    currentVM = vm;
    auto fileClass = new ObjClass("File");
    fileClass->statics["open"] = new ObjNative("open", -1, fileOpen);
    fileClass->statics["exists"] = new ObjNative("exists", 1, fileExists);
    fileClass->statics["remove"] = new ObjNative("remove", 1, fileRemove);

    fileClass->methods["read"] = new ObjNative("read", 0, fileRead);
    fileClass->methods["write"] = new ObjNative("write", 1, fileWrite);
    fileClass->methods["close"] = new ObjNative("close", 0, fileClose);

    vm->globals["File"] = fileClass;
}

void registerSymbols(SymbolTable *scope)
{
    Symbol sym;
    sym.name = "File";
    sym.type = "class";
    sym.isConst = true;
    scope->define(sym);
}

} // namespace FS
} // namespace StdLib
