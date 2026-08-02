#include <map>
#ifndef PARSER_H
#define PARSER_H

#include <initializer_list>

#include "../ast/AST.h"
#include "../lexer/Lexer.h"

class Parser
{
  public:
    Parser(Lexer &lexer);
    ASTNode *parse();

  private:
    Lexer &lexer;
    Token currentToken;
    Token previousToken;
    std::string currentNamespace = "";
    std::map<std::string, std::string> useAliases;

    // Core parsing methods
    void advance();
    void consume(TokenType type);
    bool match(TokenType type);
    bool match(std::initializer_list<TokenType> types);

    // Error recovery
    void synchronize();

    // Expression parsing methods
    ExprNode *expression();

    enum Precedence
    {
        PREC_NONE,
        PREC_ASSIGNMENT,  // = += -= *= /=
        PREC_TERNARY,     // ?:
        PREC_OR,          // or
        PREC_AND,         // and
        PREC_BITWISE_OR,  // |
        PREC_BITWISE_XOR, // ^
        PREC_BITWISE_AND, // &
        PREC_EQUALITY,    // == !=
        PREC_COMPARISON,  // < > <= >=
        PREC_SHIFT,       // << >>
        PREC_TERM,        // + -
        PREC_FACTOR,      // * / %
        PREC_UNARY,       // ! - ~ ++ --
        PREC_CALL,        // . () [] :: ++ --
        PREC_PRIMARY
    };

    typedef ExprNode *(Parser::*ParsePrefixFn)(bool canAssign);
    typedef ExprNode *(Parser::*ParseInfixFn)(ExprNode *left, bool canAssign);

    struct ParseRule
    {
        ParsePrefixFn prefix;
        ParseInfixFn infix;
        Precedence precedence;
    };

    ParseRule getRule(TokenType type);
    ExprNode *parsePrecedence(Precedence precedence);

    ExprNode *parseNumber(bool canAssign);
    ExprNode *parseString(bool canAssign);
    ExprNode *parseIdentifier(bool canAssign);
    ExprNode *parseLiteral(bool canAssign);
    ExprNode *parseThis(bool canAssign);
    ExprNode *parseSuper(bool canAssign);
    ExprNode *parseGrouping(bool canAssign);
    ExprNode *parseList(bool canAssign);
    ExprNode *parseDict(bool canAssign);
    ExprNode *parseFn(bool canAssign);

    ExprNode *parseUnary(bool canAssign);
    ExprNode *parseBinary(ExprNode *left, bool canAssign);
    ExprNode *parseCall(ExprNode *left, bool canAssign);
    ExprNode *parseDot(ExprNode *left, bool canAssign);
    ExprNode *parseIndex(ExprNode *left, bool canAssign);
    ExprNode *parseStatic(ExprNode *left, bool canAssign);
    ExprNode *parsePostfix(ExprNode *left, bool canAssign);
    ExprNode *parseTernary(ExprNode *left, bool canAssign);
    ExprNode *parseAssignment(ExprNode *left, bool canAssign);

    // Statement parsing methods
    StmtNode *statement();
    StmtNode *expressionStatement();
    StmtNode *loadStatement();
    StmtNode *block();
    StmtNode *ifStatement();
    StmtNode *whileStatement();
    StmtNode *doWhileStatement();
    StmtNode *forStatement();
    StmtNode *finishForLoop(Token keywordFor, Token leftParen, StmtNode *initializer, Token semicolon1);
    StmtNode *returnStatement();
    StmtNode *breakStatement();
    StmtNode *continueStatement();
    StmtNode *switchStatement();
    StmtNode *usingStatement();
    StmtNode *parseNamespaceDeclaration();
    StmtNode *parseUseStatement();

    // Declaration parsing methods
    ASTNode *declaration();
    StmtNode *varDeclaration();
    FieldDeclNode *parseFieldDecl(AccessModifier accessModifier = AccessModifier::PUBLIC);
    FunctionNode *parseFunction(AccessModifier accessModifier = AccessModifier::PUBLIC, bool isAbstract = false,
                                bool isStatic = false);
    ClassNode *parseClass();
    InterfaceNode *parseInterface();
    TraitNode *parseTrait();
};

#endif // PARSER_H
