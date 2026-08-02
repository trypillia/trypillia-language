// Foreign Function Interface (FFI) for Trypillia.
//
// Lets Trypillia code load a native shared library (.so/.dylib/.dll) and
// call C functions in it directly, without writing a bespoke C++ StdLib
// module for every native library the user might want to use.
//
//   let lib = FFI.open("libm.so.6").value;
//   let sqrtFn = lib.define("sqrt", "double", ["double"]).value;
//   print(sqrtFn.call(2.0).value); // 1.4142135623730951
//
// Design notes / limitations:
//  - Supported types: "void" (return only), "int32", "int64", "double",
//    "string" (const char*, copied in/out), "pointer" (opaque void*).
//  - No struct-by-value support, no varargs C functions (e.g. printf),
//    no callbacks (passing a Trypillia function as a C function pointer).
//  - Calling convention is delegated to libffi, which supports System V
//    AMD64, Win64, ARM64, and more.
//  - Argument count is no longer limited to 6.

#include "FFI.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "../StdLib.h"

#include <ffi.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace StdLib
{
namespace FFIModule
{

enum class FFIType
{
    Void,
    Int32,
    Int64,
    Double,
    String,
    Pointer
};

static bool parseType(const std::string &s, FFIType &out)
{
    if (s == "...")
        return true;
    if (s == "void")
        out = FFIType::Void;
    else if (s == "int32")
        out = FFIType::Int32;
    else if (s == "int64")
        out = FFIType::Int64;
    else if (s == "double")
        out = FFIType::Double;
    else if (s == "string")
        out = FFIType::String;
    else if (s == "pointer")
        out = FFIType::Pointer;
    else
        return false;
    return true;
}

static const char *VALID_TYPES_MSG = "void, int32, int64, double, string, pointer";

// --------------------------------------------------------------------
// Cross-platform dynamic library loading
// --------------------------------------------------------------------

static void *ffiDlOpen(const std::string &path, std::string &errOut)
{
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path.c_str());
    if (!h)
    {
        errOut = "LoadLibrary failed for '" + path + "' (error " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    return (void *)h;
#else
    dlerror(); // clear any existing error
    void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h)
    {
        const char *msg = dlerror();
        errOut = msg ? std::string(msg) : ("dlopen failed for '" + path + "'");
        return nullptr;
    }
    return h;
#endif
}

static void *ffiDlSym(void *handle, const std::string &name, std::string &errOut)
{
#if defined(_WIN32)
    FARPROC sym = GetProcAddress((HMODULE)handle, name.c_str());
    if (!sym)
    {
        errOut = "symbol not found: " + name;
        return nullptr;
    }
    return (void *)sym;
#else
    dlerror();
    void *sym = dlsym(handle, name.c_str());
    const char *err = dlerror();
    if (err)
    {
        errOut = std::string(err);
        return nullptr;
    }
    return sym;
#endif
}

static void ffiDlClose(void *handle)
{
    if (!handle)
        return;
#if defined(_WIN32)
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

// --------------------------------------------------------------------
// Native resource payloads stored in ObjInstance::nativeData
// --------------------------------------------------------------------

struct FFILibraryData
{
    void *handle;
    std::string path;
};

static void freeLibraryData(void *p)
{
    auto *data = static_cast<FFILibraryData *>(p);
    if (!data)
        return;
    if (data->handle)
        ffiDlClose(data->handle);
    delete data;
}

struct FFIFunctionData
{
    void *fnPtr;
    std::string name;
    FFIType retType;
    std::vector<FFIType> argTypes;
};

static void freeFunctionData(void *p)
{
    delete static_cast<FFIFunctionData *>(p);
}

// --------------------------------------------------------------------
// Value <-> native marshaling
// --------------------------------------------------------------------

union RawSlot {
    int64_t i;
    double d;
};

static VMValue wrapPointerResult(void *ptr)
{
    if (!ptr)
        return VMValue(nullptr);
    auto klass = currentVM->globals["FFIPointer"].asClass();
    auto instance = new ObjInstance(klass);
    instance->nativeData = ptr;
    instance->freeFn = nullptr; // unowned: the library, not Trypillia, owns this memory
    return instance;
}

static bool marshalArg(const VMValue &val, FFIType type, std::deque<std::string> &stringStorage, RawSlot &out,
                       std::string &err)
{
    switch (type)
    {
    case FFIType::Int32:
    case FFIType::Int64:
        if (!val.isNumber())
        {
            err = "expected a number";
            return false;
        }
        out.i = static_cast<int64_t>(val.asNumber());
        return true;
    case FFIType::Double:
        if (!val.isNumber())
        {
            err = "expected a number";
            return false;
        }
        out.d = val.asNumber();
        return true;
    case FFIType::String:
        if (val.isNil())
        {
            out.i = 0;
            return true;
        }
        if (!val.isString())
        {
            err = "expected a string (or nil for NULL)";
            return false;
        }
        // Kept alive in stringStorage (a deque, so pointers stay stable)
        // for the duration of the native call.
        stringStorage.push_back(val.asString()->flatten());
        out.i = reinterpret_cast<int64_t>(stringStorage.back().c_str());
        return true;
    case FFIType::Pointer:
        if (val.isNil())
        {
            out.i = 0;
            return true;
        }
        if (val.isNumber())
        {
            out.i = static_cast<int64_t>(val.asNumber());
            return true;
        }
        if (val.isInstance() && val.asInstance()->klass->name == "FFIPointer")
        {
            out.i = reinterpret_cast<int64_t>(val.asInstance()->nativeData);
            return true;
        }
        err = "expected a pointer, number, or nil";
        return false;
    case FFIType::Void:
        err = "'void' is not a valid argument type";
        return false;
    }
    err = "unknown argument type";
    return false;
}

// --------------------------------------------------------------------
// libffi-based native call
// --------------------------------------------------------------------

static VMValue callDynamic(void *fn, FFIType retType, const std::vector<FFIType> &argTypes,
                           const std::vector<RawSlot> &raw)
{
    std::vector<ffi_type *> ffiArgTypes;
    ffiArgTypes.reserve(argTypes.size());
    for (size_t i = 0; i < argTypes.size(); i++)
    {
        switch (argTypes[i])
        {
        case FFIType::Int32:
            ffiArgTypes.push_back(&ffi_type_sint32);
            break;
        case FFIType::Int64:
            ffiArgTypes.push_back(&ffi_type_sint64);
            break;
        case FFIType::Double:
            ffiArgTypes.push_back(&ffi_type_double);
            break;
        case FFIType::String:
        case FFIType::Pointer:
            ffiArgTypes.push_back(&ffi_type_pointer);
            break;
        default:
            return makeResultErr(currentVM, "unsupported argument type");
        }
    }

    ffi_type *ffiRet = &ffi_type_void;
    switch (retType)
    {
    case FFIType::Void:
        ffiRet = &ffi_type_void;
        break;
    case FFIType::Int32:
        ffiRet = &ffi_type_sint32;
        break;
    case FFIType::Int64:
        ffiRet = &ffi_type_sint64;
        break;
    case FFIType::Double:
        ffiRet = &ffi_type_double;
        break;
    case FFIType::String:
        ffiRet = &ffi_type_pointer;
        break;
    case FFIType::Pointer:
        ffiRet = &ffi_type_pointer;
        break;
    }

    ffi_cif cif;
    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, (unsigned int)argTypes.size(), ffiRet, ffiArgTypes.data());
    if (status != FFI_OK)
    {
        return makeResultErr(currentVM, "ffi_prep_cif failed");
    }

    std::vector<void *> values;
    values.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++)
    {
        values.push_back(const_cast<void *>(reinterpret_cast<const void *>(&raw[i])));
    }

    union {
        int32_t i32;
        int64_t i64;
        double d;
        void *p;
    } ret;

    ffi_call(&cif, reinterpret_cast<void (*)()>(fn), &ret, values.data());

    switch (retType)
    {
    case FFIType::Void:
        return VMValue(nullptr);
    case FFIType::Int32:
        return VMValue((double)ret.i32);
    case FFIType::Int64:
        return VMValue((double)ret.i64);
    case FFIType::Double:
        return VMValue(ret.d);
    case FFIType::String:
        return ret.p ? VMValue(std::string(static_cast<char *>(ret.p))) : VMValue(nullptr);
    case FFIType::Pointer:
        return wrapPointerResult(ret.p);
    }
    return VMValue(nullptr);
}

// --------------------------------------------------------------------
// Shared helpers for the native methods below
// --------------------------------------------------------------------

// Parses a Trypillia list-of-strings into argTypes. Returns false (and
// sets err) on any invalid entry.
static bool parseArgTypeList(const VMValue &listVal, std::vector<FFIType> &out, std::string &err)
{
    if (!listVal.isList())
    {
        err = "argument types must be a list of strings";
        return false;
    }
    auto &elements = listVal.asList()->elements;
    out.clear();
    out.reserve(elements.size());
    for (auto &el : elements)
    {
        if (!el.isString())
        {
            err = "argument types must be a list of strings";
            return false;
        }
        std::string typeStr = el.asString()->flatten();
        if (typeStr == "...")
        {
            out.push_back(FFIType::Void); // marker for varargs separator
            continue;
        }
        FFIType t;
        if (!parseType(typeStr, t) || t == FFIType::Void)
        {
            err = "invalid argument type '" + typeStr + "' (expected one of: " + VALID_TYPES_MSG +
                  ", excluding void and ...)";
            return false;
        }
        out.push_back(t);
    }
    return true;
}

// Marshals argCount values starting at args[startIdx] against argTypes,
// then performs the call. Shared by lib.call(...) and fn.call(...).
static VMValue marshalAndCall(void *fnPtr, FFIType retType, const std::vector<FFIType> &argTypes, VMValue *args,
                              int startIdx, int provided)
{
    if (provided != (int)argTypes.size())
    {
        return makeResultErr(currentVM, "expected " + std::to_string(argTypes.size()) + " argument(s) but got " +
                                            std::to_string(provided));
    }

    std::deque<std::string> stringStorage;
    std::vector<RawSlot> raw(argTypes.size());
    for (size_t i = 0; i < argTypes.size(); i++)
    {
        std::string err;
        if (!marshalArg(args[startIdx + (int)i], argTypes[i], stringStorage, raw[i], err))
        {
            return makeResultErr(currentVM, "argument " + std::to_string(i + 1) + ": " + err);
        }
    }

    VMValue result = callDynamic(fnPtr, retType, argTypes, raw);
    return makeResultOk(currentVM, result);
}

// --------------------------------------------------------------------
// FFI.load(path) -> Result<FFILibrary>
// --------------------------------------------------------------------

static VMValue ffiLoad(int argCount, VMValue *args)
{
    if (argCount != 1 || !args[0].isString())
        return makeResultErr(currentVM, "FFI.open expects a single string path");

    std::string path = args[0].asString()->flatten();
    std::string err;
    void *handle = ffiDlOpen(path, err);
    if (!handle)
        return makeResultErr(currentVM, err);

    auto *data = new FFILibraryData{handle, path};
    auto klass = currentVM->globals["FFILibrary"].asClass();
    auto instance = new ObjInstance(klass);
    instance->nativeData = data;
    instance->freeFn = freeLibraryData;
    return makeResultOk(currentVM, instance);
}

static VMValue ffiNullPointer(int argCount, VMValue *args)
{
    (void)argCount;
    (void)args;
    auto klass = currentVM->globals["FFIPointer"].asClass();
    auto instance = new ObjInstance(klass);
    instance->nativeData = nullptr;
    instance->freeFn = nullptr;
    return instance;
}

// --------------------------------------------------------------------
// FFILibrary instance methods
// --------------------------------------------------------------------

static FFILibraryData *getLibraryData(const VMValue &receiver, std::string &err)
{
    if (!receiver.isInstance())
    {
        err = "not an FFILibrary instance";
        return nullptr;
    }
    auto *data = static_cast<FFILibraryData *>(receiver.asInstance()->nativeData);
    if (!data || !data->handle)
    {
        err = "library is closed";
        return nullptr;
    }
    return data;
}

// lib.define(name, returnType, argTypes) -> Result<FFIFunction>
static VMValue libDefine(int argCount, VMValue *args)
{
    if (argCount != 3 || !args[0].isString() || !args[1].isString())
        return makeResultErr(currentVM, "define(name, returnType, argTypes) expects (string, string, list)");

    std::string err;
    auto *libData = getLibraryData(args[-1], err);
    if (!libData)
        return makeResultErr(currentVM, err);

    std::string name = args[0].asString()->flatten();
    FFIType retType;
    if (!parseType(args[1].asString()->flatten(), retType))
        return makeResultErr(currentVM, "invalid return type '" + args[1].asString()->flatten() +
                                            "' (expected one of: " + VALID_TYPES_MSG + ")");

    std::vector<FFIType> argTypes;
    if (!parseArgTypeList(args[2], argTypes, err))
        return makeResultErr(currentVM, err);

    void *fnPtr = ffiDlSym(libData->handle, name, err);
    if (!fnPtr)
        return makeResultErr(currentVM, err);

    auto *fnData = new FFIFunctionData{fnPtr, name, retType, std::move(argTypes)};
    auto klass = currentVM->globals["FFIFunction"].asClass();
    auto instance = new ObjInstance(klass);
    instance->nativeData = fnData;
    instance->freeFn = freeFunctionData;
    return makeResultOk(currentVM, instance);
}

// lib.call(name, returnType, argTypes, ...values) -> Result<value>
static VMValue libCall(int argCount, VMValue *args)
{
    if (argCount < 3 || !args[0].isString() || !args[1].isString())
        return makeResultErr(currentVM, "call(name, returnType, argTypes, ...values) expects at least "
                                        "(string, string, list)");

    std::string err;
    auto *libData = getLibraryData(args[-1], err);
    if (!libData)
        return makeResultErr(currentVM, err);

    std::string name = args[0].asString()->flatten();
    FFIType retType;
    if (!parseType(args[1].asString()->flatten(), retType))
        return makeResultErr(currentVM, "invalid return type '" + args[1].asString()->flatten() +
                                            "' (expected one of: " + VALID_TYPES_MSG + ")");

    std::vector<FFIType> argTypes;
    if (!parseArgTypeList(args[2], argTypes, err))
        return makeResultErr(currentVM, err);

    void *fnPtr = ffiDlSym(libData->handle, name, err);
    if (!fnPtr)
        return makeResultErr(currentVM, err);

    return marshalAndCall(fnPtr, retType, argTypes, args, 3, argCount - 3);
}

// lib.close() -> Result<bool>
static VMValue libClose(int argCount, VMValue *args)
{
    (void)argCount;
    if (!args[-1].isInstance())
        return makeResultErr(currentVM, "not an FFILibrary instance");
    auto instance = args[-1].asInstance();
    auto *data = static_cast<FFILibraryData *>(instance->nativeData);
    if (data && data->handle)
    {
        ffiDlClose(data->handle);
        data->handle = nullptr; // freeFn (GC time) will see this and skip re-closing
    }
    return makeResultOk(currentVM, true);
}

// --------------------------------------------------------------------
// FFIFunction instance methods
// --------------------------------------------------------------------

// fn.call(...values) -> Result<value>
static VMValue fnCall(int argCount, VMValue *args)
{
    if (!args[-1].isInstance())
        return makeResultErr(currentVM, "not an FFIFunction instance");
    auto *data = static_cast<FFIFunctionData *>(args[-1].asInstance()->nativeData);
    if (!data)
        return makeResultErr(currentVM, "function is no longer valid (its library may have been closed)");

    return marshalAndCall(data->fnPtr, data->retType, data->argTypes, args, 0, argCount);
}

// --------------------------------------------------------------------
// FFIPointer instance methods
// --------------------------------------------------------------------

static VMValue ptrIsNull(int argCount, VMValue *args)
{
    (void)argCount;
    if (!args[-1].isInstance())
        return true;
    return args[-1].asInstance()->nativeData == nullptr;
}

static VMValue ptrAddress(int argCount, VMValue *args)
{
    (void)argCount;
    if (!args[-1].isInstance())
        return VMValue(0.0);
    return VMValue((double)(uintptr_t)args[-1].asInstance()->nativeData);
}

// --------------------------------------------------------------------
// Registration
// --------------------------------------------------------------------

void registerAll(VM *vm)
{
    currentVM = vm;

    auto pointerClass = new ObjClass("FFIPointer");
    pointerClass->methods["isNull"] = new ObjNative("isNull", 0, ptrIsNull);
    pointerClass->methods["address"] = new ObjNative("address", 0, ptrAddress);
    vm->globals["FFIPointer"] = pointerClass;

    auto functionClass = new ObjClass("FFIFunction");
    functionClass->methods["call"] = new ObjNative("call", -1, fnCall);
    vm->globals["FFIFunction"] = functionClass;

    auto libraryClass = new ObjClass("FFILibrary");
    libraryClass->methods["define"] = new ObjNative("define", 3, libDefine);
    libraryClass->methods["call"] = new ObjNative("call", -1, libCall);
    libraryClass->methods["close"] = new ObjNative("close", 0, libClose);
    vm->globals["FFILibrary"] = libraryClass;

    auto ffiClass = new ObjClass("FFI");
    ffiClass->statics["open"] = new ObjNative("open", 1, ffiLoad);
    ffiClass->statics["nullPointer"] = new ObjNative("nullPointer", 0, ffiNullPointer);
    vm->globals["FFI"] = ffiClass;
}

void registerSymbols(SymbolTable *scope)
{
    auto addClass = [&](const std::string &name) {
        Symbol sym;
        sym.name = name;
        sym.type = "class";
        sym.isConst = true;
        scope->define(sym);
    };
    addClass("FFI");
    addClass("FFILibrary");
    addClass("FFIFunction");
    addClass("FFIPointer");
}

} // namespace FFIModule
} // namespace StdLib
