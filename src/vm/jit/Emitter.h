#pragma once
#include <cstdint>
#include <stddef.h>

typedef double (*JitFunc)(void *, double *, int, double);

// Abstract base class for machine code generation
class JitEmitter {
  public:
    virtual ~JitEmitter() = default;

    // Memory and invocation management
    virtual void emitPrologue(int maxLocals) = 0;
    virtual void emitEpilogue(int maxLocals) = 0;
    virtual void emitReturnValue(int stackOffset) = 0;

    // Stack operations (stackOffset is a relative index in our virtual stack)
    virtual void emitLoadConst(int stackOffset, double value) = 0;
    virtual void emitGetLocal(int stackOffset, int localSlot) = 0;
    virtual void emitSetLocal(int localSlot, int stackOffset) = 0;
    virtual void emitMove(int targetOffset, int srcOffset) = 0;

    // Arithmetic operations
    virtual void emitAdd(int targetOffset, int srcOffset) = 0;
    virtual void emitSub(int targetOffset, int srcOffset) = 0;
    virtual void emitMul(int targetOffset, int srcOffset) = 0;
    virtual void emitDiv(int targetOffset, int srcOffset) = 0;
    virtual void emitMod(int targetOffset, int srcOffset) = 0;

    // Bitwise operations
    virtual void emitBitAnd(int targetOffset, int srcOffset) = 0;
    virtual void emitBitOr(int targetOffset, int srcOffset) = 0;
    virtual void emitBitXor(int targetOffset, int srcOffset) = 0;
    virtual void emitBitNot(int targetOffset) = 0;
    virtual void emitBitShl(int targetOffset, int srcOffset) = 0;
    virtual void emitBitShr(int targetOffset, int srcOffset) = 0;

    // Comparisons (write result as 1.0 (true) or 0.0 (false))
    virtual void emitCmpEq(int targetOffset, int srcOffset) = 0;
    virtual void emitCmpNe(int targetOffset, int srcOffset) = 0;
    virtual void emitCmpLt(int targetOffset, int srcOffset) = 0;
    virtual void emitCmpLe(int targetOffset, int srcOffset) = 0;
    virtual void emitCmpGt(int targetOffset, int srcOffset) = 0;
    virtual void emitCmpGe(int targetOffset, int srcOffset) = 0;

    // Unary operations
    virtual void emitNot(int targetOffset) = 0;
    virtual void emitNegate(int targetOffset) = 0;

    // Calls and Globals
    virtual void emitCallDynamic(int targetOffset, int calleeOffset, int argCount) = 0;
    virtual void emitGetGlobal(const std::string &name, int targetOffset) = 0;
    virtual void emitSetGlobal(const std::string &name, int sourceOffset) = 0;

    // Arrays and maps
    virtual void emitIndexGet(int targetOffset, int objectOffset, int indexOffset) = 0;
    virtual void emitIndexSet(int objectOffset, int indexOffset, int valueOffset) = 0;

    // Object operations
    virtual void emitBuildList(int targetOffset, int count) = 0;
    virtual void emitBuildMap(int targetOffset, int count) = 0;
    virtual void emitPropertyGet(int objectOffset, const std::string &name) = 0;
    virtual void emitPropertySet(int objectOffset, const std::string &name) = 0;
    virtual void emitIterHasNext(int targetOffset) = 0;
    virtual void setCapturedLocals(const std::vector<int> &slots) = 0;

    // Class operations
    virtual void emitCreateClass(int targetOffset, const std::string &name) = 0;
    virtual void emitCreateAbstractClass(int targetOffset, const std::string &name) = 0;
    virtual void emitBindMethod(int targetOffset, const std::string &name, bool isAbstract) = 0;
    virtual void emitBindStaticMethod(int targetOffset, const std::string &name) = 0;
    virtual void emitInherit(int targetOffset) = 0;
    virtual void emitMixin(int targetOffset) = 0;
    virtual void emitGetSuper(int targetOffset, const std::string &name) = 0;
    virtual void emitFieldModifier(int targetOffset, const std::string &name, uint8_t modifier) = 0;

    // Closure and upvalue operations
    virtual void emitCreateClosure(int targetOffset, double funcRaw, const uint8_t *upvalueBytes, int upvalueCount) = 0;
    virtual void emitGetUpvalue(int targetOffset, int slot) = 0;
    virtual void emitSetUpvalue(int slot, int sourceOffset) = 0;
    virtual void emitCloseUpvalue(int stackOffset) = 0;

    // Control Flow
    virtual void bindLabel(size_t byteCodeIndex) = 0;
    virtual void emitJump(size_t targetByteCodeIndex) = 0;
    virtual void emitJumpIfFalse(int stackOffset, size_t targetByteCodeIndex) = 0;

    // Finalize and return function pointer
    virtual JitFunc finalize() = 0;
};
