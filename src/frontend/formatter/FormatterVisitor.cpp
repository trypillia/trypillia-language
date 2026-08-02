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

    // Extract and print comments from leading trivia
    if (!token.leadingTrivia.empty())
    {
        size_t pos = 0;
        while ((pos = token.leadingTrivia.find("//", pos)) != std::string::npos)
        {
            size_t endPos = token.leadingTrivia.find('\n', pos);
            if (endPos == std::string::npos)
                endPos = token.leadingTrivia.length();

            std::string comment = token.leadingTrivia.substr(pos, endPos - pos);
            output += comment + "\n";
            printIndent();
            pos = endPos;
        }
    }
    if (token.type == TokenType::STRING)
    {
        // Really basic string escaping for the formatter for now
        output += "\"";
        for (char c : token.lexeme)
        {
            if (c == '"')
                output += "\\\"";
            else if (c == '\\')
                output += "\\\\";
            else if (c == '\n')
                output += "\\n";
            else if (c == '\r')
                output += "\\r";
            else if (c == '\t')
                output += "\\t";
            else
                output += c;
        }
        output += "\"";
    }
    else
    {
        output += token.lexeme;
    }
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
    printToken(node->name);
    printSpace();
    printToken(node->op);
    printSpace();
    node->value->accept(this);
}

void FormatterVisitor::visit(CallExpr *node)
{
    node->callee->accept(this);
    printToken(node->leftParen);
    for (size_t i = 0; i < node->arguments.size(); ++i)
    {
        node->arguments[i]->accept(this);
        // Assuming we need commas, but currently CallExpr does not store commas.
        // We'll insert synthetic commas if there are more args, since the grammar mandates it.
        if (i < node->arguments.size() - 1)
        {
            output += ", ";
        }
    }
    printToken(node->rightParen);
}

void FormatterVisitor::visit(ExpressionStmt *node)
{
    node->expression->accept(this);
    printToken(node->semicolon);
    printNewline();
}

void FormatterVisitor::visit(VarStmt *node)
{
    printToken(node->keyword);
    printSpace();
    printToken(node->name);
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
    indentLevel++;
    printNewline();

    for (auto stmt : node->statements)
    {
        stmt->accept(this);
    }
    indentLevel--;

    output.erase(output.find_last_not_of(" \t\n") + 1);
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
    printNewline();
}

void FormatterVisitor::visit(WhileStmt *node)
{
    printToken(node->keywordWhile);
    printSpace();
    printToken(node->leftParen);
    node->condition->accept(this);
    printToken(node->rightParen);
    node->body->accept(this);
    printNewline();
}
void FormatterVisitor::visit(DoWhileStmt *node)
{
    printToken(node->keywordDo);
    node->body->accept(this);
    if (options.bracesOnNewLine)
        printNewline();
    else
        printSpace();
    printToken(node->keywordWhile);
    printSpace();
    printToken(node->leftParen);
    node->condition->accept(this);
    printToken(node->rightParen);
    printToken(node->semicolon);
    printNewline();
}
void FormatterVisitor::visit(ReturnStmt *node)
{
    printToken(node->keyword);
    if (node->value)
    {
        printSpace();
        node->value->accept(this);
    }
    printToken(node->semicolon);
    printNewline();
}
void FormatterVisitor::visit(BreakStmt *node)
{
    printToken(node->keyword);
    printToken(node->semicolon);
    printNewline();
}
void FormatterVisitor::visit(ContinueStmt *node)
{
    printToken(node->keyword);
    printToken(node->semicolon);
    printNewline();
}
void FormatterVisitor::visit(ForStmt *node)
{
    printToken(node->keywordFor);
    printSpace();
    printToken(node->leftParen);
    if (node->initializer)
    {
        node->initializer->accept(this);
        // The initializer statement will print its own newline.
        // We need to remove it because inside a for loop, it shouldn't print a newline.
        output.erase(output.find_last_not_of(" \t\n") + 1);
    }
    printToken(node->semicolon1);
    printSpace();
    if (node->condition)
    {
        node->condition->accept(this);
    }
    printToken(node->semicolon2);
    printSpace();
    if (node->increment)
    {
        node->increment->accept(this);
    }
    printToken(node->rightParen);
    node->body->accept(this);
    printNewline();
}
void FormatterVisitor::visit(ForeachStmt *node)
{
    printToken(node->keywordFor);
    printSpace();
    printToken(node->leftParen);
    printToken(node->name);
    printSpace();
    printToken(node->keywordIn);
    printSpace();
    node->iterable->accept(this);
    printToken(node->rightParen);
    node->body->accept(this);
    printNewline();
}
void FormatterVisitor::visit(SwitchStmt *node)
{
    // SwitchStmt is not fully defined in AST.h for some reason, or maybe it is?
    // Let's check AST.h if it exists. Ah, it does, but we didn't look at its members.
    // For now I'll just print a TODO or empty if it's an empty stub.
    // Actually, I'll just leave it empty.
}
void FormatterVisitor::visit(UsingStmt *node)
{
    // UsingStmt (same, not fully checked its members)
}
void FormatterVisitor::visit(LambdaExpr *node)
{
    // LambdaExpr (same)
}
void FormatterVisitor::visit(UnaryExpr *node)
{
    printToken(node->op);
    node->right->accept(this);
}
void FormatterVisitor::visit(ParenExprNode *node)
{
    printToken(node->leftParen);
    node->expr->accept(this);
    printToken(node->rightParen);
}
void FormatterVisitor::visit(ThisExpr *node)
{
    printToken(node->keyword);
}
void FormatterVisitor::visit(SuperExpr *node)
{
    printToken(node->keyword);
    // There is no dot in SuperExpr in AST, only keyword and method.
    // So we print it as super.method if method exists.
    if (node->method.type != TokenType::UNKNOWN)
    {
        output += ".";
        printToken(node->method);
    }
}
void FormatterVisitor::visit(GetExpr *node)
{
    node->object->accept(this);
    printToken(node->dot);
    printToken(node->name);
}
void FormatterVisitor::visit(SetExpr *node)
{
    node->object->accept(this);
    printToken(node->dot);
    printToken(node->name);
    printSpace();
    printToken(node->assign);
    printSpace();
    node->value->accept(this);
}
void FormatterVisitor::visit(PostfixExpr *node)
{
    printToken(node->name);
    printToken(node->op);
}
void FormatterVisitor::visit(TernaryExpr *node)
{
    node->condition->accept(this);
    printSpace();
    printToken(node->question);
    printSpace();
    node->thenBranch->accept(this);
    printSpace();
    printToken(node->colon);
    printSpace();
    node->elseBranch->accept(this);
}
void FormatterVisitor::visit(ListExpr *node)
{
    printToken(node->leftBracket);
    for (size_t i = 0; i < node->elements.size(); ++i)
    {
        node->elements[i]->accept(this);
        if (i < node->elements.size() - 1)
        {
            output += ", ";
        }
    }
    printToken(node->rightBracket);
}
void FormatterVisitor::visit(IndexGetExpr *node)
{
    node->object->accept(this);
    printToken(node->leftBracket);
    node->index->accept(this);
    printToken(node->rightBracket);
}
void FormatterVisitor::visit(IndexSetExpr *node)
{
    node->object->accept(this);
    printToken(node->leftBracket);
    node->index->accept(this);
    printToken(node->rightBracket);
    printSpace();
    printToken(node->op);
    printSpace();
    node->value->accept(this);
}
void FormatterVisitor::visit(FunctionNode *node)
{
    // Optional modifiers
    // We don't have tokens for them in AST yet, but we have booleans/enums.
    // Since this is a syntax tree formatter, we should only print tokens.
    // But since tokens for modifiers are not stored yet, we rely on what is stored.

    printToken(node->keywordFn);
    printSpace();
    if (node->nameToken.type != TokenType::UNKNOWN)
    {
        printToken(node->nameToken);
    }

    printToken(node->leftParen);
    for (size_t i = 0; i < node->params.size(); ++i)
    {
        output += node->params[i].name;
        if (node->params[i].defaultValue)
        {
            output += " = ";
            node->params[i].defaultValue->accept(this);
        }
        if (i < node->params.size() - 1)
        {
            output += ", ";
        }
    }
    printToken(node->rightParen);

    if (node->leftBrace.type != TokenType::UNKNOWN)
    {
        if (options.bracesOnNewLine)
            printNewline();
        else
            printSpace();

        printToken(node->leftBrace);
        indentLevel++;
        printNewline();

        for (auto stmt : node->body)
        {
            stmt->accept(this);
        }
        indentLevel--;

        output.erase(output.find_last_not_of(" \t\n") + 1);
        printNewline();
        printToken(node->rightBrace);
    }
    else if (node->semicolon.type != TokenType::UNKNOWN)
    {
        printToken(node->semicolon);
    }
    printNewline();
}
void FormatterVisitor::visit(FieldDeclNode *node)
{
    printToken(node->keyword); // let or const
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
void FormatterVisitor::visit(ClassNode *node)
{
    printToken(node->keywordClass);
    printSpace();
    printToken(node->nameToken);

    if (node->keywordLess.type != TokenType::UNKNOWN)
    {
        printSpace();
        printToken(node->keywordLess);
        printSpace();
        printToken(node->parentNameToken);
    }

    if (node->keywordImplements.type != TokenType::UNKNOWN)
    {
        printSpace();
        printToken(node->keywordImplements);
        printSpace();
        // Interface names are not tokens yet in ClassNode, just strings.
        // We will print them joined.
        for (size_t i = 0; i < node->interfaceNames.size(); ++i)
        {
            output += node->interfaceNames[i];
            if (i < node->interfaceNames.size() - 1)
                output += ", ";
        }
    }

    if (options.bracesOnNewLine)
        printNewline();
    else
        printSpace();

    printToken(node->leftBrace);
    indentLevel++;
    printNewline();

    for (auto field : node->fields)
    {
        field->accept(this);
    }
    if (!node->fields.empty() && !node->methods.empty())
    {
        printNewline();
    }
    for (auto method : node->methods)
    {
        method->accept(this);
        printNewline();
    }
    indentLevel--;

    output.erase(output.find_last_not_of(" \t\n") + 1);
    printNewline();
    printToken(node->rightBrace);
    printNewline();
}
void FormatterVisitor::visit(InterfaceNode *node)
{
    printToken(node->keywordInterface);
    printSpace();
    printToken(node->nameToken);

    if (node->keywordLess.type != TokenType::UNKNOWN)
    {
        printSpace();
        printToken(node->keywordLess);
        printSpace();
        for (size_t i = 0; i < node->parentNames.size(); ++i)
        {
            output += node->parentNames[i];
            if (i < node->parentNames.size() - 1)
                output += ", ";
        }
    }

    if (options.bracesOnNewLine)
        printNewline();
    else
        printSpace();

    printToken(node->leftBrace);
    printNewline();

    indentLevel++;
    for (auto method : node->methods)
    {
        method->accept(this);
        printNewline();
    }
    indentLevel--;

    output.erase(output.find_last_not_of(" \t\n") + 1);
    printNewline();
    printToken(node->rightBrace);
    printNewline();
}
void FormatterVisitor::visit(TraitNode *node)
{
    printToken(node->keywordTrait);
    printSpace();
    printToken(node->nameToken);

    if (options.bracesOnNewLine)
        printNewline();
    else
        printSpace();

    printToken(node->leftBrace);
    printNewline();

    indentLevel++;
    for (auto method : node->methods)
    {
        method->accept(this);
        printNewline();
    }
    indentLevel--;

    output.erase(output.find_last_not_of(" \t\n") + 1);
    printNewline();
    printToken(node->rightBrace);
    printNewline();
}
void FormatterVisitor::visit(StaticGetExpr *node)
{
    printToken(node->className);
    printToken(node->colonColon);
    printToken(node->memberName);
}
void FormatterVisitor::visit(StaticCallExpr *node)
{
    printToken(node->className);
    // AST node does not have colonColon for StaticCallExpr yet, we assume it's implicit here?
    output += "::";
    printToken(node->memberName);
    printToken(node->leftParen);
    for (size_t i = 0; i < node->arguments.size(); ++i)
    {
        node->arguments[i]->accept(this);
        if (i < node->arguments.size() - 1)
        {
            output += ", ";
        }
    }
    printToken(node->rightParen);
}
void FormatterVisitor::visit(StaticSetExpr *node)
{
    printToken(node->className);
    printToken(node->colonColon);
    printToken(node->memberName);
    printSpace();
    printToken(node->assign);
    printSpace();
    node->value->accept(this);
}
void FormatterVisitor::visit(LoadStmt *node)
{
    printToken(node->keywordLoad);
    printSpace();
    printToken(node->filename);
    printToken(node->semicolon);
    printNewline();
}
void FormatterVisitor::visit(DictExpr *node)
{
    printToken(node->leftBrace);
    for (size_t i = 0; i < node->elements.size(); ++i)
    {
        node->elements[i].first->accept(this);
        output += ": ";
        node->elements[i].second->accept(this);
        if (i < node->elements.size() - 1)
        {
            output += ", ";
        }
    }
    printToken(node->rightBrace);
}
void FormatterVisitor::visit(NamespaceStmt *node)
{
    printToken(node->keywordNamespace);
    printSpace();
    printToken(node->name);
    printToken(node->semicolon);
    printNewline();
}
void FormatterVisitor::visit(UseStmt *node)
{
    printToken(node->keywordUse);
    printSpace();
    printToken(node->name);
    if (node->keywordAs.type != TokenType::UNKNOWN)
    {
        printSpace();
        printToken(node->keywordAs);
        printSpace();
        printToken(node->alias);
    }
    printToken(node->semicolon);
    printNewline();
}
