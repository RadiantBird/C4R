#include "Parser.h"

#include <stdexcept>
#include <utility>

void Parser::ParseStringStatement(Statements& destination) {
    auto token = Consume(TokenType::String);
    Consume(TokenType::EndOfStatement);

    auto statement = std::make_unique<StringStatement>();
    statement->offset = token.offset;
    statement->value = std::move(token.value);
    destination.push_back(std::move(statement));
}

void Parser::ParseFormatStringStatement(Statements& destination) {
    auto statement = std::make_unique<FormatStringStatement>();
    statement->offset = currentToken.offset;
    const auto& text = currentToken.value;
    size_t position = 0;
    while (position < text.size()) {
        const size_t open = text.find('{', position);
        if (open > position) {
            auto literal = std::make_unique<FormatText>();
            literal->value = text.substr(position, open == std::string::npos ? open : open - position);
            statement->parts.push_back(std::move(literal));
        }
        if (open == std::string::npos) break;

        const size_t close = text.find('}', open + 1);
        if (close == std::string::npos) throw std::runtime_error("Unclosed format expression");
        auto name = text.substr(open + 1, close - open - 1);
        if (name.empty() || !Lexer::IsIdentifierStart(name.front()))
            throw std::runtime_error("Expected identifier in format expression");
        for (size_t i = 1; i < name.size(); ++i) {
            if (!Lexer::IsIdentifierPart(name[i])) throw std::runtime_error("Invalid identifier in format expression");
        }

        auto identifier = std::make_unique<IdentifierExpr>();
        identifier->name = std::move(name);
        statement->parts.push_back(std::move(identifier));
        position = close + 1;
    }
    Advance();
    Consume(TokenType::EndOfStatement);
    destination.push_back(std::move(statement));
}

std::unique_ptr<Block> Parser::ParseBlock() {
    auto block = std::make_unique<Block>();
    block->offset = Consume(TokenType::LeftBrace).offset;
    while (currentToken.type != TokenType::RightBrace) {
        if (currentToken.type == TokenType::EndOfFile) throw std::runtime_error("Unexpected EOF in block");
        ParseStatement(block->statements);
    }
    Advance();
    return block;
}

std::unique_ptr<Block> Parser::ParseElseBlock() {
    if (currentToken.value != "if") return ParseBlock();
    auto block = std::make_unique<Block>();
    ParseStatement(block->statements);
    return block;
}

std::unique_ptr<IfStatement> Parser::ParseIfStatement() {
    auto statement = std::make_unique<IfStatement>();
    statement->offset = currentToken.offset;
    Advance();
    statement->condition = ParseParenthesizedExpression();
    statement->thenBranch = ParseBlock();
    if (currentToken.value == "else") {
        Advance();
        statement->elseBranch = ParseElseBlock();
    }
    return statement;
}

std::unique_ptr<LoopStatement> Parser::ParseLoop() {
    auto loop = std::make_unique<LoopStatement>();
    loop->offset = currentToken.offset;
    const std::string kind = currentToken.value;
    Advance();

    if (kind == "do") {
        loop->kind = LoopStatement::Kind::DoWhile;
        loop->body = ParseBlock();
        if (currentToken.value != "while") throw std::runtime_error("Expected while after do block");
        Advance();
        loop->condition = ParseParenthesizedExpression();
        Consume(TokenType::EndOfStatement);
        return loop;
    }
    if (kind == "while") {
        loop->kind = LoopStatement::Kind::While;
        loop->condition = ParseParenthesizedExpression();
        loop->body = ParseBlock();
        return loop;
    }

    loop->kind = LoopStatement::Kind::For;
    Consume(TokenType::LeftParen);
    auto lookahead = lexer;
    const bool variable = currentToken.type == TokenType::TypeName && lookahead.NextToken().type != TokenType::LeftParen;
    if (variable || currentToken.value == "const") {
        loop->initializer = ParseVariableDeclaration();
    } else {
        if (currentToken.type != TokenType::EndOfStatement) loop->initializer = ParseExpression();
        Consume(TokenType::EndOfStatement);
    }
    if (currentToken.type != TokenType::EndOfStatement) loop->condition = ParseExpression();
    Consume(TokenType::EndOfStatement);
    if (currentToken.type != TokenType::RightParen) loop->step = ParseExpression();
    Consume(TokenType::RightParen);
    loop->body = ParseBlock();
    return loop;
}

void Parser::ParseStatement(Statements& destination) {
    if (currentToken.type == TokenType::String) {
        ParseStringStatement(destination);
        return;
    }
    if (currentToken.type == TokenType::FormatString) {
        ParseFormatStringStatement(destination);
        return;
    }
    if (currentToken.type == TokenType::Identifier) {
        auto lookahead = lexer;
        auto next = lookahead.NextToken();
        while (next.type == TokenType::Operator && next.value == "*") next = lookahead.NextToken();
        if (next.value == "mutable") next = lookahead.NextToken();
        if (next.type == TokenType::Identifier) throw std::runtime_error("Unknown type: " + currentToken.value);
    }
    if (currentToken.type == TokenType::TypeName) {
        auto lookahead = lexer;
        const auto name = ReadDeclaratorName(lookahead, lookahead.NextToken(), true);
        if (name.type != TokenType::LeftParen) {
            if (name.type == TokenType::Identifier && lookahead.NextToken().type == TokenType::LeftParen) {
                destination.push_back(ParseFunctionDeclaration());
            } else {
                destination.push_back(ParseVariableDeclaration());
            }
            return;
        }
    }
    if (currentToken.value == "const") {
        destination.push_back(ParseVariableDeclaration());
    } else if (currentToken.value == "capture") {
        auto capture = std::make_unique<CaptureDeclaration>();
        capture->offset = currentToken.offset;
        Advance();
        do {
            capture->names.push_back(Consume(TokenType::Identifier).value);
            if (currentToken.type != TokenType::Comma) break;
            Advance();
        } while (true);
        Consume(TokenType::EndOfStatement);
        destination.push_back(std::move(capture));
    } else if (currentToken.type == TokenType::LeftBrace) {
        destination.push_back(ParseBlock());
    } else if (currentToken.value == "return") {
        auto statement = std::make_unique<ReturnStatement>();
        statement->offset = currentToken.offset;
        Advance();
        if (currentToken.type != TokenType::EndOfStatement) statement->value = ParseExpression();
        Consume(TokenType::EndOfStatement);
        destination.push_back(std::move(statement));
    } else if (currentToken.value == "break" || currentToken.value == "continue") {
        auto control = std::make_unique<LoopControl>();
        control->offset = currentToken.offset;
        control->isBreak = currentToken.value == "break";
        Advance();
        Consume(TokenType::EndOfStatement);
        destination.push_back(std::move(control));
    } else if (currentToken.value == "if") {
        destination.push_back(ParseIfStatement());
    } else if (currentToken.value == "while" || currentToken.value == "for" || currentToken.value == "do") {
        destination.push_back(ParseLoop());
    } else {
        auto expression = ParseExpression();
        Consume(TokenType::EndOfStatement);
        destination.push_back(std::move(expression));
    }
}
