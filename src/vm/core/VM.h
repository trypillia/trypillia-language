#ifndef TRYPILLIA_VM_H
#define TRYPILLIA_VM_H

#include <csetjmp>
#include <csignal>
#include <string>
#include <unordered_map>
#include <vector>

#include "../compiler/Chunk.h"
#include "../jit/JITCompiler.h"

enum class InterpretResult
{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
};

struct CallFrame
{
    ObjClosure *closure;
    uint8_t *ip;
    int stackStart;
};

#define STACK_MAX 8192
static constexpr size_t STACK_BYTES = STACK_MAX * sizeof(VMValue);
static constexpr size_t GUARD_SIZE = 4096;

class VM;
extern thread_local VM *currentVM;

class VM
{
  public:
    std::vector<CallFrame> frames;
    Obj *objects = nullptr;
    size_t bytesAllocated = 0;
    size_t nextGC = 1024 * 1024;
    VMValue *stack;
    VMValue *stackTop;
    bool stackIsMMap = false;
    std::unordered_map<std::string, VMValue> globals;
    ObjUpvalue *openUpvalues;

    void resetStack();
    void push(VMValue value);
    VMValue pop();
    VMValue peek(int distance);

    InterpretResult runtimeError(const std::string &message);
    InterpretResult run(int targetFrameDepth = 0);

    bool executeCall(uint8_t argCount);
    bool executePropertyGet(const std::string &name);
    bool executeIndexGet();

  public:
    ObjUpvalue *captureUpvalue(VMValue *local);
    void closeUpvalues(VMValue *last);

    void defineNative(const std::string &name, int arity, NativeFn function);
    VMValue callClosure(VMValue closureVal, int argCount, VMValue *args);

    VM();
    ~VM();

    ObjClosure *jitClosure = nullptr;
    JITCompiler jit;
    std::unordered_map<void *, JitFunc> compiledFuncs;
    bool jitDeoptNeeded = false;

    bool suppressRuntimeErrors = false;
    bool collectCoverage = false;
    jmp_buf assertJmpBuf;
    bool assertJumpEnabled = false;
    jmp_buf catchJmpBuf;
    bool catchJumpEnabled = false;

    struct PromiseMicrotask
    {
        ObjPromise *targetPromise;
        VMValue onFulfilled;
        VMValue onRejected;
        VMValue inputValue;
    };
    std::vector<VMValue> microtaskQueue;
    std::vector<PromiseMicrotask> promiseMicrotasks;
    void drainMicrotasks();

    VMValue instantiateClass(VMValue classVal, int argCount, VMValue *args);

    InterpretResult interpret(ObjFunction *function);

    void resetCoverage();
};

#endif // TRYPILLIA_VM_H
