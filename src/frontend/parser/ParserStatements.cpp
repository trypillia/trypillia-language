#include <stdexcept>
#include <vector>

#include "../../utils/ErrorHandling.h"
#include "../ast/AST.h"
#include "Parser.h"

Parser::Parser(Lexer &lexer) : lexer(lexer)
{
    advance();
}

void Parser::advance()
{
    previousToken = currentToken;
    currentToken = lexer.nextToken();
}

void Parser::consume(TokenType type)
{
    if (currentToken.type == type)
    {
        advance();
    }
    else
    {
        std::string message = "Expected token type " + tokenTypeToString(type) + ", got " +
                              tokenTypeToString(currentToken.type) + " at line " + std::to_string(currentToken.line) +
                              ":" + std::to_string(currentToken.column);
        ErrorHandling::reportError(message);
        throw std::runtime_error(message);
    }
}

// Match checks if the current token is of the expected type
bool Parser::match(TokenType type)
{
    if (currentToken.type == type)
    {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(std::initializer_list<TokenType> types)
{
    for (auto type : types)
    {
        if (currentToken.type == type)
        {
            advance();
            return true;
        }
    }
    return false;
}

ASTNode *Parser::parse()
{
    std::vector<ASTNode *> declarations;
    try
    {
        while (currentToken.type != TokenType::END_OF_FILE)
        {
            declarations.push_back(declaration());
        }
    }
    catch (const std::exception &e)
    {
        ErrorHandling::reportError("Error while parsing: " + std::string(e.what()));
        // Synchronize to continue parsing despite errors
        synchronize();
    }

    return new ProgramNode(declarations);
}

// Primary expressions: literals, identifiers, grouped expressions
StmtNode *Parser::expressionStatement()
{
    ExprNode *expr = expression();
    Token semicolon = currentToken;
    consume(TokenType::SEMICOLON);
    return new ExpressionStmt(expr, semicolon);
}

StmtNode *Parser::loadStatement()
{
    Token keywordLoad = previousToken;
    Token filename = currentToken;
    consume(TokenType::STRING);
    Token semicolon = currentToken;
    consume(TokenType::SEMICOLON);
    return new LoadStmt(keywordLoad, filename, semicolon);
}

StmtNode *Parser::block()
{
    Token leftBrace = previousToken;
    std::vector<StmtNode *> statements;

    while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
    {
        statements.push_back(dynamic_cast<StmtNode *>(declaration()));
    }

    Token rightBrace = currentToken;
    consume(TokenType::RBRACE);
    return new BlockStmt(leftBrace, statements, rightBrace);
}

StmtNode *Parser::ifStatement()
{
    Token keywordIf = previousToken;
    Token leftParen = currentToken;
    consume(TokenType::LPAREN);
    ExprNode *condition = expression();
    Token rightParen = currentToken;
    consume(TokenType::RPAREN);

    StmtNode *thenBranch = statement();
    StmtNode *elseBranch = nullptr;

    Token keywordElse;
    keywordElse.type = TokenType::UNKNOWN;
    if (match(TokenType::ELSE))
    {
        keywordElse = previousToken;
        elseBranch = statement();
    }

    return new IfStmt(keywordIf, leftParen, condition, rightParen, thenBranch, keywordElse, elseBranch);
}

StmtNode *Parser::whileStatement()
{
    Token keywordWhile = previousToken;
    Token leftParen = currentToken;
    consume(TokenType::LPAREN);
    ExprNode *condition = expression();
    Token rightParen = currentToken;
    consume(TokenType::RPAREN);
    StmtNode *body = statement();

    return new WhileStmt(keywordWhile, leftParen, condition, rightParen, body);
}

StmtNode *Parser::doWhileStatement()
{
    Token keywordDo = previousToken;
    StmtNode *body = statement();

    Token keywordWhile = currentToken;
    consume(TokenType::WHILE);

    Token leftParen = currentToken;
    consume(TokenType::LPAREN);

    ExprNode *condition = expression();

    Token rightParen = currentToken;
    consume(TokenType::RPAREN);

    Token semicolon = currentToken;
    consume(TokenType::SEMICOLON);

    return new DoWhileStmt(keywordDo, body, keywordWhile, leftParen, condition, rightParen, semicolon);
}

StmtNode *Parser::returnStatement()
{
    Token keyword = currentToken;
    advance();

    if (currentToken.type == TokenType::SEMICOLON)
    {
        Token semicolon = currentToken;
        advance();
        return new ReturnStmt(keyword, nullptr, semicolon);
    }

    ExprNode *value = expression();
    Token semicolon = currentToken;
    consume(TokenType::SEMICOLON);
    return new ReturnStmt(keyword, value, semicolon);
}

StmtNode *Parser::breakStatement()
{
    Token keyword = currentToken;
    advance();
    Token semicolon = currentToken;
    consume(TokenType::SEMICOLON);
    return new BreakStmt(keyword, semicolon);
}

StmtNode *Parser::continueStatement()
{
    Token keyword = currentToken;
    advance();
    Token semicolon = currentToken;
    consume(TokenType::SEMICOLON);
    return new ContinueStmt(keyword, semicolon);
}

StmtNode *Parser::switchStatement()
{
    Token keywordSwitch = previousToken;
    Token leftParen = currentToken;
    consume(TokenType::LPAREN);
    ExprNode *expr = expression();
    Token rightParen = currentToken;
    consume(TokenType::RPAREN);
    Token leftBrace = currentToken;
    consume(TokenType::LBRACE);

    std::vector<SwitchStmt::Case> cases;

    while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
    {
        if (match(TokenType::CASE))
        {
            Token keywordCaseOrDefault = previousToken;
            ExprNode *value = expression();
            Token colon = currentToken;
            consume(TokenType::COLON);

            std::vector<StmtNode *> body;
            while (currentToken.type != TokenType::CASE && currentToken.type != TokenType::DEFAULT &&
                   currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
            {
                body.push_back(statement());
            }

            cases.push_back({keywordCaseOrDefault, value, colon, body});
        }
        else if (match(TokenType::DEFAULT))
        {
            Token keywordCaseOrDefault = previousToken;
            Token colon = currentToken;
            consume(TokenType::COLON);

            std::vector<StmtNode *> body;
            while (currentToken.type != TokenType::CASE && currentToken.type != TokenType::RBRACE &&
                   currentToken.type != TokenType::END_OF_FILE)
            {
                body.push_back(statement());
            }

            cases.push_back({keywordCaseOrDefault, nullptr, colon, body});
        }
        else
        {
            throw std::runtime_error("Expected 'case' or 'default' in switch statement");
        }
    }

    Token rightBrace = currentToken;
    consume(TokenType::RBRACE);
    return new SwitchStmt(keywordSwitch, leftParen, expr, rightParen, leftBrace, cases, rightBrace);
}

StmtNode *Parser::usingStatement()
{
    consume(TokenType::LPAREN);

    StmtNode *declaration = nullptr;
    if (currentToken.type == TokenType::LET || currentToken.type == TokenType::CONST)
    {
        bool isConst = (currentToken.type == TokenType::CONST);
        Token keyword = currentToken;
        consume(isConst ? TokenType::CONST : TokenType::LET);
        if (isConst)
        {
            consume(TokenType::LET);
        }

        Token name = currentToken;
        consume(TokenType::IDENTIFIER);

        Token assign;
        assign.type = TokenType::UNKNOWN;
        ExprNode *initializer = nullptr;
        if (match(TokenType::ASSIGN))
        {
            assign = previousToken;
            initializer = expression();
        }
        else if (isConst)
        {
            throw std::runtime_error("Constant declaration requires an initializer");
        }
        Token dummySemicolon;
        dummySemicolon.type = TokenType::UNKNOWN;
        declaration = new VarStmt(keyword, name, assign, initializer, dummySemicolon, isConst);
    }
    else
    {
        ExprNode *expr = expression();
        Token dummySemicolon;
        dummySemicolon.type = TokenType::UNKNOWN;
        declaration = new ExpressionStmt(expr, dummySemicolon);
    }

    consume(TokenType::RPAREN);
    StmtNode *body = statement();

    return new UsingStmt(declaration, body);
}

StmtNode *Parser::forStatement()
{
    Token keywordFor = currentToken;
    advance(); // consume FOR
    Token leftParen = currentToken;
    consume(TokenType::LPAREN);

    // Check for foreach: for (let x in iterable)
    if (match(TokenType::LET))
    {
        Token keyword = previousToken;
        Token name = currentToken;
        consume(TokenType::IDENTIFIER);

        if (match(TokenType::IN))
        {
            Token keywordIn = previousToken;
            ExprNode *iterable = expression();
            Token rightParen = currentToken;
            consume(TokenType::RPAREN);
            StmtNode *body = statement();
            return new ForeachStmt(keywordFor, leftParen, name, keywordIn, iterable, rightParen, body);
        }

        // Regular for with var initializer
        Token assign;
        assign.type = TokenType::UNKNOWN;
        ExprNode *initExpr = nullptr;
        if (match(TokenType::ASSIGN))
        {
            assign = previousToken;
            initExpr = expression();
        }

        Token dummySemicolon;
        dummySemicolon.type = TokenType::UNKNOWN;
        StmtNode *initializer = new VarStmt(keyword, name, assign, initExpr, dummySemicolon);
        Token semicolon1 = currentToken;
        consume(TokenType::SEMICOLON);
        return finishForLoop(keywordFor, leftParen, initializer, semicolon1);
    }

    // Regular for without var initializer
    StmtNode *initializer = nullptr;
    Token semicolon1;
    if (currentToken.type == TokenType::SEMICOLON)
    {
        semicolon1 = currentToken;
        advance(); // consume semicolon
    }
    else
    {
        ExprNode *expr = expression();
        Token dummySemicolon;
        dummySemicolon.type = TokenType::UNKNOWN;
        initializer = new ExpressionStmt(expr, dummySemicolon);
        semicolon1 = currentToken;
        consume(TokenType::SEMICOLON);
    }

    return finishForLoop(keywordFor, leftParen, initializer, semicolon1);
}

StmtNode *Parser::finishForLoop(Token keywordFor, Token leftParen, StmtNode *initializer, Token semicolon1)
{
    // Condition (optional, default true)
    ExprNode *condition = nullptr;
    if (currentToken.type != TokenType::SEMICOLON)
    {
        condition = expression();
    }
    Token semicolon2 = currentToken;
    consume(TokenType::SEMICOLON);

    // Increment (optional)
    ExprNode *increment = nullptr;
    if (currentToken.type != TokenType::RPAREN)
    {
        increment = expression();
    }
    Token rightParen = currentToken;
    consume(TokenType::RPAREN);

    StmtNode *body = statement();

    return new ForStmt(keywordFor, leftParen, initializer, semicolon1, condition, semicolon2, increment, rightParen,
                       body);
}

StmtNode *Parser::statement()
{
    if (match(TokenType::IF))
    {
        return ifStatement();
    }

    if (match(TokenType::LOAD))
    {
        return loadStatement();
    }

    if (match(TokenType::WHILE))
    {
        return whileStatement();
    }

    if (match(TokenType::DO))
    {
        return doWhileStatement();
    }

    if (currentToken.type == TokenType::RETURN)
    {
        return returnStatement();
    }

    if (currentToken.type == TokenType::BREAK)
    {
        return breakStatement();
    }

    if (currentToken.type == TokenType::CONTINUE)
    {
        return continueStatement();
    }

    if (currentToken.type == TokenType::FOR)
    {
        return forStatement();
    }

    if (match(TokenType::SWITCH))
    {
        return switchStatement();
    }

    if (match(TokenType::LBRACE))
    {
        return block();
    }

    if (currentToken.type == TokenType::USING)
    {
        advance();
        return usingStatement();
    }

    return expressionStatement();
}

// Declaration parsers
StmtNode *Parser::varDeclaration()
{
    Token keyword = currentToken;
    bool isConst = (currentToken.type == TokenType::CONST);
    consume(isConst ? TokenType::CONST : TokenType::LET);
    if (isConst)
    {
        consume(TokenType::LET);
    }

    Token name = currentToken;
    consume(TokenType::IDENTIFIER);

    Token assign;
    assign.type = TokenType::UNKNOWN;
    ExprNode *initializer = nullptr;
    if (match(TokenType::ASSIGN))
    {
        assign = previousToken;
        initializer = expression();
    }
    else if (isConst)
    {
        throw std::runtime_error("Constant declaration requires an initializer");
    }

    Token semicolon = currentToken;
    consume(TokenType::SEMICOLON);
    return new VarStmt(keyword, name, assign, initializer, semicolon, isConst);
}

FieldDeclNode *Parser::parseFieldDecl(AccessModifier accessModifier)
{
    bool isConst = (currentToken.type == TokenType::CONST);
    if (isConst)
    {
        advance();
    }

    // Optionally consume 'let'
    if (currentToken.type == TokenType::LET)
    {
        advance();
    }

    Token name = currentToken;
    consume(TokenType::IDENTIFIER);

    ExprNode *initializer = nullptr;
    if (currentToken.type == TokenType::ASSIGN)
    {
        advance();
        initializer = expression();
    }
    else if (isConst)
    {
        throw std::runtime_error("Constant field requires an initializer");
    }

    consume(TokenType::SEMICOLON);
    return new FieldDeclNode(name.lexeme, initializer, accessModifier, isConst);
}

FunctionNode *Parser::parseFunction(AccessModifier accessModifier, bool isAbstract, bool isStatic)
{
    Token keywordFn = currentToken;
    consume(TokenType::FN);

    Token name = currentToken;
    consume(TokenType::IDENTIFIER);

    Token leftParen = currentToken;
    consume(TokenType::LPAREN);
    std::vector<Parameter> parameters;

    if (currentToken.type != TokenType::RPAREN)
    {
        do
        {
            Token param = currentToken;
            consume(TokenType::IDENTIFIER);
            ExprNode *defVal = nullptr;
            if (match(TokenType::ASSIGN))
            {
                defVal = expression();
            }
            parameters.push_back({param.lexeme, defVal});
        } while (match(TokenType::COMMA));
    }

    Token rightParen = currentToken;
    consume(TokenType::RPAREN);

    if (isAbstract)
    {
        Token semicolon = currentToken;
        consume(TokenType::SEMICOLON);
        Token emptyLeftBrace;
        emptyLeftBrace.type = TokenType::UNKNOWN;
        Token emptyRightBrace;
        emptyRightBrace.type = TokenType::UNKNOWN;
        FunctionNode *node = new FunctionNode(name.lexeme, keywordFn, name, leftParen, parameters, rightParen,
                                              emptyLeftBrace, {}, emptyRightBrace, semicolon);
        node->accessModifier = accessModifier;
        node->isAbstract = true;
        node->isStatic = isStatic;
        return node;
    }

    // Parse function body
    Token leftBrace = currentToken;
    consume(TokenType::LBRACE);
    std::vector<StmtNode *> body;

    while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
    {
        body.push_back(dynamic_cast<StmtNode *>(declaration()));
    }

    Token rightBrace = currentToken;
    consume(TokenType::RBRACE);

    Token emptySemicolon;
    emptySemicolon.type = TokenType::UNKNOWN;
    FunctionNode *node = new FunctionNode(name.lexeme, keywordFn, name, leftParen, parameters, rightParen, leftBrace,
                                          body, rightBrace, emptySemicolon);
    node->accessModifier = accessModifier;
    node->isStatic = isStatic;
    return node;
}

ClassNode *Parser::parseClass()
{
    consume(TokenType::CLASS);

    Token name = currentToken;
    consume(TokenType::IDENTIFIER);

    // Parse optional parent class (< ParentName)
    std::string parentName = "";
    if (currentToken.type == TokenType::LESS)
    {
        advance();
        Token parent = currentToken;
        consume(TokenType::IDENTIFIER);
        parentName = parent.lexeme;
    }

    // Parse optional implements clause
    std::vector<std::string> interfaceNames;
    if (currentToken.type == TokenType::IMPLEMENTS)
    {
        advance();
        do
        {
            Token iface = currentToken;
            consume(TokenType::IDENTIFIER);
            interfaceNames.push_back(iface.lexeme);
        } while (match(TokenType::COMMA));
    }

    // Parse class body
    consume(TokenType::LBRACE);
    std::vector<FunctionNode *> methods;
    std::vector<FieldDeclNode *> fields;

    while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
    {
        AccessModifier memberAccess = AccessModifier::PUBLIC;

        if (currentToken.type == TokenType::PUBLIC)
        {
            advance();
            memberAccess = AccessModifier::PUBLIC;
        }
        else if (currentToken.type == TokenType::PRIVATE)
        {
            advance();
            memberAccess = AccessModifier::PRIVATE;
        }
        else if (currentToken.type == TokenType::PROTECTED)
        {
            advance();
            memberAccess = AccessModifier::PROTECTED;
        }

        if (currentToken.type == TokenType::ABSTRACT)
        {
            advance();
            methods.push_back(parseFunction(memberAccess, true));
        }
        else if (currentToken.type == TokenType::FN)
        {
            methods.push_back(parseFunction(memberAccess));
        }
        else if (currentToken.type == TokenType::DESTROY)
        {
            Token destroyKeyword = currentToken;
            advance();
            Token leftBrace = currentToken;
            consume(TokenType::LBRACE);
            std::vector<StmtNode *> body;
            while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
            {
                body.push_back(dynamic_cast<StmtNode *>(declaration()));
            }
            Token rightBrace = currentToken;
            consume(TokenType::RBRACE);
            Token emptyToken;
            emptyToken.type = TokenType::UNKNOWN;
            auto node = new FunctionNode("destroy", destroyKeyword, destroyKeyword, emptyToken, {}, emptyToken,
                                         leftBrace, body, rightBrace, emptyToken);
            node->accessModifier = memberAccess;
            methods.push_back(node);
        }
        else if (currentToken.type == TokenType::STATIC)
        {
            advance();
            if (currentToken.type == TokenType::FN)
            {
                methods.push_back(parseFunction(memberAccess, false, true));
            }
            else if (currentToken.type == TokenType::LET || currentToken.type == TokenType::IDENTIFIER ||
                     currentToken.type == TokenType::CONST)
            {
                auto field = parseFieldDecl(memberAccess);
                field->isStatic = true;
                fields.push_back(field);
            }
            else
            {
                throw std::runtime_error("Expected 'fn' or field declaration after 'static'");
            }
        }
        else if (currentToken.type == TokenType::LET || currentToken.type == TokenType::IDENTIFIER ||
                 currentToken.type == TokenType::CONST)
        {
            fields.push_back(parseFieldDecl(memberAccess));
        }
        else
        {
            advance();
        }
    }

    consume(TokenType::RBRACE);

    auto node = new ClassNode(name.lexeme, parentName, methods, fields);
    node->interfaceNames = interfaceNames;
    return node;
}

InterfaceNode *Parser::parseInterface()
{
    Token name = currentToken;
    consume(TokenType::IDENTIFIER);

    // Parse optional parent interfaces (< Parent1, Parent2)
    std::vector<std::string> parentNames;
    if (currentToken.type == TokenType::LESS)
    {
        advance();
        do
        {
            Token parent = currentToken;
            consume(TokenType::IDENTIFIER);
            parentNames.push_back(parent.lexeme);
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::LBRACE);
    std::vector<FunctionNode *> methods;

    while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
    {
        if (currentToken.type == TokenType::FN)
        {
            Token keywordFn = currentToken;
            advance();

            Token methodName = currentToken;
            consume(TokenType::IDENTIFIER);

            Token leftParen = currentToken;
            consume(TokenType::LPAREN);
            std::vector<Parameter> parameters;
            if (currentToken.type != TokenType::RPAREN)
            {
                do
                {
                    Token param = currentToken;
                    consume(TokenType::IDENTIFIER);
                    ExprNode *defVal = nullptr;
                    if (match(TokenType::ASSIGN))
                    {
                        defVal = expression();
                    }
                    parameters.push_back({param.lexeme, defVal});
                } while (match(TokenType::COMMA));
            }
            Token rightParen = currentToken;
            consume(TokenType::RPAREN);
            Token semicolon = currentToken;
            consume(TokenType::SEMICOLON);

            Token emptyToken;
            emptyToken.type = TokenType::UNKNOWN;
            auto method = new FunctionNode(methodName.lexeme, keywordFn, methodName, leftParen, parameters, rightParen,
                                           emptyToken, {}, emptyToken, semicolon);
            method->isAbstract = true;
            methods.push_back(method);
        }
        else
        {
            advance();
        }
    }

    consume(TokenType::RBRACE);
    return new InterfaceNode(name.lexeme, methods, parentNames);
}

TraitNode *Parser::parseTrait()
{
    Token name = currentToken;
    consume(TokenType::IDENTIFIER);

    std::vector<std::string> parentNames;
    if (currentToken.type == TokenType::LESS)
    {
        advance();
        do
        {
            Token parent = currentToken;
            consume(TokenType::IDENTIFIER);
            parentNames.push_back(parent.lexeme);
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::LBRACE);
    std::vector<FunctionNode *> methods;

    while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
    {
        if (currentToken.type == TokenType::FN)
        {
            Token keywordFn = currentToken;
            advance();

            Token methodName = currentToken;
            consume(TokenType::IDENTIFIER);

            Token leftParen = currentToken;
            consume(TokenType::LPAREN);
            std::vector<Parameter> parameters;
            if (currentToken.type != TokenType::RPAREN)
            {
                do
                {
                    Token param = currentToken;
                    consume(TokenType::IDENTIFIER);
                    ExprNode *defVal = nullptr;
                    if (match(TokenType::ASSIGN))
                    {
                        defVal = expression();
                    }
                    parameters.push_back({param.lexeme, defVal});
                } while (match(TokenType::COMMA));
            }
            Token rightParen = currentToken;
            consume(TokenType::RPAREN);

            if (currentToken.type == TokenType::SEMICOLON)
            {
                // Abstract method
                Token semicolon = currentToken;
                advance();
                Token emptyToken;
                emptyToken.type = TokenType::UNKNOWN;
                auto method = new FunctionNode(methodName.lexeme, keywordFn, methodName, leftParen, parameters,
                                               rightParen, emptyToken, {}, emptyToken, semicolon);
                method->isAbstract = true;
                methods.push_back(method);
            }
            else
            {
                // Method with body
                Token leftBrace = currentToken;
                consume(TokenType::LBRACE);
                std::vector<StmtNode *> body;
                while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
                {
                    body.push_back(dynamic_cast<StmtNode *>(declaration()));
                }
                Token rightBrace = currentToken;
                consume(TokenType::RBRACE);
                Token emptyToken;
                emptyToken.type = TokenType::UNKNOWN;
                auto method = new FunctionNode(methodName.lexeme, keywordFn, methodName, leftParen, parameters,
                                               rightParen, leftBrace, body, rightBrace, emptyToken);
                methods.push_back(method);
            }
        }
        else
        {
            advance();
        }
    }

    consume(TokenType::RBRACE);
    return new TraitNode(name.lexeme, methods, parentNames);
}

ASTNode *Parser::declaration()
{
    if (currentToken.type == TokenType::NAMESPACE)
    {
        advance();
        return parseNamespaceDeclaration();
    }
    if (currentToken.type == TokenType::USE)
    {
        advance();
        return parseUseStatement();
    }
    if (currentToken.type == TokenType::ABSTRACT)
    {
        advance();
        if (currentToken.type == TokenType::CLASS)
        {
            auto node = parseClass();
            node->isAbstract = true;
            return node;
        }
        throw std::runtime_error("Expected 'class' after 'abstract'");
    }

    if (currentToken.type == TokenType::INTERFACE)
    {
        advance();
        return parseInterface();
    }

    if (currentToken.type == TokenType::TRAIT)
    {
        advance();
        return parseTrait();
    }

    if (currentToken.type == TokenType::CLASS)
    {
        return parseClass();
    }

    if (currentToken.type == TokenType::FN)
    {
        return parseFunction();
    }

    if (currentToken.type == TokenType::LET || currentToken.type == TokenType::CONST)
    {
        return varDeclaration();
    }

    return statement();
}

// Error recovery
void Parser::synchronize()
{
    advance();

    while (currentToken.type != TokenType::END_OF_FILE)
    {
        // Look for statement boundaries to resynchronize
        if (currentToken.type == TokenType::SEMICOLON)
        {
            advance();
            return;
        }

        switch (currentToken.type)
        {
        case TokenType::CLASS:
        case TokenType::FN:
        case TokenType::LET:
        case TokenType::IF:
        case TokenType::WHILE:
        case TokenType::LOAD:
            return;
        default:
            break;
        }

        advance();
    }
}

StmtNode *Parser::parseNamespaceDeclaration()
{
    Token keywordNamespace = previousToken;
    Token nameToken = currentToken;
    std::string ns = "";
    do
    {
        consume(TokenType::IDENTIFIER);
        ns += nameToken.lexeme;
        if (match(TokenType::DOT))
        {
            ns += ".";
            nameToken = currentToken;
        }
        else
        {
            break;
        }
    } while (true);

    Token semicolon = currentToken;
    consume(TokenType::SEMICOLON);
    this->currentNamespace = ns;

    Token nsToken = nameToken;
    nsToken.lexeme = ns;
    return new NamespaceStmt(keywordNamespace, nsToken, semicolon);
}

StmtNode *Parser::parseUseStatement()
{
    Token keywordUse = previousToken;
    Token nameToken = currentToken;
    std::string fqn = "";
    std::string lastId = "";
    do
    {
        consume(TokenType::IDENTIFIER);
        fqn += nameToken.lexeme;
        lastId = nameToken.lexeme;
        if (match(TokenType::DOT))
        {
            fqn += ".";
            nameToken = currentToken;
        }
        else
        {
            break;
        }
    } while (true);

    Token keywordAs;
    keywordAs.type = TokenType::UNKNOWN;

    // We don't currently parse 'as alias', but if we did we'd match it here
    Token aliasToken = nameToken;
    aliasToken.lexeme = lastId;

    Token semicolon = currentToken;
    consume(TokenType::SEMICOLON);

    this->useAliases[lastId] = fqn;

    Token fqnToken = nameToken;
    fqnToken.lexeme = fqn;

    return new UseStmt(keywordUse, fqnToken, keywordAs, aliasToken, semicolon);
}
