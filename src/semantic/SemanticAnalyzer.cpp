#include "SemanticAnalyzer.h"
#include "../lexer/Lexer.h"
#include "../native/StdLib.h"
#include "../parser/Parser.h"
#include "../symbol/SymbolTable.h"
#include "../utils/ErrorHandling.h"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <filesystem>

class SemanticVisitor : public ASTVisitor {
  public:
    std::string currentFilename = "";

  private:
    SymbolTable *currentScope;
    std::string currentNamespace = "";
    std::map<std::string, std::string> useAliases;

    std::string resolveName(const std::string &name) {
        if (useAliases.find(name) != useAliases.end()) {
            return useAliases[name];
        }
        if (!currentNamespace.empty() && name.find('.') == std::string::npos) {
            // Check if it exists in current namespace
            std::string fullPath = currentNamespace + "." + name;
            if (currentScope->resolve(fullPath))
                return fullPath;

            // Fallback to global if it doesn't exist in namespace but exists in root
            if (currentScope->resolve(name))
                return name;

            return fullPath; // Default to namespace for error reporting
        }
        return name;
    }

  public:
    SemanticVisitor(SymbolTable *scope) : currentScope(scope) {
        StdLib::registerSymbols(currentScope);
    }

    ~SemanticVisitor() {
        // Parent scopes are deleted by the owner of the root SymbolTable
    }

    void visit(ProgramNode *node) override {
        for (auto &decl : node->declarations) {
            decl->accept(this);
        }
    }

    void visit(BinaryExpr *node) override {
        node->left->accept(this);
        node->right->accept(this);
    }

    void visit(LiteralExpr *node) override {
    }

    void visit(VariableExpr *node) override {
        std::string name = resolveName(node->name.lexeme);
        Symbol *symbol = currentScope->resolve(name);
        if (!symbol) {
            std::string error = "Undefined variable '" + node->name.lexeme + "'";
            ErrorHandling::reportError(error);
        }
    }

    void visit(AssignExpr *node) override {
        node->value->accept(this);

        std::string name = resolveName(node->name.lexeme);
        Symbol *symbol = currentScope->resolve(name);
        if (!symbol) {
            std::string error = "Undefined variable '" + node->name.lexeme + "'";
            ErrorHandling::reportError(error);
        } else if (symbol->isConst) {
            std::string error = "Cannot assign to const variable '" + node->name.lexeme + "'";
            ErrorHandling::reportError(error);
        }
    }

    void visit(CompoundAssignExpr *node) override {
        node->value->accept(this);

        std::string name = resolveName(node->name.lexeme);
        Symbol *symbol = currentScope->resolve(name);
        if (!symbol) {
            std::string error = "Undefined variable '" + node->name.lexeme + "'";
            ErrorHandling::reportError(error);
        } else if (symbol->isConst) {
            std::string error = "Cannot assign to const variable '" + node->name.lexeme + "'";
            ErrorHandling::reportError(error);
        }
    }

    void visit(UnaryExpr *node) override {
        if (node->op.type == TokenType::PLUS_PLUS || node->op.type == TokenType::MINUS_MINUS) {
            VariableExpr *var = dynamic_cast<VariableExpr *>(node->right);
            if (!var) {
                ErrorHandling::reportError("Invalid prefix expression target");
                return;
            }
            std::string name = resolveName(var->name.lexeme);
            Symbol *symbol = currentScope->resolve(name);
            if (!symbol) {
                std::string error = "Undefined variable '" + var->name.lexeme + "'";
                ErrorHandling::reportError(error);
            } else if (symbol->isConst) {
                std::string error = "Cannot modify const variable '" + var->name.lexeme + "'";
                ErrorHandling::reportError(error);
            }
            return;
        }
        node->right->accept(this);
    }

    void visit(PostfixExpr *node) override {
        std::string name = resolveName(node->name.lexeme);
        Symbol *symbol = currentScope->resolve(name);
        if (!symbol) {
            std::string error = "Undefined variable '" + node->name.lexeme + "'";
            ErrorHandling::reportError(error);
        } else if (symbol->isConst) {
            std::string error = "Cannot modify const variable '" + node->name.lexeme + "'";
            ErrorHandling::reportError(error);
        }
    }

    void visit(TernaryExpr *node) override {
        node->condition->accept(this);
        node->thenBranch->accept(this);
        node->elseBranch->accept(this);
    }

    void visit(ListExpr *node) override {
        for (auto *el : node->elements) {
            el->accept(this);
        }
    }

    void visit(IndexGetExpr *node) override {
        node->object->accept(this);
        node->index->accept(this);
    }

    void visit(IndexSetExpr *node) override {
        node->object->accept(this);
        node->index->accept(this);
        node->value->accept(this);
    }

    void visit(ThisExpr *node) override {
        Symbol *symbol = currentScope->resolve("this");
        if (!symbol) {
            ErrorHandling::reportError("'this' can only be used inside a method");
        }
    }

    void visit(SuperExpr *node) override {
        Symbol *symbol = currentScope->resolve("this");
        if (!symbol) {
            ErrorHandling::reportError("'super' can only be used inside a method");
        }
    }

    void visit(GetExpr *node) override {
        node->object->accept(this);
    }

    void visit(SetExpr *node) override {
        node->object->accept(this);
        node->value->accept(this);
    }

    void visit(CallExpr *node) override {
        node->callee->accept(this);
        for (auto &arg : node->arguments) {
            arg->accept(this);
        }
    }

    void visit(ExpressionStmt *node) override {
        node->expression->accept(this);
    }

    void visit(VarStmt *node) override {
        if (node->initializer) {
            node->initializer->accept(this);
        }

        std::string actualName = node->name.lexeme;
        if (!currentNamespace.empty() && currentScope->getParent() == nullptr) {
            actualName = currentNamespace + "." + actualName;
        }

        Symbol symbol;
        symbol.name = actualName;
        symbol.type = "variable";
        symbol.isConst = node->isConst;

        if (!currentScope->define(symbol)) {
            std::string error = "Variable '" + actualName + "' already defined in this scope";
            ErrorHandling::reportError(error);
        }
    }

    void visit(BlockStmt *node) override {
        SymbolTable *enclosingScope = currentScope;
        currentScope = new SymbolTable(enclosingScope);
        for (auto &stmt : node->statements) {
            stmt->accept(this);
        }
        SymbolTable *previous = currentScope;
        currentScope = currentScope->getParent();
        delete previous;
    }

    void visit(IfStmt *node) override {
        node->condition->accept(this);
        node->thenBranch->accept(this);
        if (node->elseBranch)
            node->elseBranch->accept(this);
    }

    void visit(WhileStmt *node) override {
        node->condition->accept(this);
        node->body->accept(this);
    }

    void visit(DoWhileStmt *node) override {
        node->body->accept(this);
        node->condition->accept(this);
    }

    void visit(ReturnStmt *node) override {
        if (node->value)
            node->value->accept(this);
    }

    void visit(BreakStmt *node) override {
    }
    void visit(ContinueStmt *node) override {
    }

    void visit(ForStmt *node) override {
        if (node->initializer)
            node->initializer->accept(this);
        if (node->condition)
            node->condition->accept(this);
        if (node->increment)
            node->increment->accept(this);
        node->body->accept(this);
    }

    void visit(ForeachStmt *node) override {
        node->iterable->accept(this);
        currentScope = new SymbolTable(currentScope);
        Symbol symbol;
        symbol.name = node->name.lexeme;
        symbol.type = "variable";
        symbol.isConst = false;
        currentScope->define(symbol);
        node->body->accept(this);
        SymbolTable *old = currentScope;
        currentScope = currentScope->getParent();
        delete old;
    }

    void visit(SwitchStmt *node) override {
        node->expression->accept(this);
        for (auto &caseClause : node->cases) {
            if (caseClause.value)
                caseClause.value->accept(this);
            for (auto &stmt : caseClause.body)
                stmt->accept(this);
        }
    }

    void visit(UsingStmt *node) override {
        currentScope = new SymbolTable(currentScope);
        node->declaration->accept(this);
        node->body->accept(this);
        SymbolTable *old = currentScope;
        currentScope = currentScope->getParent();
        delete old;
    }

    void visit(LambdaExpr *node) override {
        currentScope = new SymbolTable(currentScope);
        for (const auto &param : node->params) {
            if (param.defaultValue) {
                param.defaultValue->accept(this);
            }
            Symbol symbol;
            symbol.name = param.name;
            symbol.type = "parameter";
            symbol.isConst = false;
            currentScope->define(symbol);
        }
        for (auto &stmt : node->body)
            stmt->accept(this);
        SymbolTable *old = currentScope;
        currentScope = currentScope->getParent();
        delete old;
    }

    void visit(FunctionNode *node) override {
        std::string actualName = node->name;
        if (!currentNamespace.empty() && node->name != "init" && currentScope->getParent() == nullptr) {
            actualName = currentNamespace + "." + actualName;
        }

        Symbol functionSymbol;
        functionSymbol.name = actualName;
        functionSymbol.type = "function";
        functionSymbol.isConst = true;
        currentScope->define(functionSymbol);

        currentScope = new SymbolTable(currentScope);
        for (const auto &param : node->params) {
            if (param.defaultValue) {
                param.defaultValue->accept(this);
            }
            Symbol paramSymbol;
            paramSymbol.name = param.name;
            paramSymbol.type = "parameter";
            currentScope->define(paramSymbol);
        }
        for (auto &stmt : node->body)
            stmt->accept(this);
        SymbolTable *old = currentScope;
        currentScope = currentScope->getParent();
        delete old;
    }

    void visit(FieldDeclNode *node) override {
        Symbol fieldSymbol;
        fieldSymbol.name = node->name;
        fieldSymbol.type = "field";
        fieldSymbol.isConst = node->isConst;
        currentScope->define(fieldSymbol);
        if (node->initializer)
            node->initializer->accept(this);
    }

    void visit(ClassNode *node) override {
        std::string actualName = node->name;
        if (!currentNamespace.empty()) {
            actualName = currentNamespace + "." + actualName;
        }

        Symbol classSymbol;
        classSymbol.name = actualName;
        classSymbol.type = "class";
        classSymbol.isConst = true;
        currentScope->define(classSymbol);

        currentScope = new SymbolTable(currentScope);
        Symbol thisSymbol;
        thisSymbol.name = "this";
        thisSymbol.type = "instance";
        thisSymbol.isConst = true;
        currentScope->define(thisSymbol);

        for (auto &field : node->fields)
            field->accept(this);
        for (auto &method : node->methods)
            method->accept(this);
        currentScope = currentScope->getParent();
    }

    void visit(InterfaceNode *node) override {
        std::string actualName = node->name;
        if (!currentNamespace.empty())
            actualName = currentNamespace + "." + actualName;

        Symbol ifaceSymbol;
        ifaceSymbol.name = actualName;
        ifaceSymbol.type = "interface";
        ifaceSymbol.isConst = true;
        currentScope->define(ifaceSymbol);

        currentScope = new SymbolTable(currentScope);
        Symbol thisSymbol;
        thisSymbol.name = "this";
        thisSymbol.type = "instance";
        thisSymbol.isConst = true;
        currentScope->define(thisSymbol);

        for (auto &method : node->methods)
            method->accept(this);

        SymbolTable *old = currentScope;
        currentScope = currentScope->getParent();
        delete old;
    }

    void visit(TraitNode *node) override {
        std::string actualName = node->name;
        if (!currentNamespace.empty())
            actualName = currentNamespace + "." + actualName;

        Symbol traitSymbol;
        traitSymbol.name = actualName;
        traitSymbol.type = "trait";
        traitSymbol.isConst = true;
        currentScope->define(traitSymbol);

        currentScope = new SymbolTable(currentScope);
        Symbol thisSymbol;
        thisSymbol.name = "this";
        thisSymbol.type = "instance";
        thisSymbol.isConst = true;
        currentScope->define(thisSymbol);

        for (auto &method : node->methods)
            method->accept(this);

        SymbolTable *old = currentScope;
        currentScope = currentScope->getParent();
        delete old;
    }

    void visit(StaticGetExpr *node) override {
    }
    void visit(StaticCallExpr *node) override {
    }
    void visit(StaticSetExpr *node) override {
    }

    void visit(LoadStmt *node) override {
        std::string path = node->filename.lexeme;
        if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
            path = path.substr(1, path.size() - 2);
        }

        // Resolve relative to the current file
        if (!path.empty() && path.front() != '/') {
            size_t slashPos = currentFilename.find_last_of('/');
            if (slashPos != std::string::npos) {
                path = currentFilename.substr(0, slashPos + 1) + path;
            }
        }

        std::string previousFilename = currentFilename;
        std::string previousNamespace = currentNamespace;
        std::map<std::string, std::string> previousUseAliases = useAliases;
        currentFilename = path;

        std::ifstream file(path);
        if (!file.is_open()) {
            // Fallback to <cwd>/lib/<path>
            char cwd[4096];
            if (getcwd(cwd, sizeof(cwd))) {
                std::string libPath = std::string(cwd) + "/lib/" + path.substr(path.find_last_of('/') + 1);
                file.open(libPath);
                if (file.is_open()) {
                    currentFilename = libPath;
                }
            }
        }

        if (!file.is_open()) {
            currentFilename = previousFilename;
            return;
        }
        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        Lexer lexer(source);
        Parser parser(lexer);
        ASTNode *ast = parser.parse();
        if (ast) {
            ast->accept(this);
            delete ast;
        }
        currentFilename = previousFilename;
        currentNamespace = previousNamespace;
        useAliases = previousUseAliases;
    }

    void visit(DictExpr *node) override {
        for (auto &pair : node->elements) {
            pair.first->accept(this);
            pair.second->accept(this);
        }
    }

    void visit(NamespaceStmt *node) override {
        this->currentNamespace = node->name.lexeme;
    }

    void visit(UseStmt *node) override {
        this->useAliases[node->alias.lexeme] = node->name.lexeme;
    }
};

SymbolTable *SemanticAnalyzer::analyze(ASTNode *ast) {
    if (!ast)
        return nullptr;
    SymbolTable *globals = new SymbolTable();
    SemanticVisitor visitor(globals);
    visitor.currentFilename = this->currentFilename;
    ast->accept(&visitor);
    return globals;
}
