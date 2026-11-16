#include "Parser.h"

#include <format>
#include <stdexcept>
#include <utility>

void Parser::Advance() {
    try {
        currentToken = lexer.NextToken();
    } catch (const std::exception& error) {
        throw std::runtime_error(std::format("Lexer Error: {}\n", error.what()));
    }
}

Token Parser::Consume(TokenType type) {
    if (currentToken.type != type) throw std::runtime_error("Unexpected token");
    Token token = std::move(currentToken);
    Advance();
    return token;
}

Parser::Parser(Lexer lexer) : lexer(std::move(lexer)) {
    Advance();
}

Program Parser::ParseProgram() {
    Program program;
    while (currentToken.type != TokenType::EndOfFile) {
        if (currentToken.value == "struct" || currentToken.value == "class") {
            program.children.push_back(ParseTypeDeclaration());
        } else if (currentToken.type == TokenType::TypeName) {
            auto lookahead = lexer;
            const auto name = ReadDeclaratorName(lookahead, lookahead.NextToken(), true);
            if (name.type != TokenType::Identifier) throw std::runtime_error("Expected identifier");
            if (lookahead.NextToken().type == TokenType::LeftParen) {
                program.children.push_back(ParseFunctionDeclaration());
            } else {
                program.children.push_back(ParseVariableDeclaration());
            }
        } else {
            ParseStatement(program.children);
        }
    }
    return program;
}
