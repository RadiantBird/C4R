#include "Lexer.h"

#include <cctype>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
bool IsDigit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool IsWhitespace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}
}

const std::unordered_set<std::string>& Lexer::BuiltinTypes() {
    static const std::unordered_set<std::string> types = {"int32", "int", "bool", "float", "void"};
    return types;
}

const std::unordered_set<std::string>& Lexer::Keywords() {
    static const std::unordered_set<std::string> words = {
        "if", "else", "for", "while", "do", "return", "capture", "break", "continue",
        "const", "mutable", "struct", "class", "public", "private"
    };
    return words;
}

bool Lexer::IsIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool Lexer::IsIdentifierPart(char c) {
    return IsIdentifierStart(c) || IsDigit(c);
}

Token Lexer::ReadString(TokenType type) {
    std::string value;
    while (position < source.size() && source[position] != '"') {
        const char c = source[position++];
        if (c != '\\' || position == source.size()) {
            value += c;
            continue;
        }

        switch (source[position++]) {
        case 'n':  value += '\n'; break;
        case 't':  value += '\t'; break;
        case 'r':  value += '\r'; break;
        case '\\': value += '\\'; break;
        case '"':  value += '"'; break;
        default: throw std::runtime_error("Unknown escape sequence");
        }
    }

    if (position == source.size()) throw std::runtime_error("Unterminated string literal");
    ++position;
    return {type, std::move(value)};
}

Token Lexer::ReadNumber() {
    while (position < source.size() && IsDigit(source[position])) ++position;
    if (position + 1 < source.size() && source[position] == '.' && IsDigit(source[position + 1])) {
        ++position;
        while (position < source.size() && IsDigit(source[position])) ++position;
    }
    return {TokenType::Number, source.substr(tokenStart, position - tokenStart)};
}

Token Lexer::ReadOperator(char first) {
    static constexpr std::string_view operators[] = {
        "!&&", "!||", "<<=", ">>=", "++", "--", "==", "!=", "<=", ">=", "<<", ">>",
        "&&", "||", "^^", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^="
    };
    const auto tail = std::string_view(source).substr(position);
    for (const auto candidate : operators) {
        if (candidate.front() == first && tail.starts_with(candidate.substr(1))) {
            position += candidate.size() - 1;
            return {TokenType::Operator, std::string(candidate)};
        }
    }
    return {first == '=' ? TokenType::Assign : TokenType::Operator, std::string(1, first)};
}

Token Lexer::ReadWord() {
    while (position < source.size() && IsIdentifierPart(source[position])) ++position;
    auto word = source.substr(tokenStart, position - tokenStart);
    TokenType type = TokenType::Identifier;
    if (Keywords().contains(word)) type = TokenType::Reserved;
    else if (BuiltinTypes().contains(word) || (userTypes && userTypes->contains(word))) type = TokenType::TypeName;
    return {type, std::move(word)};
}

Token Lexer::NextToken() {
    Token token = ReadToken();
    token.offset = tokenStart;
    return token;
}

Token Lexer::ReadToken() {
    while (position < source.size()) {
        const char c = source[position++];
        if (IsWhitespace(c)) continue;
        tokenStart = position - 1;

        if (c == '/' && position < source.size() && source[position] == '/') {
            while (position < source.size() && source[position] != '\n') ++position;
            continue;
        }
        if (IsDigit(c)) return ReadNumber();
        if (std::string_view("+-*/%`!~<>=&|^").find(c) != std::string_view::npos) return ReadOperator(c);
        if (c == 'f' && position < source.size() && source[position] == '"') {
            ++position;
            return ReadString(TokenType::FormatString);
        }
        if (IsIdentifierStart(c)) return ReadWord();

        switch (c) {
        case ';': return {TokenType::EndOfStatement, ";"};
        case ',': return {TokenType::Comma, ","};
        case '.': return {TokenType::Dot, "."};
        case ':': return {TokenType::Colon, ":"};
        case '@': return {TokenType::At, "@"};
        case '(': return {TokenType::LeftParen, "("};
        case ')': return {TokenType::RightParen, ")"};
        case '{': return {TokenType::LeftBrace, "{"};
        case '}': return {TokenType::RightBrace, "}"};
        case '"': return ReadString(TokenType::String);
        default:
            while (position < source.size() && !IsWhitespace(source[position]) && source[position] != ';') ++position;
            return {TokenType::Unknown, source.substr(tokenStart, position - tokenStart)};
        }
    }
    tokenStart = position;
    return {TokenType::EndOfFile, ""};
}
