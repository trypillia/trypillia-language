#pragma once
#include "ABI.h"
#include "Emitter.h"
#include "sljitLir.h"
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" double jit_call_helper(void *vm_ptr, double callee_val, double *args, int argCount);
extern "C" double jit_get_global_helper(void *vm_ptr, const char *name);
extern "C" void jit_set_global_helper(void *vm_ptr, const char *name, double val_d);
extern "C" double jit_index_get_helper(void *vm_ptr, double object_val, double index_val);
extern "C" double jit_index_set_helper(void *vm_ptr, double object_val, double index_val, double value_val);
extern "C" double jit_mod_helper(double a, double b);
extern "C" double jit_build_list_helper(double *args, int count);
extern "C" double jit_build_map_helper(double *args, int count);
extern "C" double jit_property_get_helper(void *vm_ptr, double object_val, const char *name);
extern "C" double jit_property_set_helper(void *vm_ptr, double object_val, const char *name, double value_val);
extern "C" double jit_iter_has_next_helper(double index_val, double iterable_val);
extern "C" void *jit_resolve_global_address(void *vm_ptr, const char *name, VMValue **cell);
extern "C" double jit_create_class_helper(void *vm, const char *name);
extern "C" double jit_create_abstract_class_helper(void *vm, const char *name);
extern "C" double jit_bind_method_helper(double class_val, double method_val, const char *name, int isAbstract);
extern "C" double jit_bind_static_method_helper(double class_val, double method_val, const char *name);
extern "C" void jit_inherit_helper(double subclass_val, double superclass_val);
extern "C" void jit_mixin_helper(double target_val, double mixin_val);
extern "C" double jit_get_super_helper(double receiver_val, double superclass_val, const char *name);
extern "C" void jit_field_modifier_helper(double class_val, const char *name, int modifier);
extern "C" double jit_create_closure_helper(void *vm, double func_val, double *upvalue_data, int upvalue_count);
extern "C" double jit_get_upvalue_helper(void *vm_ptr, int slot);
extern "C" void jit_set_upvalue_helper(void *vm_ptr, int slot, double val);
extern "C" void jit_close_upvalue_helper(void *vm_ptr, double *addr);

class UniversalEmitter : public JitEmitter {
  private:
    struct sljit_compiler *compiler;
    int funcArity;
    std::map<size_t, struct sljit_label *> labels;
    std::map<size_t, std::vector<struct sljit_jump *>> unresolvedJumps;
    std::vector<char *> ownedStrings;
    std::vector<VMValue **> ownedCells;
    std::set<std::string> stringPool;

    const char *cacheString(const std::string &s) {
        auto [it, inserted] = stringPool.insert(s);
        return it->c_str();
    }

  public:
    UniversalEmitter(int arity = 0) : funcArity(arity) {
        compiler = sljit_create_compiler(NULL);
    }

    struct sljit_compiler *getCompiler() {
        return compiler;
    }

    ~UniversalEmitter() {
        if (compiler)
            sljit_free_compiler(compiler);
        for (char *s : ownedStrings)
            free(s);
        for (VMValue **cell : ownedCells)
            delete cell;
    }

    void setCapturedLocals(const std::vector<int> &slots) override {
        // No-op in memory-based emitter
    }

    void emitPrologue(int maxLocals) override {
        // ABI: void* vm (R0), double* args (R1), int argCount (R2), double n (FR0)
        sljit_emit_enter(compiler, 0, SLJIT_ARGS4(F64, W, P, W, F64), 4 | SLJIT_ENTER_FLOAT(4),
                         3 | SLJIT_ENTER_FLOAT(0), 8);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S0, 0, SLJIT_R0, 0); // vm_ptr
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S1, 0, SLJIT_R1, 0); // args_ptr
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S2, 0, SLJIT_R2, 0); // argCount

        // Sync register n (FR0) to virtual stack local 0 (args[1]) for consistency
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), JIT_FIRST_ARG_SLOT * sizeof(double), SLJIT_FR0, 0);
    }

    void emitEpilogue(int maxLocals) override {
        sljit_emit_return(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0);
    }

    void emitLoadConst(int stackOffset, double value) override {
        sljit_emit_fset64(compiler, SLJIT_FR0, value);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), stackOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitGetLocal(int stackOffset, int localSlot) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), stackOffset * sizeof(double),
                        SLJIT_MEM1(SLJIT_S1), localSlot * sizeof(double));
    }

    void emitSetLocal(int localSlot, int stackOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), localSlot * sizeof(double), SLJIT_MEM1(SLJIT_S1),
                        stackOffset * sizeof(double));
    }

    void emitMove(int targetOffset, int srcOffset) override {
        if (srcOffset == -1) {
            sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
        } else {
            sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double),
                            SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        }
    }

    void emitLoadConstToFR0(double value) {
        sljit_emit_fset64(compiler, SLJIT_FR0, value);
    }

    void emitGetLocalToFR0(int localSlot) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), localSlot * sizeof(double));
    }

    void emitSetLocalFromFR0(int localSlot) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), localSlot * sizeof(double), SLJIT_FR0, 0);
    }

    void emitAddMemToFR0(int memStackOffset) {
        sljit_emit_fop2(compiler, SLJIT_ADD_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), memStackOffset * sizeof(double),
                        SLJIT_FR0, 0);
    }

    void emitSubMemToFR0(int memStackOffset) {
        // FR0 = mem - FR0
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), memStackOffset * sizeof(double));
        sljit_emit_fop2(compiler, SLJIT_SUB_F64, SLJIT_FR0, 0, SLJIT_FR1, 0, SLJIT_FR0, 0);
    }

    void emitMulMemToFR0(int memStackOffset) {
        sljit_emit_fop2(compiler, SLJIT_MUL_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), memStackOffset * sizeof(double),
                        SLJIT_FR0, 0);
    }

    void emitDivMemToFR0(int memStackOffset) {
        // FR0 = mem / FR0
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), memStackOffset * sizeof(double));
        sljit_emit_fop2(compiler, SLJIT_DIV_F64, SLJIT_FR0, 0, SLJIT_FR1, 0, SLJIT_FR0, 0);
    }

    void emitRecursiveFastPath(int argStackOffset, double threshold, struct sljit_jump **outBaseCaseJump) {
        // Load the argument that was just put on stack
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), argStackOffset * sizeof(double));
        sljit_emit_fset64(compiler, SLJIT_FR2, threshold);
        // If arg < threshold, it is a base case
        *outBaseCaseJump = sljit_emit_fcmp(compiler, SLJIT_F_LESS, SLJIT_FR1, 0, SLJIT_FR2, 0);
    }

    void emitReturnValue(int stackOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), stackOffset * sizeof(double));
    }

    void emitCmpLtJumpIfFalse(int targetOffset, int srcOffset, size_t targetByteCodeIndex) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_GREATER_EQUAL, SLJIT_FR1, 0, SLJIT_FR2, 0);
        if (labels.count(targetByteCodeIndex)) {
            sljit_set_label(jump, labels[targetByteCodeIndex]);
        } else {
            unresolvedJumps[targetByteCodeIndex].push_back(jump);
        }
    }

    void emitCmpLeJumpIfFalse(int targetOffset, int srcOffset, size_t targetByteCodeIndex) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_GREATER, SLJIT_FR1, 0, SLJIT_FR2, 0);
        if (labels.count(targetByteCodeIndex)) {
            sljit_set_label(jump, labels[targetByteCodeIndex]);
        } else {
            unresolvedJumps[targetByteCodeIndex].push_back(jump);
        }
    }

    void emitCmpGtJumpIfFalse(int targetOffset, int srcOffset, size_t targetByteCodeIndex) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_LESS_EQUAL, SLJIT_FR1, 0, SLJIT_FR2, 0);
        if (labels.count(targetByteCodeIndex)) {
            sljit_set_label(jump, labels[targetByteCodeIndex]);
        } else {
            unresolvedJumps[targetByteCodeIndex].push_back(jump);
        }
    }

    void emitCmpGeJumpIfFalse(int targetOffset, int srcOffset, size_t targetByteCodeIndex) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_LESS, SLJIT_FR1, 0, SLJIT_FR2, 0);
        if (labels.count(targetByteCodeIndex)) {
            sljit_set_label(jump, labels[targetByteCodeIndex]);
        } else {
            unresolvedJumps[targetByteCodeIndex].push_back(jump);
        }
    }

    void emitCmpEqJumpIfFalse(int targetOffset, int srcOffset, size_t targetByteCodeIndex) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_NOT_EQUAL, SLJIT_FR1, 0, SLJIT_FR2, 0);
        if (labels.count(targetByteCodeIndex)) {
            sljit_set_label(jump, labels[targetByteCodeIndex]);
        } else {
            unresolvedJumps[targetByteCodeIndex].push_back(jump);
        }
    }

    void emitCmpNeJumpIfFalse(int targetOffset, int srcOffset, size_t targetByteCodeIndex) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_EQUAL, SLJIT_FR1, 0, SLJIT_FR2, 0);
        if (labels.count(targetByteCodeIndex)) {
            sljit_set_label(jump, labels[targetByteCodeIndex]);
        } else {
            unresolvedJumps[targetByteCodeIndex].push_back(jump);
        }
    }

    void emitAddConst(int targetOffset, double value) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fset64(compiler, SLJIT_FR2, value);
        sljit_emit_fop2(compiler, SLJIT_ADD_F64, SLJIT_FR1, 0, SLJIT_FR1, 0, SLJIT_FR2, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitSubConst(int targetOffset, double value) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fset64(compiler, SLJIT_FR2, value);
        sljit_emit_fop2(compiler, SLJIT_SUB_F64, SLJIT_FR1, 0, SLJIT_FR1, 0, SLJIT_FR2, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitCmpLtConstJumpIfFalse(int targetOffset, double value, size_t targetByteCodeIndex) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fset64(compiler, SLJIT_FR2, value);
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_GREATER_EQUAL, SLJIT_FR1, 0, SLJIT_FR2, 0);
        if (labels.count(targetByteCodeIndex)) {
            sljit_set_label(jump, labels[targetByteCodeIndex]);
        } else {
            unresolvedJumps[targetByteCodeIndex].push_back(jump);
        }
    }

    void emitCmpLeConstJumpIfFalse(int targetOffset, double value, size_t targetByteCodeIndex) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fset64(compiler, SLJIT_FR2, value);
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_GREATER, SLJIT_FR1, 0, SLJIT_FR2, 0);
        if (labels.count(targetByteCodeIndex)) {
            sljit_set_label(jump, labels[targetByteCodeIndex]);
        } else {
            unresolvedJumps[targetByteCodeIndex].push_back(jump);
        }
    }

    void emitAddNumeric(int targetOffset, int srcOffset) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop2(compiler, SLJIT_ADD_F64, SLJIT_FR1, 0, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1),
                        srcOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitSubNumeric(int targetOffset, int srcOffset) {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop2(compiler, SLJIT_SUB_F64, SLJIT_FR1, 0, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1),
                        srcOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitAdd(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop2(compiler, SLJIT_ADD_F64, SLJIT_FR1, 0, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1),
                        srcOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitSub(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop2(compiler, SLJIT_SUB_F64, SLJIT_FR1, 0, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1),
                        srcOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitMul(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop2(compiler, SLJIT_MUL_F64, SLJIT_FR1, 0, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1),
                        srcOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitDiv(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop2(compiler, SLJIT_DIV_F64, SLJIT_FR1, 0, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1),
                        srcOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitMod(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_FR1, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_FR2, 0);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(F64, F64, F64), SLJIT_IMM, (sljit_sw)jit_mod_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitBitAnd(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_CONV_SW_FROM_F64, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S1),
                        targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_CONV_SW_FROM_F64, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S1),
                        srcOffset * sizeof(double));
        sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_fop1(compiler, SLJIT_CONV_F64_FROM_SW, SLJIT_FR1, 0, SLJIT_R0, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitBitOr(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_CONV_SW_FROM_F64, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S1),
                        targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_CONV_SW_FROM_F64, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S1),
                        srcOffset * sizeof(double));
        sljit_emit_op2(compiler, SLJIT_OR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_fop1(compiler, SLJIT_CONV_F64_FROM_SW, SLJIT_FR1, 0, SLJIT_R0, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitBitXor(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_CONV_SW_FROM_F64, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S1),
                        targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_CONV_SW_FROM_F64, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S1),
                        srcOffset * sizeof(double));
        sljit_emit_op2(compiler, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_fop1(compiler, SLJIT_CONV_F64_FROM_SW, SLJIT_FR1, 0, SLJIT_R0, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitBitNot(int targetOffset) override {
        sljit_emit_fop1(compiler, SLJIT_CONV_SW_FROM_F64, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S1),
                        targetOffset * sizeof(double));
        sljit_emit_op2(compiler, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, -1);
        sljit_emit_fop1(compiler, SLJIT_CONV_F64_FROM_SW, SLJIT_FR1, 0, SLJIT_R0, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitBitShl(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_CONV_SW_FROM_F64, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S1),
                        targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_CONV_SW_FROM_F64, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S1),
                        srcOffset * sizeof(double));
        sljit_emit_op2(compiler, SLJIT_SHL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_fop1(compiler, SLJIT_CONV_F64_FROM_SW, SLJIT_FR1, 0, SLJIT_R0, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitBitShr(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_CONV_SW_FROM_F64, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S1),
                        targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_CONV_SW_FROM_F64, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S1),
                        srcOffset * sizeof(double));
        sljit_emit_op2(compiler, SLJIT_ASHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_fop1(compiler, SLJIT_CONV_F64_FROM_SW, SLJIT_FR1, 0, SLJIT_R0, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitCmpEq(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_NOT_EQUAL, SLJIT_FR1, 0, SLJIT_FR2, 0);
        sljit_emit_fset64(compiler, SLJIT_FR1, 1.0);
        struct sljit_jump *jump_end = sljit_emit_jump(compiler, SLJIT_JUMP);
        sljit_set_label(jump, sljit_emit_label(compiler));
        sljit_emit_fset64(compiler, SLJIT_FR1, 0.0);
        sljit_set_label(jump_end, sljit_emit_label(compiler));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitCmpNe(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_EQUAL, SLJIT_FR1, 0, SLJIT_FR2, 0);
        sljit_emit_fset64(compiler, SLJIT_FR1, 1.0);
        struct sljit_jump *jump_end = sljit_emit_jump(compiler, SLJIT_JUMP);
        sljit_set_label(jump, sljit_emit_label(compiler));
        sljit_emit_fset64(compiler, SLJIT_FR1, 0.0);
        sljit_set_label(jump_end, sljit_emit_label(compiler));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitCmpLt(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_GREATER_EQUAL, SLJIT_FR1, 0, SLJIT_FR2, 0);
        sljit_emit_fset64(compiler, SLJIT_FR1, 1.0);
        struct sljit_jump *jump_end = sljit_emit_jump(compiler, SLJIT_JUMP);
        sljit_set_label(jump, sljit_emit_label(compiler));
        sljit_emit_fset64(compiler, SLJIT_FR1, 0.0);
        sljit_set_label(jump_end, sljit_emit_label(compiler));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitCmpLe(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_GREATER, SLJIT_FR1, 0, SLJIT_FR2, 0);
        sljit_emit_fset64(compiler, SLJIT_FR1, 1.0);
        struct sljit_jump *jump_end = sljit_emit_jump(compiler, SLJIT_JUMP);
        sljit_set_label(jump, sljit_emit_label(compiler));
        sljit_emit_fset64(compiler, SLJIT_FR1, 0.0);
        sljit_set_label(jump_end, sljit_emit_label(compiler));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitCmpGt(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_LESS_EQUAL, SLJIT_FR1, 0, SLJIT_FR2, 0);
        sljit_emit_fset64(compiler, SLJIT_FR1, 1.0);
        struct sljit_jump *jump_end = sljit_emit_jump(compiler, SLJIT_JUMP);
        sljit_set_label(jump, sljit_emit_label(compiler));
        sljit_emit_fset64(compiler, SLJIT_FR1, 0.0);
        sljit_set_label(jump_end, sljit_emit_label(compiler));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitCmpGe(int targetOffset, int srcOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), srcOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_LESS, SLJIT_FR1, 0, SLJIT_FR2, 0);
        sljit_emit_fset64(compiler, SLJIT_FR1, 1.0);
        struct sljit_jump *jump_end = sljit_emit_jump(compiler, SLJIT_JUMP);
        sljit_set_label(jump, sljit_emit_label(compiler));
        sljit_emit_fset64(compiler, SLJIT_FR1, 0.0);
        sljit_set_label(jump_end, sljit_emit_label(compiler));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitNot(int targetOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fset64(compiler, SLJIT_FR2, 0.0);
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_NOT_EQUAL, SLJIT_FR1, 0, SLJIT_FR2, 0);
        sljit_emit_fset64(compiler, SLJIT_FR1, 1.0);
        struct sljit_jump *jump_end = sljit_emit_jump(compiler, SLJIT_JUMP);
        sljit_set_label(jump, sljit_emit_label(compiler));
        sljit_emit_fset64(compiler, SLJIT_FR1, 0.0);
        sljit_set_label(jump_end, sljit_emit_label(compiler));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitNegate(int targetOffset) override {
        sljit_emit_fop1(compiler, SLJIT_NEG_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR1, 0);
    }

    void emitCallDynamic(int targetOffset, int calleeOffset, int argCount) override {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), calleeOffset * sizeof(double));
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S1, 0, SLJIT_IMM, calleeOffset * sizeof(double));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, argCount);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(F64, W, F64, P, W), SLJIT_IMM, (sljit_sw)jit_call_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    std::vector<VMValue *> globalCaches;

    void emitGetGlobal(const std::string &name, int targetOffset) override {
        // Allocate a persistent "cell" for this global variable
        VMValue **cell = new VMValue *(nullptr);
        ownedCells.push_back(cell);

        // 1. Load the value from the cell
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)cell);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R0), 0);

        // 2. If cell is null, go to slow path to resolve it
        struct sljit_jump *is_null = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);

        // --- FAST PATH: Already cached ---
        // Load VMValue (8 bytes) from the address in R1
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_R1), 0);
        struct sljit_jump *fast_end = sljit_emit_jump(compiler, SLJIT_JUMP);

        // --- SLOW PATH: Resolve and cache ---
        sljit_set_label(is_null, sljit_emit_label(compiler));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0); // vm_ptr
        const char *cname = cacheString(name);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)cname);

        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)cell);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W), SLJIT_IMM,
                         (sljit_sw)jit_resolve_global_address);

        // Result address is in R0. Load value into FR0.
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_R0), 0);

        sljit_set_label(fast_end, sljit_emit_label(compiler));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitSetGlobal(const std::string &name, int sourceOffset) override {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        const char *cname = cacheString(name);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)cname);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), sourceOffset * sizeof(double));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, F64), SLJIT_IMM, (sljit_sw)jit_set_global_helper);
    }

    void emitIndexGet(int targetOffset, int objectOffset, int indexOffset) override {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), objectOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), indexOffset * sizeof(double));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(F64, W, F64, F64), SLJIT_IMM,
                         (sljit_sw)jit_index_get_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitIndexSet(int objectOffset, int indexOffset, int valueOffset) override {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), objectOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), indexOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), valueOffset * sizeof(double));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(F64, W, F64, F64, F64), SLJIT_IMM,
                         (sljit_sw)jit_index_set_helper);
    }

    void emitBuildList(int targetOffset, int count) override {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, SLJIT_S1, 0, SLJIT_IMM, targetOffset * sizeof(double));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, count);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(F64, P, W), SLJIT_IMM, (sljit_sw)jit_build_list_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitBuildMap(int targetOffset, int count) override {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, SLJIT_S1, 0, SLJIT_IMM, targetOffset * sizeof(double));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, count);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(F64, P, W), SLJIT_IMM, (sljit_sw)jit_build_map_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitPropertyGet(int objectOffset, const std::string &name) override {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), objectOffset * sizeof(double));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)cacheString(name));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(F64, W, F64, W), SLJIT_IMM,
                         (sljit_sw)jit_property_get_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), objectOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitPropertySet(int objectOffset, const std::string &name) override {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), objectOffset * sizeof(double));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)cacheString(name));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1),
                        (objectOffset + 1) * sizeof(double));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(F64, W, F64, W, F64), SLJIT_IMM,
                         (sljit_sw)jit_property_set_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), objectOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitIterHasNext(int targetOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1),
                        (targetOffset - 1) * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(F64, F64, F64), SLJIT_IMM,
                         (sljit_sw)jit_iter_has_next_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitCreateClass(int targetOffset, const std::string &name) override {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)cacheString(name));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(F64, W, W), SLJIT_IMM, (sljit_sw)jit_create_class_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitCreateAbstractClass(int targetOffset, const std::string &name) override {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)cacheString(name));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(F64, W, W), SLJIT_IMM,
                         (sljit_sw)jit_create_abstract_class_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitBindMethod(int targetOffset, const std::string &name, bool isAbstract) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1),
                        (targetOffset + 1) * sizeof(double));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)cacheString(name));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, isAbstract ? 1 : 0);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(F64, F64, F64, W, W), SLJIT_IMM,
                         (sljit_sw)jit_bind_method_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitBindStaticMethod(int targetOffset, const std::string &name) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1),
                        (targetOffset + 1) * sizeof(double));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)cacheString(name));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(F64, F64, F64, W), SLJIT_IMM,
                         (sljit_sw)jit_bind_static_method_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitInherit(int targetOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1),
                        (targetOffset + 1) * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(F64, F64), SLJIT_IMM, (sljit_sw)jit_inherit_helper);
    }

    void emitMixin(int targetOffset) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1),
                        (targetOffset + 1) * sizeof(double));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(F64, F64), SLJIT_IMM, (sljit_sw)jit_mixin_helper);
    }

    void emitGetSuper(int targetOffset, const std::string &name) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1),
                        (targetOffset + 1) * sizeof(double)); // receiver
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM1(SLJIT_S1),
                        targetOffset * sizeof(double)); // superclass
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)cacheString(name));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(F64, F64, F64, W), SLJIT_IMM,
                         (sljit_sw)jit_get_super_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitFieldModifier(int targetOffset, const std::string &name, uint8_t modifier) override {
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)cacheString(name));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, modifier);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(F64, W, W), SLJIT_IMM, (sljit_sw)jit_field_modifier_helper);
    }

    void emitCreateClosure(int targetOffset, double funcRaw, const uint8_t *upvalueBytes, int upvalueCount) override {
        int dataOffset = JIT_UPVALUE_DATA_SLOT; // Use end of buffer for upvalue metadata
        for (int j = 0; j < upvalueCount; j++) {
            bool isLocal = upvalueBytes[j * 2];
            int index = upvalueBytes[j * 2 + 1];
            int packed = (isLocal << 8) | index;
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, packed);
            sljit_emit_fop1(compiler, SLJIT_CONV_F64_FROM_SW, SLJIT_FR1, 0, SLJIT_R0, 0);
            sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), (dataOffset + j * 2) * sizeof(double), SLJIT_FR1, 0);
            if (isLocal) {
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, SLJIT_S1, 0, SLJIT_IMM, index * sizeof(double));
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S1), (dataOffset + j * 2 + 1) * sizeof(double), SLJIT_R0, 0);
            }
        }
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_fset64(compiler, SLJIT_FR0, funcRaw);
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S1, 0, SLJIT_IMM, dataOffset * sizeof(double));
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, upvalueCount);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(F64, W, F64, P, W), SLJIT_IMM,
                         (sljit_sw)jit_create_closure_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitGetUpvalue(int targetOffset, int slot) override {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, slot);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(F64, W, W), SLJIT_IMM, (sljit_sw)jit_get_upvalue_helper);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_MEM1(SLJIT_S1), targetOffset * sizeof(double), SLJIT_FR0, 0);
    }

    void emitSetUpvalue(int slot, int sourceOffset) override {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, slot);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_S1), sourceOffset * sizeof(double));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, F64), SLJIT_IMM, (sljit_sw)jit_set_upvalue_helper);
    }

    void emitCloseUpvalue(int stackOffset) override {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S1, 0, SLJIT_IMM, stackOffset * sizeof(double));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, P), SLJIT_IMM, (sljit_sw)jit_close_upvalue_helper);
    }

    void bindLabel(size_t byteCodeIndex) override {
        labels[byteCodeIndex] = sljit_emit_label(compiler);
        if (unresolvedJumps.count(byteCodeIndex)) {
            for (auto jump : unresolvedJumps[byteCodeIndex]) {
                sljit_set_label(jump, labels[byteCodeIndex]);
            }
            unresolvedJumps.erase(byteCodeIndex);
        }
    }

    void emitJump(size_t targetByteCodeIndex) override {
        struct sljit_jump *jump = sljit_emit_jump(compiler, SLJIT_JUMP);
        if (labels.count(targetByteCodeIndex)) {
            sljit_set_label(jump, labels[targetByteCodeIndex]);
        } else {
            unresolvedJumps[targetByteCodeIndex].push_back(jump);
        }
    }

    void emitJumpIfFalse(int stackOffset, size_t targetByteCodeIndex) override {
        sljit_emit_fset64(compiler, SLJIT_FR1, 0.0);
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR2, 0, SLJIT_MEM1(SLJIT_S1), stackOffset * sizeof(double));
        struct sljit_jump *jump = sljit_emit_fcmp(compiler, SLJIT_F_EQUAL, SLJIT_FR2, 0, SLJIT_FR1, 0);
        if (labels.count(targetByteCodeIndex)) {
            sljit_set_label(jump, labels[targetByteCodeIndex]);
        } else {
            unresolvedJumps[targetByteCodeIndex].push_back(jump);
        }
    }

    JitFunc finalize() override {
        if (!unresolvedJumps.empty()) {
            for (const auto &pair : unresolvedJumps) {
                std::cerr << "JIT Abort: unresolved jump to target bytecode index " << pair.first << std::endl;
            }
            return nullptr;
        }
        return (JitFunc)sljit_generate_code(compiler, 0, NULL);
    }
};
