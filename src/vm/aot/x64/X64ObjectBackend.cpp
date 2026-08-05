#include "X64ObjectBackend.h"

#include <cassert>
#include <cstring>
#include <unordered_map>

namespace trypillia::aot::x64
{

// =============================================================
// Calling convention (mirrors JITCompiler::compileMathFunction
// in JITCompiler.cpp + Bridge.cpp exactly):
//
//   extern "C" double trypillia_aot_entry(
//       void *vm,         // RDI
//       double *args,     // RSI   -- virtual stack base, locals live here
//       int    argCount,  // EDX
//       double n);        // XMM0  -- implicit "n" / return value
//
// Locals[i] live at args[i*8]. Same layout as the JIT's virtual
// stack; runtime helpers (jit_call_helper, etc.) consume args[]
// exactly the way the JIT does, so the AOT path is binary-compatible
// with all jit_*_helper functions.
//
// Callee-saved: R12 = saved args_ptr, R13 = saved vm_ptr.
// Scratch XMM: XMM4..XMM7 (XMM0..XMM3 used as helper-call ABI
// registers, so we keep our temporaries separate to avoid clobbering
// across call sites).
// =============================================================

using Reg = Reg64;
using XReg = Xmm;

static int32_t slotOff(int slot)
{
    return slot * 8;
}

// Write a 64-bit constant as 4+4 bytes via two MOVMI32. This
// avoids needing an Encoder primitive for "MOVSD xmm, imm64"
// (which doesn't exist in real x86-64) and avoids needing rodata
// for every ConstNum.
static void emitMovImm64ToMem(Encoder &enc, Reg base, int32_t off, uint64_t bits)
{
    int32_t lo = static_cast<int32_t>(bits & 0xffffffff);
    int32_t hi = static_cast<int32_t>((bits >> 32) & 0xffffffff);
    enc.movMI32(base, off, lo);
    enc.movMI32(base, off + 4, hi);
}

BackendResult X64ObjectBackend::compile(const IRFunction &ir)
{
    BackendResult out;
    out.entrySymbol = ir.name;
    Encoder enc;

    // ---- Prologue ----
    //   push r12; push r13; mov r12, rsi; mov r13, rdi
    enc.emitByte(0x41);
    enc.emitByte(0x54); // push r12
    enc.emitByte(0x41);
    enc.emitByte(0x55); // push r13
    enc.movRR(Reg::R12, Reg::RSI);
    enc.movRR(Reg::R13, Reg::RDI);

    // We need an instrIndex -> codeOffset map. The IR's labels are
    // bound to *instr indices* (labelTargetInstr), so we record
    // the offset at the start of each instruction.
    std::vector<size_t> instrOffsets(ir.code.size(), 0);
    std::unordered_map<int, size_t> labelToOffset;
    std::vector<size_t> labelOffsetsById(ir.labelTargetInstr.size(), 0);

    // 1st pass: walk IR linearly, emit code, record offsets.
    for (size_t i = 0; i < ir.code.size(); ++i)
    {
        // Bind any labels that point to this instr index.
        for (size_t lid = 0; lid < ir.labelTargetInstr.size(); ++lid)
        {
            if (ir.labelTargetInstr[lid] == static_cast<int>(i) && labelToOffset.count(static_cast<int>(lid)) == 0)
            {
                size_t off = enc.currentOffset();
                labelToOffset[static_cast<int>(lid)] = off;
                labelOffsetsById[lid] = off;
            }
        }
        instrOffsets[i] = enc.currentOffset();

        const IRInstr &ins = ir.code[i];
        switch (ins.op)
        {
        case IROp::Nop:
            break;

        case IROp::StoreAddr: {
            // Compute address of local variable: R12 + src1*8
            // Store it to args[dst*8]
            enc.emitByte(0x4C);
            enc.emitByte(0x8D);
            enc.emitModRM(0b10, 0, 0b100);
            enc.emitSIB(0, 0b100, 4);
            enc.emitI32(slotOff(ins.src1));
            enc.movMR(Reg::R12, Reg::RAX);
            break;
        }

        case IROp::ConstNum: {
            // args[dst*8] = immD
            uint64_t bits;
            std::memcpy(&bits, &ins.immD, sizeof(double));
            emitMovImm64ToMem(enc, Reg::R12, slotOff(ins.dst), bits);
            break;
        }

        case IROp::Move:
        case IROp::StoreLocal:
        case IROp::StoreArg: {
            int dstOff = (ins.op == IROp::StoreArg) ? slotOff(ins.slot) : slotOff(ins.dst);
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.movsdMR(Reg::R12, dstOff, XReg::XMM4);
            break;
        }
        case IROp::LoadLocal:
        case IROp::LoadArg:
            // vreg is the slot; value is already at args[dst*8].
            (void)ins;
            break;

        case IROp::Add: {
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.addsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src2));
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM4);
            break;
        }
        case IROp::Sub: {
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.subsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src2));
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM4);
            break;
        }
        case IROp::Mul: {
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.mulsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src2));
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM4);
            break;
        }
        case IROp::Div: {
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.divsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src2));
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM4);
            break;
        }
        case IROp::Mod: {
            // fmod(a, b) — call jit_mod_helper
            enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
            enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
            enc.callSymbol("jit_mod_helper");
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            break;
        }

        case IROp::And:
        case IROp::Or:
        case IROp::Xor: {
            // Bitwise op on doubles-as-bits. We convert both
            // operands to int64 via CVTTSD2SI, do the integer op,
            // and CVTSI2SD back.
            // The trick: CVTTSD2SI reads from an XMM register, but
            // CVTSI2SD reads from a memory operand. So we store
            // the int result to args[0] (the callee slot, unused
            // at this IR level) and CVTSI2SD from there.
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.cvttsd2siRM(Reg::RAX, XReg::XMM4);
            enc.movsdRM(XReg::XMM5, Reg::R12, slotOff(ins.src2));
            enc.cvttsd2siRM(Reg::RBX, XReg::XMM5);
            if (ins.op == IROp::And)
                enc.andRR(Reg::RAX, Reg::RBX);
            else if (ins.op == IROp::Or)
                enc.orRR(Reg::RAX, Reg::RBX);
            else
                enc.xorRR(Reg::RAX, Reg::RBX);
            enc.movMR(Reg::R12, Reg::RAX); // args[0] = rax
            enc.cvtsi2sdRM(XReg::XMM4, Reg::R12, 0);
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM4);
            break;
        }
        case IROp::Shl:
        case IROp::Shr: {
            // SHL/SHR with count in CL (rcx).
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.cvttsd2siRM(Reg::RAX, XReg::XMM4);
            enc.movsdRM(XReg::XMM5, Reg::R12, slotOff(ins.src2));
            enc.cvttsd2siRM(Reg::RCX, XReg::XMM5);
            if (ins.op == IROp::Shl)
                enc.shlRC(Reg::RAX);
            else
                enc.shrRC(Reg::RAX);
            enc.movMR(Reg::R12, Reg::RAX);
            enc.cvtsi2sdRM(XReg::XMM4, Reg::R12, 0);
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM4);
            break;
        }
        case IROp::BitNot: {
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.cvttsd2siRM(Reg::RAX, XReg::XMM4);
            enc.xorRI(Reg::RAX, -1);
            enc.movMR(Reg::R12, Reg::RAX);
            enc.cvtsi2sdRM(XReg::XMM4, Reg::R12, 0);
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM4);
            break;
        }
        case IROp::Not: {
            // Logical NOT: result = (src1 == 0) ? 1 : 0
            // UCOMISD xmm4, xmm5(=0) -> ZF=1 iff equal
            // SETZ al, MOVZX rax, CVTSI2SD.
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.xorpsRR(XReg::XMM5, XReg::XMM5); // xmm5 = 0
            enc.emitByte(0x66);
            enc.emitByte(0x0F);
            enc.emitByte(0x2E);
            enc.emitModRM(0b11, 4, 5); // UCOMISD xmm4, xmm5
            enc.emitByte(0x0F);
            enc.emitByte(0x94);
            enc.emitByte(0xC0); // SETZ al
            enc.emitByte(0x48);
            enc.emitByte(0x0F);
            enc.emitByte(0xB6);
            enc.emitByte(0xC0); // MOVZX rax, al
            enc.movMR(Reg::R12, Reg::RAX);
            enc.cvtsi2sdRM(XReg::XMM4, Reg::R12, 0);
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM4);
            break;
        }
        case IROp::Neg: {
            enc.xorpsRR(XReg::XMM4, XReg::XMM4); // 0
            enc.subsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM4);
            break;
        }

        case IROp::CmpEq:
        case IROp::CmpNe:
        case IROp::CmpLt:
        case IROp::CmpLe:
        case IROp::CmpGt:
        case IROp::CmpGe: {
            // SETcc + MOVZX + CVTSI2SD
            uint8_t setcc = 0;
            switch (ins.op)
            {
            case IROp::CmpEq:
                setcc = 0x94;
                break; // SETZ
            case IROp::CmpNe:
                setcc = 0x95;
                break; // SETNZ
            case IROp::CmpLt:
                setcc = 0x92;
                break; // SETB
            case IROp::CmpLe:
                setcc = 0x96;
                break; // SETBE
            case IROp::CmpGt:
                setcc = 0x97;
                break; // SETA
            case IROp::CmpGe:
                setcc = 0x93;
                break; // SETAE
            default:
                break;
            }
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.movsdRM(XReg::XMM5, Reg::R12, slotOff(ins.src2));
            enc.emitByte(0x66);
            enc.emitByte(0x0F);
            enc.emitByte(0x2E);
            enc.emitModRM(0b11, 4, 5); // UCOMISD xmm4, xmm5
            enc.emitByte(0x0F);
            enc.emitByte(setcc);
            enc.emitByte(0xC0); // SETcc al
            enc.emitByte(0x48);
            enc.emitByte(0x0F);
            enc.emitByte(0xB6);
            enc.emitByte(0xC0); // MOVZX rax, al
            enc.movMR(Reg::R12, Reg::RAX);
            enc.cvtsi2sdRM(XReg::XMM4, Reg::R12, 0);
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM4);
            break;
        }

        case IROp::Jump: {
            // src1 holds the label id (reused field).
            enc.jmpLabel(ins.src1);
            break;
        }
        case IROp::JumpIfFalse: {
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.xorpsRR(XReg::XMM5, XReg::XMM5);
            enc.emitByte(0x66);
            enc.emitByte(0x0F);
            enc.emitByte(0x2E);
            enc.emitModRM(0b11, 4, 5);
            enc.jccLabel(4, ins.src2); // JZ to label
            break;
        }
        case IROp::JumpIfTrue: {
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            enc.xorpsRR(XReg::XMM5, XReg::XMM5);
            enc.emitByte(0x66);
            enc.emitByte(0x0F);
            enc.emitByte(0x2E);
            enc.emitModRM(0b11, 4, 5);
            enc.jccLabel(5, ins.src2); // JNZ
            break;
        }

        case IROp::CallRuntime: {
            auto &rod = out.rodata;
            auto loadRodataStr = [&](const std::string &name) -> std::string {
                for (auto &r : rod)
                {
                    if (r.symbol == name)
                        return name;
                }
                RodataEntry e;
                e.symbol = "trypillia_str_" + std::to_string(rod.size());
                e.data.assign(name.begin(), name.end());
                e.data.push_back(0);
                rod.push_back(e);
                return e.symbol;
            };
            auto emitRodataLEA = [&](Reg64 reg, const std::string &name) {
                std::string sym = loadRodataStr(name);
                enc.leaRipSymbol(reg, sym);
            };

            const std::string &sym = ins.symbol;
            if (sym == "jit_call_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.emitByte(0x4C);
                enc.emitByte(0x8D);
                enc.emitModRM(0b10, 6, 0b100);
                enc.emitSIB(0, 0b100, 4);
                enc.emitI32(slotOff(ins.src1));
                enc.movRI32(Reg::RDX, ins.argc);
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_get_global_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_set_global_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.callSymbol(sym);
            }
            else if (sym == "jit_build_list_helper" || sym == "jit_build_map_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.emitByte(0x4C);
                enc.emitByte(0x8D);
                enc.emitModRM(0b10, 6, 0b100);
                enc.emitSIB(0, 0b100, 4);
                enc.emitI32(slotOff(ins.src1));
                enc.movRI32(Reg::RDX, ins.argc);
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_index_get_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_index_set_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                enc.movsdRM(XReg::XMM2, Reg::R12, slotOff(ins.src3));
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_property_get_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_property_set_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_iter_has_next_helper")
            {
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_create_class_helper" || sym == "jit_create_abstract_class_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_bind_method_helper")
            {
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.movRI32(Reg::RDX, ins.immI);
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_bind_static_method_helper")
            {
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_inherit_helper" || sym == "jit_mixin_helper")
            {
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                enc.callSymbol(sym);
            }
            else if (sym == "jit_get_super_helper")
            {
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_field_modifier_helper")
            {
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.movRI32(Reg::RDX, ins.immI);
                enc.callSymbol(sym);
            }
            else if (sym == "jit_create_closure_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.dst));
                enc.emitByte(0x4C);
                enc.emitByte(0x8D);
                enc.emitModRM(0b10, 6, 0b100);
                enc.emitSIB(0, 0b100, 4);
                enc.emitI32(slotOff(ins.src1));
                enc.movRI32(Reg::RDX, ins.argc);
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_get_upvalue_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movRI32(Reg::RDX, ins.immI);
                enc.callSymbol(sym);
                enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            }
            else if (sym == "jit_set_upvalue_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movRI32(Reg::RDX, ins.immI);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.callSymbol(sym);
            }
            else if (sym == "jit_close_upvalue_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.emitByte(0x4C);
                enc.emitByte(0x8D);
                enc.emitModRM(0b10, 6, 0b100);
                enc.emitSIB(0, 0b100, 4);
                enc.emitI32(slotOff(ins.src1));
                enc.callSymbol(sym);
            }
            else
            {
                // Fallback: generic call (vm in RDI, XMM0=src1, RSI=args*, EDX=argc)
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.emitByte(0x4C);
                enc.emitByte(0x8D);
                enc.emitModRM(0b10, 6, 0b100);
                enc.emitSIB(0, 0b100, 4);
                enc.emitI32(slotOff(ins.src1));
                enc.movRI32(Reg::RDX, ins.argc);
                enc.callSymbol(sym);
                if (ins.dst >= 0)
                {
                    enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
                }
            }
            break;
        }
        case IROp::CallRuntimeVoid: {
            auto &rod = out.rodata;
            auto loadRodataStr = [&](const std::string &name) -> std::string {
                for (auto &r : rod)
                {
                    if (r.symbol == name)
                        return name;
                }
                RodataEntry e;
                e.symbol = "trypillia_str_" + std::to_string(rod.size());
                e.data.assign(name.begin(), name.end());
                e.data.push_back(0);
                rod.push_back(e);
                return e.symbol;
            };
            auto emitRodataLEA = [&](Reg64 reg, const std::string &name) {
                std::string sym = loadRodataStr(name);
                enc.leaRipSymbol(reg, sym);
            };

            const std::string &sym = ins.symbol;
            if (sym == "jit_set_global_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.callSymbol(sym);
            }
            else if (sym == "jit_define_global_helper" || sym == "jit_set_global_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.callSymbol(sym);
            }
            else if (sym == "jit_inherit_helper" || sym == "jit_mixin_helper")
            {
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                enc.callSymbol(sym);
            }
            else if (sym == "jit_set_upvalue_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movRI32(Reg::RDX, ins.immI);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.callSymbol(sym);
            }
            else if (sym == "jit_close_upvalue_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.emitByte(0x4C);
                enc.emitByte(0x8D);
                enc.emitModRM(0b10, 6, 0b100);
                enc.emitSIB(0, 0b100, 4);
                enc.emitI32(slotOff(ins.src1));
                enc.callSymbol(sym);
            }
            else if (sym == "jit_field_modifier_helper")
            {
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.movRI32(Reg::RDX, ins.immI);
                enc.callSymbol(sym);
            }
            else if (sym == "jit_index_set_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                enc.movsdRM(XReg::XMM2, Reg::R12, slotOff(ins.src3));
                enc.callSymbol(sym);
            }
            else if (sym == "jit_property_set_helper")
            {
                enc.movRR(Reg::RDI, Reg::R13);
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                emitRodataLEA(Reg::RSI, ins.strArg);
                enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                enc.callSymbol(sym);
            }
            else
            {
                // Fallback
                enc.movRR(Reg::RDI, Reg::R13);
                if (ins.src1 >= 0)
                {
                    enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
                }
                if (ins.src2 >= 0)
                {
                    enc.movsdRM(XReg::XMM1, Reg::R12, slotOff(ins.src2));
                }
                if (!ins.strArg.empty())
                {
                    emitRodataLEA(Reg::RSI, ins.strArg);
                }
                if (ins.argc > 0)
                {
                    enc.movRI32(Reg::RDX, ins.argc);
                }
                if (ins.immI != 0)
                {
                    enc.movRI32(Reg::RDX, ins.immI);
                }
                enc.callSymbol(sym);
            }
            break;
        }
        case IROp::CallDirect: {
            // Direct call to another AOT function (recursive call).
            // rdi=vm, rsi=args, edx=argc, xmm0=args[1].
            enc.movRR(Reg::RDI, Reg::R13);
            enc.movRR(Reg::RSI, Reg::R12);
            enc.movRI32(Reg::RDX, ins.argc);
            if (ins.argc > 0)
            {
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(1));
            }
            else
            {
                enc.xorpsRR(XReg::XMM0, XReg::XMM0);
            }
            enc.callSymbol(ins.symbol);
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM0);
            break;
        }

        case IROp::Return: {
            if (ins.src1 >= 0)
            {
                enc.movsdRM(XReg::XMM0, Reg::R12, slotOff(ins.src1));
            }
            else
            {
                enc.xorpsRR(XReg::XMM0, XReg::XMM0);
            }
            enc.emitByte(0x41);
            enc.emitByte(0x5E); // pop r14 (not used but keeps stack balanced)
            enc.emitByte(0x41);
            enc.emitByte(0x5D); // pop r13
            enc.emitByte(0x41);
            enc.emitByte(0x5C); // pop r12
            enc.ret();
            break;
        }

        case IROp::RecursiveBaseCase: {
            // Inline fast path: if (args[src1*8] < immD)
            //     { args[dst*8] = args[src1*8]; jmp endLabel; }
            // The IR's lowering follows this with a CallDirect (the
            // recursive call) and an unconditional Jump to endLabel,
            // so this is a single JB-out followed by fallthrough to
            // the call.
            enc.movsdRM(XReg::XMM4, Reg::R12, slotOff(ins.src1));
            // Spill immD to args[dst*8] as a memory operand for
            // UCOMISD. This is safe because the recursive call (which
            // is the next instr) overwrites args[dst*8] with the
            // result anyway.
            uint64_t bits;
            std::memcpy(&bits, &ins.immD, sizeof(double));
            emitMovImm64ToMem(enc, Reg::R12, slotOff(ins.dst), bits);
            // UCOMISD xmm4, [r12 + dst*8]
            // Encoding: 66 REX(0x41 since R12 is high) 0F 2E
            //   ModRM(10, 4, 100) SIB(0,4,4) disp32
            enc.emitByte(0x66);
            enc.emitByte(0x41);
            enc.emitByte(0x0F);
            enc.emitByte(0x2E);
            enc.emitModRM(0b10, 4, 0b100);
            enc.emitSIB(0, 0b100, 4);
            enc.emitI32(slotOff(ins.dst));
            // JB (cc=2) to the endLabel
            enc.jccLabel(2, ins.endLabel);
            // args[dst*8] = args[src1*8] for the base case
            enc.movsdMR(Reg::R12, slotOff(ins.dst), XReg::XMM4);
            // JMP endLabel (we set src1 here as a label id reuse)
            enc.jmpLabel(ins.endLabel);
            break;
        }
        } // switch
    } // for

    // Bind "end-of-function" labels (those pointing just past the
    // last instr, used for the RecursiveBaseCase fast-path).
    for (size_t lid = 0; lid < ir.labelTargetInstr.size(); ++lid)
    {
        if (ir.labelTargetInstr[lid] == static_cast<int>(ir.code.size()))
        {
            labelToOffset[static_cast<int>(lid)] = enc.currentOffset();
            labelOffsetsById[lid] = enc.currentOffset();
        }
    }

    // If no explicit RETURN was emitted, fall through to a default
    // return-0 (matches JIT's finalize behavior).
    bool hasReturn = false;
    for (const auto &ins : ir.code)
    {
        if (ins.op == IROp::Return)
        {
            hasReturn = true;
            break;
        }
    }
    if (!hasReturn)
    {
        enc.xorpsRR(XReg::XMM0, XReg::XMM0);
        enc.emitByte(0x41);
        enc.emitByte(0x5E);
        enc.emitByte(0x41);
        enc.emitByte(0x5D);
        enc.emitByte(0x41);
        enc.emitByte(0x5C);
        enc.ret();
    }

    // Patch forward jumps with the actual offsets.
    std::vector<size_t> byId(ir.labelTargetInstr.size(), 0);
    for (auto &kv : labelToOffset)
    {
        if (kv.first >= 0 && kv.first < static_cast<int>(byId.size()))
        {
            byId[kv.first] = kv.second;
        }
    }
    enc.patchLabels(byId);

    out.code = enc.code();
    out.relocs = enc.relocs();
    out.ok = true;
    return out;
}

} // namespace trypillia::aot::x64
