#ifndef LEXER_H
#define LEXER_H

#include <string>
#include <vector>

enum class TokenType {
    // Single-character tokens
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    LBRACKET,
    RBRACKET,
    COMMA,
    DOT,
    SEMICOLON,
    QUESTION,
    COLON,

    // One or two character tokens
    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,
    PLUS_PLUS,
    MINUS_MINUS,
    PLUS_EQUAL,
    MINUS_EQUAL,
    STAR_EQUAL,
    SLASH_EQUAL,
    ASSIGN,
    EQUAL_EQUAL,
    ARROW,
    BANG,
    BANG_EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL,
    COLON_COLON,

    // Logical operators
    AND,
    OR,

    // Bitwise operators
    BITWISE_AND,
    BITWISE_OR,
    BITWISE_XOR,
    BITWISE_NOT,
    SHIFT_LEFT,
    SHIFT_RIGHT,

    // Literals
    IDENTIFIER,
    NUMBER,
    STRING,

    // Keywords
    CLASS,
    FN,
    LET,
    CONST,
    VIRTUAL,
    OVERRIDE,
    PUBLIC,
    PRIVATE,
    PROTECTED,
    STATIC,
    IF,
    ELSE,
    WHILE,
    DO,
    FOR,
    IN,
    RETURN,
    BREAK,
    CONTINUE,
    SWITCH,
    CASE,
    DEFAULT,
    ABSTRACT,
    INTERFACE,
    TRAIT,
    IMPLEMENTS,
    LOAD,
    DESTROY,
    USING,
    TRUE,
    FALSE,
    NIL,
    THIS,
    SUPER,
    NAMESPACE,
    USE,

    // Special
    END_OF_FILE,
    UNKNOWN
};

std::string tokenTypeToString(TokenType type);

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;
};

class Lexer {
  public:
    Lexer(const std::string &source);
    Token nextToken();

  private:
    std::string source;
    size_t currentIndex;
    int line;
    int column;

    char advance();
    bool match(char expected);
    bool isAtEnd();
    char peek();
    char peekNext();
    void skipWhitespace();

    Token identifier(int startColumn);
    Token number(int startColumn);
    Token string(int startColumn);
    Token scanToken();
};

#endif // LEXER_H
