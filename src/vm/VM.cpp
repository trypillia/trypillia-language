#include "VM.h"
#include "runtime/GC.h"

#include <sys/mman.h>
#include <unistd.h>
#include <csignal>
#include <csetjmp>
#include <mutex>

thread_local VM *currentVM = nullptr;
struct JmpBufHolder {
    jmp_buf buf;
};
static thread_local JmpBufHolder *stackOverflowJmpBuf = nullptr;
static std::once_flag guardHandlerFlag;

extern "C" void stackGuardHandler(int sig, siginfo_t *info, void *ctx) {
    (void)sig;
    (void)ctx;
    VM *vm = currentVM;
    if (vm) {
        char *guardStart = (char *)vm->stack + STACK_BYTES;
        char *guardEnd = guardStart + GUARD_SIZE;
        void *fault = info->si_addr;
        if (fault >= (void *)guardStart && fault < (void *)guardEnd) {
            if (stackOverflowJmpBuf) {
                siglongjmp(stackOverflowJmpBuf->buf, 1);
            }
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void installGuardHandler() {
    std::call_once(guardHandlerFlag, []() {
        struct sigaction sa;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sa.sa_sigaction = stackGuardHandler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, nullptr);
    });
}

#include "runtime/ObjectRuntime.h"
#include <cmath>
#include <iostream>
#include <map>

#include "../native/StdLib.h"

VM::VM() {
    installGuardHandler();

    // Allocate alternate signal stack for this thread
    stack_t sigstk;
    sigstk.ss_sp = mmap(nullptr, SIGSTKSZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    sigstk.ss_size = SIGSTKSZ;
    sigstk.ss_flags = 0;
    sigaltstack(&sigstk, nullptr);

    // Allocate VM stack with guard page at the end
    size_t allocSize = STACK_BYTES + GUARD_SIZE;
    void *mem = mmap(nullptr, allocSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        stack = new VMValue[STACK_MAX]();
        stackIsMMap = false;
        std::cerr << "Warning: mmap failed, stack without guard page" << std::endl;
    } else {
        stack = (VMValue *)mem;
        mprotect((char *)mem + STACK_BYTES, GUARD_SIZE, PROT_NONE);
        stackIsMMap = true;
    }
    stackTop = stack;

    currentVM = this;
    StdLib::registerAll(this);
}

VM::~VM() {
    if (stack) {
        if (stackIsMMap) {
            munmap(stack, STACK_BYTES + GUARD_SIZE);
        } else {
            delete[] stack;
        }
        stack = nullptr;
    }
}

void VM::resetStack() {
    stackTop = stack;
}

void VM::push(VMValue value) {
    *stackTop++ = value;
}

VMValue VM::pop() {
    VMValue value = *(stackTop - 1);
    stackTop--;
    return value;
}

VMValue VM::peek(int distance) {
    return stack[(stackTop - stack) - 1 - distance];
}

void VM::drainMicrotasks() {
    while (!microtaskQueue.empty() || !promiseMicrotasks.empty()) {
        while (!promiseMicrotasks.empty()) {
            auto pm = promiseMicrotasks.front();
            promiseMicrotasks.erase(promiseMicrotasks.begin());

            if (pm.onFulfilled.isClosure()) {
                auto closure = pm.onFulfilled.asClosure();
                auto fn = closure->function;
                int initialDepth = static_cast<int>(frames.size());

                push(pm.onFulfilled);
                push(pm.inputValue);
                CallFrame frame;
                frame.closure = closure;
                frame.ip = fn->chunk->code.data();
                frame.stackStart = static_cast<int>((stackTop - stack) - 2);
                frames.push_back(frame);

                InterpretResult res = run(initialDepth);

                if (res == InterpretResult::INTERPRET_OK && pm.targetPromise) {
                    VMValue result = pop();
                    pm.targetPromise->value = result;
                    pm.targetPromise->resolved = true;
                    for (size_t i = 0; i < pm.targetPromise->thenHandlers.size(); i += 3) {
                        PromiseMicrotask nextPm;
                        nextPm.targetPromise = pm.targetPromise->thenHandlers[i + 2].isPromise()
                                                   ? pm.targetPromise->thenHandlers[i + 2].asPromise()
                                                   : nullptr;
                        nextPm.onFulfilled = pm.targetPromise->thenHandlers[i];
                        nextPm.onRejected = pm.targetPromise->thenHandlers[i + 1];
                        nextPm.inputValue = result;
                        promiseMicrotasks.push_back(nextPm);
                    }
                    pm.targetPromise->thenHandlers.clear();
                } else {
                    pop();
                }
            }
        }

        if (!microtaskQueue.empty()) {
            VMValue task = microtaskQueue.front();
            microtaskQueue.erase(microtaskQueue.begin());

            if (task.isClosure()) {
                auto closure = task.asClosure();
                int initialDepth = static_cast<int>(frames.size());

                push(task);
                CallFrame frame;
                frame.closure = closure;
                frame.ip = closure->function->chunk->code.data();
                frame.stackStart = static_cast<int>((stackTop - stack) - 1);
                frames.push_back(frame);

                run(initialDepth);
            }
        }
    }
}

InterpretResult VM::interpret(ObjFunction *function) {
    resetStack();
    frames.clear();
    openUpvalues = nullptr;

    auto closure = new ObjClosure(function);
    push(closure);

    CallFrame frame;
    frame.closure = closure;
    frame.ip = function->chunk->code.data();
    frame.stackStart = 0;
    frames.push_back(frame);

    InterpretResult result = run(0);

    if (result == InterpretResult::INTERPRET_OK) {
        drainMicrotasks();
    }

    return result;
}

#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (frame->closure->function->chunk->constants[READ_BYTE()])
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

InterpretResult VM::runtimeError(const std::string &message) {
    if (catchJumpEnabled) {
        siglongjmp(catchJmpBuf, 1);
    }
    if (!suppressRuntimeErrors) {
        std::cerr << "\n ૮ ˶ᵔ ᵕ ᵔ˶ ა \n / づ 📝 ♡ \n\n";
        std::cerr << "Panic: " << message << "\n\n";
        std::cerr << "Traceback (most recent call last):\n";

    for (int i = static_cast<int>(frames.size()) - 1; i >= 0; i--) {
        CallFrame *frame = &frames[i];
        ObjFunction *function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk->code.data() - 1;
        int line = function->chunk->lines[static_cast<int>(instruction)];

        std::cerr << "  at ";
        if (function->name.empty() || function->name == "<script>") {
            std::cerr << "<main>";
        } else {
            std::cerr << function->name << "()";
        }

        std::string fname = function->filename.empty() ? "<unknown>" : function->filename;
        // if fname has path, extract only basename for cleaner output like 'test.try'
        size_t lastSlash = fname.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            fname = fname.substr(lastSlash + 1);
        }

        std::cerr << " in " << fname << ":" << line << "\n";
    }
    std::cerr << std::endl;
    }
    resetStack();
    return InterpretResult::INTERPRET_RUNTIME_ERROR;
}

InterpretResult VM::run(int targetFrameDepth) {
    CallFrame *frame = &frames.back();

    JmpBufHolder holder;
    stackOverflowJmpBuf = &holder;
    if (sigsetjmp(holder.buf, 1) != 0) {
        stackOverflowJmpBuf = nullptr;
        jitClosure = nullptr;
        return runtimeError("Stack overflow.");
    }

    for (;;) {
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
        case static_cast<uint8_t>(OpCode::OP_NOP):
            break;
        case static_cast<uint8_t>(OpCode::OP_CONSTANT): {
            VMValue constant = READ_CONSTANT();
            push(constant);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CONSTANT_WIDE): {
            uint16_t idx = READ_SHORT();
            push(frame->closure->function->chunk->constants[idx]);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_TRUE): {
            push(true);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_FALSE): {
            push(false);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_NIL): {
            push(nullptr);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_ADD): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(a.asNumber() + b.asNumber());
            } else if (a.isString() || b.isString()) {
                ObjString *strA = a.isString() ? a.asString() : new ObjString(a.toString());
                ObjString *strB = b.isString() ? b.asString() : new ObjString(b.toString());
                push(new ObjString(strA, strB));
            } else {
                return runtimeError(std::string("Operands must be numbers or strings."));
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SUBTRACT): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(a.asNumber() - b.asNumber());
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_MULTIPLY): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(a.asNumber() * b.asNumber());
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_DIVIDE): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(a.asNumber() / b.asNumber());
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_MOD): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(std::fmod(a.asNumber(), b.asNumber()));
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_AND): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(static_cast<double>(static_cast<int32_t>(a.asNumber()) & static_cast<int32_t>(b.asNumber())));
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_OR): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(static_cast<double>(static_cast<int32_t>(a.asNumber()) | static_cast<int32_t>(b.asNumber())));
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_XOR): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(static_cast<double>(static_cast<int32_t>(a.asNumber()) ^ static_cast<int32_t>(b.asNumber())));
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_SHIFT_LEFT): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(static_cast<double>(static_cast<int32_t>(a.asNumber()) << static_cast<int32_t>(b.asNumber())));
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_SHIFT_RIGHT): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(static_cast<double>(static_cast<int32_t>(a.asNumber()) >> static_cast<int32_t>(b.asNumber())));
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_EQUAL): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(a.asNumber() == b.asNumber());
            } else if (a.isString() && b.isString()) {
                push(a.asString()->flatten() == b.asString()->flatten());
            } else if (a.isBool() && b.isBool()) {
                push(a.asBool() == b.asBool());
            } else if (a.isNil() && b.isNil()) {
                push(true);
            } else {
                push(false);
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_NOT_EQUAL): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(a.asNumber() != b.asNumber());
            } else if (a.isString() && b.isString()) {
                push(a.asString()->flatten() != b.asString()->flatten());
            } else if (a.isBool() && b.isBool()) {
                push(a.asBool() != b.asBool());
            } else if (a.isNil() && b.isNil()) {
                push(false);
            } else {
                push(true);
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_LESS): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(a.asNumber() < b.asNumber());
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_LESS_EQUAL): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(a.asNumber() <= b.asNumber());
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GREATER): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(a.asNumber() > b.asNumber());
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GREATER_EQUAL): {
            VMValue b = pop();
            VMValue a = pop();
            if (a.isNumber() && b.isNumber()) {
                push(a.asNumber() >= b.asNumber());
            } else {
                return runtimeError("Operands must be numbers.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_NOT): {
            VMValue a = pop();
            if (a.isBool()) {
                push(!a.asBool());
            } else if (a.isNil()) {
                push(true);
            } else {
                push(false);
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_NOT): {
            VMValue value = pop();
            if (value.isNumber()) {
                push(static_cast<double>(~static_cast<int32_t>(value.asNumber())));
            } else {
                return runtimeError("Operand must be a number.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_NEGATE): {
            VMValue a = pop();
            if (a.isNumber()) {
                push(-a.asNumber());
            } else {
                return runtimeError("Operand must be a number.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_JUMP): {
            uint16_t offset = READ_SHORT();
            frame->ip += offset;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE): {
            uint16_t offset = READ_SHORT();
            VMValue condition = peek(0);
            bool isFalsy = false;
            if (condition.isBool()) {
                isFalsy = !condition.asBool();
            } else if (condition.isNil()) {
                isFalsy = true;
            }

            if (isFalsy) {
                frame->ip += offset;
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GET_LOCAL): {
            uint8_t slot = READ_BYTE();
            push(stack[frame->stackStart + slot]);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SET_LOCAL): {
            uint8_t slot = READ_BYTE();
            stack[frame->stackStart + slot] = peek(0);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GET_UPVALUE): {
            uint8_t slot = READ_BYTE();
            push(*frame->closure->upvalues[slot]->location);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SET_UPVALUE): {
            uint8_t slot = READ_BYTE();
            *frame->closure->upvalues[slot]->location = peek(0);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CLOSE_UPVALUE): {
            closeUpvalues((stackTop - 1));
            pop();
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CLOSURE): {
            VMValue funcVal = READ_CONSTANT();
            auto function = funcVal.asFunction();
            auto closure = new ObjClosure(function);
            push(closure);
            for (int i = 0; i < function->upvalueCount; i++) {
                uint8_t isLocal = READ_BYTE();
                uint8_t index = READ_BYTE();
                if (isLocal) {
                    closure->upvalues.push_back(captureUpvalue(&stack[frame->stackStart + index]));
                } else {
                    closure->upvalues.push_back(frame->closure->upvalues[index]);
                }
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_LOOP): {
            if (bytesAllocated > nextGC)
                GC::collect(this);
            uint16_t offset = READ_SHORT();
            frame->ip -= offset;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_ITER_HAS_NEXT): {
            VMValue indexVal = pop();
            VMValue iterableVal = pop();
            if (indexVal.isNumber()) {
                int index = static_cast<int>(indexVal.asNumber());
                if (iterableVal.isList()) {
                    auto list = iterableVal.asList();
                    push(index < list->elements.size());
                    break;
                } else if (iterableVal.isString()) {
                    auto str = iterableVal.asString()->flatten();
                    push(index < utf8_length(str));
                    break;
                }
            }
            return runtimeError(std::string("Invalid operand types for iteration."));
        }
        case static_cast<uint8_t>(OpCode::OP_DUP): {
            push(peek(0));
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL): {
            std::string name = READ_CONSTANT().asString()->flatten();
            globals[name] = pop();
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GET_GLOBAL): {
            std::string name = READ_CONSTANT().asString()->flatten();
            if (globals.find(name) == globals.end()) {
                return runtimeError(std::string("Undefined variable '") + name + "'.");
            }
            push(globals[name]);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SET_GLOBAL): {
            std::string name = READ_CONSTANT().asString()->flatten();
            if (globals.find(name) == globals.end()) {
                return runtimeError(std::string("Undefined variable '") + name + "'.");
            }
            globals[name] = peek(0);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_POP): {
            pop();
            break;
        }

        case static_cast<uint8_t>(OpCode::OP_BUILD_LIST): {
            uint8_t count = READ_BYTE();
            std::vector<VMValue> elements(count);
            for (int i = count - 1; i >= 0; i--) {
                elements[i] = pop();
            }
            push(new ObjList(elements));
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BUILD_MAP): {
            uint8_t count = READ_BYTE();
            auto map = new ObjMap();
            for (int i = count - 1; i >= 0; i--) {
                VMValue value = pop();
                VMValue key = pop();
                map->values[key] = value;
            }
            push(map);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_INDEX_GET): {
            if (!executeIndexGet())
                return InterpretResult::INTERPRET_RUNTIME_ERROR;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_INDEX_SET): {
            VMValue value = pop();
            VMValue index = pop();
            VMValue listVal = pop();
            if (listVal.isList()) {
                auto list = listVal.asList();
                if (index.isNumber()) {
                    int i = static_cast<int>(index.asNumber());
                    if (i >= 0 && i < static_cast<int>(list->elements.size())) {
                        list->elements[i] = value;
                        push(value);
                    } else {
                        return runtimeError(std::string("Index out of bounds."));
                    }
                } else {
                    return runtimeError(std::string("List index must be a number."));
                }
            } else if (listVal.isMap()) {
                auto map = listVal.asMap();
                map->values[index] = value;
                push(value);
            } else {
                return runtimeError(std::string("Can only set elements in lists or maps."));
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CLASS): {
            std::string name = READ_CONSTANT().asString()->flatten();
            push(new ObjClass(name));
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_ABSTRACT_CLASS): {
            std::string name = READ_CONSTANT().asString()->flatten();
            auto klass = new ObjClass(name);
            klass->isAbstract = true;
            push(klass);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_INHERIT): {
            VMValue superclassVal = pop();
            VMValue subclassVal = pop();
            if (!superclassVal.isClass()) {
                return runtimeError(std::string("Superclass must be a class."));
            }
            auto subclass = subclassVal.asClass();
            auto superclass = superclassVal.asClass();
            subclass->superclass = superclass;
            for (auto const &[name, mod] : superclass->fieldModifiers) {
                subclass->fieldModifiers[name] = mod;
            }
            for (auto const &[name, method] : superclass->methods) {
                subclass->methods[name] = method;
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_MIXIN): {
            VMValue mixinVal = pop();
            VMValue targetVal = pop();
            if (!mixinVal.isClass()) {
                return runtimeError(std::string("Mixin must be a class/trait."));
            }
            auto targetClass = targetVal.asClass();
            auto mixinClass = mixinVal.asClass();
            for (auto const &[name, method] : mixinClass->methods) {
                targetClass->methods[name] = method;
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GET_SUPER): {
            std::string methodName = READ_CONSTANT().asString()->flatten();
            VMValue superclassVal = pop();
            VMValue receiverVal = pop();
            auto superclass = superclassVal.asClass();
            auto receiver = receiverVal.asInstance();
            if (superclass->methods.count(methodName)) {
                auto method = superclass->methods[methodName];
                push(new ObjBoundMethod(receiver, method));
            } else {
                return runtimeError(std::string("Undefined superclass method '") + methodName + "'.");
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_METHOD): {
            std::string name = READ_CONSTANT().asString()->flatten();
            VMValue methodVal = pop();
            VMValue classVal = peek(0);
            auto method = methodVal;
            auto klass = classVal.asClass();
            klass->methods[name] = method;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_ABSTRACT_METHOD): {
            std::string name = READ_CONSTANT().asString()->flatten();
            VMValue methodVal = pop();
            VMValue classVal = peek(0);
            auto method = methodVal;
            if (method.isClosure())
                method.asClosure()->function->isAbstract = true;
            else
                method.asNative()->isAbstract = true;
            auto klass = classVal.asClass();
            klass->methods[name] = method;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_STATIC_METHOD): {
            std::string name = READ_CONSTANT().asString()->flatten();
            VMValue methodVal = pop();
            VMValue classVal = peek(0);
            auto klass = classVal.asClass();
            klass->statics[name] = methodVal;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_FIELD_MODIFIER): {
            std::string name = READ_CONSTANT().asString()->flatten();
            VMAccessModifier modifier = static_cast<VMAccessModifier>(READ_BYTE());
            VMValue classVal = peek(0);
            auto klass = classVal.asClass();
            klass->fieldModifiers[name] = modifier;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_PROPERTY_GET): {
            std::string name = READ_CONSTANT().asString()->flatten();
            if (!executePropertyGet(name))
                return InterpretResult::INTERPRET_RUNTIME_ERROR;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_PROPERTY_SET): {
            std::string name = READ_CONSTANT().asString()->flatten();
            VMValue value = pop();
            VMValue instanceVal = pop();
            std::string callerClass = frame->closure ? frame->closure->function->enclosingClassName : "";

            if (instanceVal.isInstance()) {
                auto instance = instanceVal.asInstance();
                VMAccessModifier mod = VMAccessModifier::PUBLIC;
                if (instance->klass->fieldModifiers.count(name))
                    mod = instance->klass->fieldModifiers[name];
                if (!checkAccess(mod, instance->klass, callerClass)) {
                    return runtimeError(std::string("Access error: Cannot set '") + name + "'.");
                }
                instance->fields[name] = value;
                push(value);
            } else if (instanceVal.isClass()) {
                auto klass = instanceVal.asClass();
                klass->statics[name] = value;
                push(value);
            } else {
                return runtimeError(std::string("Only instances and classes have properties."));
            }
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CALL): {
            uint8_t argCount = READ_BYTE();
            if (!executeCall(argCount))
                return InterpretResult::INTERPRET_RUNTIME_ERROR;
            frame = &frames.back();
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_RETURN): {
            VMValue result = pop();
            closeUpvalues(&stack[frame->stackStart]);
            int newStackSize = frame->stackStart;
            frames.pop_back();

            if (frames.size() == targetFrameDepth) {
                stackTop = stack + newStackSize;
                push(result);
                return InterpretResult::INTERPRET_OK;
            }

            stackTop = stack + newStackSize;
            push(result);
            frame = &frames.back();
            break;
        }
        default:
            return runtimeError(std::string("Unknown opcode"));
        }
    }
}

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT

bool VM::executeIndexGet() {
    VMValue index = pop();
    VMValue listVal = pop();
    if (listVal.isList()) {
        auto list = listVal.asList();
        if (index.isNumber()) {
            int i = static_cast<int>(index.asNumber());
            if (i >= 0 && i < static_cast<int>(list->elements.size())) {
                push(list->elements[i]);
            } else {
                runtimeError(std::string("Index out of bounds."));
                return false;
            }
        } else {
            runtimeError(std::string("List index must be a number."));
            return false;
        }
    } else if (listVal.isString()) {
        auto str = listVal.asString()->flatten();
        if (index.isNumber()) {
            int i = static_cast<int>(index.asNumber());
            int len = utf8_length(str);
            if (i >= 0 && i < len) {
                push(utf8_char_at(str, i));
            } else {
                runtimeError(std::string("String index out of bounds."));
                return false;
            }
        } else {
            runtimeError(std::string("String index must be a number."));
            return false;
        }
    } else if (listVal.isMap()) {
        auto map = listVal.asMap();
        if (map->values.count(index)) {
            push(map->values[index]);
        } else {
            push(nullptr); // Return nil for missing keys
        }
    } else {
        runtimeError(std::string("Can only index into lists, maps, or strings."));
        return false;
    }
    return true;
}

bool VM::executePropertyGet(const std::string &name) {
    VMValue instanceVal = peek(0);
    std::string callerClass = frames.back().closure ? frames.back().closure->function->enclosingClassName : "";

    if (instanceVal.isInstance()) {
        auto instance = instanceVal.asInstance();
        if (instance->fields.count(name)) {
            VMAccessModifier mod = VMAccessModifier::PUBLIC;
            if (instance->klass->fieldModifiers.count(name))
                mod = instance->klass->fieldModifiers[name];
            if (!checkAccess(mod, instance->klass, callerClass)) {
                runtimeError(std::string("Access error: Cannot access '") + name + "'.");
                return false;
            }
            pop();
            push(instance->fields[name]);
        } else if (instance->klass->methods.count(name)) {
            auto method = instance->klass->methods[name];
            VMAccessModifier mod = VMAccessModifier::PUBLIC;
            if (method.isClosure()) {
                mod = method.asClosure()->function->accessModifier;
            }
            if (!checkAccess(mod, instance->klass, callerClass)) {
                runtimeError(std::string("Access error: Cannot access method '") + name + "'.");
                return false;
            }
            pop(); // instance
            push(new ObjBoundMethod(instance, method));
        } else {
            runtimeError(std::string("Undefined property '") + name + "'.");
            return false;
        }
    } else if (instanceVal.isClass()) {
        auto klass = instanceVal.asClass();
        if (klass->statics.count(name)) {
            auto methodVal = klass->statics[name];
            if (methodVal.isClosure()) {
                auto func = methodVal.asClosure()->function;
                if (!checkAccess(func->accessModifier, klass, callerClass)) {
                    runtimeError(std::string("Access error: Cannot access static method '") + name + "'.");
                    return false;
                }
            }
            // statics (fields) access modifier check can be added here if static fields have modifiers.
            pop();
            push(klass->statics[name]);
        } else {
            runtimeError(std::string("Undefined static property '") + name + "'.");
            return false;
        }
    } else if (instanceVal.isString()) {
        if (globals.count("String")) {
            auto klass = globals["String"].asClass();
            if (klass->statics.count(name)) {
                pop(); // pop string
                push(new ObjBoundMethod(instanceVal, klass->statics[name]));
                return true;
            }
        }
        runtimeError(std::string("Undefined property '") + name + "' on String.");
        return false;
    } else if (instanceVal.isList()) {
        if (globals.count("List")) {
            auto klass = globals["List"].asClass();
            if (klass->statics.count(name)) {
                pop(); // pop list
                push(new ObjBoundMethod(instanceVal, klass->statics[name]));
                return true;
            }
        }
        runtimeError(std::string("Undefined property '") + name + "' on List.");
        return false;
    } else if (instanceVal.isMap()) {
        if (globals.count("Map")) {
            auto klass = globals["Map"].asClass();
            if (klass->statics.count(name)) {
                pop(); // pop map
                push(new ObjBoundMethod(instanceVal, klass->statics[name]));
                return true;
            }
        }
        runtimeError(std::string("Undefined property '") + name + "' on Map.");
        return false;
    } else if (instanceVal.isPromise()) {
        if (globals.count("__promise_then") && name == "then") {
            pop();
            push(new ObjBoundMethod(instanceVal, globals["__promise_then"]));
            return true;
        }
        runtimeError(std::string("Undefined property '") + name + "' on Promise.");
        return false;
    } else {
        runtimeError(std::string("Only instances and classes have properties."));
        return false;
    }
    return true;
}

bool VM::executeCall(uint8_t argCount) {
    VMValue callee = peek(argCount);
    if (callee.isClosure()) {
        auto closure = callee.asClosure();
        auto function = closure->function;
        if (function->arity != -1 && (argCount < function->arity || argCount > function->maxArity)) {
            std::string expected = function->arity == function->maxArity
                                       ? std::to_string(function->arity)
                                       : std::to_string(function->arity) + "-" + std::to_string(function->maxArity);
            runtimeError(std::string("Expected ") + expected + " arguments but got " + std::to_string(argCount) + ".");
            return false;
        }
        while (function->maxArity != -1 && argCount < function->maxArity) {
            push(nullptr);
            argCount++;
        }
        if (frames.size() == 256) {
            runtimeError(std::string("Stack overflow."));
            return false;
        }

        JitFunc nativeJitFunc = nullptr;
        auto funcPtr = function;
        if (compiledFuncs.count(funcPtr)) {
            nativeJitFunc = compiledFuncs[funcPtr];
        } else if (funcPtr->callCount >= 50) {
            nativeJitFunc = jit.compileMathFunction(function);
            if (nativeJitFunc) {
                compiledFuncs[funcPtr] = nativeJitFunc;
                funcPtr->jitAddr = (void *)nativeJitFunc;
                std::cerr << "JIT compiled " << function->name << " at call #" << funcPtr->callCount << std::endl;
            } else {
                compiledFuncs[funcPtr] = nullptr;
                std::cerr << "JIT ABORTED for " << function->name << " at call #" << funcPtr->callCount << std::endl;
            }
        } else {
            funcPtr->callCount++;
        }

        if (nativeJitFunc) {
            std::vector<double> jitArgs(2048, 0.0);
            for (int i = 0; i <= argCount; ++i) {
                VMValue arg = peek(argCount - i);
                double raw;
                memcpy(&raw, &arg, sizeof(double));
                jitArgs[i] = raw;
            }

            // Guard: JIT assumes all values are numbers; fall back to
            // interpreter if any argument is not a number
            bool allNumbers = true;
            for (int i = 1; i <= argCount; i++) {
                VMValue val;
                memcpy(&val, &jitArgs[i], sizeof(double));
                if (!val.isNumber()) {
                    allNumbers = false;
                    break;
                }
            }

            if (allNumbers) {
                jitClosure = closure;
                double result = nativeJitFunc(this, jitArgs.data(), argCount, argCount > 0 ? jitArgs[1] : 0.0);
                jitClosure = nullptr;
                stackTop -= argCount + 1;
                push(result);
                return true; // Skip standard frame push!
            }
            // Fall through to interpreter if not all numbers
        }
        CallFrame newFrame;
        newFrame.closure = closure;
        newFrame.ip = function->chunk->code.data();
        newFrame.stackStart = static_cast<int>((stackTop - stack) - argCount - 1);
        frames.push_back(newFrame);

    } else if (callee.isNative()) {
        auto native = callee.asNative();
        if (native->arity != -1 && argCount != native->arity) {
            runtimeError(std::string("Expected ") + std::to_string(native->arity) + " arguments but got " +
                         std::to_string(argCount) + ".");
            return false;
        }
        if (sigsetjmp(assertJmpBuf, 1) == 0) {
            assertJumpEnabled = true;
            VMValue result = native->function(argCount, stack + (stackTop - stack) - argCount);
            assertJumpEnabled = false;
            stackTop -= argCount + 1;
            push(result);
        } else {
            assertJumpEnabled = false;
            resetStack();
            return false;
        }

    } else if (callee.isClass()) {
        auto klass = callee.asClass();
        if (klass->isAbstract) {
            runtimeError(std::string("Cannot instantiate abstract class '") + klass->name + "'.");
            return false;
        }
        for (auto const &[name, method] : klass->methods) {
            if (isMethodAbstract(method)) {
                runtimeError(std::string("Cannot instantiate class '") + klass->name + "' because abstract method '" +
                             name + "' is not implemented.");
                return false;
            }
        }
        auto instance = new ObjInstance(klass);

        stack[(stackTop - stack) - argCount - 1] = instance;

        if (klass->methods.count("init")) {
            auto initMethod = klass->methods["init"];
            int minArity = getMethodMinArity(initMethod);
            int maxArity = getMethodMaxArity(initMethod);
            if (minArity != -1 && (argCount < minArity || argCount > maxArity)) {
                std::string expected = minArity == maxArity ? std::to_string(minArity)
                                                            : std::to_string(minArity) + "-" + std::to_string(maxArity);
                runtimeError(std::string("Expected ") + expected + " arguments but got " + std::to_string(argCount) +
                             ".");
                return false;
            }
            while (maxArity != -1 && argCount < maxArity) {
                push(nullptr);
                argCount++;
            }

            if (initMethod.isClosure()) {
                auto closure = initMethod.asClosure();
                auto func = closure->function;
                CallFrame newFrame;
                newFrame.closure = closure;
                newFrame.ip = func->chunk->code.data();
                newFrame.stackStart = static_cast<int>((stackTop - stack) - argCount - 1);
                frames.push_back(newFrame);

            } else {
                auto native = initMethod.asNative();
                native->function(argCount, stack + (stackTop - stack) - argCount);
                stackTop -= argCount; // leave the instance on stack
            }
        } else if (argCount != 0) {
            runtimeError(std::string("Expected 0 arguments but got ") + std::to_string(argCount) + ".");
            return false;
        }
    } else if (callee.isBoundMethod()) {
        auto bound = callee.asBoundMethod();
        auto function = bound->method;
        if (isMethodAbstract(function)) {
            runtimeError(std::string("Cannot call abstract method '") + getMethodName(function) + "'.");
            return false;
        }
        int minArity = getMethodMinArity(function);
        int maxArity = getMethodMaxArity(function);
        if (function.isNative()) {
            if (!bound->receiver.isInstance()) {
                if (minArity != -1)
                    minArity -= 1;
                if (maxArity != -1)
                    maxArity -= 1;
            }
        }
        if (minArity != -1 && (argCount < minArity || argCount > maxArity)) {
            std::string expected = minArity == maxArity ? std::to_string(minArity)
                                                        : std::to_string(minArity) + "-" + std::to_string(maxArity);
            runtimeError(std::string("Expected ") + expected + " arguments but got " + std::to_string(argCount) + ".");
            return false;
        }
        while (maxArity != -1 && argCount < maxArity) {
            push(nullptr);
            argCount++;
        }
        if (frames.size() == 256) {
            runtimeError(std::string("Stack overflow."));
            return false;
        }
        stack[(stackTop - stack) - argCount - 1] = bound->receiver;

        if (function.isClosure()) {
            auto closure = function.asClosure();
            auto func = closure->function;
            CallFrame newFrame;
            newFrame.closure = closure;
            newFrame.ip = func->chunk->code.data();
            newFrame.stackStart = static_cast<int>((stackTop - stack) - argCount - 1);
            frames.push_back(newFrame);

        } else {
            auto native = function.asNative();
            int passedArgCount = argCount;
            VMValue *argsPtr;
            if (!bound->receiver.isInstance()) {
                passedArgCount += 1;
                argsPtr = stack + (stackTop - stack) - argCount - 1; // Primitive methods expect receiver at args[0]
            } else {
                argsPtr = stack + (stackTop - stack) - argCount; // Instance methods expect receiver at args[-1]
            }
            VMValue result = native->function(passedArgCount, argsPtr);
            stackTop -= argCount + 1;
            push(result);
        }
    } else {
        runtimeError(std::string("Can only call functions and classes."));
        return false;
    }
    return true;
}
