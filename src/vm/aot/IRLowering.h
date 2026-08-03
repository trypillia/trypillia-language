#ifndef TRYPILLIA_IR_LOWERING_H
#define TRYPILLIA_IR_LOWERING_H

#include <string>

#include "../compiler/Chunk.h"
#include "IR.h"

namespace trypillia::aot
{

// Lowers a single ObjFunction's Chunk to a typed linear IR. This
// is the Phase-1 AOT equivalent of the analysis pass inside
// JITCompiler::compileMathFunction. The lowering is *backend
// independent* — it produces IR that any CodeSink (sljit today,
// x64-object tomorrow) can consume.
//
// Phase-1 scope (matches what `compileMathFunction` already
// supports, minus classes/lists/maps/closures which are runtime
// helpers used by every backend identically):
//
//   * Pure numeric functions with at most 1-arg recursive calls
//   * Locals/args, arith, comparisons, control flow
//   * Modulo (folded to jit_mod_helper call)
//   * Bitwise ops (folded to int ALU via CVTTSD2SI/CVTSI2SD)
//   * Recursive base-case fast path (the "hasBaseCase" heuristic
//     from the JIT, applied identically so AOT and JIT emit
//     byte-equivalent fast paths for fib-like functions)
//
// Out of Phase-1 scope (will be added by re-using jit_*_helper
// symbols, Phase 3 in the RFC): strings, lists, maps, classes,
// closures, exceptions, FFI, async.
class IRLowering
{
  public:
    // Returns true on success; on failure (a Chunk construct we
    // don't yet support), returns false and `outError` is set.
    // The caller (AOTModule) is then expected to fall back to
    // the legacy "self-contained bytecode" build.
    static bool lower(ObjFunction *function, IRFunction &out, std::string &outError);
};

} // namespace trypillia::aot

#endif // TRYPILLIA_IR_LOWERING_H
