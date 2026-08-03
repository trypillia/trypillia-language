#ifndef TRYPILLIA_X64_OBJECT_BACKEND_H
#define TRYPILLIA_X64_OBJECT_BACKEND_H

#include <cstdint>
#include <string>
#include <vector>

#include "../CodeSink.h"
#include "../IR.h"
#include "Encoder.h"

namespace trypillia::aot::x64
{

// =============================================================
// X64ObjectBackend
//
// Translates the IR defined in IR.h to x86-64 machine bytes plus a
// list of relocations.  Does NOT write a .o file itself — that is
// the ObjectFile's job. The backend's responsibility ends at producing
//   1. code bytes (ready to land in .text)
//   2. relocations (ready to land in .rela.text)
//   3. rodata entries (constants used by LEA / MOVSD via RIP-relative)
//
// Register allocation strategy (Phase 1, deliberately simple):
//
//   SysV ABI argument registers: RDI=vm_ptr, RSI=args_ptr, RDX=argCount,
//   XMM0=n (the implicit FR0/return value).  We follow the JIT's
//   calling convention: callee expects vm in RDI, args in RSI, argCount
//   in EDX, and the implicit n (for OP_RETURN) in XMM0.  These map
//   one-to-one to the JIT's convention used in Bridge.cpp.
//
//   We dedicate XMM4..XMM7 as a small pool of 4 "scratch" XMM
//   registers used for in-flight arithmetic.  The IR's virtual
//   registers are *not* machine registers — they are "stack slots":
//   they live in a shadow stack at [args_ptr + slot*8], just like
//   the JIT's virtual stack.  XMM4..XMM7 are used purely as the
//   operand/result register for SSE2 ops (x86 requires one operand
//   in a register).
//
//   The slot count is bounded by JIT_MAX_SLOTS (256) — same cap as
//   the JIT (see ABI.h).
//
// The reason for this deliberately-boring design: it matches the JIT
// semantics 1:1, so we can drop the AOT and JIT paths into the same
// byte-for-byte-compatible behavior on the same input Chunk.  A
// future Phase can introduce a real linear-scan allocator.
// =============================================================

struct RodataEntry
{
    std::string symbol;        // local symbol name (e.g. ".LCfib_const_3")
    std::vector<uint8_t> data; // raw bytes (e.g. 8 bytes of a double)
};

struct BackendResult
{
    std::vector<uint8_t> code;
    std::vector<Reloc> relocs;
    std::vector<RodataEntry> rodata;
    std::string entrySymbol; // public symbol for this function
    bool ok = false;
    std::string error;
};

class X64ObjectBackend
{
  public:
    // Build a relocatable function from an IRFunction. The function's
    // `name` becomes the public symbol (mangled) that the rest of the
    // AOT binary / linker will resolve against.
    static BackendResult compile(const IRFunction &ir);

  private:
    // Implementation lives in X64ObjectBackend.cpp. We don't expose
    // the per-instruction emitters because the IR-to-x86 translation
    // is monolithic (each IRInstr may emit 1..8 machine instructions
    // and the register-window logic is interleaved).
};

} // namespace trypillia::aot::x64

#endif // TRYPILLIA_X64_OBJECT_BACKEND_H
