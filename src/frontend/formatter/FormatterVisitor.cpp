#include "FormatterVisitor.h"
#include <iostream>

FormatterVisitor::FormatterVisitor(const FormatterOptions &options) : options(options), indentLevel(0)
{
}

std::string FormatterVisitor::getOutput() const
{
    return output;
}

void FormatterVisitor::printIndent()
{
    for (int i = 0; i < indentLevel; ++i)
    {
        if (options.useTabs)
        {
            output += "\t";
        }
        else
        {
            for (int j = 0; j < options.indentWidth; ++j)
            {
                output += " ";
            }
        }
    }
}

void FormatterVisitor::printSpace()
{
    output += " ";
}

void FormatterVisitor::printNewline()
{
    output += "\n";
    printIndent();
}

void FormatterVisitor::printToken(const Token &token)
{
    if (token.type == TokenType::UNKNOWN)
        return;

    // TODO: Handle leading and trailing trivia (comments) when they are fully implemented
    output += token.lexeme;
}

void FormatterVisitor::visit(ProgramNode *node)
{
    for (auto decl : node->declarations)
    {
        decl->accept(this);
        output += "\n";
    }
}

void FormatterVisitor::visit(BinaryExpr *node)
{
    node->left->accept(this);
    printSpace();
    printToken(node->op);
    printSpace();
    node->right->accept(this);
}

void FormatterVisitor::visit(LiteralExpr *node)
{
    printToken(node->value);
}

void FormatterVisitor::visit(VariableExpr *node)
{
    printToken(node->name);
}

void FormatterVisitor::visit(AssignExpr *node)
{
    printToken(node->name);
    printSpace();
    printToken(node->assign);
    printSpace();
    node->value->accept(this);
}

void FormatterVisitor::visit(CompoundAssignExpr *node)
{
}

void FormatterVisitor::visit(CallExpr *node)
{
}

void FormatterVisitor::visit(ExpressionStmt *node)
{
    node->expr->accept(this);
    printToken(node->semicolon);
    printNewline();
}

void FormatterVisitor::visit(VarStmt *node)
{
    printToken(node->keyword);
    printSpace();
    printToken(node->nameToken);
    if (node->initializer)
    {
        printSpace();
        printToken(node->assign);
        printSpace();
        node->initializer->accept(this);
    }
    printToken(node->semicolon);
    printNewline();
}

void FormatterVisitor::visit(BlockStmt *node)
{
    if (options.bracesOnNewLine)
    {
        printNewline();
    }
    else
    {
        printSpace();
    }
    printToken(node->leftBrace);
    printNewline();

    indentLevel++;
    for (auto stmt : node->statements)
    {
        stmt->accept(this);
    }
    indentLevel--;

    // We already moved to the new line at the end of the last statement
    // but the indent was higher. Let's fix that later, this is just a stub.
    output.erase(output.find_last_not_of(" \t") + 1); // remove indent
    printNewline();
    printToken(node->rightBrace);
}

void FormatterVisitor::visit(IfStmt *node)
{
    printToken(node->keywordIf);
    printSpace();
    printToken(node->leftParen);
    node->condition->accept(this);
    printToken(node->rightParen);

    node->thenBranch->accept(this);

    if (node->elseBranch)
    {
        if (options.bracesOnNewLine)
            printNewline();
        else
            printSpace();

        printToken(node->keywordElse);
        node->elseBranch->accept(this);
    }
}

void FormatterVisitor::visit(WhileStmt *node)
{
}
void FormatterVisitor::visit(DoWhileStmt *node)
{
}
void FormatterVisitor::visit(ReturnStmt *node)
{
}
void FormatterVisitor::visit(BreakStmt *node)
{
}
void FormatterVisitor::visit(ContinueStmt *node)
{
}
void FormatterVisitor::visit(ForStmt *node)
{
}
void FormatterVisitor::visit(ForeachStmt *node)
{
}
void FormatterVisitor::visit(SwitchStmt *node)
{
}
void FormatterVisitor::visit(UsingStmt *node)
{
}
void FormatterVisitor::visit(LambdaExpr *node)
{
}
void FormatterVisitor::visit(UnaryExpr *node)
{
}
void FormatterVisitor::visit(ParenExprNode *node)
{
}
void FormatterVisitor::visit(ThisExpr *node)
{
}
void FormatterVisitor::visit(SuperExpr *node)
{
}
void FormatterVisitor::visit(GetExpr *node)
{
}
void FormatterVisitor::visit(SetExpr *node)
{
}
void FormatterVisitor::visit(PostfixExpr *node)
{
}
void FormatterVisitor::visit(TernaryExpr *node)
{
}
void FormatterVisitor::visit(ListExpr *node)
{
}
void FormatterVisitor::visit(IndexGetExpr *node)
{
}
void FormatterVisitor::visit(IndexSetExpr *node)
{
}
void FormatterVisitor::visit(FunctionNode *node)
{
}
void FormatterVisitor::visit(FieldDeclNode *node)
{
}
void FormatterVisitor::visit(ClassNode *node)
{
}
void FormatterVisitor::visit(InterfaceNode *node)
{
}
void FormatterVisitor::visit(TraitNode *node)
{
}
void FormatterVisitor::visit(StaticGetExpr *node)
{
}
void FormatterVisitor::visit(StaticCallExpr *node)
{
}
void FormatterVisitor::visit(StaticSetExpr *node)
{
}
void FormatterVisitor::visit(LoadStmt *node)
{
}
void FormatterVisitor::visit(DictExpr *node)
{
}
void FormatterVisitor::visit(NamespaceStmt *node)
{
}
void FormatterVisitor::visit(UseStmt *node)
{
}
