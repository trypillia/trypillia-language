#include "Compiler.h"
#include "../lexer/Lexer.h"
#include "../native/StdLib.h"
#include "../parser/Parser.h"
#include "../symbol/SymbolTable.h"
#include "../utils/ErrorHandling.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <filesystem>

class CompilerVisitor : public ASTVisitor {
  private:
    Chunk *chunk;
    std::string compiler_filename;
    SymbolTable *globalSymbols;

    struct Local {
        std::string name;
        int depth;
        bool isCaptured = false;
    };
    std::vector<Local> locals;

    struct Upvalue {
        uint8_t index;
        bool isLocal;
    };
    std::vector<Upvalue> upvalues;
    CompilerVisitor *enclosing = nullptr;

    int scopeDepth = 0;
    int currentLine = 1;

    void beginScope() {
        scopeDepth++;
    }

    void compileDefaultParameters(const std::vector<Parameter> &params) {
        int slot = 1;
        for (const auto &param : params) {
            if (param.defaultValue) {
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(slot));
                emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
                emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL));

                int jumpOverInit = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                param.defaultValue->accept(this);

                emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL), static_cast<uint8_t>(slot));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));

                int jumpEnd = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP));

                patchJump(jumpOverInit);
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));
                patchJump(jumpEnd);
            }
            slot++;
        }
    }

    void endScope() {
        scopeDepth--;
        while (locals.size() > 0 && locals.back().depth > scopeDepth) {
            if (locals.back().isCaptured) {
                emitByte(static_cast<uint8_t>(OpCode::OP_CLOSE_UPVALUE));
            } else {
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            }
            locals.pop_back();
        }
    }

    int resolveLocal(const std::string &name) {
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
            if (locals[i].name == name) {
                return i;
            }
        }
        return -1;
    }

    std::string resolveName(const std::string &name) {
        if (useAliases.count(name)) {
            return useAliases[name];
        }
        if (!currentNamespace.empty() && name.find('.') == std::string::npos) {
            std::string fullPath = currentNamespace + "." + name;
            if (globalSymbols) {
                if (globalSymbols->resolve(fullPath)) {
                    return fullPath;
                }
                if (globalSymbols->resolve(name)) {
                    return name;
                }
            }
            return fullPath;
        }
        return name;
    }

    int resolveUpvalue(const std::string &name) {
        if (enclosing == nullptr) {
            return -1;
        }

        int local = enclosing->resolveLocal(name);
        if (local != -1) {
            enclosing->locals[local].isCaptured = true;
            return addUpvalue((uint8_t)local, true);
        }

        int upvalue = enclosing->resolveUpvalue(name);
        if (upvalue != -1) {
            return addUpvalue((uint8_t)upvalue, false);
        }

        return -1;
    }

    int addUpvalue(uint8_t index, bool isLocal) {
        for (size_t i = 0; i < upvalues.size(); i++) {
            if (upvalues[i].index == index && upvalues[i].isLocal == isLocal) {
                return static_cast<int>(i);
            }
        }
        if (upvalues.size() == 255) {
            ErrorHandling::reportError("Too many closure variables in function.");
            return 0;
        }
        upvalues.push_back({index, isLocal});
        return static_cast<int>(upvalues.size() - 1);
    }

  public:
    std::string currentClassName = "";
    std::string currentParentName = "";

    struct LoopContext {
        int startOffset;
        int scopeDepth;
        std::vector<int> breakJumps;
        std::vector<int> continueJumps;
    };
    std::vector<LoopContext> loops;
    std::string currentNamespace = "";
    std::map<std::string, std::string> useAliases;

    CompilerVisitor(Chunk *chunk, const std::string &fname, SymbolTable *symbols, CompilerVisitor *enc = nullptr)
        : chunk(chunk), compiler_filename(fname), globalSymbols(symbols), enclosing(enc) {
        if (enc) {
            this->currentNamespace = enc->currentNamespace;
            this->useAliases = enc->useAliases;
        }
        locals.push_back({"", 0, false});
    }

    void emitByte(uint8_t byte) {
        chunk->write(byte, currentLine);
    }

    void emitBytes(uint8_t byte1, uint8_t byte2) {
        emitByte(byte1);
        emitByte(byte2);
    }

    void emitConstantIndex(int index) {
        if (index <= 255) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT), static_cast<uint8_t>(index));
        } else {
            emitByte(static_cast<uint8_t>(OpCode::OP_CONSTANT_WIDE));
            emitByte(static_cast<uint8_t>((index >> 8) & 0xff));
            emitByte(static_cast<uint8_t>(index & 0xff));
        }
    }

    void emitConstant(VMValue value) {
        emitConstantIndex(chunk->addConstant(value));
    }

    int emitJump(uint8_t instruction) {
        emitByte(instruction);
        emitByte(0xff);
        emitByte(0xff);
        return static_cast<int>(chunk->code.size() - 2);
    }

    void patchJump(int offset) {
        int jump = static_cast<int>(chunk->code.size() - offset - 2);
        if (jump > 65535) {
            ErrorHandling::reportError("Too much code to jump over.");
        }
        chunk->code[offset] = (jump >> 8) & 0xff;
        chunk->code[offset + 1] = jump & 0xff;
    }

    void emitLoop(int loopStart) {
        emitByte(static_cast<uint8_t>(OpCode::OP_LOOP));
        int offset = static_cast<int>(chunk->code.size() - loopStart + 2);
        if (offset > 65535) {
            ErrorHandling::reportError("Loop body too large.");
        }
        emitByte((offset >> 8) & 0xff);
        emitByte(offset & 0xff);
    }

    void visit(ProgramNode *node) override {
        for (auto &decl : node->declarations) {
            decl->accept(this);
        }
    }

    void visit(BinaryExpr *node) override {
        currentLine = node->op.line;
        if (node->op.type == TokenType::AND) {
            node->left->accept(this);
            int endJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            node->right->accept(this);
            patchJump(endJump);
            return;
        } else if (node->op.type == TokenType::OR) {
            node->left->accept(this);
            int elseJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
            int endJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP));
            patchJump(elseJump);
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            node->right->accept(this);
            patchJump(endJump);
            return;
        }

        node->left->accept(this);
        node->right->accept(this);

        switch (node->op.type) {
        case TokenType::PLUS:
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
            break;
        case TokenType::MINUS:
            emitByte(static_cast<uint8_t>(OpCode::OP_SUBTRACT));
            break;
        case TokenType::STAR:
            emitByte(static_cast<uint8_t>(OpCode::OP_MULTIPLY));
            break;
        case TokenType::SLASH:
            emitByte(static_cast<uint8_t>(OpCode::OP_DIVIDE));
            break;
        case TokenType::PERCENT:
            emitByte(static_cast<uint8_t>(OpCode::OP_MOD));
            break;
        case TokenType::EQUAL_EQUAL:
            emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL));
            break;
        case TokenType::BANG_EQUAL:
            emitByte(static_cast<uint8_t>(OpCode::OP_NOT_EQUAL));
            break;
        case TokenType::LESS:
            emitByte(static_cast<uint8_t>(OpCode::OP_LESS));
            break;
        case TokenType::LESS_EQUAL:
            emitByte(static_cast<uint8_t>(OpCode::OP_LESS_EQUAL));
            break;
        case TokenType::GREATER:
            emitByte(static_cast<uint8_t>(OpCode::OP_GREATER));
            break;
        case TokenType::GREATER_EQUAL:
            emitByte(static_cast<uint8_t>(OpCode::OP_GREATER_EQUAL));
            break;
        case TokenType::BITWISE_AND:
            emitByte(static_cast<uint8_t>(OpCode::OP_BIT_AND));
            break;
        case TokenType::BITWISE_OR:
            emitByte(static_cast<uint8_t>(OpCode::OP_BIT_OR));
            break;
        case TokenType::BITWISE_XOR:
            emitByte(static_cast<uint8_t>(OpCode::OP_BIT_XOR));
            break;
        case TokenType::SHIFT_LEFT:
            emitByte(static_cast<uint8_t>(OpCode::OP_BIT_SHIFT_LEFT));
            break;
        case TokenType::SHIFT_RIGHT:
            emitByte(static_cast<uint8_t>(OpCode::OP_BIT_SHIFT_RIGHT));
            break;
        default:
            break;
        }
    }

    void visit(LiteralExpr *node) override {
        if (node->value.type == TokenType::NUMBER) {
            emitConstant(std::stod(node->value.lexeme));
        } else if (node->value.type == TokenType::STRING) {
            emitConstant(node->value.lexeme);
        } else if (node->value.type == TokenType::TRUE) {
            emitByte(static_cast<uint8_t>(OpCode::OP_TRUE));
        } else if (node->value.type == TokenType::FALSE) {
            emitByte(static_cast<uint8_t>(OpCode::OP_FALSE));
        } else if (node->value.type == TokenType::NIL) {
            emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
        }
    }

    void visit(ExpressionStmt *node) override {
        node->expression->accept(this);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    }

    void visit(VariableExpr *node) override {
        currentLine = node->name.line;
        int arg = resolveLocal(node->name.lexeme);
        if (arg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(arg));
        } else if ((arg = resolveUpvalue(node->name.lexeme)) != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_UPVALUE), static_cast<uint8_t>(arg));
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(node->name.lexeme))));
        }
    }
    void visit(AssignExpr *node) override {
        currentLine = node->name.line;
        node->value->accept(this);
        int arg = resolveLocal(node->name.lexeme);
        if (arg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL), static_cast<uint8_t>(arg));
        } else if ((arg = resolveUpvalue(node->name.lexeme)) != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE), static_cast<uint8_t>(arg));
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(node->name.lexeme))));
        }
    }
    void visit(CompoundAssignExpr *node) override {
        int arg = resolveLocal(node->name.lexeme);
        int upArg = -1;
        if (arg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(arg));
        } else if ((upArg = resolveUpvalue(node->name.lexeme)) != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_UPVALUE), static_cast<uint8_t>(upArg));
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(node->name.lexeme))));
        }

        node->value->accept(this);

        switch (node->op.type) {
        case TokenType::PLUS_EQUAL:
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
            break;
        case TokenType::MINUS_EQUAL:
            emitByte(static_cast<uint8_t>(OpCode::OP_SUBTRACT));
            break;
        case TokenType::STAR_EQUAL:
            emitByte(static_cast<uint8_t>(OpCode::OP_MULTIPLY));
            break;
        case TokenType::SLASH_EQUAL:
            emitByte(static_cast<uint8_t>(OpCode::OP_DIVIDE));
            break;
        default:
            break;
        }

        if (arg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL), static_cast<uint8_t>(arg));
        } else if (upArg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE), static_cast<uint8_t>(upArg));
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(node->name.lexeme))));
        }
    }
    void visit(CallExpr *node) override {
        currentLine = node->paren.line;
        node->callee->accept(this);
        for (auto &arg : node->arguments)
            arg->accept(this);
        emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), static_cast<uint8_t>(node->arguments.size()));
    }
    void visit(VarStmt *node) override {
        if (node->initializer) {
            node->initializer->accept(this);
        } else {
            emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
        }
        if (scopeDepth > 0) {
            locals.push_back({node->name.lexeme, scopeDepth, false});
        } else {
            std::string actualName = node->name.lexeme;
            if (!currentNamespace.empty()) {
                actualName = currentNamespace + "." + actualName;
            }
            emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(actualName)));
        }
    }
    void visit(BlockStmt *node) override {
        beginScope();
        for (auto &stmt : node->statements)
            stmt->accept(this);
        endScope();
    }
    void visit(IfStmt *node) override {
        node->condition->accept(this);
        int thenJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        node->thenBranch->accept(this);
        int elseJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP));
        patchJump(thenJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        if (node->elseBranch) {
            node->elseBranch->accept(this);
        }
        patchJump(elseJump);
    }
    void visit(WhileStmt *node) override {
        int loopStart = static_cast<int>(chunk->code.size());
        loops.push_back({loopStart, scopeDepth, {}, {}});
        node->condition->accept(this);
        int exitJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        node->body->accept(this);
        LoopContext loop = loops.back();
        loops.pop_back();
        for (int continueJump : loop.continueJumps) {
            patchJump(continueJump);
        }
        emitLoop(loopStart);
        patchJump(exitJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        for (int breakJump : loop.breakJumps) {
            patchJump(breakJump);
        }
    }
    void visit(DoWhileStmt *node) override {
        int loopStart = static_cast<int>(chunk->code.size());
        loops.push_back({loopStart, scopeDepth, {}, {}});
        node->body->accept(this);
        LoopContext loop = loops.back();
        loops.pop_back();
        for (int continueJump : loop.continueJumps) {
            patchJump(continueJump);
        }
        node->condition->accept(this);
        int exitJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        emitLoop(loopStart);
        patchJump(exitJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        for (int breakJump : loop.breakJumps) {
            patchJump(breakJump);
        }
    }
    void visit(ReturnStmt *node) override {
        if (node->value) {
            node->value->accept(this);
        } else {
            emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
    }
    void visit(BreakStmt *node) override {
        if (loops.empty()) {
            ErrorHandling::reportError("Cannot break outside a loop.");
            return;
        }
        int depth = loops.back().scopeDepth;
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0 && locals[i].depth > depth; i--) {
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        loops.back().breakJumps.push_back(emitJump(static_cast<uint8_t>(OpCode::OP_JUMP)));
    }
    void visit(ContinueStmt *node) override {
        if (loops.empty()) {
            ErrorHandling::reportError("Cannot continue outside a loop.");
            return;
        }
        int depth = loops.back().scopeDepth;
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0 && locals[i].depth > depth; i--) {
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        loops.back().continueJumps.push_back(emitJump(static_cast<uint8_t>(OpCode::OP_JUMP)));
    }
    void visit(ForStmt *node) override {
        beginScope();
        if (node->initializer) {
            node->initializer->accept(this);
        }
        int loopStart = static_cast<int>(chunk->code.size());
        loops.push_back({loopStart, scopeDepth, {}, {}});
        int exitJump = -1;
        if (node->condition) {
            node->condition->accept(this);
            exitJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        node->body->accept(this);
        LoopContext loop = loops.back();
        loops.pop_back();
        for (int continueJump : loop.continueJumps) {
            patchJump(continueJump);
        }
        if (node->increment) {
            node->increment->accept(this);
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        emitLoop(loopStart);
        if (exitJump != -1) {
            patchJump(exitJump);
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        for (int breakJump : loop.breakJumps) {
            patchJump(breakJump);
        }
        endScope();
    }
    void visit(ForeachStmt *node) override {
        beginScope();
        node->iterable->accept(this);
        locals.push_back({"<iterable>", scopeDepth});
        int iterableLocal = static_cast<int>(locals.size()) - 1;
        emitConstantIndex(chunk->addConstant(0.0));
        locals.push_back({"<index>", scopeDepth});
        int indexLocal = static_cast<int>(locals.size()) - 1;
        int loopStart = static_cast<int>(chunk->code.size());
        loops.push_back({loopStart, scopeDepth, {}, {}});
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(iterableLocal));
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(indexLocal));
        emitByte(static_cast<uint8_t>(OpCode::OP_ITER_HAS_NEXT));
        int exitJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        beginScope();
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(iterableLocal));
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(indexLocal));
        emitByte(static_cast<uint8_t>(OpCode::OP_INDEX_GET));
        locals.push_back({node->name.lexeme, scopeDepth});
        node->body->accept(this);
        endScope();
        LoopContext loop = loops.back();
        loops.pop_back();
        for (int continueJump : loop.continueJumps)
            patchJump(continueJump);
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(indexLocal));
        emitConstantIndex(chunk->addConstant(1.0));
        emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL), static_cast<uint8_t>(indexLocal));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        emitLoop(loopStart);
        patchJump(exitJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        for (int breakJump : loop.breakJumps)
            patchJump(breakJump);
        endScope();
    }
    void visit(SwitchStmt *node) override {
        node->expression->accept(this);
        std::vector<int> endJumps;
        for (auto &case_ : node->cases) {
            if (case_.value) {
                emitByte(static_cast<uint8_t>(OpCode::OP_DUP));
                case_.value->accept(this);
                emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL));
                int nextCaseJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));
                for (auto *stmt : case_.body)
                    stmt->accept(this);
                endJumps.push_back(emitJump(static_cast<uint8_t>(OpCode::OP_JUMP)));
                patchJump(nextCaseJump);
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            } else
                for (auto *stmt : case_.body)
                    stmt->accept(this);
        }
        for (int jump : endJumps)
            patchJump(jump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    }
    void visit(UsingStmt *node) override {
        beginScope();
        node->declaration->accept(this);
        node->body->accept(this);
        if (dynamic_cast<VarStmt *>(node->declaration)) {
            uint8_t slot = static_cast<uint8_t>(locals.size() - 1);
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), slot);
            emitBytes(static_cast<uint8_t>(OpCode::OP_PROPERTY_GET),
                      static_cast<uint8_t>(chunk->addConstant("destroy")));
            emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 0);
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
        endScope();
    }
    void visit(LambdaExpr *node) override {
        auto func = new ObjFunction();
        func->name = "lambda";
        func->maxArity = static_cast<int>(node->params.size());
        func->arity = func->maxArity;
        for (auto it = node->params.rbegin(); it != node->params.rend(); ++it) {
            if (it->defaultValue)
                func->arity--;
            else
                break;
        }
        func->chunk = new Chunk();
        func->filename = compiler_filename;
        CompilerVisitor funcCompiler(func->chunk, compiler_filename, globalSymbols, this);
        funcCompiler.beginScope();
        for (const auto &param : node->params)
            funcCompiler.locals.push_back({param.name, 1, false});
        funcCompiler.compileDefaultParameters(node->params);
        for (auto &stmt : node->body)
            stmt->accept(&funcCompiler);
        funcCompiler.emitBytes(static_cast<uint8_t>(OpCode::OP_NIL), static_cast<uint8_t>(OpCode::OP_RETURN));
        func->upvalueCount = static_cast<int>(funcCompiler.upvalues.size());
        emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE), static_cast<uint8_t>(chunk->addConstant(func)));
        for (const auto &upval : funcCompiler.upvalues) {
            emitByte(upval.isLocal ? 1 : 0);
            emitByte(upval.index);
        }
    }
    void visit(UnaryExpr *node) override {
        currentLine = node->op.line;
        node->right->accept(this);
        switch (node->op.type) {
        case TokenType::BANG:
            emitByte(static_cast<uint8_t>(OpCode::OP_NOT));
            break;
        case TokenType::BITWISE_NOT:
            emitByte(static_cast<uint8_t>(OpCode::OP_BIT_NOT));
            break;
        case TokenType::MINUS:
            emitByte(static_cast<uint8_t>(OpCode::OP_NEGATE));
            break;
        default:
            break;
        }
    }
    void visit(ThisExpr *node) override {
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
            if (locals[i].name == "this") {
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(i));
                return;
            }
        }
    }
    void visit(SuperExpr *node) override {
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
            if (locals[i].name == "this") {
                emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(i));
                break;
            }
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                  static_cast<uint8_t>(chunk->addConstant(currentParentName)));
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_SUPER),
                  static_cast<uint8_t>(chunk->addConstant(node->method.lexeme)));
    }
    void visit(GetExpr *node) override {
        currentLine = node->name.line;
        node->object->accept(this);
        emitBytes(static_cast<uint8_t>(OpCode::OP_PROPERTY_GET),
                  static_cast<uint8_t>(chunk->addConstant(node->name.lexeme)));
    }
    void visit(SetExpr *node) override {
        currentLine = node->name.line;
        node->object->accept(this);
        node->value->accept(this);
        emitBytes(static_cast<uint8_t>(OpCode::OP_PROPERTY_SET),
                  static_cast<uint8_t>(chunk->addConstant(node->name.lexeme)));
    }
    void visit(PostfixExpr *node) override {
        int arg = resolveLocal(node->name.lexeme);
        int upArg = -1;
        if (arg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(arg));
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(arg));
        } else if ((upArg = resolveUpvalue(node->name.lexeme)) != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_UPVALUE), static_cast<uint8_t>(upArg));
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_UPVALUE), static_cast<uint8_t>(upArg));
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(node->name.lexeme))));
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(node->name.lexeme))));
        }

        emitConstantIndex(chunk->addConstant(1.0));

        if (node->op.type == TokenType::PLUS_PLUS) {
            emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
        } else {
            emitByte(static_cast<uint8_t>(OpCode::OP_SUBTRACT));
        }

        if (arg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL), static_cast<uint8_t>(arg));
        } else if (upArg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE), static_cast<uint8_t>(upArg));
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(node->name.lexeme))));
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    }
    void visit(TernaryExpr *node) override {
        node->condition->accept(this);

        int thenJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));

        node->thenBranch->accept(this);

        int elseJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP));

        patchJump(thenJump);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));

        node->elseBranch->accept(this);

        patchJump(elseJump);
    }
    void visit(ListExpr *node) override {
        for (auto &el : node->elements) {
            el->accept(this);
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_BUILD_LIST), static_cast<uint8_t>(node->elements.size()));
    }
    void visit(IndexGetExpr *node) override {
        node->object->accept(this);
        node->index->accept(this);
        emitByte(static_cast<uint8_t>(OpCode::OP_INDEX_GET));
    }
    void visit(IndexSetExpr *node) override {
        node->object->accept(this);
        node->index->accept(this);
        node->value->accept(this);
        emitByte(static_cast<uint8_t>(OpCode::OP_INDEX_SET));
    }

    void visit(FunctionNode *node) override {
        auto func = new ObjFunction();
        std::string actualName = node->name;
        if (!currentNamespace.empty() && node->name != "init" && enclosing == nullptr) {
            actualName = currentNamespace + "." + actualName;
        }
        func->name = actualName;
        func->maxArity = static_cast<int>(node->params.size());
        func->arity = func->maxArity;
        for (auto it = node->params.rbegin(); it != node->params.rend(); ++it) {
            if (it->defaultValue)
                func->arity--;
            else
                break;
        }
        func->chunk = new Chunk();
        func->filename = compiler_filename;

        CompilerVisitor funcCompiler(func->chunk, compiler_filename, globalSymbols, this);

        funcCompiler.beginScope();
        for (const auto &param : node->params) {
            funcCompiler.locals.push_back({param.name, 1, false});
        }
        funcCompiler.compileDefaultParameters(node->params);

        for (auto &stmt : node->body) {
            stmt->accept(&funcCompiler);
        }

        funcCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
        funcCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));

        func->upvalueCount = static_cast<int>(funcCompiler.upvalues.size());

        emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE), static_cast<uint8_t>(chunk->addConstant(func)));
        for (const auto &upval : funcCompiler.upvalues) {
            emitByte(upval.isLocal ? 1 : 0);
            emitByte(upval.index);
        }

        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), static_cast<uint8_t>(chunk->addConstant(actualName)));
    }
    void visit(FieldDeclNode *node) override {
        if (!currentClassName.empty()) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(currentClassName))));
            emitBytes(static_cast<uint8_t>(OpCode::OP_FIELD_MODIFIER),
                      static_cast<uint8_t>(chunk->addConstant(node->name)));
            emitByte(static_cast<uint8_t>(getVMAccessModifier(node->accessModifier)));
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        }
    }
    VMAccessModifier getVMAccessModifier(AccessModifier am) {
        if (am == AccessModifier::PRIVATE) {
            return VMAccessModifier::PRIVATE;
        }
        if (am == AccessModifier::PROTECTED) {
            return VMAccessModifier::PROTECTED;
        }
        return VMAccessModifier::PUBLIC;
    }

    void compileMethod(FunctionNode *node, ClassNode *classNode = nullptr) {
        auto func = new ObjFunction();
        func->name = node->name;
        func->maxArity = static_cast<int>(node->params.size());
        func->arity = func->maxArity;
        for (auto it = node->params.rbegin(); it != node->params.rend(); ++it) {
            if (it->defaultValue)
                func->arity--;
            else
                break;
        }
        func->chunk = new Chunk();
        func->filename = compiler_filename;
        func->accessModifier = getVMAccessModifier(node->accessModifier);
        func->enclosingClassName = currentClassName;

        CompilerVisitor funcCompiler(func->chunk, compiler_filename, globalSymbols, this);

        funcCompiler.currentClassName = currentClassName;
        funcCompiler.currentParentName = currentParentName;

        funcCompiler.locals[0].name = "this";

        funcCompiler.beginScope();

        for (const auto &param : node->params) {
            funcCompiler.locals.push_back({param.name, 1, false});
        }
        funcCompiler.compileDefaultParameters(node->params);

        if (node->name == "init" && classNode) {
            for (auto field : classNode->fields) {
                funcCompiler.emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), 0);
                if (field->initializer) {
                    field->initializer->accept(&funcCompiler);
                } else {
                    funcCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
                }
                funcCompiler.emitBytes(static_cast<uint8_t>(OpCode::OP_PROPERTY_SET),
                                       static_cast<uint8_t>(funcCompiler.chunk->addConstant(field->name)));
                funcCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            }
        }

        for (auto &stmt : node->body) {
            stmt->accept(&funcCompiler);
        }

        if (node->name == "init") {
            funcCompiler.emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), 0);
            funcCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
        } else {
            funcCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
            funcCompiler.emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
        }

        func->upvalueCount = static_cast<int>(funcCompiler.upvalues.size());

        emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE), static_cast<uint8_t>(chunk->addConstant(func)));
        for (const auto &upval : funcCompiler.upvalues) {
            emitByte(upval.isLocal ? 1 : 0);
            emitByte(upval.index);
        }

        if (node->isStatic) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_STATIC_METHOD),
                      static_cast<uint8_t>(chunk->addConstant(node->name)));
        } else if (node->isAbstract) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_ABSTRACT_METHOD),
                      static_cast<uint8_t>(chunk->addConstant(node->name)));
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_METHOD), static_cast<uint8_t>(chunk->addConstant(node->name)));
        }
    }

    void visit(ClassNode *node) override {
        std::string enclosingClassName = currentClassName;
        std::string enclosingParentName = currentParentName;
        std::string actualName = node->name;
        if (!currentNamespace.empty()) {
            actualName = currentNamespace + "." + actualName;
        }
        currentClassName = actualName;
        currentParentName = node->parentName.empty() ? "" : resolveName(node->parentName);

        if (node->isAbstract) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_ABSTRACT_CLASS),
                      static_cast<uint8_t>(chunk->addConstant(actualName)));
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_CLASS), static_cast<uint8_t>(chunk->addConstant(actualName)));
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), static_cast<uint8_t>(chunk->addConstant(actualName)));

        if (!node->parentName.empty()) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(actualName)));
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(currentParentName)));
            emitByte(static_cast<uint8_t>(OpCode::OP_INHERIT));
        }

        for (auto &traitName : node->interfaceNames) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(actualName)));
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(traitName))));
            emitByte(static_cast<uint8_t>(OpCode::OP_MIXIN));
        }

        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(chunk->addConstant(actualName)));
        for (auto field : node->fields) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_FIELD_MODIFIER),
                      static_cast<uint8_t>(chunk->addConstant(field->name)));
            emitByte(static_cast<uint8_t>(getVMAccessModifier(field->accessModifier)));
        }
        bool hasInit = false;
        for (auto &method : node->methods) {
            if (method->name == "init") {
                hasInit = true;
            }
            compileMethod(method, node);
        }
        if (!hasInit) {
            auto dummyInit = new FunctionNode("init", {}, {});
            compileMethod(dummyInit, node);
            delete dummyInit;
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));

        currentClassName = enclosingClassName;
        currentParentName = enclosingParentName;
    }
    void visit(InterfaceNode *node) override {
        std::string actualName = node->name;
        if (!currentNamespace.empty()) {
            actualName = currentNamespace + "." + actualName;
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_CLASS), static_cast<uint8_t>(chunk->addConstant(actualName)));
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), static_cast<uint8_t>(chunk->addConstant(actualName)));

        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(chunk->addConstant(actualName)));
        for (auto &method : node->methods) {
            compileMethod(method, nullptr);
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    }
    void visit(TraitNode *node) override {
        std::string actualName = node->name;
        if (!currentNamespace.empty()) {
            actualName = currentNamespace + "." + actualName;
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_CLASS), static_cast<uint8_t>(chunk->addConstant(actualName)));
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), static_cast<uint8_t>(chunk->addConstant(actualName)));

        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(chunk->addConstant(actualName)));
        for (auto &method : node->methods) {
            compileMethod(method);
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    }
    void visit(StaticGetExpr *node) override {
        int arg = resolveLocal(node->className.lexeme);
        if (arg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(arg));
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(node->className.lexeme))));
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_PROPERTY_GET),
                  static_cast<uint8_t>(chunk->addConstant(node->memberName.lexeme)));
    }
    void visit(StaticCallExpr *node) override {
        int arg = resolveLocal(node->className.lexeme);
        if (arg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(arg));
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(node->className.lexeme))));
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_PROPERTY_GET),
                  static_cast<uint8_t>(chunk->addConstant(node->memberName.lexeme)));

        for (auto &argExpr : node->arguments) {
            argExpr->accept(this);
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), static_cast<uint8_t>(node->arguments.size()));
    }
    void visit(StaticSetExpr *node) override {
        node->value->accept(this);
        int arg = resolveLocal(node->className.lexeme);
        if (arg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(arg));
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL),
                      static_cast<uint8_t>(chunk->addConstant(resolveName(node->className.lexeme))));
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_PROPERTY_SET),
                  static_cast<uint8_t>(chunk->addConstant(node->memberName.lexeme)));
    }
    void visit(LoadStmt *node) override {
        std::string path = node->filename.lexeme;
        if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
            path = path.substr(1, path.size() - 2);
        }

        // Resolve relative to the current file
        if (!path.empty() && path.front() != '/') {
            size_t slashPos = compiler_filename.find_last_of('/');
            if (slashPos != std::string::npos) {
                path = compiler_filename.substr(0, slashPos + 1) + path;
            }
        }

        std::ifstream file(path);
        if (!file.is_open()) {
            // Fallback to <cwd>/lib/<path>
            char cwd[4096];
            if (getcwd(cwd, sizeof(cwd))) {
                std::string libPath = std::string(cwd) + "/lib/" + path.substr(path.find_last_of('/') + 1);
                file.open(libPath);
                if (file.is_open()) {
                    path = libPath;
                }
            }
        }

        if (!file.is_open()) {
            std::cerr << "Compile error: Could not load file '" << path << "'" << std::endl;
            return;
        }

        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        Lexer lexer(source);
        Parser parser(lexer);
        ASTNode *ast = parser.parse();

        if (ast) {
            std::string old_fname = compiler_filename;
            std::string old_namespace = currentNamespace;
            std::map<std::string, std::string> old_useAliases = useAliases;
            int old_scopeDepth = scopeDepth;
            size_t old_localsSize = locals.size();
            size_t old_upvaluesSize = upvalues.size();
            size_t old_loopsSize = loops.size();
            compiler_filename = path;
            scopeDepth = 0;
            ast->accept(this);
            delete ast;
            compiler_filename = old_fname;
            currentNamespace = old_namespace;
            useAliases = old_useAliases;
            scopeDepth = old_scopeDepth;
            locals.resize(old_localsSize);
            upvalues.resize(old_upvaluesSize);
            loops.resize(old_loopsSize);
        }
    }

    void visit(DictExpr *node) override {
        for (auto &pair : node->elements) {
            pair.first->accept(this);
            pair.second->accept(this);
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_BUILD_MAP), static_cast<uint8_t>(node->elements.size()));
    }

    void visit(NamespaceStmt *node) override {
        this->currentNamespace = node->name.lexeme;
    }

    void visit(UseStmt *node) override {
        this->useAliases[node->alias.lexeme] = node->name.lexeme;
    }
};

ObjFunction *Compiler::compile(ASTNode *ast, SymbolTable *globals) {
    if (!ast) {
        return nullptr;
    }

    auto script = new ObjFunction();
    script->name = "<script>";
    script->chunk = new Chunk();
    script->filename = currentFilename;

    CompilerVisitor visitor(script->chunk, currentFilename, globals);
    ast->accept(&visitor);

    visitor.emitBytes(static_cast<uint8_t>(OpCode::OP_NIL), static_cast<uint8_t>(OpCode::OP_RETURN));

    return script;
}
