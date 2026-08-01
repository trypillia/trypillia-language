#include "VM.h"
#include "../memory/GC.h"

#ifdef _WIN32
#include <malloc.h>
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <csetjmp>
#include <csignal>
#include <mutex>

thread_local VM *currentVM = nullptr;
struct JmpBufHolder {
  jmp_buf buf;
};
static thread_local JmpBufHolder *stackOverflowJmpBuf = nullptr;
static std::once_flag guardHandlerFlag;

#ifndef _WIN32
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
        longjmp(stackOverflowJmpBuf->buf, 1);
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
#else
static LONG
    WINAPI windowsStackOverflowHandler(EXCEPTION_POINTERS *ExceptionInfo) {
  if (ExceptionInfo->ExceptionRecord->ExceptionCode ==
      EXCEPTION_STACK_OVERFLOW) {
    if (stackOverflowJmpBuf) {
      _resetstkoflw();
      longjmp(stackOverflowJmpBuf->buf, 1);
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

static void installGuardHandler() {
  std::call_once(guardHandlerFlag, []() {
    AddVectoredExceptionHandler(1, windowsStackOverflowHandler);
  });
}
#endif

#include "../memory/ObjectRuntime.h"
#include <cmath>
#include <iostream>
#include <map>

#include "../../native/StdLib.h"

VM::VM() {
  installGuardHandler();

#ifndef _WIN32
  // Allocate alternate signal stack for this thread
  stack_t sigstk;
  sigstk.ss_sp = mmap(nullptr, SIGSTKSZ, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  sigstk.ss_size = SIGSTKSZ;
  sigstk.ss_flags = 0;
  sigaltstack(&sigstk, nullptr);
#endif

  // Allocate VM stack with guard page at the end
  size_t allocSize = STACK_BYTES + GUARD_SIZE;
#ifdef _WIN32
  void *mem = VirtualAlloc(nullptr, allocSize, MEM_COMMIT | MEM_RESERVE,
                           PAGE_READWRITE);
  if (mem == nullptr) {
    stack = new VMValue[STACK_MAX]();
    stackIsMMap = false;
    std::cerr << "Warning: VirtualAlloc failed, stack without guard page"
              << std::endl;
  } else {
    stack = (VMValue *)mem;
    DWORD oldProtect;
    VirtualProtect((char *)mem + STACK_BYTES, GUARD_SIZE, PAGE_NOACCESS,
                   &oldProtect);
    stackIsMMap = true;
  }
#else
  void *mem = mmap(nullptr, allocSize, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) {
    stack = new VMValue[STACK_MAX]();
    stackIsMMap = false;
    std::cerr << "Warning: mmap failed, stack without guard page" << std::endl;
  } else {
    stack = (VMValue *)mem;
    mprotect((char *)mem + STACK_BYTES, GUARD_SIZE, PROT_NONE);
    stackIsMMap = true;
  }
#endif
  stackTop = stack;

  currentVM = this;
  StdLib::registerAll(this);
}

VM::~VM() {
  if (stack) {
    if (stackIsMMap) {
#ifdef _WIN32
      VirtualFree(stack, 0, MEM_RELEASE);
#else
      munmap(stack, STACK_BYTES + GUARD_SIZE);
#endif
    } else {
      delete[] stack;
    }
    stack = nullptr;
  }
}

void VM::resetCoverage() {
  Obj *obj = objects;
  while (obj) {
    if (obj->type == ObjType::OBJ_FUNCTION) {
      auto func = reinterpret_cast<ObjFunction *>(obj);
      if (func->chunk) {
        func->chunk->resetCoverage();
      }
    }
    obj = obj->nextObj;
  }
}

void VM::resetStack() { stackTop = stack; }

void VM::push(VMValue value) { *stackTop++ = value; }

VMValue VM::pop() {
  VMValue value = *(stackTop - 1);
  stackTop--;
  return value;
}

VMValue VM::peek(int distance) {
  return stack[(stackTop - stack) - 1 - distance];
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
#define READ_CONSTANT()                                                        \
  (frame->closure->function->chunk->constants[READ_BYTE()])
#define READ_SHORT()                                                           \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

InterpretResult VM::runtimeError(const std::string &message) {
  if (catchJumpEnabled) {
    longjmp(catchJmpBuf, 1);
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

      std::string fname =
          function->filename.empty() ? "<unknown>" : function->filename;
      // if fname has path, extract only basename for cleaner output like
      // 'test.try'
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
  if (setjmp(holder.buf) != 0) {
    stackOverflowJmpBuf = nullptr;
    jitClosure = nullptr;
    return runtimeError("Stack overflow.");
  }

  for (;;) {
    uint8_t instruction;
    instruction = READ_BYTE();
    if (collectCoverage) {
      Chunk *chunk = frame->closure->function->chunk;
      size_t offset = static_cast<size_t>(frame->ip - chunk->code.data() - 1);
      if (offset < chunk->coverage.size())
        chunk->coverage[offset]++;
    }
    switch (instruction) {
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
        ObjString *strA =
            a.isString() ? a.asString() : new ObjString(a.toString());
        ObjString *strB =
            b.isString() ? b.asString() : new ObjString(b.toString());
        push(new ObjString(strA, strB));
      } else {
        return runtimeError(
            std::string("Operands must be numbers or strings."));
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
        push(static_cast<double>(static_cast<int32_t>(a.asNumber()) &
                                 static_cast<int32_t>(b.asNumber())));
      } else {
        return runtimeError("Operands must be numbers.");
      }
      break;
    }
    case static_cast<uint8_t>(OpCode::OP_BIT_OR): {
      VMValue b = pop();
      VMValue a = pop();
      if (a.isNumber() && b.isNumber()) {
        push(static_cast<double>(static_cast<int32_t>(a.asNumber()) |
                                 static_cast<int32_t>(b.asNumber())));
      } else {
        return runtimeError("Operands must be numbers.");
      }
      break;
    }
    case static_cast<uint8_t>(OpCode::OP_BIT_XOR): {
      VMValue b = pop();
      VMValue a = pop();
      if (a.isNumber() && b.isNumber()) {
        push(static_cast<double>(static_cast<int32_t>(a.asNumber()) ^
                                 static_cast<int32_t>(b.asNumber())));
      } else {
        return runtimeError("Operands must be numbers.");
      }
      break;
    }
    case static_cast<uint8_t>(OpCode::OP_BIT_SHIFT_LEFT): {
      VMValue b = pop();
      VMValue a = pop();
      if (a.isNumber() && b.isNumber()) {
        push(static_cast<double>(static_cast<int32_t>(a.asNumber())
                                 << static_cast<int32_t>(b.asNumber())));
      } else {
        return runtimeError("Operands must be numbers.");
      }
      break;
    }
    case static_cast<uint8_t>(OpCode::OP_BIT_SHIFT_RIGHT): {
      VMValue b = pop();
      VMValue a = pop();
      if (a.isNumber() && b.isNumber()) {
        push(static_cast<double>(static_cast<int32_t>(a.asNumber()) >>
                                 static_cast<int32_t>(b.asNumber())));
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
          closure->upvalues.push_back(
              captureUpvalue(&stack[frame->stackStart + index]));
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
        return runtimeError(
            std::string("Can only set elements in lists or maps."));
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
        return runtimeError(std::string("Undefined superclass method '") +
                            methodName + "'.");
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
      std::string callerClass =
          frame->closure ? frame->closure->function->enclosingClassName : "";

      if (instanceVal.isInstance()) {
        auto instance = instanceVal.asInstance();
        VMAccessModifier mod = VMAccessModifier::PUBLIC;
        if (instance->klass->fieldModifiers.count(name))
          mod = instance->klass->fieldModifiers[name];
        if (!checkAccess(mod, instance->klass, callerClass)) {
          return runtimeError(std::string("Access error: Cannot set '") + name +
                              "'.");
        }
        instance->fields[name] = value;
        push(value);
      } else if (instanceVal.isClass()) {
        auto klass = instanceVal.asClass();
        klass->statics[name] = value;
        push(value);
      } else {
        return runtimeError(
            std::string("Only instances and classes have properties."));
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
