#ifndef TRYPILLIA_CODE_SINK_H
#define TRYPILLIA_CODE_SINK_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "IR.h"

namespace trypillia::aot
{

// A backend "sink" turns a stream of IR instructions into machine code.
// Two implementations exist:
//
//   - SljitCodeSink (eventual): wraps the existing UniversalEmitter so the
//     runtime-JIT keeps working. Not part of Phase 1 — Phase 1 only ships
//     the AOT path; the JIT remains using its current Emitter interface
//     directly (we did not change JITCompiler.cpp in this phase).
//
//   - X64ObjectBackend: emits x86-64 bytes into a per-function buffer and
//     records relocations against external symbols. A separate ObjectFile
//     then serializes the buffers into an ELF64 ET_REL .o.
//
// Why a new abstraction (not reusing JitEmitter 1:1):
//   JitEmitter is procedural ("do this sljit call now"). The AOT path
//   needs the IR to be re-walkable for size measurement (x86-64
//   instructions are variable-length, so we patch RIP-relative offsets in
//   a second pass). JitEmitter has no notion of "label as a first-class
//   value" and no relocation list.
//
// The sink is intentionally minimal: it consumes the IR linearly and
// exposes just enough for the AOT backend to translate each IRInstr to
// machine bytes + relocs. The runtime-JIT can later be retrofitted onto
// this same sink without disturbing JITCompiler.cpp's existing logic
// (that is Phase 2 work).
class CodeSink
{
  public:
    virtual ~CodeSink() = default;

    // Bind a label to the current code emission offset.
    virtual void bindLabel(int labelId) = 0;

    // Emit a single IR instruction. Implementations translate to
    // machine bytes (AOT) or sljit calls (JIT) internally.
    virtual void emit(const IRInstr &instr) = 0;

    // Finalize emission and return true on success. Backend may
    // perform a second pass to patch forward jumps / RIP-relative
    // addresses once all instruction sizes are known.
    virtual bool finalize() = 0;
};

} // namespace trypillia::aot

#endif // TRYPILLIA_CODE_SINK_H
