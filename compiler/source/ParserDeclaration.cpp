#include "Parser.h"

#include <stdexcept>
#include <utility>

Token Parser::ReadDeclaratorName(Lexer& lookahead, Token next, bool allowConsumable) {
    while (next.type == TokenType::Operator && next.value == "*") next = lookahead.NextToken();
    if (next.value == "mutable") next = lookahead.NextToken();
    if (allowConsumable && next.type == TokenType::Operator && next.value == "&&") next = lookahead.NextToken();
    if (next.type == TokenType::Operator && next.value == "&") {
        next = lookahead.NextToken();
        if (next.type == TokenType::At) {
            lookahead.NextToken();
            next = lookahead.NextToken();
        }
    }
    return next;
}

std::string Parser::ParseTypeName() {
    auto type = Consume(TokenType::TypeName).value;
    while (currentToken.type == TokenType::Operator && currentToken.value == "*") {
        type += "*";
        Advance();
    }
    return type;
}

std::string Parser::ParseLifetime() {
    if (currentToken.type != TokenType::At) return {};
    Advance();
    return Consume(TokenType::Identifier).value;
}

std::vector<FunctionDeclaration::Parameter> Parser::ParseParameters(bool mustBeEmpty) {
    std::vector<FunctionDeclaration::Parameter> parameters;
    if (mustBeEmpty && currentToken.type != TokenType::RightParen)
        throw std::runtime_error("Destructor cannot have parameters");
    if (currentToken.type != TokenType::RightParen) {
        do {
            FunctionDeclaration::Parameter parameter;
            parameter.offset = currentToken.offset;
            if (currentToken.type == TokenType::Identifier) throw std::runtime_error("Unknown type: " + currentToken.value);
            parameter.type = ParseTypeName();
            if (currentToken.value == "mutable") {
                parameter.isMutable = true;
                Advance();
            }
            if (currentToken.type == TokenType::Operator && currentToken.value == "&&") {
                parameter.isConsumable = true;
                Advance();
            } else if (currentToken.type == TokenType::Operator && currentToken.value == "&") {
                parameter.isReference = true;
                parameter.isMutableReference = parameter.isMutable;
                Advance();
                parameter.lifetime = ParseLifetime();
            }
            parameter.name = Consume(TokenType::Identifier).value;
            parameters.push_back(std::move(parameter));
            if (currentToken.type != TokenType::Comma) break;
            Advance();
        } while (true);
    }
    Consume(TokenType::RightParen);
    return parameters;
}

std::unique_ptr<FunctionDeclaration> Parser::ParseFunctionDeclaration() {
    auto function = std::make_unique<FunctionDeclaration>();
    function->offset = currentToken.offset;
    function->returnType = ParseTypeName();
    if (currentToken.value == "mutable") {
        function->returnsMutableReference = true;
        Advance();
    }
    if (currentToken.type == TokenType::Operator && currentToken.value == "&&") {
        function->returnsConsumable = true;
        Advance();
    } else if (currentToken.type == TokenType::Operator && currentToken.value == "&") {
        function->returnsReference = true;
        Advance();
        function->returnLifetime = ParseLifetime();
    } else if (function->returnsMutableReference) {
        throw std::runtime_error("mutable return requires a reference type");
    }

    function->name = Consume(TokenType::Identifier).value;
    Consume(TokenType::LeftParen);
    function->parameters = ParseParameters();
    function->body = std::move(ParseBlock()->statements);
    return function;
}

std::unique_ptr<VariableDeclaration> Parser::ParseVariableDeclaration(bool field) {
    auto variable = std::make_unique<VariableDeclaration>();
    variable->offset = currentToken.offset;
    if (currentToken.type == TokenType::Reserved && currentToken.value == "const") {
        variable->isConst = true;
        Advance();
    }

    if (currentToken.type == TokenType::Identifier) throw std::runtime_error("Unknown type: " + currentToken.value);
    variable->type = ParseTypeName();

    if (currentToken.type == TokenType::Reserved && currentToken.value == "mutable") {
        if (variable->isConst) throw std::runtime_error("const and mutable cannot coexist");
        variable->isMutable = true;
        Advance();
    }

    if (currentToken.type == TokenType::Operator && currentToken.value == "&&") {
        variable->isConsumable = true;
        Advance();
    } else if (currentToken.type == TokenType::Operator && currentToken.value == "&") {
        variable->isReference = true;
        variable->isMutableReference = variable->isMutable;
        Advance();
    }

    variable->name = Consume(TokenType::Identifier).value;
    if (currentToken.type == TokenType::Assign) {
        Advance();
        variable->initialValue = ParseExpression();
    } else if (currentToken.type == TokenType::LeftBrace && !field) {
        Advance();
        Consume(TokenType::RightBrace);
        variable->aggregateInit = true;
    } else if (!field) {
        throw std::runtime_error("Variable requires an initializer");
    }

    Consume(TokenType::EndOfStatement);
    return variable;
}

std::unique_ptr<FunctionDeclaration> Parser::ParseSpecialMember(const std::string& typeName, bool destructor) {
    const size_t offset = currentToken.offset;
    if (destructor) Advance();
    if (currentToken.value != typeName || currentToken.type != TokenType::TypeName)
        throw std::runtime_error("Special member name must match the enclosing type");
    Advance();
    Consume(TokenType::LeftParen);

    std::unique_ptr<FunctionDeclaration> declaration;
    if (destructor) declaration = std::make_unique<DestructorDeclaration>();
    else declaration = std::make_unique<ConstructorDeclaration>();
    declaration->offset = offset;
    declaration->name = typeName;
    declaration->returnType = "void";
    declaration->parameters = ParseParameters(destructor);
    declaration->body = std::move(ParseBlock()->statements);
    return declaration;
}

std::unique_ptr<ASTNode> Parser::ParseTypeMember(const std::string& typeName) {
    if (currentToken.type == TokenType::Operator && currentToken.value == "~") {
        return ParseSpecialMember(typeName, true);
    }

    auto lookahead = lexer;
    auto next = lookahead.NextToken();
    if (currentToken.type == TokenType::TypeName && currentToken.value == typeName && next.type == TokenType::LeftParen) {
        return ParseSpecialMember(typeName, false);
    }
    const auto name = ReadDeclaratorName(lookahead, std::move(next), false);
    if (currentToken.type == TokenType::TypeName && name.type == TokenType::Identifier &&
        lookahead.NextToken().type == TokenType::LeftParen) {
        return ParseFunctionDeclaration();
    }
    return ParseVariableDeclaration(true);
}

std::unique_ptr<TypeDeclaration> Parser::ParseTypeDeclaration() {
    auto type = std::make_unique<TypeDeclaration>();
    type->isStruct = currentToken.value == "struct";
    type->offset = currentToken.offset;
    Advance();
    if (currentToken.type != TokenType::Identifier && currentToken.type != TokenType::TypeName)
        throw std::runtime_error("Expected type name");
    type->name = currentToken.value;
    Advance();
    Consume(TokenType::LeftBrace);

    bool access = type->isStruct;
    while (currentToken.type != TokenType::RightBrace) {
        if (currentToken.type == TokenType::EndOfFile) throw std::runtime_error("Unexpected EOF in type");
        bool memberAccess = access;
        if (currentToken.value == "public" || currentToken.value == "private") {
            memberAccess = currentToken.value == "public";
            Advance();
            if (currentToken.type == TokenType::Colon) {
                access = memberAccess;
                Advance();
                continue;
            }
        }
        type->members.push_back({memberAccess, ParseTypeMember(type->name)});
    }
    Advance();
    Consume(TokenType::EndOfStatement);
    return type;
}
