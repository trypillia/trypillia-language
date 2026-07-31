#include "AST.h"

// Implementation of accept methods for each AST node type

void ProgramNode::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void UnaryExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void ThisExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void SuperExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void GetExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void SetExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void PostfixExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void TernaryExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void ListExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void IndexGetExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void IndexSetExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void BinaryExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void LiteralExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void VariableExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void AssignExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void CompoundAssignExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void CallExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void ExpressionStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void VarStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void BlockStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void IfStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void WhileStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void DoWhileStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void ReturnStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void BreakStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void ContinueStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void ForStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void ForeachStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void FunctionNode::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void SwitchStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void FieldDeclNode::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void ClassNode::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void InterfaceNode::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void TraitNode::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void StaticGetExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void StaticCallExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void StaticSetExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void LoadStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}
void DictExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}
void UsingStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}
void LambdaExpr::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void NamespaceStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}

void UseStmt::accept(ASTVisitor *visitor) {
    visitor->visit(this);
}
