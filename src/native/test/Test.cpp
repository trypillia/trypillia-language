#include "Test.h"
#include <csignal>
#include <vector>
#include <string>

namespace StdLib {
namespace Test {

static std::string valueToString(const VMValue &val, int depth = 0) {
    if (depth > 3) {
        return "...";
    }
    if (val.isList()) {
        auto list = val.asList();
        std::string result = "[";
        for (size_t i = 0; i < list->elements.size(); i++) {
            if (i > 0) result += ", ";
            result += valueToString(list->elements[i], depth + 1);
        }
        result += "]";
        return result;
    }
    if (val.isMap()) {
        auto map = val.asMap();
        std::string result = "{";
        bool first = true;
        for (auto &[k, v] : map->values) {
            if (!first) result += ", ";
            first = false;
            result += k.toString() + ": " + valueToString(v, depth + 1);
        }
        result += "}";
        return result;
    }
    return val.toString(true);
}

static void getSourceLocation(VM *vm, std::string &filename, int &line) {
    if (!vm || vm->frames.empty())
        return;
    auto &frame = vm->frames.back();
    auto func = frame.closure->function;
    filename = func->filename;
    auto ip = frame.ip;
    auto codeStart = func->chunk->code.data();
    if (ip > codeStart) {
        size_t instruction = (ip - codeStart) - 1;
        if (instruction < func->chunk->lines.size())
            line = func->chunk->lines[instruction];
    }
}

static void failAssertion(VM *vm, const std::string &message) {
    std::cerr << message << std::endl;
    if (vm->catchJumpEnabled) {
        longjmp(vm->catchJmpBuf, 1);
    } else if (vm->assertJumpEnabled) {
        longjmp(vm->assertJmpBuf, 1);
    }
}

static void trackTestResult(VM *vm, const std::string &testName, bool passed) {
    auto countIt = vm->globals.find("__test_count");
    int count = 0;
    if (countIt != vm->globals.end() && countIt->second.isNumber()) {
        count = (int)countIt->second.asNumber();
    }
    count++;
    vm->globals["__test_count"] = VMValue((double)count);

    auto namesIt = vm->globals.find("__test_names");
    ObjList *namesList;
    if (namesIt == vm->globals.end() || !namesIt->second.isList()) {
        namesList = new ObjList({});
        vm->globals["__test_names"] = VMValue(namesList);
    } else {
        namesList = namesIt->second.asList();
    }
    namesList->elements.push_back(VMValue(testName));

    auto resultsIt = vm->globals.find("__test_results");
    ObjList *resultsList;
    if (resultsIt == vm->globals.end() || !resultsIt->second.isList()) {
        resultsList = new ObjList({});
        vm->globals["__test_results"] = VMValue(resultsList);
    } else {
        resultsList = resultsIt->second.asList();
    }
    resultsList->elements.push_back(VMValue(passed));
}

static VMValue assertNative(int argCount, VMValue *args) {
    if (argCount < 1) {
        failAssertion(currentVM, "FAIL: assert requires at least 1 argument (condition)");
        return nullptr;
    }

    VM *vm = currentVM;
    bool condition = args[0].isBool() ? args[0].asBool() : !args[0].isNil();

    if (!condition) {
        std::string filename;
        int line = 0;
        getSourceLocation(vm, filename, line);

        std::string msg;
        if (argCount >= 2 && args[1].isString()) {
            msg = " — " + args[1].asString()->flatten();
        }

        std::string loc;
        if (!filename.empty())
            loc = filename + ":" + std::to_string(line) + " ";

        failAssertion(vm, "FAIL: " + loc + "assertion failed" + msg);
    }

    return nullptr;
}

static VMValue assertEqNative(int argCount, VMValue *args) {
    if (argCount < 2) {
        failAssertion(currentVM, "FAIL: assertEq requires at least 2 arguments (actual, expected)");
        return nullptr;
    }

    VM *vm = currentVM;
    VMValue actual = args[0];
    VMValue expected = args[1];

    if (!(actual == expected)) {
        std::string filename;
        int line = 0;
        getSourceLocation(vm, filename, line);

        std::string msg;
        if (argCount >= 3 && args[2].isString()) {
            msg = " — " + args[2].asString()->flatten();
        }

        std::string loc;
        if (!filename.empty())
            loc = filename + ":" + std::to_string(line) + " ";

        failAssertion(vm, "FAIL: " + loc + "expected " + valueToString(expected) + " but got " +
                           valueToString(actual) + msg);
    }

    return nullptr;
}

static VMValue assertNeqNative(int argCount, VMValue *args) {
    if (argCount < 2) {
        failAssertion(currentVM, "FAIL: assertNeq requires at least 2 arguments (actual, unexpected)");
        return nullptr;
    }

    VM *vm = currentVM;
    VMValue actual = args[0];
    VMValue unexpected = args[1];

    if (actual == unexpected) {
        std::string filename;
        int line = 0;
        getSourceLocation(vm, filename, line);

        std::string msg;
        if (argCount >= 3 && args[2].isString()) {
            msg = " — " + args[2].asString()->flatten();
        }

        std::string loc;
        if (!filename.empty())
            loc = filename + ":" + std::to_string(line) + " ";

        failAssertion(vm, "FAIL: " + loc + "expected " + valueToString(actual) + " to differ from " +
                           valueToString(unexpected) + msg);
    }

    return nullptr;
}

static VMValue assertThrowsNative(int argCount, VMValue *args) {
    if (argCount < 1) {
        failAssertion(currentVM, "FAIL: assertThrows requires at least 1 argument (function)");
        return nullptr;
    }

    VM *vm = currentVM;
    VMValue fn = args[0];

    if (!fn.isClosure()) {
        failAssertion(vm, "FAIL: assertThrows argument must be a function");
        return nullptr;
    }

    auto closure = fn.asClosure();

    auto savedStackTop = vm->stackTop;
    auto savedFrameCount = vm->frames.size();

    if (setjmp(vm->catchJmpBuf) == 0) {
        vm->catchJumpEnabled = true;
        vm->suppressRuntimeErrors = true;

        vm->push(fn);

        CallFrame frame;
        frame.closure = closure;
        frame.ip = closure->function->chunk->code.data();
        frame.stackStart = static_cast<int>((vm->stackTop - vm->stack) - 1);
        vm->frames.push_back(frame);

        InterpretResult result = vm->run(savedFrameCount);

        vm->catchJumpEnabled = false;
        vm->suppressRuntimeErrors = false;
        vm->stackTop = savedStackTop;
        vm->frames.resize(savedFrameCount);

        if (result == InterpretResult::INTERPRET_OK) {
            std::string filename;
            int line = 0;
            getSourceLocation(vm, filename, line);
            std::string loc;
            if (!filename.empty())
                loc = filename + ":" + std::to_string(line) + " ";

            std::string msg;
            if (argCount >= 2 && args[1].isString()) {
                msg = " — " + args[1].asString()->flatten();
            }

            failAssertion(vm, "FAIL: " + loc + "expected function to throw but it did not" + msg);
        }
    } else {
        vm->catchJumpEnabled = false;
        vm->suppressRuntimeErrors = false;
        vm->stackTop = savedStackTop;
        vm->frames.resize(savedFrameCount);
    }

    return nullptr;
}

static void runCallbacks(VM *vm, const std::string &key) {
    auto it = vm->globals.find(key);
    if (it == vm->globals.end() || !it->second.isList())
        return;
    auto list = it->second.asList();
    for (auto &cb : list->elements) {
        if (cb.isClosure()) {
            vm->callClosure(cb, 0, nullptr);
        }
    }
}

static void addCallback(VM *vm, const std::string &key, VMValue fn) {
    auto it = vm->globals.find(key);
    ObjList *list;
    if (it == vm->globals.end() || !it->second.isList()) {
        list = new ObjList({});
        vm->globals[key] = VMValue(list);
    } else {
        list = it->second.asList();
    }
    list->elements.push_back(fn);
}

static VMValue beforeEachNative(int argCount, VMValue *args) {
    VM *vm = currentVM;
    if (argCount < 1 || !args[0].isClosure()) {
        failAssertion(vm, "FAIL: beforeEach requires a function argument");
        return nullptr;
    }
    addCallback(vm, "__test_beforeEach", args[0]);
    return nullptr;
}

static VMValue afterEachNative(int argCount, VMValue *args) {
    VM *vm = currentVM;
    if (argCount < 1 || !args[0].isClosure()) {
        failAssertion(vm, "FAIL: afterEach requires a function argument");
        return nullptr;
    }
    addCallback(vm, "__test_afterEach", args[0]);
    return nullptr;
}

static VMValue beforeNative(int argCount, VMValue *args) {
    VM *vm = currentVM;
    if (argCount < 1 || !args[0].isClosure()) {
        failAssertion(vm, "FAIL: before requires a function argument");
        return nullptr;
    }
    addCallback(vm, "__test_before", args[0]);
    return nullptr;
}

static VMValue afterNative(int argCount, VMValue *args) {
    VM *vm = currentVM;
    if (argCount < 1 || !args[0].isClosure()) {
        failAssertion(vm, "FAIL: after requires a function argument");
        return nullptr;
    }
    addCallback(vm, "__test_after", args[0]);
    return nullptr;
}

static bool isOnlyMode(VM *vm) {
    auto it = vm->globals.find("__test_only");
    return it != vm->globals.end() && it->second.isBool() && it->second.asBool();
}

static VMValue describeNative(int argCount, VMValue *args) {
    VM *vm = currentVM;
    if (argCount < 2 || !args[0].isString() || !args[1].isClosure()) {
        return nullptr;
    }
    if (isOnlyMode(vm)) {
        return nullptr;
    }
    std::string name = args[0].asString()->flatten();

    auto it = vm->globals.find("__test_describe");
    std::string prev;
    if (it != vm->globals.end() && it->second.isString()) {
        prev = it->second.asString()->flatten();
    }

    vm->globals["__test_describe"] = VMValue(name);

    vm->callClosure(args[1], 0, nullptr);

    runCallbacks(vm, "__test_after");

    vm->globals["__test_before"] = VMValue(new ObjList({}));
    vm->globals["__test_after"] = VMValue(new ObjList({}));

    if (prev.empty()) {
        vm->globals.erase("__test_describe");
    } else {
        vm->globals["__test_describe"] = VMValue(prev);
    }
    return nullptr;
}

static VMValue itNative(int argCount, VMValue *args) {
    if (argCount < 2) {
        failAssertion(currentVM, "FAIL: it requires 2 arguments (name, fn)");
        return nullptr;
    }
    VM *vm = currentVM;
    VMValue name = args[0];
    VMValue fn = args[1];

    if (!name.isString()) {
        failAssertion(vm, "FAIL: it() first argument must be a string (test name)");
        return nullptr;
    }
    if (!fn.isClosure()) {
        failAssertion(vm, "FAIL: it() second argument must be a function");
        return nullptr;
    }

    if (isOnlyMode(vm)) {
        return nullptr;
    }

    std::string testName = name.asString()->flatten();
    auto prefixIt = vm->globals.find("__test_describe");
    if (prefixIt != vm->globals.end() && prefixIt->second.isString()) {
        testName = prefixIt->second.asString()->flatten() + " " + testName;
    }

    auto closure = fn.asClosure();

    runCallbacks(vm, "__test_before");
    vm->globals["__test_before"] = VMValue(new ObjList({}));

    runCallbacks(vm, "__test_beforeEach");

    auto savedStackTop = vm->stackTop;
    auto savedFrameCount = vm->frames.size();
    bool passed = true;

    if (setjmp(vm->catchJmpBuf) == 0) {
        vm->catchJumpEnabled = true;

        vm->push(fn);
        CallFrame frame;
        frame.closure = closure;
        frame.ip = closure->function->chunk->code.data();
        frame.stackStart = static_cast<int>((vm->stackTop - vm->stack) - 1);
        vm->frames.push_back(frame);

        InterpretResult result = vm->run(savedFrameCount);
        vm->catchJumpEnabled = false;

        if (result != InterpretResult::INTERPRET_OK) {
            passed = false;
        }
    } else {
        vm->catchJumpEnabled = false;
        passed = false;
    }

    vm->stackTop = savedStackTop;
    vm->frames.resize(savedFrameCount);

    runCallbacks(vm, "__test_afterEach");

    trackTestResult(vm, testName, passed);

    return nullptr;
}

static VMValue xitNative(int argCount, VMValue *args) {
    VM *vm = currentVM;
    if (argCount < 1 || !args[0].isString()) {
        return nullptr;
    }
    std::string testName = args[0].asString()->flatten();
    auto prefixIt = vm->globals.find("__test_describe");
    if (prefixIt != vm->globals.end() && prefixIt->second.isString()) {
        testName = prefixIt->second.asString()->flatten() + " " + testName;
    }

    trackTestResult(vm, testName, true);

    return nullptr;
}

static VMValue fitNative(int argCount, VMValue *args) {
    VM *vm = currentVM;
    if (argCount < 2) {
        failAssertion(vm, "FAIL: fit requires 2 arguments (name, fn)");
        return nullptr;
    }
    VMValue name = args[0];
    VMValue fn = args[1];

    if (!name.isString()) {
        failAssertion(vm, "FAIL: fit() first argument must be a string (test name)");
        return nullptr;
    }
    if (!fn.isClosure()) {
        failAssertion(vm, "FAIL: fit() second argument must be a function");
        return nullptr;
    }

    std::string testName = name.asString()->flatten();
    auto prefixIt = vm->globals.find("__test_describe");
    if (prefixIt != vm->globals.end() && prefixIt->second.isString()) {
        testName = prefixIt->second.asString()->flatten() + " " + testName;
    }

    auto closure = fn.asClosure();

    runCallbacks(vm, "__test_before");
    vm->globals["__test_before"] = VMValue(new ObjList({}));

    runCallbacks(vm, "__test_beforeEach");

    auto savedStackTop = vm->stackTop;
    auto savedFrameCount = vm->frames.size();
    bool passed = true;

    if (setjmp(vm->catchJmpBuf) == 0) {
        vm->catchJumpEnabled = true;

        vm->push(fn);
        CallFrame frame;
        frame.closure = closure;
        frame.ip = closure->function->chunk->code.data();
        frame.stackStart = static_cast<int>((vm->stackTop - vm->stack) - 1);
        vm->frames.push_back(frame);

        InterpretResult result = vm->run(savedFrameCount);
        vm->catchJumpEnabled = false;

        if (result != InterpretResult::INTERPRET_OK) {
            passed = false;
        }
    } else {
        vm->catchJumpEnabled = false;
        passed = false;
    }

    vm->stackTop = savedStackTop;
    vm->frames.resize(savedFrameCount);

    runCallbacks(vm, "__test_afterEach");

    trackTestResult(vm, testName, passed);

    return nullptr;
}

static VMValue fdescribeNative(int argCount, VMValue *args) {
    VM *vm = currentVM;
    if (argCount < 2 || !args[0].isString() || !args[1].isClosure()) {
        return nullptr;
    }
    std::string name = args[0].asString()->flatten();

    auto it = vm->globals.find("__test_describe");
    std::string prev;
    if (it != vm->globals.end() && it->second.isString()) {
        prev = it->second.asString()->flatten();
    }

    vm->globals["__test_describe"] = VMValue(name);

    vm->callClosure(args[1], 0, nullptr);

    runCallbacks(vm, "__test_after");

    vm->globals["__test_before"] = VMValue(new ObjList({}));
    vm->globals["__test_after"] = VMValue(new ObjList({}));

    if (prev.empty()) {
        vm->globals.erase("__test_describe");
    } else {
        vm->globals["__test_describe"] = VMValue(prev);
    }
    return nullptr;
}

void registerAll(VM *vm) {
    vm->defineNative("assert", -1, assertNative);
    vm->defineNative("assertEq", -1, assertEqNative);
    vm->defineNative("assertNeq", -1, assertNeqNative);
    vm->defineNative("assertThrows", -1, assertThrowsNative);
    vm->defineNative("describe", -1, describeNative);
    vm->defineNative("it", -1, itNative);
    vm->defineNative("xit", -1, xitNative);
    vm->defineNative("fit", -1, fitNative);
    vm->defineNative("fdescribe", -1, fdescribeNative);
    vm->defineNative("before", -1, beforeNative);
    vm->defineNative("after", -1, afterNative);
    vm->defineNative("beforeEach", -1, beforeEachNative);
    vm->defineNative("afterEach", -1, afterEachNative);
}

void registerSymbols(SymbolTable *scope) {
    auto addFunc = [&](const std::string &name) {
        Symbol sym;
        sym.name = name;
        sym.type = "function";
        sym.isConst = true;
        scope->define(sym);
    };
    addFunc("assert");
    addFunc("assertEq");
    addFunc("assertNeq");
    addFunc("assertThrows");
    addFunc("describe");
    addFunc("it");
    addFunc("xit");
    addFunc("fit");
    addFunc("fdescribe");
    addFunc("before");
    addFunc("after");
    addFunc("beforeEach");
    addFunc("afterEach");
}

} // namespace Test
} // namespace StdLib
