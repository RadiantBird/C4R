#pragma once

#include "AST.h"
#include "Lexer.h"

class Parser {
public:
    explicit Parser(Lexer lexer);
    Program ParseProgram();
    size_t DiagnosticOffset() const { return currentToken.offset; }

private:
    using Statements = std::vector<std::unique_ptr<ASTNode>>;

    Lexer lexer;
    Token currentToken;

    void Advance();
    Token Consume(TokenType type);

    std::unique_ptr<Expression> ParseExpression(int minPrecedence = 1);
    std::unique_ptr<Expression> ParsePrimary();
    std::unique_ptr<Expression> ParseParenthesizedExpression();
    std::unique_ptr<Expression> ParseCall(std::unique_ptr<Expression> callee, size_t offset);
    std::vector<std::unique_ptr<Expression>> ParseArguments();

    void ParseStringStatement(Statements& destination);
    void ParseFormatStringStatement(Statements& destination);
    std::unique_ptr<Block> ParseBlock();
    std::unique_ptr<Block> ParseElseBlock();
    std::unique_ptr<IfStatement> ParseIfStatement();
    std::unique_ptr<LoopStatement> ParseLoop();
    void ParseStatement(Statements& destination);

    static Token ReadDeclaratorName(Lexer& lookahead, Token next, bool allowConsumable);
    std::string ParseTypeName();
    std::string ParseLifetime();
    std::vector<FunctionDeclaration::Parameter> ParseParameters(bool mustBeEmpty = false);
    std::unique_ptr<FunctionDeclaration> ParseFunctionDeclaration();
    std::unique_ptr<VariableDeclaration> ParseVariableDeclaration(bool field = false);
    std::unique_ptr<FunctionDeclaration> ParseSpecialMember(const std::string& typeName, bool destructor);
    std::unique_ptr<ASTNode> ParseTypeMember(const std::string& typeName);
    std::unique_ptr<TypeDeclaration> ParseTypeDeclaration();
};
