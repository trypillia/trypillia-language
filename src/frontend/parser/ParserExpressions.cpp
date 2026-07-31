#include "Parser.h"
#include "../ast/AST.h"
#include "../../utils/ErrorHandling.h"
#include <stdexcept>
#include <vector>

ExprNode *Parser::primary() {
    bool isArrow = false;
    if (currentToken.type == TokenType::IDENTIFIER) {
        Lexer tempLexer = lexer;
        Token next = tempLexer.nextToken();
        if (next.type == TokenType::ARROW) {
            isArrow = true;
        }
    } else if (currentToken.type == TokenType::LPAREN) {
        Lexer tempLexer = lexer;
        Token t = tempLexer.nextToken();
        bool validParams = true;
        if (t.type != TokenType::RPAREN) {
            while (true) {
                if (t.type != TokenType::IDENTIFIER) {
                    validParams = false;
                    break;
                }
                t = tempLexer.nextToken();
                if (t.type == TokenType::COMMA) {
                    t = tempLexer.nextToken();
                } else {
                    break;
                }
            }
        }
        if (validParams && t.type == TokenType::RPAREN) {
            Token afterParen = tempLexer.nextToken();
            if (afterParen.type == TokenType::ARROW) {
                isArrow = true;
            }
        }
    }

    if (isArrow) {
        std::vector<Parameter> parameters;
        if (currentToken.type == TokenType::IDENTIFIER) {
            parameters.push_back({currentToken.lexeme, nullptr});
            advance(); // consume ident
        } else {
            consume(TokenType::LPAREN);
            if (currentToken.type != TokenType::RPAREN) {
                do {
                    Token param = currentToken;
                    consume(TokenType::IDENTIFIER);
                    ExprNode *defVal = nullptr;
                    if (match(TokenType::ASSIGN)) {
                        defVal = expression();
                    }
                    parameters.push_back({param.lexeme, defVal});
                } while (match(TokenType::COMMA));
            }
            consume(TokenType::RPAREN);
        }
        consume(TokenType::ARROW);

        std::vector<StmtNode *> body;
        if (currentToken.type == TokenType::LBRACE) {
            consume(TokenType::LBRACE);
            while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE) {
                body.push_back(dynamic_cast<StmtNode *>(declaration()));
            }
            consume(TokenType::RBRACE);
        } else {
            ExprNode *expr = expression();
            Token retToken;
            retToken.type = TokenType::RETURN;
            retToken.lexeme = "return";
            retToken.line = currentToken.line;
            body.push_back(new ReturnStmt(retToken, expr));
        }
        return new LambdaExpr(parameters, body);
    }

    if (currentToken.type == TokenType::STRING) {
        Token literal = currentToken;
        advance();

        std::string str = literal.lexeme;
        // Check for interpolation, but respect escaped braces \{
        bool hasInterpolation = false;
        for (size_t i = 0; i < str.length(); i++) {
            if (str[i] == '{' && (i == 0 || str[i - 1] != '\\')) {
                hasInterpolation = true;
                break;
            }
        }

        if (hasInterpolation) {
            std::vector<ExprNode *> parts;
            std::string currentPart = "";

            for (size_t i = 0; i < str.length(); i++) {
                if (str[i] == '\\' && i + 1 < str.length() && (str[i + 1] == '{' || str[i + 1] == '}')) {
                    currentPart += str[i + 1];
                    i++;
                    continue;
                }

                if (str[i] == '{') {
                    if (!currentPart.empty()) {
                        Token t = literal;
                        t.lexeme = currentPart;
                        parts.push_back(new LiteralExpr(t));
                        currentPart = "";
                    }

                    size_t start = i + 1;
                    int nest = 1;
                    while (i + 1 < str.length() && nest > 0) {
                        i++;
                        if (str[i] == '{' && str[i - 1] != '\\')
                            nest++;
                        if (str[i] == '}' && str[i - 1] != '\\')
                            nest--;
                    }

                    std::string exprStr = str.substr(start, i - start);
                    try {
                        Lexer lex(exprStr);
                        Parser p(lex);
                        ExprNode *e = p.expression();
                        if (e)
                            parts.push_back(e);
                    } catch (...) {
                        Token t = literal;
                        t.lexeme = "{" + exprStr + "}";
                        parts.push_back(new LiteralExpr(t));
                    }
                } else {
                    currentPart += str[i];
                }
            }

            if (!currentPart.empty()) {
                Token t = literal;
                t.lexeme = currentPart;
                parts.push_back(new LiteralExpr(t));
            }

            if (parts.empty())
                return new LiteralExpr(literal);

            ExprNode *result = parts[0];
            for (size_t j = 1; j < parts.size(); j++) {
                Token plusToken;
                plusToken.type = TokenType::PLUS;
                plusToken.lexeme = "+";
                plusToken.line = literal.line;
                result = new BinaryExpr(result, plusToken, parts[j]);
            }
            return result;
        }
        std::string cleanedStr = "";
        for (size_t i = 0; i < str.length(); i++) {
            if (str[i] == '\\' && i + 1 < str.length() && (str[i + 1] == '{' || str[i + 1] == '}')) {
                cleanedStr += str[i + 1];
                i++;
            } else {
                cleanedStr += str[i];
            }
        }
        Token t = literal;
        t.lexeme = cleanedStr;
        return new LiteralExpr(t);
    }

    if (currentToken.type == TokenType::NUMBER) {
        Token literal = currentToken;
        advance();
        return new LiteralExpr(literal);
    }

    if (currentToken.type == TokenType::TRUE || currentToken.type == TokenType::FALSE ||
        currentToken.type == TokenType::NIL) {
        Token literal = currentToken;
        advance();
        return new LiteralExpr(literal);
    }

    if (currentToken.type == TokenType::IDENTIFIER) {
        Token name = currentToken;
        advance();
        return new VariableExpr(name);
    }

    if (currentToken.type == TokenType::THIS) {
        Token keyword = currentToken;
        advance();
        return new ThisExpr(keyword);
    }

    if (currentToken.type == TokenType::SUPER) {
        Token keyword = currentToken;
        advance();
        consume(TokenType::DOT);
        Token method = currentToken;
        consume(TokenType::IDENTIFIER);
        return new SuperExpr(keyword, method);
    }

    if (match(TokenType::LPAREN)) {
        ExprNode *expr = expression();
        consume(TokenType::RPAREN);
        return expr;
    }

    if (match(TokenType::LBRACKET)) {
        std::vector<ExprNode *> elements;

        if (currentToken.type != TokenType::RBRACKET) {
            do {
                elements.push_back(expression());
            } while (match(TokenType::COMMA));
        }

        consume(TokenType::RBRACKET);
        return new ListExpr(elements);
    }

    if (match(TokenType::LBRACE)) {
        std::vector<std::pair<ExprNode *, ExprNode *>> elements;

        if (currentToken.type != TokenType::RBRACE) {
            do {
                ExprNode *key = expression();
                consume(TokenType::COLON);
                ExprNode *value = expression();
                elements.push_back({key, value});
            } while (match(TokenType::COMMA));
        }

        consume(TokenType::RBRACE);
        return new DictExpr(elements);
    }

    if (match(TokenType::FN)) {
        consume(TokenType::LPAREN);
        std::vector<Parameter> parameters;

        if (currentToken.type != TokenType::RPAREN) {
            do {
                Token param = currentToken;
                consume(TokenType::IDENTIFIER);
                ExprNode *defVal = nullptr;
                if (match(TokenType::ASSIGN)) {
                    defVal = expression();
                }
                parameters.push_back({param.lexeme, defVal});
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RPAREN);

        consume(TokenType::LBRACE);
        std::vector<StmtNode *> body;
        while (currentToken.type != TokenType::RBRACE && currentToken.type != TokenType::END_OF_FILE) {
            body.push_back(dynamic_cast<StmtNode *>(declaration()));
        }
        consume(TokenType::RBRACE);

        return new LambdaExpr(parameters, body);
    }

    std::string errMsg = "Unexpected token in expression: " + currentToken.lexeme + " at line " +
                         std::to_string(currentToken.line) + ":" + std::to_string(currentToken.column);
    throw std::runtime_error(errMsg);
}

// Call expressions and member access
ExprNode *Parser::finishCall(ExprNode *callee) {
    std::vector<ExprNode *> arguments;

    if (currentToken.type != TokenType::RPAREN) {
        do {
            arguments.push_back(expression());
        } while (match(TokenType::COMMA));
    }

    Token paren = currentToken;
    consume(TokenType::RPAREN);

    return new CallExpr(callee, paren, arguments);
}

ExprNode *Parser::call() {
    ExprNode *expr = primary();

    while (true) {
        if (match(TokenType::LPAREN)) {
            expr = finishCall(expr);
        } else if (match(TokenType::DOT)) {
            Token name = currentToken;
            consume(TokenType::IDENTIFIER);
            expr = new GetExpr(expr, name);
        } else if (match(TokenType::LBRACKET)) {
            ExprNode *index = expression();
            consume(TokenType::RBRACKET);
            expr = new IndexGetExpr(expr, index);
        } else if (match(TokenType::COLON_COLON)) {
            if (!dynamic_cast<VariableExpr *>(expr)) {
                throw std::runtime_error("Static access requires a class name");
            }
            Token className = dynamic_cast<VariableExpr *>(expr)->name;
            Token member = currentToken;
            consume(TokenType::IDENTIFIER);
            if (match(TokenType::LPAREN)) {
                std::vector<ExprNode *> arguments;
                if (currentToken.type != TokenType::RPAREN) {
                    do {
                        arguments.push_back(expression());
                    } while (match(TokenType::COMMA));
                }
                Token paren = currentToken;
                consume(TokenType::RPAREN);
                return new StaticCallExpr(className, member, paren, arguments);
            } else {
                return new StaticGetExpr(className, member);
            }
        } else if (currentToken.type == TokenType::PLUS_PLUS || currentToken.type == TokenType::MINUS_MINUS) {
            Token op = currentToken;
            advance();
            if (VariableExpr *varExpr = dynamic_cast<VariableExpr *>(expr)) {
                expr = new PostfixExpr(varExpr->name, op);
            } else {
                throw std::runtime_error("Invalid postfix expression target");
            }
        } else {
            break;
        }
    }

    return expr;
}

// Unary operators (!, -, ~)
ExprNode *Parser::unary() {
    if (currentToken.type == TokenType::BANG || currentToken.type == TokenType::MINUS ||
        currentToken.type == TokenType::PLUS_PLUS || currentToken.type == TokenType::MINUS_MINUS ||
        currentToken.type == TokenType::BITWISE_NOT) {
        Token op = currentToken;
        advance();
        ExprNode *right = unary();
        return new UnaryExpr(op, right);
    }

    return call();
}

// Multiplication and division
ExprNode *Parser::factor() {
    ExprNode *expr = unary();

    while (currentToken.type == TokenType::STAR || currentToken.type == TokenType::SLASH ||
           currentToken.type == TokenType::PERCENT) {
        Token op = currentToken;
        advance();
        ExprNode *right = unary();
        expr = new BinaryExpr(expr, op, right);
    }

    return expr;
}

// Addition and subtraction
ExprNode *Parser::term() {
    ExprNode *expr = factor();

    while (currentToken.type == TokenType::PLUS || currentToken.type == TokenType::MINUS) {
        Token op = currentToken;
        advance();
        ExprNode *right = factor();
        expr = new BinaryExpr(expr, op, right);
    }

    return expr;
}

// Comparison operators (<, <=, >, >=)
ExprNode *Parser::comparison() {
    ExprNode *expr = bitwiseOrExpr();

    while (currentToken.type == TokenType::LESS || currentToken.type == TokenType::LESS_EQUAL ||
           currentToken.type == TokenType::GREATER || currentToken.type == TokenType::GREATER_EQUAL) {
        Token op = currentToken;
        advance();
        ExprNode *right = bitwiseOrExpr();
        expr = new BinaryExpr(expr, op, right);
    }

    return expr;
}

ExprNode *Parser::bitwiseOrExpr() {
    ExprNode *expr = bitwiseXorExpr();

    while (currentToken.type == TokenType::BITWISE_OR) {
        Token op = currentToken;
        advance();
        ExprNode *right = bitwiseXorExpr();
        expr = new BinaryExpr(expr, op, right);
    }

    return expr;
}

ExprNode *Parser::bitwiseXorExpr() {
    ExprNode *expr = bitwiseAndExpr();

    while (currentToken.type == TokenType::BITWISE_XOR) {
        Token op = currentToken;
        advance();
        ExprNode *right = bitwiseAndExpr();
        expr = new BinaryExpr(expr, op, right);
    }

    return expr;
}

ExprNode *Parser::bitwiseAndExpr() {
    ExprNode *expr = shiftExpr();

    while (currentToken.type == TokenType::BITWISE_AND) {
        Token op = currentToken;
        advance();
        ExprNode *right = shiftExpr();
        expr = new BinaryExpr(expr, op, right);
    }

    return expr;
}

ExprNode *Parser::shiftExpr() {
    ExprNode *expr = term();

    while (currentToken.type == TokenType::SHIFT_LEFT || currentToken.type == TokenType::SHIFT_RIGHT) {
        Token op = currentToken;
        advance();
        ExprNode *right = term();
        expr = new BinaryExpr(expr, op, right);
    }

    return expr;
}

// Ternary conditional (?:)
ExprNode *Parser::ternary() {
    ExprNode *expr = orExpr();

    if (currentToken.type == TokenType::QUESTION) {
        advance();
        ExprNode *thenBranch = expression();
        consume(TokenType::COLON);
        ExprNode *elseBranch = ternary();
        expr = new TernaryExpr(expr, thenBranch, elseBranch);
    }

    return expr;
}

// Logical OR
ExprNode *Parser::orExpr() {
    ExprNode *expr = andExpr();

    while (currentToken.type == TokenType::OR) {
        Token op = currentToken;
        advance();
        ExprNode *right = andExpr();
        expr = new BinaryExpr(expr, op, right);
    }

    return expr;
}

// Logical AND
ExprNode *Parser::andExpr() {
    ExprNode *expr = equality();

    while (currentToken.type == TokenType::AND) {
        Token op = currentToken;
        advance();
        ExprNode *right = equality();
        expr = new BinaryExpr(expr, op, right);
    }

    return expr;
}

// Equality operators (==, !=)
ExprNode *Parser::equality() {
    ExprNode *expr = comparison();

    while (currentToken.type == TokenType::BANG_EQUAL || currentToken.type == TokenType::EQUAL_EQUAL) {
        Token op = currentToken;
        advance();
        ExprNode *right = comparison();
        expr = new BinaryExpr(expr, op, right);
    }

    return expr;
}

// Assignment expressions
ExprNode *Parser::assignment() {
    ExprNode *expr = ternary();

    if (currentToken.type == TokenType::ASSIGN) {
        Token equals = currentToken;
        advance();
        ExprNode *value = assignment();

        if (VariableExpr *varExpr = dynamic_cast<VariableExpr *>(expr)) {
            Token name = varExpr->name;
            return new AssignExpr(name, value);
        }

        if (GetExpr *getExpr = dynamic_cast<GetExpr *>(expr)) {
            return new SetExpr(getExpr->object, getExpr->name, value);
        }

        if (IndexGetExpr *indexGetExpr = dynamic_cast<IndexGetExpr *>(expr)) {
            return new IndexSetExpr(indexGetExpr->object, indexGetExpr->index, value);
        }

        if (StaticGetExpr *staticGetExpr = dynamic_cast<StaticGetExpr *>(expr)) {
            return new StaticSetExpr(staticGetExpr->className, staticGetExpr->memberName, value);
        }

        throw std::runtime_error("Invalid assignment target");
    }

    if (currentToken.type == TokenType::PLUS_EQUAL || currentToken.type == TokenType::MINUS_EQUAL ||
        currentToken.type == TokenType::STAR_EQUAL || currentToken.type == TokenType::SLASH_EQUAL) {
        Token op = currentToken;
        advance();
        ExprNode *value = assignment();

        if (VariableExpr *varExpr = dynamic_cast<VariableExpr *>(expr)) {
            Token name = varExpr->name;
            return new CompoundAssignExpr(name, op, value);
        }

        throw std::runtime_error("Invalid compound assignment target");
    }

    return expr;
}

// Main expression parser
ExprNode *Parser::expression() {
    return assignment();
}

// Statement parsers
