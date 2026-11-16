#include "EditorSymbols.h"

#include <ostream>
#include <utility>

std::vector<Token> EditorSymbols::ReadPrefix(const std::string& prefix) {
    Lexer lexer(prefix);
    std::vector<Token> tokens;
    try {
        for (Token token = lexer.NextToken(); token.type != TokenType::EndOfFile; token = lexer.NextToken()) {
            tokens.push_back(std::move(token));
        }
    } catch (const std::exception&) {
        // The last token may be unfinished while the user is typing.
    }
    return tokens;
}

std::optional<EditorSymbols::Declaration> EditorSymbols::ReadDeclaration(
    const std::vector<Token>& tokens, size_t start
) {
    if (tokens[start].type != TokenType::TypeName) return std::nullopt;
    std::string type = tokens[start].value;
    size_t next = start + 1;
    while (next < tokens.size() && tokens[next].type == TokenType::Operator && tokens[next].value == "*") {
        type += "*";
        ++next;
    }
    if (next < tokens.size() && tokens[next].value == "mutable") {
        type += " mutable";
        ++next;
    }
    if (next < tokens.size() && tokens[next].type == TokenType::Operator && tokens[next].value == "&") {
        type += "&";
        ++next;
        if (next < tokens.size() && tokens[next].type == TokenType::At) next += 2;
    }
    if (next >= tokens.size() || tokens[next].type != TokenType::Identifier) return std::nullopt;
    return Declaration{std::move(type), tokens[next].value, next};
}

void EditorSymbols::Write(const std::string& source, size_t limit, std::ostream& output) const {
    const auto tokens = ReadPrefix(source.substr(0, limit));
    struct Symbol {
        const char* kind;
        std::string name;
        std::string detail;
    };
    std::vector<std::vector<Symbol>> scopes(1);
    std::vector<Symbol> parameters;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::LeftBrace) {
            scopes.push_back(std::move(parameters));
            parameters.clear();
        } else if (tokens[i].type == TokenType::RightBrace) {
            if (scopes.size() > 1) scopes.pop_back();
            parameters.clear();
        } else if (auto declaration = ReadDeclaration(tokens, i)) {
            size_t next = declaration->nameIndex + 1;
            if (next >= tokens.size() || tokens[next].type != TokenType::LeftParen) {
                scopes.back().push_back({"variable", declaration->name, declaration->type});
                i = declaration->nameIndex;
                continue;
            }

            std::string detail = declaration->type + " " + declaration->name + "(";
            std::vector<Symbol> arguments;
            ++next;
            while (next < tokens.size()) {
                auto parameter = ReadDeclaration(tokens, next);
                if (!parameter) break;
                if (!arguments.empty()) detail += ", ";
                detail += parameter->type + " " + parameter->name;
                arguments.push_back({"variable", std::move(parameter->name), std::move(parameter->type)});
                next = parameter->nameIndex + 1;
                if (next >= tokens.size() || tokens[next].type != TokenType::Comma) break;
                ++next;
            }
            detail += ")";
            scopes.back().push_back({"function", std::move(declaration->name), std::move(detail)});
            if (next < tokens.size() && tokens[next].type == TokenType::RightParen) {
                parameters = std::move(arguments);
                i = next;
            }
        }
    }

    std::unordered_set<std::string> seen;
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
        for (auto symbol = scope->rbegin(); symbol != scope->rend(); ++symbol) {
            if (seen.insert(symbol->name).second) {
                output << "C4R_SYMBOL\t" << symbol->kind << '\t' << symbol->name << '\t' << symbol->detail << '\n';
            }
        }
    }
    for (const auto& name : Lexer::BuiltinTypes()) output << "C4R_SYMBOL\ttype\t" << name << '\t' << name << '\n';
    for (const auto& name : Lexer::Keywords()) output << "C4R_SYMBOL\tkeyword\t" << name << '\t' << name << '\n';
    for (const auto* name : {"true", "false"}) output << "C4R_SYMBOL\tkeyword\t" << name << "\tbool\n";
}
