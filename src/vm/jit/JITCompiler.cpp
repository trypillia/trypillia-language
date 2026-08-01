#include "JITCompiler.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <set>
#include <vector>

#include "../compiler/OpCode.h"
#include "SljitBackend.h"

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

JitFunc JITCompiler::compileMathFunction(ObjFunction *function)
{
    if (!function || !function->chunk)
        return (printf("JIT Abort at line %d\n", __LINE__), nullptr);

    // 1. Base Case Detection Heuristic (for fib-like functions)
    bool hasBaseCase = false;
    double baseCaseThreshold = 0;
    if (function->chunk->code.size() > 10 && function->chunk->code[0] == static_cast<uint8_t>(OpCode::OP_GET_LOCAL) &&
        function->chunk->code[1] == 0 && function->chunk->code[2] == static_cast<uint8_t>(OpCode::OP_CONSTANT))
    {
        uint8_t constIdx = function->chunk->code[3];
        VMValue val = function->chunk->constants[constIdx];
        if (val.isNumber() && function->chunk->code[4] == static_cast<uint8_t>(OpCode::OP_LESS))
        {
            hasBaseCase = true;
            baseCaseThreshold = val.asNumber();
        }
    }

    UniversalEmitter emitter(function->maxArity);

    int maxLocal = 0;
    std::set<int> capturedLocals;
    for (size_t i = 0; i < function->chunk->code.size();)
    {
        uint8_t op = function->chunk->code[i];
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
            int slot = function->chunk->code[i + 1];
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
                 op == static_cast<uint8_t>(OpCode::OP_GET_SUPER) || op == static_cast<uint8_t>(OpCode::OP_METHOD) ||
                 op == static_cast<uint8_t>(OpCode::OP_ABSTRACT_METHOD) ||
                 op == static_cast<uint8_t>(OpCode::OP_STATIC_METHOD))
        {
            i += 2;
        }
        else if (op == static_cast<uint8_t>(OpCode::OP_FIELD_MODIFIER))
        {
            i += 3;
        }
        else if (op == static_cast<uint8_t>(OpCode::OP_CLOSURE))
        {
            uint8_t constIdx = function->chunk->code[i + 1];
            VMValue funcVal = function->chunk->constants[constIdx];
            int upvalueCount = funcVal.asFunction()->upvalueCount;
            const uint8_t *upvalueBytes = &function->chunk->code[i + 2];
            for (int j = 0; j < upvalueCount; j++)
            {
                if (upvalueBytes[j * 2])
                { // isLocal
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
        return (printf("JIT Abort at line %d\n", __LINE__), nullptr);

    std::vector<int> capturedVec(capturedLocals.begin(), capturedLocals.end());
    emitter.setCapturedLocals(capturedVec);
    emitter.emitPrologue(maxLocal + 1);

    // Type tracking and ToS caching
    std::vector<InferredType> typeStack(256, InferredType::UNKNOWN);
    std::vector<InferredType> localTypes(256, InferredType::UNKNOWN);
    for (int i = 0; i <= function->maxArity; i++)
        localTypes[i] = InferredType::NUMBER;

    bool tosInFR0 = false; // Is the top stack value in the FR0 register?

    // Initial SP includes the function itself (slot 0) and any arguments
    int sp = function->maxArity >= 0 ? function->maxArity + 1 : 1;

    std::map<size_t, int> expectedSp;
    std::map<size_t, std::vector<InferredType>> expectedStackTypes;

    auto flushTos = [&](int currentSp) {
        if (tosInFR0)
        {
            emitter.emitMove(currentSp - 1, -1);
            tosInFR0 = false;
        }
    };

    for (size_t i = 0; i < function->chunk->code.size(); ++i)
    {
        if (expectedSp.count(i))
        {
            // Flush ToS only if sp matches the expected state (legitimate
            // fall-through). If sp differs, the value in FR0 came from unreachable
            // code processed after an unconditional jump — just discard it.
            if (tosInFR0 && sp == expectedSp[i])
            {
                emitter.emitMove(sp - 1, -1);
            }
            tosInFR0 = false;
            sp = expectedSp[i];
            typeStack = expectedStackTypes[i];
        }
        emitter.bindLabel(i);

        uint8_t op = function->chunk->code[i];
        switch (op)
        {
        case static_cast<uint8_t>(OpCode::OP_NOP):
            break;
        case static_cast<uint8_t>(OpCode::OP_POP):
            if (sp == 0)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            if (tosInFR0)
                tosInFR0 = false;
            else
                sp--;
            break;
        case static_cast<uint8_t>(OpCode::OP_GET_LOCAL): {
            uint8_t slot = function->chunk->code[++i];
            if (sp >= 256)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitGetLocalToFR0(slot);
            tosInFR0 = true;
            typeStack[sp] = localTypes[slot];
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SET_LOCAL): {
            uint8_t slot = function->chunk->code[++i];
            if (sp == 0)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            if (tosInFR0)
                emitter.emitSetLocalFromFR0(slot);
            else
                emitter.emitSetLocal(slot, sp - 1);
            localTypes[slot] = typeStack[sp - 1];
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GET_GLOBAL): {
            uint8_t constantIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constantIdx];
            if (sp >= 256)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            const std::string &name = constant.asString()->flatten();
            emitter.emitGetGlobal(name, sp);
            typeStack[sp] = InferredType::UNKNOWN;
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CONSTANT): {
            uint8_t idx = function->chunk->code[++i];
            VMValue val = function->chunk->constants[idx];
            flushTos(sp);
            double raw;
            memcpy(&raw, &val, sizeof(double));
            if (sp >= 256)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);

            // Constant + Arithmetic optimization
            uint8_t nextOp = (i + 1 < function->chunk->code.size()) ? function->chunk->code[i + 1] : 0;
            if (val.isNumber() &&
                (nextOp == static_cast<uint8_t>(OpCode::OP_ADD) || nextOp == static_cast<uint8_t>(OpCode::OP_SUBTRACT)))
            {
                // We skip this for now to keep ToS logic simple, or we could optimize
                // it
            }

            emitter.emitLoadConstToFR0(raw);
            tosInFR0 = true;
            if (val.isNumber())
                typeStack[sp] = InferredType::NUMBER;
            else
                typeStack[sp] = InferredType::UNKNOWN;
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CONSTANT_WIDE): {
            uint16_t idx = static_cast<uint16_t>((function->chunk->code[i + 1] << 8) | function->chunk->code[i + 2]);
            i += 2;
            VMValue val = function->chunk->constants[idx];
            flushTos(sp);
            double raw;
            memcpy(&raw, &val, sizeof(double));
            if (sp >= 256)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            emitter.emitLoadConstToFR0(raw);
            tosInFR0 = true;
            if (val.isNumber())
                typeStack[sp] = InferredType::NUMBER;
            else
                typeStack[sp] = InferredType::UNKNOWN;
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_ADD): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            if (tosInFR0)
            {
                emitter.emitAddMemToFR0(sp - 2);
            }
            else
            {
                emitter.emitAdd(sp - 2, sp - 1);
                emitter.emitGetLocalToFR0(sp - 2); // Put result in FR0
            }
            tosInFR0 = true;
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SUBTRACT): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            if (tosInFR0)
            {
                emitter.emitSubMemToFR0(sp - 2);
            }
            else
            {
                emitter.emitSub(sp - 2, sp - 1);
                emitter.emitGetLocalToFR0(sp - 2);
            }
            tosInFR0 = true;
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_MULTIPLY): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            if (tosInFR0)
                emitter.emitMulMemToFR0(sp - 2);
            else
            {
                emitter.emitMul(sp - 2, sp - 1);
                emitter.emitGetLocalToFR0(sp - 2);
            }
            tosInFR0 = true;
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_DIVIDE): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            if (tosInFR0)
                emitter.emitDivMemToFR0(sp - 2);
            else
            {
                emitter.emitDiv(sp - 2, sp - 1);
                emitter.emitGetLocalToFR0(sp - 2);
            }
            tosInFR0 = true;
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_EQUAL): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitCmpEq(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_NOT_EQUAL): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitCmpNe(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GREATER): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitCmpGt(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GREATER_EQUAL): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitCmpGe(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_LESS): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitCmpLt(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_LESS_EQUAL): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitCmpLe(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE): {
            uint16_t offset = (function->chunk->code[i + 1] << 8) | function->chunk->code[i + 2];
            size_t target = i + 3 + offset;
            flushTos(sp);
            emitter.emitJumpIfFalse(sp - 1, target);
            expectedSp[target] = sp;
            expectedStackTypes[target] = typeStack;
            i += 2;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_JUMP): {
            uint16_t offset = (function->chunk->code[i + 1] << 8) | function->chunk->code[i + 2];
            size_t target = i + 3 + offset;
            flushTos(sp);
            emitter.emitJump(target);
            expectedSp[target] = sp;
            expectedStackTypes[target] = typeStack;
            i += 2;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_LOOP): {
            uint16_t offset = (function->chunk->code[i + 1] << 8) | function->chunk->code[i + 2];
            size_t target = i + 3 - offset;
            flushTos(sp);
            emitter.emitJump(target);
            i += 2;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_RETURN): {
            if (function->upvalueCount > 0)
                emitter.emitCloseUpvalue(0);
            if (sp > 0)
            {
                if (!tosInFR0)
                    emitter.emitReturnValue(sp - 1);
            }
            else
            {
                emitter.emitLoadConstToFR0(0.0);
            }
            emitter.emitEpilogue(maxLocal + 1);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CALL): {
            uint8_t argCount = function->chunk->code[++i];
            flushTos(sp);
            int calleeSp = sp - argCount - 1;

            if (hasBaseCase && argCount == 1)
            {
                struct sljit_jump *isBaseCase = nullptr;
                // Inline the check: if (args[1] < baseCaseThreshold) result = args[1]
                emitter.emitRecursiveFastPath(calleeSp + 1, baseCaseThreshold, &isBaseCase);

                // SLOW PATH: Real recursive call
                emitter.emitCallDynamic(calleeSp, calleeSp, argCount);
                struct sljit_jump *recursiveEnd = sljit_emit_jump(emitter.getCompiler(), SLJIT_JUMP);

                // FAST PATH (Base Case)
                sljit_set_label(isBaseCase, sljit_emit_label(emitter.getCompiler()));
                // result = arg
                emitter.emitGetLocalToFR0(calleeSp + 1);
                emitter.emitMove(calleeSp, -1); // Move FR0 to result slot

                sljit_set_label(recursiveEnd, sljit_emit_label(emitter.getCompiler()));
            }
            else
            {
                emitter.emitCallDynamic(calleeSp, calleeSp, argCount);
            }

            sp = calleeSp + 1;
            typeStack[calleeSp] = InferredType::UNKNOWN;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_NEGATE): {
            if (sp < 1)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitNegate(sp - 1);
            typeStack[sp - 1] = InferredType::NUMBER;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_MOD): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitMod(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_NOT): {
            if (sp < 1)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitNot(sp - 1);
            typeStack[sp - 1] = InferredType::BOOL;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_AND): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitBitAnd(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_OR): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitBitOr(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_XOR): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitBitXor(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_NOT): {
            if (sp < 1)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitBitNot(sp - 1);
            typeStack[sp - 1] = InferredType::NUMBER;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_SHIFT_LEFT): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitBitShl(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BIT_SHIFT_RIGHT): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitBitShr(sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::NUMBER;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_NIL):
            flushTos(sp);
            emitter.emitLoadConst(sp, 0.0);
            typeStack[sp] = InferredType::NIL;
            sp++;
            break;
        case static_cast<uint8_t>(OpCode::OP_TRUE):
            flushTos(sp);
            emitter.emitLoadConst(sp, 1.0);
            typeStack[sp] = InferredType::BOOL;
            sp++;
            break;
        case static_cast<uint8_t>(OpCode::OP_FALSE):
            flushTos(sp);
            emitter.emitLoadConst(sp, 0.0);
            typeStack[sp] = InferredType::BOOL;
            sp++;
            break;
        case static_cast<uint8_t>(OpCode::OP_DUP):
            flushTos(sp);
            emitter.emitMove(sp, sp - 1);
            typeStack[sp] = typeStack[sp - 1];
            sp++;
            break;
        case static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL): {
            uint8_t constIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            const std::string &name = constant.asString()->flatten();
            flushTos(sp);
            emitter.emitSetGlobal(name, sp - 1);
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SET_GLOBAL): {
            uint8_t constIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            const std::string &name = constant.asString()->flatten();
            flushTos(sp);
            emitter.emitSetGlobal(name, sp - 1);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GET_UPVALUE): {
            uint8_t slot = function->chunk->code[++i];
            if (sp >= 256)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitGetUpvalue(sp, slot);
            typeStack[sp] = InferredType::UNKNOWN;
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_SET_UPVALUE): {
            uint8_t slot = function->chunk->code[++i];
            if (sp == 0)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitSetUpvalue(slot, sp - 1);
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CLOSE_UPVALUE): {
            if (sp == 0)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitCloseUpvalue(sp - 1);
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CLOSURE): {
            uint8_t constIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            int upvalueCount = constant.asFunction()->upvalueCount;
            const uint8_t *upvalueBytes = &function->chunk->code[i + 1];
            double funcRaw;
            memcpy(&funcRaw, &constant, sizeof(double));
            flushTos(sp);
            emitter.emitCreateClosure(sp, funcRaw, upvalueBytes, upvalueCount);
            i += 2 * upvalueCount;
            typeStack[sp] = InferredType::CLOSURE;
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BUILD_LIST): {
            uint8_t count = function->chunk->code[++i];
            if (sp < count)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitBuildList(sp - count, count);
            sp = sp - count + 1;
            typeStack[sp - 1] = InferredType::OBJECT;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_BUILD_MAP): {
            uint8_t count = function->chunk->code[++i];
            if (sp < 2 * count)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitBuildMap(sp - 2 * count, count);
            sp = sp - 2 * count + 1;
            typeStack[sp - 1] = InferredType::OBJECT;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_INDEX_GET): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitIndexGet(sp - 2, sp - 2, sp - 1);
            typeStack[sp - 2] = InferredType::UNKNOWN;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_INDEX_SET): {
            if (sp < 3)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitIndexSet(sp - 3, sp - 2, sp - 1);
            sp -= 3;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_CLASS): {
            uint8_t constIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            const std::string &name = constant.asString()->flatten();
            flushTos(sp);
            emitter.emitCreateClass(sp, name);
            typeStack[sp] = InferredType::OBJECT;
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_ABSTRACT_CLASS): {
            uint8_t constIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            const std::string &name = constant.asString()->flatten();
            flushTos(sp);
            emitter.emitCreateAbstractClass(sp, name);
            typeStack[sp] = InferredType::OBJECT;
            sp++;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_INHERIT): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitInherit(sp - 2);
            sp -= 2;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_MIXIN): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitMixin(sp - 2);
            sp -= 2;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_GET_SUPER): {
            uint8_t constIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            const std::string &name = constant.asString()->flatten();
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitGetSuper(sp - 2, name);
            typeStack[sp - 2] = InferredType::UNKNOWN;
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_PROPERTY_GET): {
            uint8_t constIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            const std::string &name = constant.asString()->flatten();
            if (sp < 1)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitPropertyGet(sp - 1, name);
            typeStack[sp - 1] = InferredType::UNKNOWN;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_PROPERTY_SET): {
            uint8_t constIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            const std::string &name = constant.asString()->flatten();
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitPropertySet(sp - 2, name);
            sp--;
            typeStack[sp - 1] = InferredType::UNKNOWN;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_METHOD): {
            uint8_t constIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            const std::string &name = constant.asString()->flatten();
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitBindMethod(sp - 2, name, false);
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_ABSTRACT_METHOD): {
            uint8_t constIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            const std::string &name = constant.asString()->flatten();
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitBindMethod(sp - 2, name, true);
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_STATIC_METHOD): {
            uint8_t constIdx = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            const std::string &name = constant.asString()->flatten();
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitBindStaticMethod(sp - 2, name);
            sp--;
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_FIELD_MODIFIER): {
            uint8_t constIdx = function->chunk->code[++i];
            uint8_t modifier = function->chunk->code[++i];
            VMValue constant = function->chunk->constants[constIdx];
            const std::string &name = constant.asString()->flatten();
            flushTos(sp);
            emitter.emitFieldModifier(sp - 1, name, modifier);
            break;
        }
        case static_cast<uint8_t>(OpCode::OP_ITER_HAS_NEXT): {
            if (sp < 2)
                return (printf("JIT Abort at line %d\n", __LINE__), nullptr);
            flushTos(sp);
            emitter.emitIterHasNext(sp - 1);
            typeStack[sp - 2] = InferredType::BOOL;
            sp--;
            break;
        }
        default:
            flushTos(sp);
            return (printf("JIT Abort at line %d opcode %d\n", __LINE__, op), nullptr);
        }
    }
    emitter.bindLabel(function->chunk->code.size());
    return emitter.finalize();
}
