#include "Lexer.h"
#include <cctype>
#include <stdexcept>
#include <unordered_map>

// Define keywords
static std::unordered_map<std::string, TokenType> keywords = {{"class", TokenType::CLASS},
                                                              {"fn", TokenType::FN},
                                                              {"let", TokenType::LET},
                                                              {"const", TokenType::CONST},
                                                              {"virtual", TokenType::VIRTUAL},
                                                              {"override", TokenType::OVERRIDE},
                                                              {"public", TokenType::PUBLIC},
                                                              {"private", TokenType::PRIVATE},
                                                              {"protected", TokenType::PROTECTED},
                                                              {"if", TokenType::IF},
                                                              {"else", TokenType::ELSE},
                                                              {"while", TokenType::WHILE},
                                                              {"for", TokenType::FOR},
                                                              {"in", TokenType::IN},
                                                              {"return", TokenType::RETURN},
                                                              {"break", TokenType::BREAK},
                                                              {"continue", TokenType::CONTINUE},
                                                              {"do", TokenType::DO},
                                                              {"abstract", TokenType::ABSTRACT},
                                                              {"interface", TokenType::INTERFACE},
                                                              {"implements", TokenType::IMPLEMENTS},
                                                              {"trait", TokenType::TRAIT},
                                                              {"load", TokenType::LOAD},
                                                              {"using", TokenType::USING},
                                                              {"namespace", TokenType::NAMESPACE},
                                                              {"use", TokenType::USE},
                                                              {"destroy", TokenType::DESTROY},
                                                              {"switch", TokenType::SWITCH},
                                                              {"case", TokenType::CASE},
                                                              {"default", TokenType::DEFAULT},
                                                              {"true", TokenType::TRUE},
                                                              {"false", TokenType::FALSE},
                                                              {"nil", TokenType::NIL},
                                                              {"this", TokenType::THIS},
                                                              {"super", TokenType::SUPER},
                                                              {"static", TokenType::STATIC}};

Lexer::Lexer(const std::string &source) : source(source), currentIndex(0), line(1), column(1) {
}

Token Lexer::nextToken() {
    skipWhitespace();

    int startColumn = column;

    if (isAtEnd()) {
        return {TokenType::END_OF_FILE, "", line, column};
    }

    char c = advance();

    if (std::isalpha(c) || c == '_') {
        return identifier(startColumn);
    }

    if (std::isdigit(c)) {
        return number(startColumn);
    }

    switch (c) {
    case '(':
        return {TokenType::LPAREN, "(", line, startColumn};
    case ')':
        return {TokenType::RPAREN, ")", line, startColumn};
    case '{':
        return {TokenType::LBRACE, "{", line, startColumn};
    case '}':
        return {TokenType::RBRACE, "}", line, startColumn};
    case '[':
        return {TokenType::LBRACKET, "[", line, startColumn};
    case ']':
        return {TokenType::RBRACKET, "]", line, startColumn};
    case ',':
        return {TokenType::COMMA, ",", line, startColumn};
    case '.':
        return {TokenType::DOT, ".", line, startColumn};
    case ';':
        return {TokenType::SEMICOLON, ";", line, startColumn};
    case '+':
        if (match('+')) {
            return {TokenType::PLUS_PLUS, "++", line, startColumn};
        } else if (match('=')) {
            return {TokenType::PLUS_EQUAL, "+=", line, startColumn};
        } else {
            return {TokenType::PLUS, "+", line, startColumn};
        }
    case '-':
        if (match('-')) {
            return {TokenType::MINUS_MINUS, "--", line, startColumn};
        } else if (match('=')) {
            return {TokenType::MINUS_EQUAL, "-=", line, startColumn};
        } else {
            return {TokenType::MINUS, "-", line, startColumn};
        }
    case '*':
        if (match('=')) {
            return {TokenType::STAR_EQUAL, "*=", line, startColumn};
        } else {
            return {TokenType::STAR, "*", line, startColumn};
        }
    case '/':
        if (match('=')) {
            return {TokenType::SLASH_EQUAL, "/=", line, startColumn};
        } else if (match('/')) {
            // Comment extends to the end of the line
            while (peek() != '\n' && !isAtEnd()) {
                advance();
            }
            return nextToken();
        } else {
            return {TokenType::SLASH, "/", line, startColumn};
        }
    case '%':
        return {TokenType::PERCENT, "%", line, startColumn};
    case '=':
        if (match('=')) {
            return {TokenType::EQUAL_EQUAL, "==", line, startColumn};
        } else if (match('>')) {
            return {TokenType::ARROW, "=>", line, startColumn};
        } else {
            return {TokenType::ASSIGN, "=", line, startColumn};
        }
    case '!':
        if (match('=')) {
            return {TokenType::BANG_EQUAL, "!=", line, startColumn};
        } else {
            return {TokenType::BANG, "!", line, startColumn};
        }
    case '<':
        if (match('=')) {
            return {TokenType::LESS_EQUAL, "<=", line, startColumn};
        } else if (match('<')) {
            return {TokenType::SHIFT_LEFT, "<<", line, startColumn};
        } else {
            return {TokenType::LESS, "<", line, startColumn};
        }
    case '>':
        if (match('=')) {
            return {TokenType::GREATER_EQUAL, ">=", line, startColumn};
        } else if (match('>')) {
            return {TokenType::SHIFT_RIGHT, ">>", line, startColumn};
        } else {
            return {TokenType::GREATER, ">", line, startColumn};
        }
    case '"':
        return string(startColumn);
    case '?':
        return {TokenType::QUESTION, "?", line, startColumn};
    case ':':
        if (match(':')) {
            return {TokenType::COLON_COLON, "::", line, startColumn};
        }
        return {TokenType::COLON, ":", line, startColumn};
    case '&':
        if (match('&')) {
            return {TokenType::AND, "&&", line, startColumn};
        } else {
            return {TokenType::BITWISE_AND, "&", line, startColumn};
        }
    case '|':
        if (match('|')) {
            return {TokenType::OR, "||", line, startColumn};
        } else {
            return {TokenType::BITWISE_OR, "|", line, startColumn};
        }
    case '^':
        return {TokenType::BITWISE_XOR, "^", line, startColumn};
    case '~':
        return {TokenType::BITWISE_NOT, "~", line, startColumn};
    }

    return {TokenType::UNKNOWN, std::string(1, c), line, startColumn};
}

void Lexer::skipWhitespace() {
    while (true) {
        char c = peek();
        switch (c) {
        case ' ':
        case '\r':
        case '\t':
            advance();
            break;
        case '\n':
            line++;
            column = 1;
            currentIndex++;
            break;
        default:
            return;
        }
    }
}

char Lexer::advance() {
    char c = source[currentIndex++];
    column++;
    return c;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source[currentIndex] != expected) {
        return false;
    }

    currentIndex++;
    column++;
    return true;
}

bool Lexer::isAtEnd() {
    return currentIndex >= source.length();
}

char Lexer::peek() {
    if (isAtEnd())
        return '\0';
    return source[currentIndex];
}

char Lexer::peekNext() {
    if (currentIndex + 1 >= source.length())
        return '\0';
    return source[currentIndex + 1];
}

Token Lexer::identifier(int startColumn) {
    size_t start = currentIndex - 1;

    while (std::isalnum(peek()) || peek() == '_') {
        advance();
    }

    std::string text = source.substr(start, currentIndex - start);

    auto it = keywords.find(text);
    if (it != keywords.end()) {
        return {it->second, text, line, startColumn};
    }

    return {TokenType::IDENTIFIER, text, line, startColumn};
}

Token Lexer::number(int startColumn) {
    size_t start = currentIndex - 1;

    while (std::isdigit(peek())) {
        advance();
    }

    // Look for a decimal part
    if (peek() == '.' && std::isdigit(peekNext())) {
        // Consume the '.'
        advance();

        while (std::isdigit(peek())) {
            advance();
        }
    }

    std::string text = source.substr(start, currentIndex - start);
    return {TokenType::NUMBER, text, line, startColumn};
}

Token Lexer::string(int startColumn) {
    std::string value = "";

    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') {
            line++;
            column = 1;
        }
        char c = peek();
        advance();

        if (c == '\\' && !isAtEnd()) {
            char escape = peek();
            advance();
            switch (escape) {
            case 'n':
                value += '\n';
                break;
            case 'r':
                value += '\r';
                break;
            case 't':
                value += '\t';
                break;
            case '\\':
                value += '\\';
                break;
            case '"':
                value += '"';
                break;
            case 'e':
                value += '\x1b';
                break; // \e for Escape is useful
            case 'b':
                value += '\b';
                break;
            case 'f':
                value += '\f';
                break;
            case 'v':
                value += '\v';
                break;
            case '0':
                value += '\0';
                break;
            case 'u': {
                if (currentIndex + 3 < source.length()) {
                    std::string hexStr = source.substr(currentIndex, 4);
                    bool isHex = true;
                    for (char h : hexStr) {
                        if (!std::isxdigit(h))
                            isHex = false;
                    }
                    if (isHex) {
                        int codePoint = std::stoi(hexStr, nullptr, 16);
                        currentIndex += 4;
                        column += 4;

                        if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                            if (currentIndex + 5 < source.length() && source[currentIndex] == '\\' &&
                                source[currentIndex + 1] == 'u') {
                                std::string hexStr2 = source.substr(currentIndex + 2, 4);
                                bool isHex2 = true;
                                for (char h : hexStr2) {
                                    if (!std::isxdigit(h))
                                        isHex2 = false;
                                }
                                if (isHex2) {
                                    int lowSurrogate = std::stoi(hexStr2, nullptr, 16);
                                    if (lowSurrogate >= 0xDC00 && lowSurrogate <= 0xDFFF) {
                                        codePoint = 0x10000 + (((codePoint - 0xD800) << 10) | (lowSurrogate - 0xDC00));
                                        currentIndex += 6;
                                        column += 6;
                                    }
                                }
                            }
                        }

                        if (codePoint <= 0x7F) {
                            value += static_cast<char>(codePoint);
                        } else if (codePoint <= 0x7FF) {
                            value += static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F));
                            value += static_cast<char>(0x80 | (codePoint & 0x3F));
                        } else if (codePoint <= 0xFFFF) {
                            value += static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F));
                            value += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                            value += static_cast<char>(0x80 | (codePoint & 0x3F));
                        } else if (codePoint <= 0x10FFFF) {
                            value += static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07));
                            value += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                            value += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                            value += static_cast<char>(0x80 | (codePoint & 0x3F));
                        }
                        break;
                    }
                }
                value += "\\u";
                break;
            }
            case 'x': {
                if (currentIndex + 1 < source.length()) {
                    std::string hexStr = source.substr(currentIndex, 2);
                    bool isHex = true;
                    for (char h : hexStr) {
                        if (!std::isxdigit(h))
                            isHex = false;
                    }
                    if (isHex) {
                        char hexChar = static_cast<char>(std::stoi(hexStr, nullptr, 16));
                        value += hexChar;
                        currentIndex += 2;
                        column += 2;
                        break;
                    }
                }
                value += "\\x";
                break;
            }
            default:
                value += '\\';
                value += escape;
                break;
            }
        } else {
            value += c;
        }
    }

    if (isAtEnd()) {

        return {TokenType::UNKNOWN, "", line, startColumn};
    }

    // Consume the closing "
    advance();

    return {TokenType::STRING, value, line, startColumn};
}

Token Lexer::scanToken() {
    skipWhitespace();

    int startColumn = column;

    if (isAtEnd()) {
        return {TokenType::END_OF_FILE, "", line, column};
    }

    char c = advance();

    if (std::isalpha(c)) {
        return identifier(startColumn);
    }

    if (std::isdigit(c)) {
        return number(startColumn);
    }

    // Other token types would be handled here

    return {TokenType::UNKNOWN, std::string(1, c), line};
}

std::string tokenTypeToString(TokenType type) {
    switch (type) {
    case TokenType::LPAREN:
        return "LPAREN";
    case TokenType::RPAREN:
        return "RPAREN";
    case TokenType::LBRACE:
        return "LBRACE";
    case TokenType::RBRACE:
        return "RBRACE";
    case TokenType::LBRACKET:
        return "LBRACKET";
    case TokenType::RBRACKET:
        return "RBRACKET";
    case TokenType::COMMA:
        return "COMMA";
    case TokenType::DOT:
        return "DOT";
    case TokenType::SEMICOLON:
        return "SEMICOLON";
    case TokenType::QUESTION:
        return "QUESTION";
    case TokenType::COLON:
        return "COLON";
    case TokenType::PLUS:
        return "PLUS";
    case TokenType::MINUS:
        return "MINUS";
    case TokenType::STAR:
        return "STAR";
    case TokenType::SLASH:
        return "SLASH";
    case TokenType::PERCENT:
        return "PERCENT";
    case TokenType::PLUS_PLUS:
        return "PLUS_PLUS";
    case TokenType::MINUS_MINUS:
        return "MINUS_MINUS";
    case TokenType::PLUS_EQUAL:
        return "PLUS_EQUAL";
    case TokenType::MINUS_EQUAL:
        return "MINUS_EQUAL";
    case TokenType::STAR_EQUAL:
        return "STAR_EQUAL";
    case TokenType::SLASH_EQUAL:
        return "SLASH_EQUAL";
    case TokenType::ASSIGN:
        return "ASSIGN";
    case TokenType::EQUAL_EQUAL:
        return "EQUAL_EQUAL";
    case TokenType::ARROW:
        return "ARROW";
    case TokenType::BANG:
        return "BANG";
    case TokenType::BANG_EQUAL:
        return "BANG_EQUAL";
    case TokenType::LESS:
        return "LESS";
    case TokenType::LESS_EQUAL:
        return "LESS_EQUAL";
    case TokenType::GREATER:
        return "GREATER";
    case TokenType::GREATER_EQUAL:
        return "GREATER_EQUAL";
    case TokenType::COLON_COLON:
        return "COLON_COLON";
    case TokenType::AND:
        return "AND";
    case TokenType::OR:
        return "OR";
    case TokenType::BITWISE_AND:
        return "BITWISE_AND";
    case TokenType::BITWISE_OR:
        return "BITWISE_OR";
    case TokenType::BITWISE_XOR:
        return "BITWISE_XOR";
    case TokenType::BITWISE_NOT:
        return "BITWISE_NOT";
    case TokenType::SHIFT_LEFT:
        return "SHIFT_LEFT";
    case TokenType::SHIFT_RIGHT:
        return "SHIFT_RIGHT";
    case TokenType::IDENTIFIER:
        return "IDENTIFIER";
    case TokenType::NUMBER:
        return "NUMBER";
    case TokenType::STRING:
        return "STRING";
    case TokenType::CLASS:
        return "CLASS";
    case TokenType::FN:
        return "FN";
    case TokenType::LET:
        return "LET";
    case TokenType::CONST:
        return "CONST";
    case TokenType::VIRTUAL:
        return "VIRTUAL";
    case TokenType::OVERRIDE:
        return "OVERRIDE";
    case TokenType::PUBLIC:
        return "PUBLIC";
    case TokenType::PRIVATE:
        return "PRIVATE";
    case TokenType::PROTECTED:
        return "PROTECTED";
    case TokenType::STATIC:
        return "STATIC";
    case TokenType::IF:
        return "IF";
    case TokenType::ELSE:
        return "ELSE";
    case TokenType::WHILE:
        return "WHILE";
    case TokenType::DO:
        return "DO";
    case TokenType::FOR:
        return "FOR";
    case TokenType::IN:
        return "IN";
    case TokenType::RETURN:
        return "RETURN";
    case TokenType::BREAK:
        return "BREAK";
    case TokenType::CONTINUE:
        return "CONTINUE";
    case TokenType::SWITCH:
        return "SWITCH";
    case TokenType::CASE:
        return "CASE";
    case TokenType::DEFAULT:
        return "DEFAULT";
    case TokenType::ABSTRACT:
        return "ABSTRACT";
    case TokenType::INTERFACE:
        return "INTERFACE";
    case TokenType::TRAIT:
        return "TRAIT";
    case TokenType::IMPLEMENTS:
        return "IMPLEMENTS";
    case TokenType::LOAD:
        return "LOAD";
    case TokenType::USING:
        return "USING";
    case TokenType::NAMESPACE:
        return "NAMESPACE";
    case TokenType::USE:
        return "USE";
    case TokenType::DESTROY:
        return "DESTROY";
    case TokenType::TRUE:
        return "TRUE";
    case TokenType::FALSE:
        return "FALSE";
    case TokenType::NIL:
        return "NIL";
    case TokenType::THIS:
        return "THIS";
    case TokenType::SUPER:
        return "SUPER";
    case TokenType::END_OF_FILE:
        return "END_OF_FILE";
    case TokenType::UNKNOWN:
        return "UNKNOWN";
    default:
        return "UNKNOWN";
    }
}
