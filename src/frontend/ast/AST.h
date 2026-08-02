#ifndef AST_H
#define AST_H

#include <string>
#include <vector>

#include "../lexer/Lexer.h"

// Forward declaration
class ASTVisitor;

// Access modifier for class members
enum class AccessModifier
{
    PUBLIC,
    PRIVATE,
    PROTECTED
};

struct Parameter
{
    std::string name;
    class ExprNode *defaultValue;
};

class ASTNode
{
  public:
    int line = 0;
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor *visitor) = 0;
};

class ExprNode : public ASTNode
{
  public:
    virtual ~ExprNode() = default;
};

class StmtNode : public ASTNode
{
  public:
    virtual ~StmtNode() = default;
};

class ProgramNode : public ASTNode
{
  public:
    std::vector<ASTNode *> declarations;

    ProgramNode(const std::vector<ASTNode *> &declarations) : declarations(declarations)
    {
    }
    virtual ~ProgramNode()
    {
        for (auto decl : declarations)
        {
            delete decl;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class UnaryExpr : public ExprNode
{
  public:
    Token op;
    ExprNode *right;

    UnaryExpr(Token op, ExprNode *right) : op(op), right(right)
    {
    }

    ~UnaryExpr()
    {
        delete right;
    }

    void accept(ASTVisitor *visitor) override;
};

class ThisExpr : public ExprNode
{
  public:
    Token keyword;

    ThisExpr(Token keyword) : keyword(keyword)
    {
    }

    void accept(ASTVisitor *visitor) override;
};

class SuperExpr : public ExprNode
{
  public:
    Token keyword;
    Token method;

    SuperExpr(Token keyword, Token method) : keyword(keyword), method(method)
    {
    }

    void accept(ASTVisitor *visitor) override;
};

class GetExpr : public ExprNode
{
  public:
    ExprNode *object;
    Token dot;
    Token name;

    GetExpr(ExprNode *object, Token dot, Token name) : object(object), dot(dot), name(name)
    {
    }

    ~GetExpr()
    {
        delete object;
    }

    void accept(ASTVisitor *visitor) override;
};

class SetExpr : public ExprNode
{
  public:
    ExprNode *object;
    Token dot;
    Token name;
    Token assign;
    ExprNode *value;

    SetExpr(ExprNode *object, Token dot, Token name, Token assign, ExprNode *value)
        : object(object), dot(dot), name(name), assign(assign), value(value)
    {
    }

    ~SetExpr()
    {
        delete object;
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class ParenExprNode : public ExprNode
{
  public:
    Token leftParen;
    ExprNode *expr;
    Token rightParen;

    ParenExprNode(Token leftParen, ExprNode *expr, Token rightParen)
        : leftParen(leftParen), expr(expr), rightParen(rightParen)
    {
    }

    ~ParenExprNode()
    {
        delete expr;
    }

    void accept(ASTVisitor *visitor) override;
};

class PostfixExpr : public ExprNode
{
  public:
    Token name;
    Token op;

    PostfixExpr(Token name, Token op) : name(name), op(op)
    {
    }

    void accept(ASTVisitor *visitor) override;
};

class TernaryExpr : public ExprNode
{
  public:
    ExprNode *condition;
    Token question;
    ExprNode *thenBranch;
    Token colon;
    ExprNode *elseBranch;

    TernaryExpr(ExprNode *condition, Token question, ExprNode *thenBranch, Token colon, ExprNode *elseBranch)
        : condition(condition), question(question), thenBranch(thenBranch), colon(colon), elseBranch(elseBranch)
    {
    }

    ~TernaryExpr()
    {
        delete condition;
        delete thenBranch;
        delete elseBranch;
    }

    void accept(ASTVisitor *visitor) override;
};

class BinaryExpr : public ExprNode
{
  public:
    ExprNode *left;
    Token op;
    ExprNode *right;

    BinaryExpr(ExprNode *left, Token op, ExprNode *right) : left(left), op(op), right(right)
    {
    }

    ~BinaryExpr()
    {
        delete left;
        delete right;
    }

    void accept(ASTVisitor *visitor) override;
};

class LiteralExpr : public ExprNode
{
  public:
    Token value;

    LiteralExpr(Token value) : value(value)
    {
    }

    void accept(ASTVisitor *visitor) override;
};

class VariableExpr : public ExprNode
{
  public:
    Token name;

    VariableExpr(Token name) : name(name)
    {
    }

    void accept(ASTVisitor *visitor) override;
};

class AssignExpr : public ExprNode
{
  public:
    Token name;
    Token assign;
    ExprNode *value;

    AssignExpr(Token name, Token assign, ExprNode *value) : name(name), assign(assign), value(value)
    {
    }
    ~AssignExpr()
    {
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class CompoundAssignExpr : public ExprNode
{
  public:
    Token name;
    Token op;
    ExprNode *value;

    CompoundAssignExpr(Token name, Token op, ExprNode *value) : name(name), op(op), value(value)
    {
    }
    ~CompoundAssignExpr()
    {
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class CallExpr : public ExprNode
{
  public:
    ExprNode *callee;
    Token leftParen;
    std::vector<ExprNode *> arguments;
    Token rightParen;

    CallExpr(ExprNode *callee, Token leftParen, std::vector<ExprNode *> arguments, Token rightParen)
        : callee(callee), leftParen(leftParen), arguments(arguments), rightParen(rightParen)
    {
    }

    ~CallExpr()
    {
        delete callee;
        for (auto arg : arguments)
        {
            delete arg;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class ExpressionStmt : public StmtNode
{
  public:
    ExprNode *expression;
    Token semicolon;

    ExpressionStmt(ExprNode *expression, Token semicolon) : expression(expression), semicolon(semicolon)
    {
    }
    ~ExpressionStmt()
    {
        delete expression;
    }

    void accept(ASTVisitor *visitor) override;
};

class ListExpr : public ExprNode
{
  public:
    Token leftBracket;
    std::vector<ExprNode *> elements;
    Token rightBracket;

    ListExpr(Token leftBracket, std::vector<ExprNode *> elements, Token rightBracket)
        : leftBracket(leftBracket), elements(elements), rightBracket(rightBracket)
    {
    }

    ~ListExpr()
    {
        for (auto *el : elements)
            delete el;
    }

    void accept(ASTVisitor *visitor) override;
};

class DictExpr : public ExprNode
{
  public:
    Token leftBrace;
    std::vector<std::pair<ExprNode *, ExprNode *>> elements; // key, value
    Token rightBrace;

    DictExpr(Token leftBrace, std::vector<std::pair<ExprNode *, ExprNode *>> elements, Token rightBrace)
        : leftBrace(leftBrace), elements(elements), rightBrace(rightBrace)
    {
    }

    ~DictExpr()
    {
        for (auto &s : elements)
        {
            delete s.first;
            delete s.second;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class LambdaExpr : public ExprNode
{
  public:
    Token leftParen;
    std::vector<Parameter> params;
    Token rightParen;
    Token arrow;
    bool isExpressionBody;
    Token leftBrace;
    std::vector<StmtNode *> body;
    Token rightBrace;

    LambdaExpr(Token leftParen, std::vector<Parameter> params, Token rightParen, Token arrow, bool isExpressionBody,
               Token leftBrace, std::vector<StmtNode *> body, Token rightBrace)
        : leftParen(leftParen), params(params), rightParen(rightParen), arrow(arrow),
          isExpressionBody(isExpressionBody), leftBrace(leftBrace), body(body), rightBrace(rightBrace)
    {
    }

    ~LambdaExpr()
    {
        for (auto &param : params)
        {
            if (param.defaultValue)
                delete param.defaultValue;
        }
        for (auto stmt : body)
            delete stmt;
    }

    void accept(ASTVisitor *visitor) override;
};

class IndexGetExpr : public ExprNode
{
  public:
    ExprNode *object;
    ExprNode *index;

    IndexGetExpr(ExprNode *object, ExprNode *index) : object(object), index(index)
    {
    }

    ~IndexGetExpr()
    {
        delete object;
        delete index;
    }

    void accept(ASTVisitor *visitor) override;
};

class IndexSetExpr : public ExprNode
{
  public:
    ExprNode *object;
    ExprNode *index;
    ExprNode *value;

    IndexSetExpr(ExprNode *object, ExprNode *index, ExprNode *value) : object(object), index(index), value(value)
    {
    }

    ~IndexSetExpr()
    {
        delete object;
        delete index;
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class VarStmt : public StmtNode
{
  public:
    Token keyword;
    Token name;
    Token assign;
    ExprNode *initializer;
    Token semicolon;
    bool isConst;

    VarStmt(Token keyword, Token name, Token assign, ExprNode *initializer, Token semicolon, bool isConst = false)
        : keyword(keyword), name(name), assign(assign), initializer(initializer), semicolon(semicolon), isConst(isConst)
    {
    }

    ~VarStmt()
    {
        delete initializer;
    }

    void accept(ASTVisitor *visitor) override;
};

class BlockStmt : public StmtNode
{
  public:
    Token leftBrace;
    std::vector<StmtNode *> statements;
    Token rightBrace;

    BlockStmt(Token leftBrace, std::vector<StmtNode *> statements, Token rightBrace)
        : leftBrace(leftBrace), statements(statements), rightBrace(rightBrace)
    {
    }

    ~BlockStmt()
    {
        for (auto stmt : statements)
        {
            delete stmt;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class IfStmt : public StmtNode
{
  public:
    Token keywordIf;
    Token leftParen;
    ExprNode *condition;
    Token rightParen;
    StmtNode *thenBranch;
    Token keywordElse;
    StmtNode *elseBranch;

    IfStmt(Token keywordIf, Token leftParen, ExprNode *condition, Token rightParen, StmtNode *thenBranch,
           Token keywordElse, StmtNode *elseBranch)
        : keywordIf(keywordIf), leftParen(leftParen), condition(condition), rightParen(rightParen),
          thenBranch(thenBranch), keywordElse(keywordElse), elseBranch(elseBranch)
    {
    }

    ~IfStmt()
    {
        delete condition;
        delete thenBranch;
        delete elseBranch;
    }

    void accept(ASTVisitor *visitor) override;
};

class WhileStmt : public StmtNode
{
  public:
    Token keywordWhile;
    Token leftParen;
    ExprNode *condition;
    Token rightParen;
    StmtNode *body;

    WhileStmt(Token keywordWhile, Token leftParen, ExprNode *condition, Token rightParen, StmtNode *body)
        : keywordWhile(keywordWhile), leftParen(leftParen), condition(condition), rightParen(rightParen), body(body)
    {
    }

    ~WhileStmt()
    {
        delete condition;
        delete body;
    }

    void accept(ASTVisitor *visitor) override;
};

class ReturnStmt : public StmtNode
{
  public:
    Token keyword;
    ExprNode *value;
    Token semicolon;

    ReturnStmt(Token keyword, ExprNode *value, Token semicolon) : keyword(keyword), value(value), semicolon(semicolon)
    {
    }

    ~ReturnStmt()
    {
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class BreakStmt : public StmtNode
{
  public:
    Token keyword;

    BreakStmt(Token keyword) : keyword(keyword)
    {
    }

    void accept(ASTVisitor *visitor) override;
};

class ContinueStmt : public StmtNode
{
  public:
    Token keyword;

    ContinueStmt(Token keyword) : keyword(keyword)
    {
    }

    void accept(ASTVisitor *visitor) override;
};

class ForStmt : public StmtNode
{
  public:
    Token keywordFor;
    Token leftParen;
    StmtNode *initializer;
    Token semicolon1;
    ExprNode *condition;
    Token semicolon2;
    ExprNode *increment;
    Token rightParen;
    StmtNode *body;

    ForStmt(Token keywordFor, Token leftParen, StmtNode *initializer, Token semicolon1, ExprNode *condition,
            Token semicolon2, ExprNode *increment, Token rightParen, StmtNode *body)
        : keywordFor(keywordFor), leftParen(leftParen), initializer(initializer), semicolon1(semicolon1),
          condition(condition), semicolon2(semicolon2), increment(increment), rightParen(rightParen), body(body)
    {
    }

    ~ForStmt()
    {
        delete initializer;
        delete condition;
        delete increment;
        delete body;
    }

    void accept(ASTVisitor *visitor) override;
};

class ForeachStmt : public StmtNode
{
  public:
    Token name;
    ExprNode *iterable;
    StmtNode *body;

    ForeachStmt(Token name, ExprNode *iterable, StmtNode *body) : name(name), iterable(iterable), body(body)
    {
    }

    ~ForeachStmt()
    {
        delete iterable;
        delete body;
    }

    void accept(ASTVisitor *visitor) override;
};

class DoWhileStmt : public StmtNode
{
  public:
    Token keywordDo;
    StmtNode *body;
    Token keywordWhile;
    Token leftParen;
    ExprNode *condition;
    Token rightParen;
    Token semicolon;

    DoWhileStmt(Token keywordDo, StmtNode *body, Token keywordWhile, Token leftParen, ExprNode *condition,
                Token rightParen, Token semicolon)
        : keywordDo(keywordDo), body(body), keywordWhile(keywordWhile), leftParen(leftParen), condition(condition),
          rightParen(rightParen), semicolon(semicolon)
    {
    }

    ~DoWhileStmt()
    {
        delete condition;
        delete body;
    }

    void accept(ASTVisitor *visitor) override;
};

class UsingStmt : public StmtNode
{
  public:
    StmtNode *declaration;
    StmtNode *body;

    UsingStmt(StmtNode *declaration, StmtNode *body) : declaration(declaration), body(body)
    {
    }

    ~UsingStmt()
    {
        delete declaration;
        delete body;
    }

    void accept(ASTVisitor *visitor) override;
};

class SwitchStmt : public StmtNode
{
  public:
    ExprNode *expression;
    struct Case
    {
        ExprNode *value; // nullptr for default
        std::vector<StmtNode *> body;
    };
    std::vector<Case> cases;

    SwitchStmt(ExprNode *expression, std::vector<Case> cases) : expression(expression), cases(std::move(cases))
    {
    }

    ~SwitchStmt()
    {
        delete expression;
        for (auto &c : cases)
        {
            delete c.value;
            for (auto *s : c.body)
                delete s;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class FunctionNode : public StmtNode
{
  public:
    std::string name;
    std::vector<Parameter> params;
    std::vector<StmtNode *> body;
    AccessModifier accessModifier;
    bool isAbstract;
    bool isStatic;

    FunctionNode(std::string name, std::vector<Parameter> params, std::vector<StmtNode *> body)
        : name(name), params(params), body(body), accessModifier(AccessModifier::PUBLIC), isAbstract(false),
          isStatic(false)
    {
    }

    FunctionNode()
        : name(""), params(), body(), accessModifier(AccessModifier::PUBLIC), isAbstract(false), isStatic(false)
    {
    }

    ~FunctionNode()
    {
        for (auto &param : params)
        {
            if (param.defaultValue)
                delete param.defaultValue;
        }
        for (auto stmt : body)
        {
            delete stmt;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class FieldDeclNode : public StmtNode
{
  public:
    std::string name;
    ExprNode *initializer;
    AccessModifier accessModifier;
    bool isConst;
    bool isStatic;

    FieldDeclNode(std::string name, ExprNode *initializer, AccessModifier accessModifier = AccessModifier::PUBLIC,
                  bool isConst = false)
        : name(name), initializer(initializer), accessModifier(accessModifier), isConst(isConst), isStatic(false)
    {
    }

    ~FieldDeclNode()
    {
        delete initializer;
    }

    void accept(ASTVisitor *visitor) override;
};

class ClassNode : public StmtNode
{
  public:
    std::string name;
    std::string parentName;
    std::vector<FunctionNode *> methods;
    std::vector<FieldDeclNode *> fields;
    bool isAbstract;
    std::vector<std::string> interfaceNames;

    ClassNode(std::string name, std::string parentName, std::vector<FunctionNode *> methods,
              std::vector<FieldDeclNode *> fields = {})
        : name(name), parentName(parentName), methods(methods), fields(fields), isAbstract(false)
    {
    }

    ~ClassNode()
    {
        for (auto method : methods)
        {
            delete method;
        }
        for (auto field : fields)
        {
            delete field;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class InterfaceNode : public StmtNode
{
  public:
    std::string name;
    std::vector<FunctionNode *> methods;
    std::vector<std::string> parentNames;

    InterfaceNode(std::string name, std::vector<FunctionNode *> methods, std::vector<std::string> parentNames = {})
        : name(name), methods(methods), parentNames(parentNames)
    {
    }

    ~InterfaceNode()
    {
        for (auto method : methods)
        {
            delete method;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class TraitNode : public StmtNode
{
  public:
    std::string name;
    std::vector<FunctionNode *> methods;
    std::vector<std::string> parentNames;

    TraitNode(std::string name, std::vector<FunctionNode *> methods, std::vector<std::string> parentNames = {})
        : name(name), methods(methods), parentNames(parentNames)
    {
    }

    ~TraitNode()
    {
        for (auto method : methods)
        {
            delete method;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class StaticGetExpr : public ExprNode
{
  public:
    Token className;
    Token colonColon;
    Token memberName;

    StaticGetExpr(Token className, Token colonColon, Token memberName)
        : className(className), colonColon(colonColon), memberName(memberName)
    {
    }

    void accept(ASTVisitor *visitor) override;
};

class StaticCallExpr : public ExprNode
{
  public:
    Token className;
    Token memberName;
    Token leftParen;
    std::vector<ExprNode *> arguments;
    Token rightParen;

    StaticCallExpr(Token className, Token memberName, Token leftParen, std::vector<ExprNode *> arguments,
                   Token rightParen)
        : className(className), memberName(memberName), leftParen(leftParen), arguments(arguments),
          rightParen(rightParen)
    {
    }

    ~StaticCallExpr()
    {
        for (auto arg : arguments)
        {
            delete arg;
        }
    }

    void accept(ASTVisitor *visitor) override;
};

class StaticSetExpr : public ExprNode
{
  public:
    Token className;
    Token colonColon;
    Token memberName;
    Token assign;
    ExprNode *value;

    StaticSetExpr(Token className, Token colonColon, Token memberName, Token assign, ExprNode *value)
        : className(className), colonColon(colonColon), memberName(memberName), assign(assign), value(value)
    {
    }

    ~StaticSetExpr()
    {
        delete value;
    }

    void accept(ASTVisitor *visitor) override;
};

class LoadStmt : public StmtNode
{
  public:
    Token filename;

    LoadStmt(Token filename) : filename(filename)
    {
    }

    void accept(ASTVisitor *visitor) override;
};

class NamespaceStmt : public StmtNode
{
  public:
    Token name;

    NamespaceStmt(Token name) : name(name)
    {
    }

    void accept(ASTVisitor *visitor) override;
};

class UseStmt : public StmtNode
{
  public:
    Token name;
    Token alias;

    UseStmt(Token name, Token alias) : name(name), alias(alias)
    {
    }

    void accept(ASTVisitor *visitor) override;
};

class ASTVisitor
{
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
    virtual void visit(ParenExprNode *node) = 0;
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
