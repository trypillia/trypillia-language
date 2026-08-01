#pragma once
#include <cstddef>

#include "../compiler/Chunk.h"
#include "../compiler/Value.h"

// ============================================================
// ABI constants for JIT compiler
//
// All C++ struct field offsets are computed via offsetof() at
// compile time. This guarantees correctness regardless of the
// platform, compiler version, or standard library, and allows
// struct layout to be changed without silently breaking the
// JIT.
//
// offsetof() on non-standard-layout types (with virtual
// functions) is conditionally-supported by C++ but works on
// GCC/Clang. The -Winvalid-offsetof warning is suppressed
// because we explicitly validate offsets at build time.
// ============================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

// --- Obj base (with virtual destructor) ---
// +0: vtable pointer (8 bytes)
// +8: Obj::type (4 bytes)
// +12: padding (4 bytes)
// +16: Obj::nextObj (8 bytes)
// +24: Obj::isMarked (1 byte)
// +25-31: padding
// sizeof(Obj) = 32
static constexpr int OBJ_TYPE_OFFSET = offsetof(Obj, type);

// --- ObjType enum values used in JIT comparisons ---
static constexpr int OBJ_TYPE_CLOSURE_INT =
    static_cast<int>(ObjType::OBJ_CLOSURE);

// --- ObjClosure ---
// +0-31: Obj base (32 bytes)
// +32: ObjClosure::function (ObjFunction*, 8 bytes)
// +40: std::vector<ObjUpvalue*> upvalues (24 bytes)
// sizeof(ObjClosure) = 64
static constexpr int OBJ_CLOSURE_FUNCTION_OFFSET =
    offsetof(ObjClosure, function);

// --- ObjFunction ---
// +0-31: Obj base (32 bytes)
// +32: std::string name (implementation-dependent size)
// Then: arity, maxArity, chunk, isAbstract, statics, etc.
// +jitAddr: void* (varies by platform)
static constexpr int OBJ_FUNCTION_JITADDR_OFFSET =
    offsetof(ObjFunction, jitAddr);

// ============================================================
// JIT virtual stack slot conventions
//
// The JIT's virtual stack is an array of doubles in the caller's
// argument buffer, addressed via SLJIT_S1 (args_ptr).
// Slot index * sizeof(double) = byte offset from args_ptr.
//
// Layout: [callee][arg0][arg1]...[argN][locals...][scratch...]
// ============================================================

// Slot 0: callee/closure value (for dynamic calls)
// Slot 1: first function argument
#define JIT_FIRST_ARG_SLOT 1

// Scratch slot for double<->raw uint64 bit conversion
// Must be beyond the maximum possible stack depth
static constexpr int JIT_SCRATCH_SLOT = 250;

// Base slot for upvalue metadata buffer in closure creation
static constexpr int JIT_UPVALUE_DATA_SLOT = 200;

// Maximum virtual stack slots
static constexpr int JIT_MAX_SLOTS = 256;

#pragma GCC diagnostic pop
