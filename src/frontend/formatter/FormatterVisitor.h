#ifndef FORMATTER_VISITOR_H
#define FORMATTER_VISITOR_H

#include "../ast/AST.h"
#include <string>

struct FormatterOptions
{
    int indentWidth = 4;
    bool useTabs = false;
    bool bracesOnNewLine = false; // true for Allman style, false for K&R
};

class FormatterVisitor : public ASTVisitor
{
  private:
    std::string output;
    int indentLevel;
    FormatterOptions options;

    void printIndent();
    void printToken(const Token &token);
    void printSpace();
    void printNewline();

  public:
    FormatterVisitor(const FormatterOptions &options = FormatterOptions());

    std::string getOutput() const;

    void visit(ProgramNode *node) override;
    void visit(BinaryExpr *node) override;
    void visit(LiteralExpr *node) override;
    void visit(VariableExpr *node) override;
    void visit(AssignExpr *node) override;
    void visit(CompoundAssignExpr *node) override;
    void visit(CallExpr *node) override;
    void visit(ExpressionStmt *node) override;
    void visit(VarStmt *node) override;
    void visit(BlockStmt *node) override;
    void visit(IfStmt *node) override;
    void visit(WhileStmt *node) override;
    void visit(DoWhileStmt *node) override;
    void visit(ReturnStmt *node) override;
    void visit(BreakStmt *node) override;
    void visit(ContinueStmt *node) override;
    void visit(ForStmt *node) override;
    void visit(ForeachStmt *node) override;
    void visit(SwitchStmt *node) override;
    void visit(UsingStmt *node) override;
    void visit(LambdaExpr *node) override;
    void visit(UnaryExpr *node) override;
    void visit(ParenExprNode *node) override;
    void visit(ThisExpr *node) override;
    void visit(SuperExpr *node) override;
    void visit(GetExpr *node) override;
    void visit(SetExpr *node) override;
    void visit(PostfixExpr *node) override;
    void visit(TernaryExpr *node) override;
    void visit(ListExpr *node) override;
    void visit(IndexGetExpr *node) override;
    void visit(IndexSetExpr *node) override;
    void visit(FunctionNode *node) override;
    void visit(FieldDeclNode *node) override;
    void visit(ClassNode *node) override;
    void visit(InterfaceNode *node) override;
    void visit(TraitNode *node) override;
    void visit(StaticGetExpr *node) override;
    void visit(StaticCallExpr *node) override;
    void visit(StaticSetExpr *node) override;
    void visit(LoadStmt *node) override;
    void visit(DictExpr *node) override;
    void visit(NamespaceStmt *node) override;
    void visit(UseStmt *node) override;
};

#endif // FORMATTER_VISITOR_H
