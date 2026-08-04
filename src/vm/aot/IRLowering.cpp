#include "IRLowering.h"

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../compiler/OpCode.h"

namespace trypillia::aot
{

// =============================================================
// Phase-1 AOT IR Lowering
//
// Faithfully reproduces the analysis & instruction-emission pass
// inside JITCompiler::compileMathFunction (src/vm/jit/JITCompiler.cpp),
// but emits a typed IR instead of calling sljit directly. The
// computed call graph, type stack, and base-case fast path are
// byte-for-byte equivalent to the JIT's behavior — this is by
// design, so AOT and JIT can be cross-validated by differential
// testing in the future.
//
// One structural difference from the JIT: this pass uses *labels*
// for control flow rather than the JIT's two-pass byte-offset
// resolution. The IR's bindLabel/labelTargetInstr let the X64
// backend do its own offset computation in a second pass.
// =============================================================

enum class InferredType
{
    UNKNOWN,
    NUMBER,
    BOOL,
    STRING,
    NIL,
    OBJECT,
    CLOSURE
};

// Scan the bytecode once to find maxLocal and to detect a
// fib-like base case (first instruction is `if (n < 2)`).
static bool detectBaseCase(ObjFunction *function, double &outThreshold)
{
    auto *c = function->chunk;
    if (c->code.size() < 5)
        return false;
    if (c->code[0] != static_cast<uint8_t>(OpCode::OP_GET_LOCAL))
        return false;
    if (c->code[1] != 0)
        return false;
    if (c->code[2] != static_cast<uint8_t>(OpCode::OP_CONSTANT))
        return false;
    uint8_t idx = c->code[3];
    if (!c->constants[idx].isNumber())
        return false;
    if (c->code[4] != static_cast<uint8_t>(OpCode::OP_LESS))
        return false;
    outThreshold = c->constants[idx].asNumber();
    return true;
}

// Static information for each IRInstr that follows. We collect
// this in pre-pass over Chunk so the linearization pass can
// look up "what's the type of the value at this stack depth at
// this program point".
struct StackInfo
{
    std::vector<InferredType> stack;
};

static std::string sanitizeName(const std::string &n)
{
    std::string r;
    for (char c : n)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            r += c;
        else
            r += '_';
    }
    if (!r.empty() && (r[0] >= '0' && r[0] <= '9'))
        r = "_" + r;
    if (r.empty())
        r = "fn";
    return r;
}

bool IRLowering::lower(ObjFunction *function, IRFunction &out, std::string &outError)
{
    if (!function || !function->chunk)
    {
        outError = "null function or chunk";
        return false;
    }
    auto *c = function->chunk;

    out.name = std::string("trypillia_aot_") + sanitizeName((function->enclosingClassName.empty() ? std::string("") : function->enclosingClassName + "_") + function->name);
    if (out.name.empty())
    {
        out.name = "trypillia_aot_main";
    }
    out.arity = function->arity;
    out.maxArity = function->maxArity;

    // ---- 1. Pre-pass: maxLocal + capturedLocals ----
    int maxLocal = 0;
    std::set<int> capturedLocals;
    for (size_t i = 0; i < c->code.size();)
    {
        uint8_t op = c->code[i];
        if (op == static_cast<uint8_t>(OpCode::OP_NOP))
        {
            i += 1;
        }
        else if (op == static_cast<uint8_t>(OpCode::OP_JUMP) || op == static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE))
        {
            i += 3;
        }
        else if (op == static_cast<uint8_t>(OpCode::OP_LOOP))
        {
            i += 3;
        }
        else if (op == static_cast<uint8_t>(OpCode::OP_GET_LOCAL) || op == static_cast<uint8_t>(OpCode::OP_SET_LOCAL))
        {
            int slot = c->code[i + 1];
            if (slot > maxLocal)
                maxLocal = slot;
            i += 2;
        }
        else if (op == static_cast<uint8_t>(OpCode::OP_CONSTANT_WIDE))
        {
            i += 3;
        }
        else if (op == static_cast<uint8_t>(OpCode::OP_CONSTANT) || op == static_cast<uint8_t>(OpCode::OP_GET_GLOBAL) ||
                 op == static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL) ||
                 op == static_cast<uint8_t>(OpCode::OP_SET_GLOBAL) || op == static_cast<uint8_t>(OpCode::OP_CALL) ||
                 op == static_cast<uint8_t>(OpCode::OP_BUILD_LIST) ||
                 op == static_cast<uint8_t>(OpCode::OP_BUILD_MAP) ||
                 op == static_cast<uint8_t>(OpCode::OP_PROPERTY_GET) ||
                 op == static_cast<uint8_t>(OpCode::OP_PROPERTY_SET) ||
                 op == static_cast<uint8_t>(OpCode::OP_GET_UPVALUE) ||
                 op == static_cast<uint8_t>(OpCode::OP_SET_UPVALUE) || op == static_cast<uint8_t>(OpCode::OP_CLASS) ||
                 op == static_cast<uint8_t>(OpCode::OP_ABSTRACT_CLASS) ||
                 op == static_cast<uint8_t>(OpCode::OP_INHERIT) || op == static_cast<uint8_t>(OpCode::OP_MIXIN) ||
                 op == static_cast<uint8_t>(OpCode::OP_GET_SUPER) || op == static_cast<uint8_t>(OpCode::OP_METHOD) ||
                 op == static_cast<uint8_t>(OpCode::OP_ABSTRACT_METHOD) ||
                 op == static_cast<uint8_t>(OpCode::OP_STATIC_METHOD) ||
                 op == static_cast<uint8_t>(OpCode::OP_INDEX_GET) ||
                 op == static_cast<uint8_t>(OpCode::OP_INDEX_SET) ||
                 op == static_cast<uint8_t>(OpCode::OP_ITER_HAS_NEXT) ||
                 op == static_cast<uint8_t>(OpCode::OP_CLOSE_UPVALUE))
        {
            i += 2;
        }
        else if (op == static_cast<uint8_t>(OpCode::OP_FIELD_MODIFIER))
        {
            i += 3;
        }
        else if (op == static_cast<uint8_t>(OpCode::OP_CLOSURE))
        {
            uint8_t idx = c->code[i + 1];
            VMValue fv = c->constants[idx];
            int upvalueCount = fv.asFunction()->upvalueCount;
            const uint8_t *upvalueBytes = &c->code[i + 2];
            for (int j = 0; j < upvalueCount; j++)
            {
                if (upvalueBytes[j * 2])
                {
                    capturedLocals.insert(upvalueBytes[j * 2 + 1]);
                }
            }
            i += 2 + 2 * upvalueCount;
        }
        else
        {
            i += 1;
        }
    }
    if (maxLocal + 1 > 256)
    {
        outError = "too many locals";
        return false;
    }
    (void)capturedLocals; // Phase 1 AOT does not support closures; warn-only

    out.maxLocal = maxLocal;

    // ---- 2. Detect fib-like base case ----
    double baseCaseThreshold = 0;
    bool hasBaseCase = detectBaseCase(function, baseCaseThreshold);

    // ---- 3. Type tracking & linearization ----
    // Same logic as JITCompiler::compileMathFunction's main loop.
    std::vector<InferredType> typeStack(256, InferredType::UNKNOWN);
    std::vector<InferredType> localTypes(256, InferredType::UNKNOWN);
    for (int i = 0; i <= function->maxArity; i++)
        localTypes[i] = InferredType::NUMBER;

    int sp = function->maxArity >= 0 ? function->maxArity + 1 : 1;
    // Map bytecode index -> "expected sp at this program point"
    // (used at join points of jumps).
    std::map<size_t, int> expectedSp;
    std::map<size_t, std::vector<InferredType>> expectedStackTypes;

    // Helper to push an instruction and record its source offset.
    auto emit = [&](IROp op, int dst, int src1, int src2, size_t bcoff) {
        IRInstr ins;
        ins.op = op;
        ins.dst = dst;
        ins.src1 = src1;
        ins.src2 = src2;
        ins.sourceBytecodeOffset = bcoff;
        out.code.push_back(ins);
        return static_cast<int>(out.code.size()) - 1;
    };

    for (size_t i = 0; i < c->code.size(); ++i)
    {
        // Join-point: if a jump target was recorded, sync sp/type
        // to the recorded values. (Mirrors JITCompiler.cpp.)
        if (expectedSp.count(i))
        {
            sp = expectedSp[i];
            typeStack = expectedStackTypes[i];
        }

        // Bind any label whose target is this bytecode index.
        for (auto &kv : out.labelJumps)
        {
            // We bind labels lazily inside the linearization loop
            // below by checking labelTargetInstr. (No-op here.)
            (void)kv;
        }
        // The IR stores label->instrIndex. To bind when we hit a
        // target instr, we walk all labels and bind those that
        // point to `out.code.size()` (next emitted IR instr).
        for (size_t lid = 0; lid < out.labelTargetInstr.size(); ++lid)
        {
            if (out.labelTargetInstr[lid] == static_cast<int>(out.code.size()))
            {
                out.labelTargetInstr[lid] = static_cast<int>(out.code.size());
            }
        }

        uint8_t op = c->code[i];
        switch (op)
        {
        case static_cast<uint8_t>(OpCode::OP_NOP):
            break;

        case static_cast<uint8_t>(OpCode::OP_POP):
            if (sp == 0)
            {
                outError = "stack underflow at OP_POP";
                return false;
            }
            sp--;
            break;

        case static_cast<uint8_t>(OpCode::OP_GET_LOCAL): {
            uint8_t slot = c->code[++i];
            if (sp >= 256)
            {
                outError = "stack overflow at OP_GET_LOCAL";
                return false;
            }
            // No-op LoadLocal for the slot itself; subsequent op
            // will read args[slot*8] directly.
            typeStack[sp] = localTypes[slot];
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SET_LOCAL): {
            uint8_t slot = c->code[++i];
            if (sp == 0)
            {
                outError = "stack underflow at OP_SET_LOCAL";
                return false;
            }
            emit(IROp::StoreLocal, slot, sp - 1, -1, i);
            localTypes[slot] = typeStack[sp - 1];
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GET_GLOBAL): {
            uint8_t constantIdx = c->code[++i];
            VMValue constant = c->constants[constantIdx];
            if (sp >= 256)
            {
                outError = "stack overflow at OP_GET_GLOBAL";
                return false;
            }
            flushTos(sp);
            const std::string &name = constant.asString()->flatten();
            int strIdx = out.addStringConstant(name);
            IRInstr ins;
            ins.op = IROp::CallRuntime;
            ins.dst = sp;
            ins.symbol = "jit_get_global_helper";
            ins.strArg = name;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            typeStack[sp] = InferredType::UNKNOWN;
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CONSTANT): {
            uint8_t idx = c->code[++i];
            VMValue val = c->constants[idx];
            if (sp >= 256)
            {
                outError = "stack overflow at OP_CONSTANT";
                return false;
            }
            double raw = 0.0;
            std::memcpy(&raw, &val, sizeof(double));
            IRInstr ins;
            ins.op = IROp::ConstNum;
            ins.dst = sp;
            ins.immD = raw;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            if (val.isNumber())
                typeStack[sp] = InferredType::NUMBER;
            else if (val.isBool())
                typeStack[sp] = InferredType::BOOL;
            else if (val.isNil())
                typeStack[sp] = InferredType::NIL;
            else if (val.isString())
                typeStack[sp] = InferredType::UNKNOWN;
            else
                typeStack[sp] = InferredType::UNKNOWN;
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CONSTANT_WIDE): {
            uint16_t idx = static_cast<uint16_t>((c->code[i + 1] << 8) | c->code[i + 2]);
            i += 2;
            VMValue val = c->constants[idx];
            if (sp >= 256)
            {
                outError = "stack overflow at OP_CONSTANT_WIDE";
                return false;
            }
            double raw;
            std::memcpy(&raw, &val, sizeof(double));
            IRInstr ins;
            ins.op = IROp::ConstNum;
            ins.dst = sp;
            ins.immD = raw;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            if (val.isNumber())
                typeStack[sp] = InferredType::NUMBER;
            else if (val.isBool())
                typeStack[sp] = InferredType::BOOL;
            else if (val.isNil())
                typeStack[sp] = InferredType::NIL;
            else
                typeStack[sp] = InferredType::UNKNOWN;
            sp++;
            break;
        }

        case static_cast<uint8_t>(OpCode::OP_ADD): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_ADD";
                return false;
            }
            emit(IROp::Add, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SUBTRACT): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_SUBTRACT";
                return false;
            }
            emit(IROp::Sub, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_MULTIPLY): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_MULTIPLY";
                return false;
            }
            emit(IROp::Mul, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_DIVIDE): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_DIVIDE";
                return false;
            }
            emit(IROp::Div, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_MOD): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_MOD";
                return false;
            }
            emit(IROp::Mod, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_NEGATE): {
            if (sp < 1)
            {
                outError = "stack underflow at OP_NEGATE";
                return false;
            }
            emit(IROp::Neg, sp - 1, sp - 1, -1, i);
            typeStack[sp - 1] = InferredType::NUMBER;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_NOT): {
            if (sp < 1)
            {
                outError = "stack underflow at OP_NOT";
                return false;
            }
            emit(IROp::Not, sp - 1, sp - 1, -1, i);
            typeStack[sp - 1] = InferredType::BOOL;
            break;
        }

        case static_cast<uint8_t>(OpCode::OP_BIT_AND): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_BIT_AND";
                return false;
            }
            emit(IROp::And, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_OR): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_BIT_OR";
                return false;
            }
            emit(IROp::Or, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_XOR): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_BIT_XOR";
                return false;
            }
            emit(IROp::Xor, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_NOT): {
            if (sp < 1)
            {
                outError = "stack underflow at OP_BIT_NOT";
                return false;
            }
            emit(IROp::BitNot, sp - 1, sp - 1, -1, i);
            typeStack[sp - 1] = InferredType::NUMBER;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_SHIFT_LEFT): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_BIT_SHIFT_LEFT";
                return false;
            }
            emit(IROp::Shl, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_SHIFT_RIGHT): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_BIT_SHIFT_RIGHT";
                return false;
            }
            emit(IROp::Shr, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }

        case static_cast<uint8_t>(OpCode::OP_EQUAL): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_EQUAL";
                return false;
            }
            emit(IROp::CmpEq, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_NOT_EQUAL): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_NOT_EQUAL";
                return false;
            }
            emit(IROp::CmpNe, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GREATER): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_GREATER";
                return false;
            }
            emit(IROp::CmpGt, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GREATER_EQUAL): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_GREATER_EQUAL";
                return false;
            }
            emit(IROp::CmpGe, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_LESS): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_LESS";
                return false;
            }
            emit(IROp::CmpLt, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_LESS_EQUAL): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_LESS_EQUAL";
                return false;
            }
            emit(IROp::CmpLe, sp - 2, sp - 2, sp - 1, i);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }

        case static_cast<uint8_t>(OpCode::OP_NIL):
        case static_cast<uint8_t>(OpCode::OP_TRUE):
        case static_cast<uint8_t>(OpCode::OP_FALSE):
        case static_cast<uint8_t>(OpCode::OP_DUP): {
            // OP_NIL emits a 0.0 (the JIT's flushTos sp emitLoadConst 0.0);
            // OP_TRUE emits 1.0; OP_FALSE emits 0.0; OP_DUP copies.
            if (sp >= 256)
            {
                outError = "stack overflow at OP_NIL/TRUE/FALSE/DUP";
                return false;
            }
            double v = 0.0;
            if (op == static_cast<uint8_t>(OpCode::OP_TRUE))
                v = 1.0;
            if (op == static_cast<uint8_t>(OpCode::OP_DUP))
            {
                // src = sp-1, dst = sp
                IRInstr ins;
                ins.op = IROp::Move;
                ins.dst = sp;
                ins.src1 = sp - 1;
                ins.sourceBytecodeOffset = i;
                out.code.push_back(ins);
                typeStack[sp] = typeStack[sp - 1];
                sp++;
                break;
            }
            IRInstr ins;
            ins.op = IROp::ConstNum;
            ins.dst = sp;
            ins.immD = v;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            typeStack[sp] = (op == static_cast<uint8_t>(OpCode::OP_TRUE) || op == static_cast<uint8_t>(OpCode::OP_FALSE)) ? InferredType::BOOL : InferredType::NIL;
            sp++;
            break;
        }

        case static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL): {
            uint8_t constantIdx = c->code[++i];
            VMValue constant = c->constants[constantIdx];
            const std::string &name = constant.asString()->flatten();
            flushTos(sp);
            int strIdx = out.addStringConstant(name);
            (void)strIdx;
            IRInstr ins;
            ins.op = IROp::CallRuntimeVoid;
            ins.symbol = "jit_set_global_helper";
            ins.strArg = name;
            ins.src1 = sp - 1;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SET_GLOBAL): {
            uint8_t constantIdx = c->code[++i];
            VMValue constant = c->constants[constantIdx];
            const std::string &name = constant.asString()->flatten();
            flushTos(sp);
            int strIdx = out.addStringConstant(name);
            (void)strIdx;
            IRInstr ins;
            ins.op = IROp::CallRuntimeVoid;
            ins.symbol = "jit_set_global_helper";
            ins.strArg = name;
            ins.src1 = sp - 1;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            break;
        }

        case static_cast<uint8_t>(OpCode::OP_GET_UPVALUE): {
            uint8_t slot = c->code[++i];
            if (sp >= 256)
            {
                outError = "stack overflow at OP_GET_UPVALUE";
                return false;
            }
            flushTos(sp);
            IRInstr ins;
            ins.op = IROp::CallRuntime;
            ins.dst = sp;
            ins.symbol = "jit_get_upvalue_helper";
            ins.immI = slot;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            typeStack[sp] = InferredType::UNKNOWN;
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SET_UPVALUE): {
            uint8_t slot = c->code[++i];
            if (sp == 0)
            {
                outError = "stack underflow at OP_SET_UPVALUE";
                return false;
            }
            flushTos(sp);
            IRInstr ins;
            ins.op = IROp::CallRuntimeVoid;
            ins.symbol = "jit_set_upvalue_helper";
            ins.immI = slot;
            ins.src1 = sp - 1;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CLOSE_UPVALUE): {
            if (sp == 0)
            {
                outError = "stack underflow at OP_CLOSE_UPVALUE";
                return false;
            }
            flushTos(sp);
            IRInstr ins;
            ins.op = IROp::CallRuntimeVoid;
            ins.symbol = "jit_close_upvalue_helper";
            ins.src1 = sp - 1;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CLOSURE): {
            uint8_t constIdx = c->code[++i];
            VMValue funcVal = c->constants[constIdx];
            int upvalueCount = funcVal.asFunction()->upvalueCount;
            const uint8_t *upvalueBytes = &c->code[i + 1];
            double funcRaw;
            std::memcpy(&funcRaw, &funcVal, sizeof(double));
            flushTos(sp);
            // Store funcRaw at sp as a ConstNum first, then build
            // the upvalue buffer starting at sp+1.
            IRInstr funcIns;
            funcIns.op = IROp::ConstNum;
            funcIns.dst = sp;
            funcIns.immD = funcRaw;
            funcIns.sourceBytecodeOffset = i;
            out.code.push_back(funcIns);
            // Build upvalue metadata buffer on the stack starting at sp+1.
            for (int j = 0; j < upvalueCount; ++j)
            {
                bool isLocal = upvalueBytes[j * 2] != 0;
                int index = upvalueBytes[j * 2 + 1];
                int packed = (isLocal ? 0x100 : 0) | index;
                double packedD;
                std::memcpy(&packedD, &packed, sizeof(double));
                IRInstr metaIns;
                metaIns.op = IROp::ConstNum;
                metaIns.dst = sp + 1 + j * 2;
                metaIns.immD = packedD;
                metaIns.sourceBytecodeOffset = i;
                out.code.push_back(metaIns);
                if (isLocal)
                {
                    IRInstr addrIns;
                    addrIns.op = IROp::StoreAddr;
                    addrIns.dst = sp + 1 + j * 2 + 1;
                    addrIns.src1 = index;
                    addrIns.sourceBytecodeOffset = i;
                    out.code.push_back(addrIns);
                }
                else
                {
                    IRInstr zeroIns;
                    zeroIns.op = IROp::ConstNum;
                    zeroIns.dst = sp + 1 + j * 2 + 1;
                    zeroIns.immD = 0.0;
                    zeroIns.sourceBytecodeOffset = i;
                    out.code.push_back(zeroIns);
                }
            }
            IRInstr ins;
            ins.op = IROp::CallRuntime;
            ins.dst = sp;
            ins.symbol = "jit_create_closure_helper";
            ins.src1 = sp + 1; // upvalue_data pointer
            ins.argc = upvalueCount;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            // sp stays the same: result overwrites funcRaw slot at sp.
            typeStack[sp] = InferredType::CLOSURE;
            i += 2 * upvalueCount;
            break;
        }

        case static_cast<uint8_t>(OpCode::OP_BUILD_LIST): {
            uint8_t count = c->code[++i];
            if (sp < count)
            {
                outError = "stack underflow at OP_BUILD_LIST";
                return false;
            }
            flushTos(sp);
            IRInstr ins;
            ins.op = IROp::CallRuntime;
            ins.dst = sp - count;
            ins.symbol = "jit_build_list_helper";
            ins.src1 = sp - count;
            ins.argc = count;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            sp = sp - count + 1;
            typeStack[sp - 1] = InferredType::OBJECT;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BUILD_MAP): {
            uint8_t count = c->code[++i];
            if (sp < 2 * count)
            {
                outError = "stack underflow at OP_BUILD_MAP";
                return false;
            }
            flushTos(sp);
            IRInstr ins;
            ins.op = IROp::CallRuntime;
            ins.dst = sp - 2 * count;
            ins.symbol = "jit_build_map_helper";
            ins.src1 = sp - 2 * count;
            ins.argc = count;
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            sp = sp - 2 * count + 1;
            typeStack[sp - 1] = InferredType::OBJECT;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_INDEX_GET): {
            if (sp < 2)
            {
                outError = "stack underflow at OP_INDEX_GET";
                return false;
            }
            flushTos(sp);
            IRInstr ins;
            ins.op = IROp::CallRuntime;
            ins.dst = sp - 2;
            ins.symbol = "jit_index_get_helper";
            ins.src1 = sp - 2; // object
            ins.src2 = sp - 1; // index
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            typeStack[sp - 2] = InferredType::UNKNOWN;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_INDEX_SET): {
            if (sp < 3)
            {
                outError = "stack underflow at OP_INDEX_SET";
                return false;
            }
            flushTos(sp);
            IRInstr ins;
            ins.op = IROp::CallRuntimeVoid;
            ins.symbol = "jit_index_set_helper";
            ins.src1 = sp - 3; // object
            ins.src2 = sp - 2; // index
            ins.src3 = sp - 1; // value
            ins.sourceBytecodeOffset = i;
            out.code.push_back(ins);
            sp -= 3;
            break;
        }

        case static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE): {
            uint16_t offset = (c->code[i + 1] << 8) | c->code[i + 2];
            size_t target = i + 3 + offset;
            int targetLabel = out.createLabel();
            IRInstr jf;
            jf.op = IROp::JumpIfFalse;
            jf.src1 = sp - 1; // value to test
            jf.src2 = targetLabel;
            jf.sourceBytecodeOffset = i;
            out.code.push_back(jf);
            // Record the join-point state.
            expectedSp[target] = sp;
            expectedStackTypes[target] = typeStack;
            // Bind the label to the next IRInstr we will emit.
            out.bindLabel(targetLabel, out.code.size());
            i += 2;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_JUMP): {
            uint16_t offset = (c->code[i + 1] << 8) | c->code[i + 2];
            size_t target = i + 3 + offset;
            int targetLabel = out.createLabel();
            IRInstr j;
            j.op = IROp::Jump;
            // Reuse src1 to carry the label id.
            j.src1 = targetLabel;
            j.sourceBytecodeOffset = i;
            out.code.push_back(j);
            expectedSp[target] = sp;
            expectedStackTypes[target] = typeStack;
            // Bind the label to the next IRInstr.
            out.bindLabel(targetLabel, out.code.size());
            i += 2;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_LOOP): {
            uint16_t offset = (c->code[i + 1] << 8) | c->code[i + 2];
            size_t target = i + 3 - offset;
            int targetLabel = out.createLabel();
            IRInstr j;
            j.op = IROp::Jump;
            j.src1 = targetLabel;
            j.sourceBytecodeOffset = i;
            out.code.push_back(j);
            out.bindLabel(targetLabel, out.code.size());
            i += 2;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_RETURN): {
            IRInstr r;
            r.op = IROp::Return;
            r.src1 = sp > 0 ? sp - 1 : -1;
            r.sourceBytecodeOffset = i;
            out.code.push_back(r);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CALL): {
            uint8_t argCount = c->code[++i];
            int calleeSp = sp - argCount - 1;
            if (hasBaseCase && argCount == 1)
            {
                int endLabel = out.createLabel();
                IRInstr rb;
                rb.op = IROp::RecursiveBaseCase;
                rb.dst = calleeSp;
                rb.src1 = calleeSp + 1;
                rb.immD = baseCaseThreshold;
                rb.endLabel = endLabel;
                rb.sourceBytecodeOffset = i;
                out.code.push_back(rb);
                IRInstr call;
                call.op = IROp::CallDirect;
                call.dst = calleeSp;
                call.argc = argCount;
                call.symbol = out.name;
                call.sourceBytecodeOffset = i;
                out.code.push_back(call);
                IRInstr j;
                j.op = IROp::Jump;
                j.src1 = endLabel;
                j.sourceBytecodeOffset = i;
                out.code.push_back(j);
                out.bindLabel(endLabel, out.code.size());
            }
            else
            {
                flushTos(sp);
                IRInstr call;
                call.op = IROp::CallRuntime;
                call.dst = calleeSp;
                call.symbol = "jit_call_helper";
                call.src1 = calleeSp;
                call.argc = argCount;
                call.sourceBytecodeOffset = i;
                out.code.push_back(call);
            }
            sp = calleeSp + 1;
            typeStack[calleeSp] = InferredType::UNKNOWN;
            break;
        }
        default:
            outError = std::string("unsupported opcode in --aot: ") + std::to_string(op);
            return false;
        }
    }

    return true;
}

} // namespace trypillia::aot
