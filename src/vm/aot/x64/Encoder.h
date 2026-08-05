#ifndef TRYPILLIA_X64_ENCODER_H
#define TRYPILLIA_X64_ENCODER_H

#include <cstdint>
#include <string>
#include <vector>

namespace trypillia::aot::x64
{

// =============================================================
// x86-64 (System V AMD64 ABI) instruction encoder.
//
// Scopes of supported instructions — deliberately tiny. This is not a
// general-purpose x86-64 assembler; it covers exactly what the Trypillia
// AOT backend needs to emit for the IR defined in IR.h:
//
//   - register-register / register-memory / memory-register / reg-imm moves
//   - scalar double-precision (SSE2) add/sub/mul/div, comparison, cvt
//   - signed integer add/sub/and/or/xor/shl/shr/not (for bitwise ops on
//     doubles-as-bits; see Bridge.cpp's emitBitAnd family)
//   - RIP-relative LEA (for accessing .rodata constants and globals)
//   - CALL rel32 with a relocation against an external symbol
//   - JMP / Jcc rel32 (forward/backward labels resolved in a 2nd pass)
//   - PUSH/POP a single 64-bit GP register
//   - RET
//
// We follow the Intel SDM encoding rules for REX, ModR/M, SIB. Operand-
// size / REX.W are explicit; we never rely on the default operand size.
// =============================================================

enum class Reg64 : uint8_t
{
    RAX = 0,
    RCX = 1,
    RDX = 2,
    RBX = 3,
    RSP = 4,
    RBP = 5,
    RSI = 6,
    RDI = 7,
    R8 = 8,
    R9 = 9,
    R10 = 10,
    R11 = 11,
    R12 = 12,
    R13 = 13,
    R14 = 14,
    R15 = 15,
};

enum class Xmm : uint8_t
{
    XMM0 = 0,
    XMM1 = 1,
    XMM2 = 2,
    XMM3 = 3,
    XMM4 = 4,
    XMM5 = 5,
    XMM6 = 6,
    XMM7 = 7,
    XMM8 = 8,
    XMM9 = 9,
    XMM10 = 10,
    XMM11 = 11,
    XMM12 = 12,
    XMM13 = 13,
    XMM14 = 14,
    XMM15 = 15,
};

enum class RelocKind : uint32_t
{
    // 32-bit signed PC-relative offset used by CALL/Jcc/JMP rel32 and
    // by LEA using RIP-relative addressing. addend is the offset that
    // the linker should add to the resolved symbol value (e.g. -4 for
    // the displacement field of a CALL since the offset is computed
    // from the end of the instruction).
    R_X86_64_PC32 = 2,

    // 64-bit absolute address used by MOV imm64 (e.g. absolute pointer
    // to a global variable). The linker resolves a 64-bit symbol
    // reference at this offset.
    R_X86_64_64 = 1,
};

struct Reloc
{
    size_t offset;       // byte offset within the encoded code buffer
    std::string symbol;  // symbol name to resolve against
    int64_t addend;      // constant added to the resolved symbol value
    RelocKind kind;
};

// A forward/backward label. The encoder resolves labels in two passes:
// pass 1 lays down instructions and records (label, jump-instr-offset)
// pairs; pass 2 fills in the 4-byte displacement field of every jump.
struct PendingLabel
{
    int labelId;
    size_t atCodeOffset; // where the 4-byte disp starts
};

class Encoder
{
  public:
    Encoder() = default;

    // --- Low-level emit helpers ---

    void emitByte(uint8_t b)
    {
        code_.push_back(b);
    }
    void emitU32(uint32_t v)
    {
        emitByte(static_cast<uint8_t>(v & 0xff));
        emitByte(static_cast<uint8_t>((v >> 8) & 0xff));
        emitByte(static_cast<uint8_t>((v >> 16) & 0xff));
        emitByte(static_cast<uint8_t>((v >> 24) & 0xff));
    }
    void emitI32(int32_t v)
    {
        emitU32(static_cast<uint32_t>(v));
    }
    void emitU64(uint64_t v)
    {
        for (int i = 0; i < 8; i++)
            emitByte(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
    }

    // REX prefix. REX.W=1 forces 64-bit operand size (mandatory for our
    // use of GP regs in the SysV ABI). R=1 extends the ModR/M reg field,
    // X=1 extends SIB index, B=1 extends ModR/M r/m or SIB base.
    void emitRex(bool W, uint8_t R, uint8_t X, uint8_t B)
    {
        uint8_t rex = 0x40 | (W ? 0x08 : 0) | (R & 1 ? 0x04 : 0) | (X & 1 ? 0x02 : 0) | (B & 1 ? 0x01 : 0);
        emitByte(rex);
    }

    // ModR/M byte helper.
    void emitModRM(uint8_t mod, uint8_t reg, uint8_t rm)
    {
        emitByte((mod << 6) | ((reg & 7) << 3) | (rm & 7));
    }

    // SIB byte helper (only used for [reg + index*scale] addressing,
    // e.g. locals[rbp+rax*8]; we keep this small for now).
    void emitSIB(uint8_t scale, uint8_t index, uint8_t base)
    {
        emitByte((scale << 6) | ((index & 7) << 3) | (base & 7));
    }

    // --- GP register moves (64-bit) ---

    // MOV r64, r64
    void movRR(Reg64 dst, Reg64 src);

    // MOV r/m64, r64 (dst in memory via [base+disp0])
    void movMR(Reg64 base, Reg64 src);

    // MOV r64, r/m64 (src in memory via [base+disp0])
    void movRM(Reg64 dst, Reg64 base);

    // MOV r64, imm64
    void movRI64(Reg64 dst, uint64_t imm);

    // MOV r32, imm32 (zero-extends to 64-bit in x86-64; useful for
    // loading int constants when the upper bits don't matter)
    void movRI32(Reg64 dst, int32_t imm);

    // MOV [r64], imm32 (sign-extended; for storing small ints in
    // local slots)
    void movMI32(Reg64 base, int32_t disp, int32_t imm);

    // LEA r64, [base+disp32]
    void leaRM(Reg64 dst, Reg64 base, int32_t disp);

    // --- XMM (SSE2 scalar double) ---

    // MOVSD xmm, xmm
    void movsdRR(Xmm dst, Xmm src);

    // MOVSD xmm, [r64+disp8/32] (load scalar double)
    void movsdRM(Xmm dst, Reg64 base, int32_t disp);

    // MOVSD [r64+disp], xmm (store scalar double)
    void movsdMR(Reg64 base, int32_t disp, Xmm src);

    // ADDSD/ SUBSD/ MULSD/ DIVSD xmm, [r64+disp] (the typical shape we
    // emit: operand1 in xmm, operand2 in memory)
    void addsdRM(Xmm dst, Reg64 base, int32_t disp);
    void subsdRM(Xmm dst, Reg64 base, int32_t disp);
    void mulsdRM(Xmm dst, Reg64 base, int32_t disp);
    void divsdRM(Xmm dst, Reg64 base, int32_t disp);

    // Reverse forms: dst is in memory, src in xmm
    void addsdMR(Reg64 base, int32_t disp, Xmm src);
    void subsdMR(Reg64 base, int32_t disp, Xmm src);
    void mulsdMR(Reg64 base, int32_t disp, Xmm src);
    void divsdMR(Reg64 base, int32_t disp, Xmm src);

    // XORPS xmm, xmm  (zero a XMM register; also used to clear the
    // upper bits before a CVT)
    void xorpsRR(Xmm dst, Xmm src);

    // CVTSI2SD xmm, r/m64 (convert signed 64-bit int to scalar double)
    void cvtsi2sdRM(Xmm dst, Reg64 base, int32_t disp);

    // CVTTSD2SI r64, xmm (truncate scalar double to signed 64-bit int)
    void cvttsd2siRM(Reg64 dst, Xmm src);

    // UCOMISD xmm, [r64+disp] (compare scalar doubles, set EFLAGS)
    void ucomisdRM(Xmm a, Reg64 base, int32_t disp);

    // --- Integer ALU (for bitwise ops on doubles-as-bits) ---

    void andRR(Reg64 dst, Reg64 src);
    void orRR(Reg64 dst, Reg64 src);
    void xorRR(Reg64 dst, Reg64 src);
    void xorRI(Reg64 dst, int32_t imm); // XOR r64, imm32
    void shlRC(Reg64 dst);              // SHL r64, cl  (count in CL)
    void shrRC(Reg64 dst);              // SAR r64, cl
    void movRC(Reg64 dst, Reg64 src);   // MOV r64, rcx-equivalent (for CL)

    // SUB rsp, imm8 / ADD rsp, imm8  (small stack adjust for spills)
    void subRspI8(int8_t imm);
    void addRspI8(int8_t imm);

    // --- Control flow ---

    // CALL rel32 (relocation: R_X86_64_PC32 against `symbol` with
    // addend = -4 because the PC-relative disp is measured from the
    // end of the CALL instruction).
    void callSymbol(const std::string &symbol);

    // JMP rel32 — internal, used for jumps to labels in the same
    // function. The label system records a pending relocation that
    // is patched in patchLabels().
    void jmpLabel(int labelId);

    // Jcc rel32 — `cc` is the Jcc opcode suffix. We use the encoding
    // 0F 8x for near jumps.
    //   cc: 0=jo,1=jno,2=jb,3=jnb,4=jz,5=jnz,6=jbe,7=ja,
    //       8=js,9=jns,a=jp,b=jnp,c=jl,d=jge,e=jle,f=jg
    // (for our purposes we use jz/jnz/jl/jle/jg/jge mostly)
    void jccLabel(int cc, int labelId);

    // RET
    void ret();

    // --- Two-pass label patching ---

    // After the IR has been emitted (and all instructions have known
    // sizes — x86 instructions are variable length, but their length
    // is fully determined at emit time, so a single pass is enough if
    // we know target offsets). We record forward jumps; this method
    // walks them and writes the 4-byte displacement.
    //
    // labelOffsets[labelId] = absolute offset within code_ of the
    // instruction that the label points to. The caller computes this
    // from the IR's labelTargetInstr (which is filled by IRLowering).
    void patchLabels(const std::vector<size_t> &labelOffsets);

    // --- RIP-relative data access ---

    // LEA r64, [rip + disp32]  -- used to load the address of a
    // .rodata entry. The linker will resolve the relocation.
    // We use a synthetic symbol name ("<funcname>.Lconst.<n>") and
    // record a R_X86_64_PC32 relocation.
    void leaRipSymbol(Reg64 dst, const std::string &symbol, int32_t addend = 0);

    // --- Accessors ---

    const std::vector<uint8_t> &code() const
    {
        return code_;
    }
    std::vector<uint8_t> &code()
    {
        return code_;
    }
    const std::vector<Reloc> &relocs() const
    {
        return relocs_;
    }
    size_t currentOffset() const
    {
        return code_.size();
    }

    void recordReloc(size_t offset, const std::string &sym, int64_t addend, RelocKind kind)
    {
        relocs_.push_back({offset, sym, addend, kind});
    }

  private:
    std::vector<uint8_t> code_;
    std::vector<Reloc> relocs_;

    // Forward-jump bookkeeping: for each label id, the list of
    // (code-offset where the 4-byte disp is) entries that need patching.
    std::vector<std::vector<size_t>> pendingLabelJumps_;

    int allocLabel()
    {
        int id = static_cast<int>(pendingLabelJumps_.size());
        pendingLabelJumps_.push_back({});
        return id;
    }
    void recordPendingJump(int labelId, size_t dispOffset)
    {
        pendingLabelJumps_[labelId].push_back(dispOffset);
    }

  public:
    // Public so X64ObjectBackend can compute forward-jump targets
    // using its own instr-index->code-offset map.
    const std::vector<std::vector<size_t>> &pendingJumps() const
    {
        return pendingLabelJumps_;
    }
};

} // namespace trypillia::aot::x64

#endif // TRYPILLIA_X64_ENCODER_H
