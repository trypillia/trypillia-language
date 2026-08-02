#include <stdexcept>
#include <vector>

#include "../../utils/ErrorHandling.h"
#include "../ast/AST.h"
#include "Parser.h"

Parser::ParseRule Parser::getRule(TokenType type)
{
    switch (type)
    {
    case TokenType::LPAREN:
        return {&Parser::parseGrouping, &Parser::parseCall, PREC_CALL};
    case TokenType::RPAREN:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::LBRACE:
        return {&Parser::parseDict, nullptr, PREC_NONE};
    case TokenType::RBRACE:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::LBRACKET:
        return {&Parser::parseList, &Parser::parseIndex, PREC_CALL};
    case TokenType::RBRACKET:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::COMMA:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::DOT:
        return {nullptr, &Parser::parseDot, PREC_CALL};
    case TokenType::MINUS:
        return {&Parser::parseUnary, &Parser::parseBinary, PREC_TERM};
    case TokenType::PLUS:
        return {nullptr, &Parser::parseBinary, PREC_TERM};
    case TokenType::SEMICOLON:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::SLASH:
        return {nullptr, &Parser::parseBinary, PREC_FACTOR};
    case TokenType::STAR:
        return {nullptr, &Parser::parseBinary, PREC_FACTOR};
    case TokenType::PERCENT:
        return {nullptr, &Parser::parseBinary, PREC_FACTOR};
    case TokenType::BANG:
        return {&Parser::parseUnary, nullptr, PREC_NONE};
    case TokenType::BANG_EQUAL:
        return {nullptr, &Parser::parseBinary, PREC_EQUALITY};
    case TokenType::ASSIGN:
        return {nullptr, &Parser::parseAssignment, PREC_ASSIGNMENT};
    case TokenType::EQUAL_EQUAL:
        return {nullptr, &Parser::parseBinary, PREC_EQUALITY};
    case TokenType::GREATER:
        return {nullptr, &Parser::parseBinary, PREC_COMPARISON};
    case TokenType::GREATER_EQUAL:
        return {nullptr, &Parser::parseBinary, PREC_COMPARISON};
    case TokenType::LESS:
        return {nullptr, &Parser::parseBinary, PREC_COMPARISON};
    case TokenType::LESS_EQUAL:
        return {nullptr, &Parser::parseBinary, PREC_COMPARISON};
    case TokenType::IDENTIFIER:
        return {&Parser::parseIdentifier, nullptr, PREC_NONE};
    case TokenType::STRING:
        return {&Parser::parseString, nullptr, PREC_NONE};
    case TokenType::NUMBER:
        return {&Parser::parseNumber, nullptr, PREC_NONE};
    case TokenType::AND:
        return {nullptr, &Parser::parseBinary, PREC_AND};
    case TokenType::CLASS:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::ELSE:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::FALSE:
        return {&Parser::parseLiteral, nullptr, PREC_NONE};
    case TokenType::FOR:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::FN:
        return {&Parser::parseFn, nullptr, PREC_NONE};
    case TokenType::IF:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::NIL:
        return {&Parser::parseLiteral, nullptr, PREC_NONE};
    case TokenType::OR:
        return {nullptr, &Parser::parseBinary, PREC_OR};
    case TokenType::RETURN:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::SUPER:
        return {&Parser::parseSuper, nullptr, PREC_NONE};
    case TokenType::THIS:
        return {&Parser::parseThis, nullptr, PREC_NONE};
    case TokenType::TRUE:
        return {&Parser::parseLiteral, nullptr, PREC_NONE};
    case TokenType::LET:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::WHILE:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::END_OF_FILE:
        return {nullptr, nullptr, PREC_NONE};
    case TokenType::PLUS_PLUS:
        return {&Parser::parseUnary, &Parser::parsePostfix, PREC_CALL};
    case TokenType::MINUS_MINUS:
        return {&Parser::parseUnary, &Parser::parsePostfix, PREC_CALL};
    case TokenType::PLUS_EQUAL:
        return {nullptr, &Parser::parseAssignment, PREC_ASSIGNMENT};
    case TokenType::MINUS_EQUAL:
        return {nullptr, &Parser::parseAssignment, PREC_ASSIGNMENT};
    case TokenType::STAR_EQUAL:
        return {nullptr, &Parser::parseAssignment, PREC_ASSIGNMENT};
    case TokenType::SLASH_EQUAL:
        return {nullptr, &Parser::parseAssignment, PREC_ASSIGNMENT};
    case TokenType::BITWISE_NOT:
        return {&Parser::parseUnary, nullptr, PREC_NONE};
    case TokenType::BITWISE_AND:
        return {nullptr, &Parser::parseBinary, PREC_BITWISE_AND};
    case TokenType::BITWISE_OR:
        return {nullptr, &Parser::parseBinary, PREC_BITWISE_OR};
    case TokenType::BITWISE_XOR:
        return {nullptr, &Parser::parseBinary, PREC_BITWISE_XOR};
    case TokenType::SHIFT_LEFT:
        return {nullptr, &Parser::parseBinary, PREC_SHIFT};
    case TokenType::SHIFT_RIGHT:
        return {nullptr, &Parser::parseBinary, PREC_SHIFT};
    case TokenType::QUESTION:
        return {nullptr, &Parser::parseTernary, PREC_TERNARY};
    case TokenType::COLON_COLON:
        return {nullptr, &Parser::parseStatic, PREC_CALL};
    default:
        return {nullptr, nullptr, PREC_NONE};
    }
}

ExprNode *Parser::expression()
{
    return parsePrecedence(PREC_ASSIGNMENT);
}

ExprNode *Parser::parsePrecedence(Precedence precedence)
{
    advance();
    ParsePrefixFn prefixRule = getRule(previousToken.type).prefix;
    if (prefixRule == nullptr)
    {
        throw std::runtime_error("Expect expression at '" + previousToken.lexeme + "' line " +
                                 std::to_string(previousToken.line));
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    ExprNode *node = (this->*prefixRule)(canAssign);

    while (precedence <= getRule(currentToken.type).precedence)
    {
        advance();
        ParseInfixFn infixRule = getRule(previousToken.type).infix;
        node = (this->*infixRule)(node, canAssign);
    }

    if (canAssign && match(TokenType::ASSIGN))
    {
        throw std::runtime_error("Invalid assignment target.");
    }

    return node;
}

ExprNode *Parser::parseNumber(bool canAssign)
{
    return new LiteralExpr(previousToken);
}

ExprNode *Parser::parseString(bool canAssign)
{
    Token literal = previousToken;
    std::string str = literal.lexeme;

    // Check for interpolation
    bool hasInterpolation = false;
    for (size_t i = 0; i < str.length(); i++)
    {
        if (str[i] == '{' && (i == 0 || str[i - 1] != '\\'))
        {
            hasInterpolation = true;
            break;
        }
    }

    if (hasInterpolation)
    {
        std::vector<ExprNode *> parts;
        std::string currentPart = "";

        for (size_t i = 0; i < str.length(); i++)
        {
            if (str[i] == '\\' && i + 1 < str.length() && (str[i + 1] == '{' || str[i + 1] == '}'))
            {
                currentPart += str[i + 1];
                i++;
                continue;
            }

            if (str[i] == '{')
            {
                if (!currentPart.empty())
                {
                    Token t = literal;
                    t.lexeme = currentPart;
                    parts.push_back(new LiteralExpr(t));
                    currentPart = "";
                }

                size_t start = i + 1;
                int nest = 1;
                while (i + 1 < str.length() && nest > 0)
                {
                    i++;
                    if (str[i] == '{' && str[i - 1] != '\\')
                        nest++;
                    if (str[i] == '}' && str[i - 1] != '\\')
                        nest--;
                }

                std::string exprStr = str.substr(start, i - start);
                try
                {
                    Lexer lex(exprStr);
                    Parser p(lex);
                    ExprNode *e = p.expression();
                    if (e)
                        parts.push_back(e);
                }
                catch (...)
                {
                    Token t = literal;
                    t.lexeme = "{" + exprStr + "}";
                    parts.push_back(new LiteralExpr(t));
                }
            }
            else
            {
                currentPart += str[i];
            }
        }

        if (!currentPart.empty())
        {
            Token t = literal;
            t.lexeme = currentPart;
            parts.push_back(new LiteralExpr(t));
        }

        if (parts.empty())
            return new LiteralExpr(literal);

        ExprNode *result = parts[0];
        for (size_t j = 1; j < parts.size(); j++)
        {
            Token plusToken;
            plusToken.type = TokenType::PLUS;
            plusToken.lexeme = "+";
            plusToken.line = literal.line;
            result = new BinaryExpr(result, plusToken, parts[j]);
        }
        return result;
    }

    std::string cleanedStr = "";
    for (size_t i = 0; i < str.length(); i++)
    {
        if (str[i] == '\\' && i + 1 < str.length() && (str[i + 1] == '{' || str[i + 1] == '}'))
        {
            cleanedStr += str[i + 1];
            i++;
        }
        else
        {
            cleanedStr += str[i];
        }
    }
    Token t = literal;
    t.lexeme = cleanedStr;
    return new LiteralExpr(t);
}

ExprNode *Parser::parseLiteral(bool canAssign)
{
    return new LiteralExpr(previousToken);
}

ExprNode *Parser::parseThis(bool canAssign)
{
    return new ThisExpr(previousToken);
}

ExprNode *Parser::parseSuper(bool canAssign)
{
    Token keyword = previousToken;
    consume(TokenType::DOT);
    Token method = currentToken;
    consume(TokenType::IDENTIFIER);
    return new SuperExpr(keyword, method);
}

ExprNode *Parser::parseIdentifier(bool canAssign)
{
    Token name = previousToken;

    // Check for arrow lambda: ident =>
    if (currentToken.type == TokenType::ARROW)
    {
        advance(); // consume =>
        std::vector<Parameter> parameters;
        parameters.push_back({name.lexeme, nullptr});

        std::vector<StmtNode *> body;
        if (match(TokenType::LBRACE))
        {
            while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
            {
                body.push_back(dynamic_cast<StmtNode *>(declaration()));
            }
            consume(TokenType::RBRACE);
        }
        else
        {
            ExprNode *expr = expression();
            Token retToken;
            retToken.type = TokenType::RETURN;
            retToken.lexeme = "return";
            retToken.line = currentToken.line;
            body.push_back(new ReturnStmt(retToken, expr));
        }
        return new LambdaExpr(parameters, body);
    }

    return new VariableExpr(name);
}

ExprNode *Parser::parseGrouping(bool canAssign)
{
    Token leftParen = previousToken;
    // We are at LPAREN. Could be a normal grouping (expr), or an arrow lambda (a,
    // b) =>
    Lexer tempLexer = lexer;
    Token t = currentToken; // the token AFTER LPAREN, since we already advanced
    bool isArrow = false;

    if (t.type != TokenType::RPAREN)
    {
        bool validParams = true;
        while (true)
        {
            if (t.type != TokenType::IDENTIFIER)
            {
                validParams = false;
                break;
            }
            t = tempLexer.nextToken();
            if (t.type == TokenType::COMMA)
            {
                t = tempLexer.nextToken();
            }
            else
            {
                break;
            }
        }
        if (validParams && t.type == TokenType::RPAREN)
        {
            Token afterParen = tempLexer.nextToken();
            if (afterParen.type == TokenType::ARROW)
            {
                isArrow = true;
            }
        }
    }
    else
    {
        Token afterParen = tempLexer.nextToken();
        if (afterParen.type == TokenType::ARROW)
        {
            isArrow = true;
        }
    }

    if (isArrow)
    {
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
        consume(TokenType::RPAREN);
        consume(TokenType::ARROW);

        std::vector<StmtNode *> body;
        if (match(TokenType::LBRACE))
        {
            while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
            {
                body.push_back(dynamic_cast<StmtNode *>(declaration()));
            }
            consume(TokenType::RBRACE);
        }
        else
        {
            ExprNode *expr = expression();
            Token retToken;
            retToken.type = TokenType::RETURN;
            retToken.lexeme = "return";
            retToken.line = currentToken.line;
            body.push_back(new ReturnStmt(retToken, expr));
        }
        return new LambdaExpr(parameters, body);
    }

    ExprNode *expr = expression();
    Token rightParen = currentToken;
    consume(TokenType::RPAREN);
    return new ParenExprNode(leftParen, expr, rightParen);
}

ExprNode *Parser::parseList(bool canAssign)
{
    Token leftBracket = previousToken;
    std::vector<ExprNode *> elements;
    if (currentToken.type != TokenType::RBRACKET)
    {
        do
        {
            elements.push_back(expression());
        } while (match(TokenType::COMMA));
    }
    Token rightBracket = currentToken;
    consume(TokenType::RBRACKET);
    return new ListExpr(leftBracket, elements, rightBracket);
}

ExprNode *Parser::parseDict(bool canAssign)
{
    std::vector<std::pair<ExprNode *, ExprNode *>> elements;
    if (currentToken.type != TokenType::RBRACE)
    {
        do
        {
            ExprNode *key = expression();
            consume(TokenType::COLON);
            ExprNode *value = expression();
            elements.push_back({key, value});
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RBRACE);
    return new DictExpr(elements);
}

ExprNode *Parser::parseFn(bool canAssign)
{
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
    consume(TokenType::RPAREN);
    consume(TokenType::LBRACE);

    std::vector<StmtNode *> body;
    while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE)
    {
        body.push_back(dynamic_cast<StmtNode *>(declaration()));
    }
    consume(TokenType::RBRACE);
    return new LambdaExpr(parameters, body);
}

ExprNode *Parser::parseUnary(bool canAssign)
{
    Token op = previousToken;
    // Right associative, use precedence of UNARY
    ExprNode *right = parsePrecedence(PREC_UNARY);
    return new UnaryExpr(op, right);
}

ExprNode *Parser::parseBinary(ExprNode *left, bool canAssign)
{
    Token op = previousToken;
    ParseRule rule = getRule(op.type);
    // Left associative: use rule.precedence + 1. Right associative:
    // rule.precedence All our binaries (except assignment and maybe ternary) are
    // left associative
    ExprNode *right = parsePrecedence((Precedence)(rule.precedence + 1));
    return new BinaryExpr(left, op, right);
}

ExprNode *Parser::parseCall(ExprNode *left, bool canAssign)
{
    Token paren = previousToken; // LPAREN
    std::vector<ExprNode *> arguments;
    if (currentToken.type != TokenType::RPAREN)
    {
        do
        {
            arguments.push_back(expression());
        } while (match(TokenType::COMMA));
    }
    Token rparen = currentToken;
    consume(TokenType::RPAREN);
    return new CallExpr(left, rparen, arguments);
}

ExprNode *Parser::parseDot(ExprNode *left, bool canAssign)
{
    Token name = currentToken;
    consume(TokenType::IDENTIFIER);
    return new GetExpr(left, name);
}

ExprNode *Parser::parseIndex(ExprNode *left, bool canAssign)
{
    ExprNode *index = expression();
    consume(TokenType::RBRACKET);
    return new IndexGetExpr(left, index);
}

ExprNode *Parser::parseStatic(ExprNode *left, bool canAssign)
{
    if (!dynamic_cast<VariableExpr *>(left))
    {
        throw std::runtime_error("Static access requires a class name");
    }
    Token className = dynamic_cast<VariableExpr *>(left)->name;
    Token member = currentToken;
    consume(TokenType::IDENTIFIER);

    if (match(TokenType::LPAREN))
    {
        std::vector<ExprNode *> arguments;
        if (currentToken.type != TokenType::RPAREN)
        {
            do
            {
                arguments.push_back(expression());
            } while (match(TokenType::COMMA));
        }
        Token paren = currentToken;
        consume(TokenType::RPAREN);
        return new StaticCallExpr(className, member, paren, arguments);
    }
    else
    {
        return new StaticGetExpr(className, member);
    }
}

ExprNode *Parser::parsePostfix(ExprNode *left, bool canAssign)
{
    Token op = previousToken;
    if (VariableExpr *varExpr = dynamic_cast<VariableExpr *>(left))
    {
        return new PostfixExpr(varExpr->name, op);
    }
    else
    {
        throw std::runtime_error("Invalid postfix expression target");
    }
}

ExprNode *Parser::parseTernary(ExprNode *left, bool canAssign)
{
    ExprNode *thenBranch = expression();
    consume(TokenType::COLON);
    // Right associative for ternary
    ExprNode *elseBranch = parsePrecedence(PREC_TERNARY);
    return new TernaryExpr(left, thenBranch, elseBranch);
}

ExprNode *Parser::parseAssignment(ExprNode *left, bool canAssign)
{
    if (!canAssign)
    {
        throw std::runtime_error("Invalid assignment target");
    }
    Token op = previousToken;
    // Right associative
    ExprNode *value = parsePrecedence(PREC_ASSIGNMENT);

    if (op.type == TokenType::ASSIGN)
    {
        if (VariableExpr *varExpr = dynamic_cast<VariableExpr *>(left))
        {
            return new AssignExpr(varExpr->name, value);
        }
        else if (GetExpr *getExpr = dynamic_cast<GetExpr *>(left))
        {
            return new SetExpr(getExpr->object, getExpr->name, value);
        }
        else if (IndexGetExpr *idxExpr = dynamic_cast<IndexGetExpr *>(left))
        {
            return new IndexSetExpr(idxExpr->object, idxExpr->index, value);
        }
        else if (StaticGetExpr *statExpr = dynamic_cast<StaticGetExpr *>(left))
        {
            return new StaticSetExpr(statExpr->className, statExpr->memberName, value);
        }
        throw std::runtime_error("Invalid assignment target");
    }
    else
    {
        if (VariableExpr *varExpr = dynamic_cast<VariableExpr *>(left))
        {
            return new CompoundAssignExpr(varExpr->name, op, value);
        }
        throw std::runtime_error("Invalid compound assignment target");
    }
}
