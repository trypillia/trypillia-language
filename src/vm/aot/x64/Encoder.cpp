#include "Encoder.h"

#include <cassert>
#include <cstring>

namespace trypillia::aot::x64
{

// ============================================================
// ModR/M helpers
//
// All GP-register addressing we generate is of the form:
//   [reg + disp0]  (mod=01, rm=reg, no SIB; 1-byte disp8 when possible)
//   [reg + disp32] (mod=10, rm=reg, no SIB; 4-byte disp32)
//   [rsp/r12 + disp] needs SIB even with no index (forced by rm=4)
//
// For RBP/R13 with mod=00 (no disp), rm=5 is repurposed to mean
// [RIP+disp32], so we always emit a disp8 with mod=01 when the base is
// RBP/R13 in disp0 mode. This is the classic "mandatory SIB / disp8"
// gotcha on x86-64.
// ============================================================

static bool isHighReg(Reg64 r)
{
    auto v = static_cast<uint8_t>(r);
    return v >= 8;
}

static uint8_t low3(Reg64 r)
{
    return static_cast<uint8_t>(r) & 7;
}

// --- GP register-register moves ---

void Encoder::movRR(Reg64 dst, Reg64 src)
{
    // REX.W + 89 /r  (MOV r/m64, r64)
    emitRex(true, isHighReg(src) ? 1 : 0, 0, isHighReg(dst) ? 1 : 0);
    emitByte(0x89);
    emitModRM(0b11, low3(src), low3(dst));
}

void Encoder::movMR(Reg64 base, Reg64 src)
{
    // MOV [base+disp0], r64 -> REX.W + 89 /r with mod=01, rm=base, disp8=0
    if (base == Reg64::RSP || base == Reg64::R12)
    {
        // Need SIB for rsp/r12
        emitRex(true, isHighReg(src) ? 1 : 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x89);
        emitModRM(0b01, low3(src), 0b100);
        emitSIB(0, 0b100, low3(base)); // [rsp/r12]
        emitByte(0);
    }
    else if (base == Reg64::RBP || base == Reg64::R13)
    {
        // mod=01 with rbp/r13 is fine (just a 0 disp8)
        emitRex(true, isHighReg(src) ? 1 : 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x89);
        emitModRM(0b01, low3(src), low3(base));
        emitByte(0);
    }
    else
    {
        emitRex(true, isHighReg(src) ? 1 : 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x89);
        emitModRM(0b00, low3(src), low3(base));
    }
}

void Encoder::movRM(Reg64 dst, Reg64 base)
{
    // MOV r64, [base+disp0] -> REX.W + 8B /r with mod=01
    if (base == Reg64::RSP || base == Reg64::R12)
    {
        emitRex(true, 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x8B);
        emitModRM(0b01, low3(dst), 0b100);
        emitSIB(0, 0b100, low3(base));
        emitByte(0);
    }
    else if (base == Reg64::RBP || base == Reg64::R13)
    {
        emitRex(true, 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x8B);
        emitModRM(0b01, low3(dst), low3(base));
        emitByte(0);
    }
    else
    {
        emitRex(true, 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x8B);
        emitModRM(0b00, low3(dst), low3(base));
    }
}

void Encoder::movRI64(Reg64 dst, uint64_t imm)
{
    // REX.W + B8+rd io  (MOV r64, imm64)
    emitRex(true, 0, 0, isHighReg(dst) ? 1 : 0);
    emitByte(0xB8 + low3(dst));
    emitU64(imm);
}

void Encoder::movRI32(Reg64 dst, int32_t imm)
{
    // REX.W + C7 /0 id  (MOV r/m64, imm32, sign-extended)
    if (dst == Reg64::RSP || dst == Reg64::R12)
    {
        emitRex(true, 0, 0, isHighReg(dst) ? 1 : 0);
        emitByte(0xC7);
        emitModRM(0b11, 0, 0b100);
        emitSIB(0, 0b100, low3(dst));
    }
    else
    {
        emitRex(true, 0, 0, isHighReg(dst) ? 1 : 0);
        emitByte(0xC7);
        emitModRM(0b11, 0, low3(dst));
    }
    emitI32(imm);
}

void Encoder::movMI32(Reg64 base, int32_t imm)
{
    // MOV [base], imm32 -> REX.W + C7 /0
    if (base == Reg64::RSP || base == Reg64::R12)
    {
        emitRex(true, 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0xC7);
        emitModRM(0b00, 0, 0b100);
        emitSIB(0, 0b100, low3(base));
    }
    else
    {
        emitRex(true, 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0xC7);
        emitModRM(0b00, 0, low3(base));
    }
    emitI32(imm);
}

// --- XMM scalar double ---

void Encoder::movsdRR(Xmm dst, Xmm src)
{
    // F2 0F 10 /r  (MOVSD xmm, xmm)
    auto d = static_cast<uint8_t>(dst);
    auto s = static_cast<uint8_t>(src);
    emitByte(0xF2);
    if (d >= 8 || s >= 8)
        emitRex(true, s >= 8 ? 1 : 0, 0, d >= 8 ? 1 : 0);
    emitByte(0x0F);
    emitByte(0x10);
    emitModRM(0b11, s & 7, d & 7);
}

void Encoder::movsdRM(Xmm dst, Reg64 base, int32_t disp)
{
    // F2 REX.W 0F 10 /r  (MOVSD xmm, [r64+disp])
    auto d = static_cast<uint8_t>(dst);
    bool disp8 = (disp >= -128 && disp <= 127);
    if (base == Reg64::RSP || base == Reg64::R12)
    {
        emitByte(0xF2);
        emitRex(true, 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x0F);
        emitByte(0x10);
        if (disp8)
        {
            emitModRM(0b01, d & 7, 0b100);
            emitSIB(0, 0b100, low3(base));
            emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            emitModRM(0b10, d & 7, 0b100);
            emitSIB(0, 0b100, low3(base));
            emitI32(disp);
        }
    }
    else
    {
        emitByte(0xF2);
        emitRex(true, 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x0F);
        emitByte(0x10);
        if (disp8)
        {
            emitModRM(0b01, d & 7, low3(base));
            emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            emitModRM(0b10, d & 7, low3(base));
            emitI32(disp);
        }
    }
}

void Encoder::movsdMR(Reg64 base, int32_t disp, Xmm src)
{
    // F2 REX.W 0F 11 /r  (MOVSD [r64+disp], xmm)
    auto s = static_cast<uint8_t>(src);
    bool disp8 = (disp >= -128 && disp <= 127);
    if (base == Reg64::RSP || base == Reg64::R12)
    {
        emitByte(0xF2);
        emitRex(true, s >= 8 ? 1 : 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x0F);
        emitByte(0x11);
        if (disp8)
        {
            emitModRM(0b01, s & 7, 0b100);
            emitSIB(0, 0b100, low3(base));
            emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            emitModRM(0b10, s & 7, 0b100);
            emitSIB(0, 0b100, low3(base));
            emitI32(disp);
        }
    }
    else
    {
        emitByte(0xF2);
        emitRex(true, s >= 8 ? 1 : 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x0F);
        emitByte(0x11);
        if (disp8)
        {
            emitModRM(0b01, s & 7, low3(base));
            emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            emitModRM(0b10, s & 7, low3(base));
            emitI32(disp);
        }
    }
}

static void emitSseArithRM(uint8_t opcode_ext, Xmm dst, Reg64 base, int32_t disp, Encoder &self)
{
    // F2 REX.W 0F (58|5C|59|5E) /r  (ADDSD/SUBSD/MULSD/DIVSD xmm, [r64+disp])
    auto d = static_cast<uint8_t>(dst);
    bool disp8 = (disp >= -128 && disp <= 127);
    self.emitByte(0xF2);
    self.emitRex(true, 0, 0, isHighReg(base) ? 1 : 0);
    self.emitByte(0x0F);
    self.emitByte(opcode_ext);
    if (base == Reg64::RSP || base == Reg64::R12)
    {
        if (disp8)
        {
            self.emitModRM(0b01, d & 7, 0b100);
            self.emitSIB(0, 0b100, low3(base));
            self.emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            self.emitModRM(0b10, d & 7, 0b100);
            self.emitSIB(0, 0b100, low3(base));
            self.emitI32(disp);
        }
    }
    else
    {
        if (disp8)
        {
            self.emitModRM(0b01, d & 7, low3(base));
            self.emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            self.emitModRM(0b10, d & 7, low3(base));
            self.emitI32(disp);
        }
    }
}

void Encoder::addsdRM(Xmm dst, Reg64 base, int32_t disp)
{
    emitSseArithRM(0x58, dst, base, disp, *this);
}
void Encoder::subsdRM(Xmm dst, Reg64 base, int32_t disp)
{
    emitSseArithRM(0x5C, dst, base, disp, *this);
}
void Encoder::mulsdRM(Xmm dst, Reg64 base, int32_t disp)
{
    emitSseArithRM(0x59, dst, base, disp, *this);
}
void Encoder::divsdRM(Xmm dst, Reg64 base, int32_t disp)
{
    emitSseArithRM(0x5E, dst, base, disp, *this);
}

static void emitSseArithMR(uint8_t opcode_ext, Reg64 base, int32_t disp, Xmm src, Encoder &self)
{
    // F2 REX.W 0F (58|5C|59|5E) /r with mod=src  (r/m is memory)
    auto s = static_cast<uint8_t>(src);
    bool disp8 = (disp >= -128 && disp <= 127);
    self.emitByte(0xF2);
    self.emitRex(true, s >= 8 ? 1 : 0, 0, isHighReg(base) ? 1 : 0);
    self.emitByte(0x0F);
    self.emitByte(opcode_ext);
    if (base == Reg64::RSP || base == Reg64::R12)
    {
        if (disp8)
        {
            self.emitModRM(0b01, s & 7, 0b100);
            self.emitSIB(0, 0b100, low3(base));
            self.emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            self.emitModRM(0b10, s & 7, 0b100);
            self.emitSIB(0, 0b100, low3(base));
            self.emitI32(disp);
        }
    }
    else
    {
        if (disp8)
        {
            self.emitModRM(0b01, s & 7, low3(base));
            self.emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            self.emitModRM(0b10, s & 7, low3(base));
            self.emitI32(disp);
        }
    }
}

void Encoder::addsdMR(Reg64 base, int32_t disp, Xmm src)
{
    emitSseArithMR(0x58, base, disp, src, *this);
}
void Encoder::subsdMR(Reg64 base, int32_t disp, Xmm src)
{
    emitSseArithMR(0x5C, base, disp, src, *this);
}
void Encoder::mulsdMR(Reg64 base, int32_t disp, Xmm src)
{
    emitSseArithMR(0x59, base, disp, src, *this);
}
void Encoder::divsdMR(Reg64 base, int32_t disp, Xmm src)
{
    emitSseArithMR(0x5E, base, disp, src, *this);
}

void Encoder::xorpsRR(Xmm dst, Xmm src)
{
    // 0F 57 /r  (XORPS xmm, xmm — used to zero)
    auto d = static_cast<uint8_t>(dst);
    auto s = static_cast<uint8_t>(src);
    if (d >= 8 || s >= 8)
        emitRex(false, s >= 8 ? 1 : 0, 0, d >= 8 ? 1 : 0);
    emitByte(0x0F);
    emitByte(0x57);
    emitModRM(0b11, s & 7, d & 7);
}

void Encoder::cvtsi2sdRM(Xmm dst, Reg64 base, int32_t disp)
{
    // F2 REX.W 0F 2A /r  (CVTSI2SD xmm, r/m64)
    auto d = static_cast<uint8_t>(dst);
    bool disp8 = (disp >= -128 && disp <= 127);
    emitByte(0xF2);
    if (base == Reg64::RSP || base == Reg64::R12)
    {
        emitRex(true, 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x0F);
        emitByte(0x2A);
        if (disp8)
        {
            emitModRM(0b01, d & 7, 0b100);
            emitSIB(0, 0b100, low3(base));
            emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            emitModRM(0b10, d & 7, 0b100);
            emitSIB(0, 0b100, low3(base));
            emitI32(disp);
        }
    }
    else
    {
        emitRex(true, 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x0F);
        emitByte(0x2A);
        if (disp8)
        {
            emitModRM(0b01, d & 7, low3(base));
            emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            emitModRM(0b10, d & 7, low3(base));
            emitI32(disp);
        }
    }
}

void Encoder::cvttsd2siRM(Reg64 dst, Xmm src)
{
    // F2 REX.W 0F 2C /r  (CVTTSD2SI r64, xmm)
    auto d = static_cast<uint8_t>(dst);
    auto s = static_cast<uint8_t>(src);
    emitByte(0xF2);
    emitRex(true, s >= 8 ? 1 : 0, 0, d >= 8 ? 1 : 0);
    emitByte(0x0F);
    emitByte(0x2C);
    emitModRM(0b11, s & 7, d & 7);
}

void Encoder::ucomisdRM(Xmm a, Reg64 base, int32_t disp)
{
    // 66 0F 2E /r  (UCOMISD xmm, xmm/m64)
    auto aa = static_cast<uint8_t>(a);
    bool disp8 = (disp >= -128 && disp <= 127);
    emitByte(0x66);
    if (base == Reg64::RSP || base == Reg64::R12)
    {
        emitRex(false, 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x0F);
        emitByte(0x2E);
        if (disp8)
        {
            emitModRM(0b01, aa & 7, 0b100);
            emitSIB(0, 0b100, low3(base));
            emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            emitModRM(0b10, aa & 7, 0b100);
            emitSIB(0, 0b100, low3(base));
            emitI32(disp);
        }
    }
    else
    {
        emitRex(false, 0, 0, isHighReg(base) ? 1 : 0);
        emitByte(0x0F);
        emitByte(0x2E);
        if (disp8)
        {
            emitModRM(0b01, aa & 7, low3(base));
            emitByte(static_cast<int8_t>(disp));
        }
        else
        {
            emitModRM(0b10, aa & 7, low3(base));
            emitI32(disp);
        }
    }
}

// --- Integer ALU (used for bitwise ops on doubles-as-bits) ---

void Encoder::andRR(Reg64 dst, Reg64 src)
{
    // REX.W + 21 /r  (AND r/m64, r64)
    emitRex(true, isHighReg(src) ? 1 : 0, 0, isHighReg(dst) ? 1 : 0);
    emitByte(0x21);
    emitModRM(0b11, low3(src), low3(dst));
}

void Encoder::orRR(Reg64 dst, Reg64 src)
{
    // REX.W + 09 /r
    emitRex(true, isHighReg(src) ? 1 : 0, 0, isHighReg(dst) ? 1 : 0);
    emitByte(0x09);
    emitModRM(0b11, low3(src), low3(dst));
}

void Encoder::xorRR(Reg64 dst, Reg64 src)
{
    // REX.W + 31 /r
    emitRex(true, isHighReg(src) ? 1 : 0, 0, isHighReg(dst) ? 1 : 0);
    emitByte(0x31);
    emitModRM(0b11, low3(src), low3(dst));
}

void Encoder::xorRI(Reg64 dst, int32_t imm)
{
    // REX.W + 81 /6 id  (XOR r/m64, imm32)
    emitRex(true, 0, 0, isHighReg(dst) ? 1 : 0);
    emitByte(0x81);
    emitModRM(0b11, 6, low3(dst));
    emitI32(imm);
}

void Encoder::movRC(Reg64 dst, Reg64 src)
{
    // Generic MOV r64, r64 — provided as an alias for clarity in
    // bitwise op sequences (where we need to move a value into RCX
    // before SHL/SHR with CL).
    movRR(dst, src);
}

void Encoder::shlRC(Reg64 dst)
{
    // REX.W + D3 /4  (SHL r/m64, CL)
    emitRex(true, 0, 0, isHighReg(dst) ? 1 : 0);
    emitByte(0xD3);
    emitModRM(0b11, 4, low3(dst));
}

void Encoder::shrRC(Reg64 dst)
{
    // REX.W + D3 /7  (SAR r/m64, CL)
    emitRex(true, 0, 0, isHighReg(dst) ? 1 : 0);
    emitByte(0xD3);
    emitModRM(0b11, 7, low3(dst));
}

void Encoder::subRspI8(int8_t imm)
{
    // REX.W + 83 /5 ib  (SUB r/m64, imm8)
    emitRex(true, 0, 0, 0);
    emitByte(0x83);
    emitModRM(0b11, 5, low3(Reg64::RSP));
    emitByte(static_cast<uint8_t>(static_cast<int8_t>(imm)));
}

void Encoder::addRspI8(int8_t imm)
{
    // REX.W + 83 /0 ib
    emitRex(true, 0, 0, 0);
    emitByte(0x83);
    emitModRM(0b11, 0, low3(Reg64::RSP));
    emitByte(static_cast<uint8_t>(static_cast<int8_t>(imm)));
}

// --- Control flow ---

void Encoder::callSymbol(const std::string &symbol)
{
    // E8 cd  (CALL rel32)
    emitByte(0xE8);
    size_t dispOffset = code_.size();
    emitI32(0); // placeholder
    recordReloc(dispOffset, symbol, -4, RelocKind::R_X86_64_PC32);
}

void Encoder::jmpLabel(int labelId)
{
    // E9 cd  (JMP rel32)
    emitByte(0xE9);
    size_t dispOffset = code_.size();
    emitI32(0); // placeholder
    if (labelId >= static_cast<int>(pendingLabelJumps_.size()))
    {
        // Make sure the bookkeeping vector is large enough
        pendingLabelJumps_.resize(labelId + 1);
    }
    recordPendingJump(labelId, dispOffset);
}

void Encoder::jccLabel(int cc, int labelId)
{
    // 0F 8x cd  (Jcc rel32, near)
    assert(cc >= 0 && cc <= 15);
    emitByte(0x0F);
    emitByte(static_cast<uint8_t>(0x80 + cc));
    size_t dispOffset = code_.size();
    emitI32(0);
    if (labelId >= static_cast<int>(pendingLabelJumps_.size()))
    {
        pendingLabelJumps_.resize(labelId + 1);
    }
    recordPendingJump(labelId, dispOffset);
}

void Encoder::ret()
{
    emitByte(0xC3);
}

void Encoder::patchLabels(const std::vector<size_t> &labelOffsets)
{
    for (size_t labelId = 0; labelId < pendingLabelJumps_.size(); ++labelId)
    {
        if (labelId >= labelOffsets.size())
            continue;
        size_t target = labelOffsets[labelId];
        for (size_t dispOff : pendingLabelJumps_[labelId])
        {
            // disp = target - (dispOff + 4)  (relative to next instruction)
            int32_t disp = static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(dispOff + 4));
            std::memcpy(&code_[dispOff], &disp, sizeof(int32_t));
        }
    }
}

void Encoder::leaRipSymbol(Reg64 dst, const std::string &symbol, int32_t addend)
{
    // REX.W + 8D /r with mod=00, rm=5  (RIP-relative, disp32 follows)
    emitRex(true, 0, 0, isHighReg(dst) ? 1 : 0);
    emitByte(0x8D);
    emitModRM(0b00, low3(dst), 0b101);
    size_t dispOff = code_.size();
    emitI32(0);
    recordReloc(dispOff, symbol, addend - 4, RelocKind::R_X86_64_PC32);
}

} // namespace trypillia::aot::x64
