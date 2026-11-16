#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>

enum class TokenType {
    EndOfStatement,
    LeftParen,
    RightParen,
    String,
    EndOfFile,
    Unknown,

    TypeName,
    Identifier,
    LeftBrace,
    RightBrace,

    Number,
    Assign,
    Reserved,
    FormatString,

    Comma,
    Dot,
    Colon,
    At,
    Operator,
};

struct Token {
    TokenType type;
    std::string value;
    size_t offset = 0;
};

class Lexer {
public:
    explicit Lexer(const std::string& source, const std::unordered_map<std::string, std::string>* userTypes = nullptr)
        : source(source), userTypes(userTypes) {}

    Token NextToken();
    static bool IsIdentifierStart(char c);
    static bool IsIdentifierPart(char c);
    static const std::unordered_set<std::string>& BuiltinTypes();
    static const std::unordered_set<std::string>& Keywords();

private:
    const std::string& source;
    const std::unordered_map<std::string, std::string>* userTypes;
    size_t position = 0;
    size_t tokenStart = 0;

    Token ReadToken();
    Token ReadString(TokenType type);
    Token ReadNumber();
    Token ReadOperator(char first);
    Token ReadWord();
};
