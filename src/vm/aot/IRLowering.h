#ifndef TRYPILLIA_IR_LOWERING_H
#define TRYPILLIA_IR_LOWERING_H

#include <string>

#include "../compiler/Chunk.h"
#include "IR.h"

namespace trypillia::aot
{

// Lowers a single ObjFunction's Chunk to a typed linear IR. This
// is the AOT equivalent of the analysis pass inside
// JITCompiler::compileMathFunction. The lowering is *backend
// independent* — it produces IR that any CodeSink (x64-object)
// can consume.
//
// Supports the full Trypillia language: arithmetic, comparisons,
// control flow, globals, closures, upvalues, classes, methods,
// properties, lists, maps, indexing, iteration, and all object
// operations. Operations that require runtime support are lowered
// to CallRuntime/CallRuntimeVoid instructions that invoke the
// corresponding jit_*_helper symbols from libtrypillia_rt.
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
