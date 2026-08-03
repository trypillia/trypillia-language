#ifndef TRYPILLIA_IR_H
#define TRYPILLIA_IR_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../compiler/Value.h"

namespace trypillia::aot
{

enum class IROp : uint8_t
{
    ConstNum,    // dst = immD
    Move,        // dst = src1

    LoadLocal,   // dst = locals[slot]
    StoreLocal,  // locals[slot] = src1
    LoadArg,     // dst = args[slot]
    StoreArg,    // args[slot] = src1

    Add, // dst = src1 + src2
    Sub, // dst = src1 - src2
    Mul, // dst = src1 * src2
    Div, // dst = src1 / src2
    Mod, // dst = fmod(src1, src2) -- calls jit_mod_helper

    And,  // dst = (int64)src1 & (int64)src2
    Or,   // dst = (int64)src1 | (int64)src2
    Xor,  // dst = (int64)src1 ^ (int64)src2
    Shl,  // dst = (int64)src1 << (int64)src2
    Shr,  // dst = (int64)src1 >> (int64)src2
    Not,  // dst = (src1 == 0) ? 1 : 0
    BitNot, // dst = ~(int64)src1
    Neg,  // dst = -src1

    CmpEq, // dst = (src1 == src2) ? 1 : 0
    CmpNe, // dst = (src1 != src2) ? 1 : 0
    CmpLt, // dst = (src1 <  src2) ? 1 : 0
    CmpLe, // dst = (src1 <= src2) ? 1 : 0
    CmpGt, // dst = (src1 >  src2) ? 1 : 0
    CmpGe, // dst = (src1 >= src2) ? 1 : 0

    Jump,          // pc = target (label index)
    JumpIfFalse,   // if (src1 == 0) pc = target
    JumpIfTrue,    // if (src1 != 0) pc = target

    // Runtime calls. Arguments and return are NaN-boxed doubles
    // (passed/returned as raw uint64 via xmm0/rax). The runtime helper
    // symbol is resolved by the linker; backend records a relocation.
    CallRuntime,  // dst = runtime(args[src1..src1+argc-1])
    CallRuntimeVoid, // no return (e.g. jit_set_global_helper)

    // For recursive tail calls: call into a *known* AOT-compiled function
    // whose symbol will be defined in this same object file. The argCount
    // is implicit (function arity).
    CallDirect,   // dst = functionSymbol(args[1..argc])

    // For OP_RETURN: place src1 in xmm0 / ret-register, then `ret`.
    Return,       // return src1

    // Recursive base case fast-path: identical to JIT's `hasBaseCase` heuristic.
    // if (src1 <  immD) { dst = src1; Jump to endLabel } else fallthrough
    RecursiveBaseCase,

    Nop,
};

struct IRInstr
{
    IROp op = IROp::Nop;

    int dst = -1;        // virtual register (output)
    int src1 = -1;       // virtual register (operand 1, or -1 for imm)
    int src2 = -1;       // virtual register (operand 2, or -1)

    int slot = -1;       // for LoadLocal/StoreLocal/LoadArg/StoreArg
    int argc = 0;        // for CallRuntime/CallDirect (number of VMValue args)
    int endLabel = -1;   // for RecursiveBaseCase (target label after the fast path)

    double immD = 0.0;   // numeric immediate (ConstNum, RecursiveBaseCase threshold)

    std::string symbol;  // for CallRuntime/CallDirect: name of the helper to call

    size_t sourceBytecodeOffset = 0; // for diagnostics / deopt metadata
};

struct IRFunction
{
    std::string name;            // mangled symbol: "<file>_<class>_<func>"
    int arity = 0;
    int maxArity = 0;
    int maxLocal = 0;

    std::vector<IRInstr> code;
    // `labels[i]` is a forward/backward label (target of Jump/JumpIfFalse).
    // Labels use a flat counter: every label is a position-in-instr-stream
    // (or -1 for "not yet resolved" — backend resolves via two-pass emit).
    std::vector<int> labelTargetInstr; // label id -> instr index
    // Map of "label id" -> set of instr indices that jump to it
    std::vector<std::vector<size_t>> labelJumps;

    int createLabel()
    {
        int id = static_cast<int>(labelTargetInstr.size());
        labelTargetInstr.push_back(-1);
        labelJumps.push_back({});
        return id;
    }

    void bindLabel(int labelId, size_t instrIndex)
    {
        labelTargetInstr[labelId] = static_cast<int>(instrIndex);
    }

    void jumpTo(int labelId, size_t fromInstr)
    {
        labelJumps[labelId].push_back(fromInstr);
    }
};

} // namespace trypillia::aot

#endif // TRYPILLIA_IR_H
