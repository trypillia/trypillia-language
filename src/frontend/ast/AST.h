#ifndef AST_H
#define AST_H

#include "../lexer/Lexer.h"
#include <string>
#include <vector>

// Forward declaration
class ASTVisitor;

// Access modifier for class members
enum class AccessModifier { PUBLIC, PRIVATE, PROTECTED };

struct Parameter {
    std::string name;
    class ExprNode *defaultValue;
};

class ASTNode {
  public:
    int line = 0;
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor *visitor) = 0;
};

class ExprNode : public ASTNode {
  public:
    virtual ~ExprNode() = default;
};

class StmtNode : public ASTNode {
  public:
    virtual ~StmtNode() = default;
};

class ProgramNode : public ASTNode {
  public:
    std::vector<ASTNode *> declarations;

    ProgramNode(const std::vector<ASTNode *> &declarations) : declarations(declarations) {
    }
    virtual ~ProgramNode() {
        for (auto decl : declarations) {
            delete decl;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class UnaryExpr : public ExprNode {
  public:
    Token op;
    ExprNode *right;

    UnaryExpr(Token op, ExprNode *right) : op(op), right(right) {
    }

    ~UnaryExpr() {
        delete right;
    }

    void accept(ASTVisitor *visitor) override;
};

class ThisExpr : public ExprNode {
  public:
    Token keyword;

    ThisExpr(Token keyword) : keyword(keyword) {
    }

    void accept(ASTVisitor *visitor) override;
};

class SuperExpr : public ExprNode {
  public:
    Token keyword;
    Token method;

    SuperExpr(Token keyword, Token method) : keyword(keyword), method(method) {
    }

    void accept(ASTVisitor *visitor) override;
};

class GetExpr : public ExprNode {
  public:
    ExprNode *object;
    Token name;

    GetExpr(ExprNode *object, Token name) : object(object), name(name) {
    }

    ~GetExpr() {
        delete object;
    }

    void accept(ASTVisitor *visitor) override;
};

class SetExpr : public ExprNode {
  public:
    ExprNode *object;
    Token name;
    ExprNode *value;

    SetExpr(ExprNode *object, Token name, ExprNode *value) : object(object), name(name), value(value) {
    }

    ~SetExpr() {
        delete object;
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class PostfixExpr : public ExprNode {
  public:
    Token name;
    Token op;

    PostfixExpr(Token name, Token op) : name(name), op(op) {
    }

    void accept(ASTVisitor *visitor) override;
};

class TernaryExpr : public ExprNode {
  public:
    ExprNode *condition;
    ExprNode *thenBranch;
    ExprNode *elseBranch;

    TernaryExpr(ExprNode *condition, ExprNode *thenBranch, ExprNode *elseBranch)
        : condition(condition), thenBranch(thenBranch), elseBranch(elseBranch) {
    }

    ~TernaryExpr() {
        delete condition;
        delete thenBranch;
        delete elseBranch;
    }

    void accept(ASTVisitor *visitor) override;
};

class BinaryExpr : public ExprNode {
  public:
    ExprNode *left;
    Token op;
    ExprNode *right;

    BinaryExpr(ExprNode *left, Token op, ExprNode *right) : left(left), op(op), right(right) {
    }

    ~BinaryExpr() {
        delete left;
        delete right;
    }

    void accept(ASTVisitor *visitor) override;
};

class LiteralExpr : public ExprNode {
  public:
    Token value;

    LiteralExpr(Token value) : value(value) {
    }

    void accept(ASTVisitor *visitor) override;
};

class VariableExpr : public ExprNode {
  public:
    Token name;

    VariableExpr(Token name) : name(name) {
    }

    void accept(ASTVisitor *visitor) override;
};

class AssignExpr : public ExprNode {
  public:
    Token name;
    ExprNode *value;

    AssignExpr(Token name, ExprNode *value) : name(name), value(value) {
    }
    ~AssignExpr() {
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class CompoundAssignExpr : public ExprNode {
  public:
    Token name;
    Token op;
    ExprNode *value;

    CompoundAssignExpr(Token name, Token op, ExprNode *value) : name(name), op(op), value(value) {
    }
    ~CompoundAssignExpr() {
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class CallExpr : public ExprNode {
  public:
    ExprNode *callee;
    Token paren;
    std::vector<ExprNode *> arguments;

    CallExpr(ExprNode *callee, Token paren, std::vector<ExprNode *> arguments)
        : callee(callee), paren(paren), arguments(arguments) {
    }

    ~CallExpr() {
        delete callee;
        for (auto arg : arguments) {
            delete arg;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class ExpressionStmt : public StmtNode {
  public:
    ExprNode *expression;

    ExpressionStmt(ExprNode *expression) : expression(expression) {
    }
    ~ExpressionStmt() {
        delete expression;
    }

    void accept(ASTVisitor *visitor) override;
};

class ListExpr : public ExprNode {
  public:
    std::vector<ExprNode *> elements;

    ListExpr(std::vector<ExprNode *> elements) : elements(elements) {
    }

    ~ListExpr() {
        for (auto *el : elements)
            delete el;
    }

    void accept(ASTVisitor *visitor) override;
};

class DictExpr : public ExprNode {
  public:
    std::vector<std::pair<ExprNode *, ExprNode *>> elements; // key, value

    DictExpr(std::vector<std::pair<ExprNode *, ExprNode *>> elements) : elements(elements) {
    }

    ~DictExpr() {
        for (auto &s : elements) {
            delete s.first;
            delete s.second;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class LambdaExpr : public ExprNode {
  public:
    std::vector<Parameter> params;
    std::vector<StmtNode *> body;

    LambdaExpr(std::vector<Parameter> params, std::vector<StmtNode *> body) : params(params), body(body) {
    }

    ~LambdaExpr() {
        for (auto &param : params) {
            if (param.defaultValue)
                delete param.defaultValue;
        }
        for (auto stmt : body)
            delete stmt;
    }

    void accept(ASTVisitor *visitor) override;
};

class IndexGetExpr : public ExprNode {
  public:
    ExprNode *object;
    ExprNode *index;

    IndexGetExpr(ExprNode *object, ExprNode *index) : object(object), index(index) {
    }

    ~IndexGetExpr() {
        delete object;
        delete index;
    }

    void accept(ASTVisitor *visitor) override;
};

class IndexSetExpr : public ExprNode {
  public:
    ExprNode *object;
    ExprNode *index;
    ExprNode *value;

    IndexSetExpr(ExprNode *object, ExprNode *index, ExprNode *value) : object(object), index(index), value(value) {
    }

    ~IndexSetExpr() {
        delete object;
        delete index;
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class VarStmt : public StmtNode {
  public:
    Token name;
    ExprNode *initializer;
    bool isConst;

    VarStmt(Token name, ExprNode *initializer, bool isConst = false)
        : name(name), initializer(initializer), isConst(isConst) {
    }

    ~VarStmt() {
        delete initializer;
    }

    void accept(ASTVisitor *visitor) override;
};

class BlockStmt : public StmtNode {
  public:
    std::vector<StmtNode *> statements;

    BlockStmt(std::vector<StmtNode *> statements) : statements(statements) {
    }

    ~BlockStmt() {
        for (auto stmt : statements) {
            delete stmt;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class IfStmt : public StmtNode {
  public:
    ExprNode *condition;
    StmtNode *thenBranch;
    StmtNode *elseBranch;

    IfStmt(ExprNode *condition, StmtNode *thenBranch, StmtNode *elseBranch)
        : condition(condition), thenBranch(thenBranch), elseBranch(elseBranch) {
    }

    ~IfStmt() {
        delete condition;
        delete thenBranch;
        delete elseBranch;
    }

    void accept(ASTVisitor *visitor) override;
};

class WhileStmt : public StmtNode {
  public:
    ExprNode *condition;
    StmtNode *body;

    WhileStmt(ExprNode *condition, StmtNode *body) : condition(condition), body(body) {
    }

    ~WhileStmt() {
        delete condition;
        delete body;
    }

    void accept(ASTVisitor *visitor) override;
};

class ReturnStmt : public StmtNode {
  public:
    Token keyword;
    ExprNode *value;

    ReturnStmt(Token keyword, ExprNode *value) : keyword(keyword), value(value) {
    }

    ~ReturnStmt() {
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class BreakStmt : public StmtNode {
  public:
    Token keyword;

    BreakStmt(Token keyword) : keyword(keyword) {
    }

    void accept(ASTVisitor *visitor) override;
};

class ContinueStmt : public StmtNode {
  public:
    Token keyword;

    ContinueStmt(Token keyword) : keyword(keyword) {
    }

    void accept(ASTVisitor *visitor) override;
};

class ForStmt : public StmtNode {
  public:
    StmtNode *initializer;
    ExprNode *condition;
    ExprNode *increment;
    StmtNode *body;

    ForStmt(StmtNode *initializer, ExprNode *condition, ExprNode *increment, StmtNode *body)
        : initializer(initializer), condition(condition), increment(increment), body(body) {
    }

    ~ForStmt() {
        delete initializer;
        delete condition;
        delete increment;
        delete body;
    }

    void accept(ASTVisitor *visitor) override;
};

class ForeachStmt : public StmtNode {
  public:
    Token name;
    ExprNode *iterable;
    StmtNode *body;

    ForeachStmt(Token name, ExprNode *iterable, StmtNode *body) : name(name), iterable(iterable), body(body) {
    }

    ~ForeachStmt() {
        delete iterable;
        delete body;
    }

    void accept(ASTVisitor *visitor) override;
};

class DoWhileStmt : public StmtNode {
  public:
    ExprNode *condition;
    StmtNode *body;

    DoWhileStmt(ExprNode *condition, StmtNode *body) : condition(condition), body(body) {
    }

    ~DoWhileStmt() {
        delete condition;
        delete body;
    }

    void accept(ASTVisitor *visitor) override;
};

class UsingStmt : public StmtNode {
  public:
    StmtNode *declaration;
    StmtNode *body;

    UsingStmt(StmtNode *declaration, StmtNode *body) : declaration(declaration), body(body) {
    }

    ~UsingStmt() {
        delete declaration;
        delete body;
    }

    void accept(ASTVisitor *visitor) override;
};

class SwitchStmt : public StmtNode {
  public:
    ExprNode *expression;
    struct Case {
        ExprNode *value; // nullptr for default
        std::vector<StmtNode *> body;
    };
    std::vector<Case> cases;

    SwitchStmt(ExprNode *expression, std::vector<Case> cases) : expression(expression), cases(std::move(cases)) {
    }

    ~SwitchStmt() {
        delete expression;
        for (auto &c : cases) {
            delete c.value;
            for (auto *s : c.body)
                delete s;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class FunctionNode : public StmtNode {
  public:
    std::string name;
    std::vector<Parameter> params;
    std::vector<StmtNode *> body;
    AccessModifier accessModifier;
    bool isAbstract;
    bool isStatic;

    FunctionNode(std::string name, std::vector<Parameter> params, std::vector<StmtNode *> body)
        : name(name), params(params), body(body), accessModifier(AccessModifier::PUBLIC), isAbstract(false),
          isStatic(false) {
    }

    FunctionNode()
        : name(""), params(), body(), accessModifier(AccessModifier::PUBLIC), isAbstract(false), isStatic(false) {
    }

    ~FunctionNode() {
        for (auto &param : params) {
            if (param.defaultValue)
                delete param.defaultValue;
        }
        for (auto stmt : body) {
            delete stmt;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class FieldDeclNode : public StmtNode {
  public:
    std::string name;
    ExprNode *initializer;
    AccessModifier accessModifier;
    bool isConst;
    bool isStatic;

    FieldDeclNode(std::string name, ExprNode *initializer, AccessModifier accessModifier = AccessModifier::PUBLIC,
                  bool isConst = false)
        : name(name), initializer(initializer), accessModifier(accessModifier), isConst(isConst), isStatic(false) {
    }

    ~FieldDeclNode() {
        delete initializer;
    }

    void accept(ASTVisitor *visitor) override;
};

class ClassNode : public StmtNode {
  public:
    std::string name;
    std::string parentName;
    std::vector<FunctionNode *> methods;
    std::vector<FieldDeclNode *> fields;
    bool isAbstract;
    std::vector<std::string> interfaceNames;

    ClassNode(std::string name, std::string parentName, std::vector<FunctionNode *> methods,
              std::vector<FieldDeclNode *> fields = {})
        : name(name), parentName(parentName), methods(methods), fields(fields), isAbstract(false) {
    }

    ~ClassNode() {
        for (auto method : methods) {
            delete method;
        }
        for (auto field : fields) {
            delete field;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class InterfaceNode : public StmtNode {
  public:
    std::string name;
    std::vector<FunctionNode *> methods;
    std::vector<std::string> parentNames;

    InterfaceNode(std::string name, std::vector<FunctionNode *> methods, std::vector<std::string> parentNames = {})
        : name(name), methods(methods), parentNames(parentNames) {
    }

    ~InterfaceNode() {
        for (auto method : methods) {
            delete method;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class TraitNode : public StmtNode {
  public:
    std::string name;
    std::vector<FunctionNode *> methods;
    std::vector<std::string> parentNames;

    TraitNode(std::string name, std::vector<FunctionNode *> methods, std::vector<std::string> parentNames = {})
        : name(name), methods(methods), parentNames(parentNames) {
    }

    ~TraitNode() {
        for (auto method : methods) {
            delete method;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class StaticGetExpr : public ExprNode {
  public:
    Token className;
    Token memberName;

    StaticGetExpr(Token className, Token memberName) : className(className), memberName(memberName) {
    }

    void accept(ASTVisitor *visitor) override;
};

class StaticCallExpr : public ExprNode {
  public:
    Token className;
    Token memberName;
    Token paren;
    std::vector<ExprNode *> arguments;

    StaticCallExpr(Token className, Token memberName, Token paren, std::vector<ExprNode *> arguments)
        : className(className), memberName(memberName), paren(paren), arguments(arguments) {
    }

    ~StaticCallExpr() {
        for (auto arg : arguments) {
            delete arg;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class StaticSetExpr : public ExprNode {
  public:
    Token className;
    Token memberName;
    ExprNode *value;

    StaticSetExpr(Token className, Token memberName, ExprNode *value)
        : className(className), memberName(memberName), value(value) {
    }

    ~StaticSetExpr() {
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class LoadStmt : public StmtNode {
  public:
    Token filename;

    LoadStmt(Token filename) : filename(filename) {
    }

    void accept(ASTVisitor *visitor) override;
};

class NamespaceStmt : public StmtNode {
  public:
    Token name;

    NamespaceStmt(Token name) : name(name) {
    }

    void accept(ASTVisitor *visitor) override;
};

class UseStmt : public StmtNode {
  public:
    Token name;
    Token alias;

    UseStmt(Token name, Token alias) : name(name), alias(alias) {
    }

    void accept(ASTVisitor *visitor) override;
};

class ASTVisitor {
  public:
    virtual ~ASTVisitor() = default;
    virtual void visit(ProgramNode *node) = 0;
    virtual void visit(BinaryExpr *node) = 0;
    virtual void visit(LiteralExpr *node) = 0;
    virtual void visit(VariableExpr *node) = 0;
    virtual void visit(AssignExpr *node) = 0;
    virtual void visit(CompoundAssignExpr *node) = 0;
    virtual void visit(CallExpr *node) = 0;
    virtual void visit(ExpressionStmt *node) = 0;
    virtual void visit(VarStmt *node) = 0;
    virtual void visit(BlockStmt *node) = 0;
    virtual void visit(IfStmt *node) = 0;
    virtual void visit(WhileStmt *node) = 0;
    virtual void visit(DoWhileStmt *node) = 0;
    virtual void visit(ReturnStmt *node) = 0;
    virtual void visit(BreakStmt *node) = 0;
    virtual void visit(ContinueStmt *node) = 0;
    virtual void visit(ForStmt *node) = 0;
    virtual void visit(ForeachStmt *node) = 0;
    virtual void visit(SwitchStmt *node) = 0;
    virtual void visit(UsingStmt *node) = 0;
    virtual void visit(LambdaExpr *node) = 0;
    virtual void visit(UnaryExpr *node) = 0;
    virtual void visit(ThisExpr *node) = 0;
    virtual void visit(SuperExpr *node) = 0;
    virtual void visit(GetExpr *node) = 0;
    virtual void visit(SetExpr *node) = 0;
    virtual void visit(PostfixExpr *node) = 0;
    virtual void visit(TernaryExpr *node) = 0;
    virtual void visit(ListExpr *node) = 0;
    virtual void visit(IndexGetExpr *node) = 0;
    virtual void visit(IndexSetExpr *node) = 0;
    virtual void visit(FunctionNode *node) = 0;
    virtual void visit(FieldDeclNode *node) = 0;
    virtual void visit(ClassNode *node) = 0;
    virtual void visit(InterfaceNode *node) = 0;
    virtual void visit(TraitNode *node) = 0;
    virtual void visit(StaticGetExpr *node) = 0;
    virtual void visit(StaticCallExpr *node) = 0;
    virtual void visit(StaticSetExpr *node) = 0;
    virtual void visit(LoadStmt *node) = 0;
    virtual void visit(DictExpr *node) = 0;
    virtual void visit(NamespaceStmt *node) = 0;
    virtual void visit(UseStmt *node) = 0;
};

#endif // AST_H
