#include "Parser.h"

#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
constexpr int AssignmentPrecedence = 1;
constexpr int EqualityPrecedence = 10;
constexpr int OrderingPrecedence = 11;
constexpr int PrefixPrecedence = 16;
constexpr int PostfixPrecedence = 17;

int Precedence(std::string_view op) {
    if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
        op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=") return AssignmentPrecedence;
    if (op == "!||") return 2;
    if (op == "!&&") return 3;
    if (op == "||") return 4;
    if (op == "^^") return 5;
    if (op == "&&") return 6;
    if (op == "|") return 7;
    if (op == "^") return 8;
    if (op == "&") return 9;
    if (op == "==" || op == "!=") return EqualityPrecedence;
    if (op == "<" || op == "<=" || op == ">" || op == ">=") return OrderingPrecedence;
    if (op == "<<" || op == ">>") return 12;
    if (op == "+" || op == "-") return 13;
    if (op == "*" || op == "/" || op == "%") return 14;
    if (op == "`") return 15;
    return 0;
}

bool IsComparison(int precedence) {
    return precedence == EqualityPrecedence || precedence == OrderingPrecedence;
}

bool IsPrefix(const Token& token) {
    const auto& op = token.value;
    return token.type == TokenType::Operator &&
        (op == "+" || op == "-" || op == "!" || op == "~" || op == "&" || op == "++" || op == "--");
}
}

std::unique_ptr<Expression> Parser::ParsePrimary() {
    if (IsPrefix(currentToken)) {
        auto unary = std::make_unique<UnaryExpr>();
        unary->op = Consume(TokenType::Operator).value;
        unary->operand = ParseExpression(PrefixPrecedence);
        return unary;
    }
    if (currentToken.value == "if") {
        auto value = std::make_unique<IfExpr>();
        Advance();
        value->condition = ParseParenthesizedExpression();
        value->thenBranch = ParseBlock();
        if (currentToken.value != "else") throw std::runtime_error("if expression requires else");
        Advance();
        value->elseBranch = ParseElseBlock();
        return value;
    }
    if (currentToken.type == TokenType::LeftBrace) {
        auto value = std::make_unique<BlockExpr>();
        value->block = ParseBlock();
        return value;
    }
    if (currentToken.type == TokenType::LeftParen) return ParseParenthesizedExpression();
    if (currentToken.type == TokenType::Number) {
        auto value = std::make_unique<NumericLiteral>();
        value->value = Consume(TokenType::Number).value;
        return value;
    }
    if (currentToken.type == TokenType::TypeName) {
        auto value = std::make_unique<ConstructExpr>();
        value->type = Consume(TokenType::TypeName).value;
        value->arguments = ParseArguments();
        return value;
    }
    if (currentToken.type == TokenType::Identifier) {
        auto name = Consume(TokenType::Identifier).value;
        if (name == "true" || name == "false") {
            auto value = std::make_unique<BooleanLiteral>();
            value->value = name == "true";
            return value;
        }
        auto value = std::make_unique<IdentifierExpr>();
        value->name = std::move(name);
        return value;
    }
    throw std::runtime_error("Expected expression, found: " + currentToken.value);
}

std::unique_ptr<Expression> Parser::ParseParenthesizedExpression() {
    Consume(TokenType::LeftParen);
    auto expression = ParseExpression();
    Consume(TokenType::RightParen);
    return expression;
}

std::vector<std::unique_ptr<Expression>> Parser::ParseArguments() {
    Consume(TokenType::LeftParen);
    std::vector<std::unique_ptr<Expression>> arguments;
    if (currentToken.type != TokenType::RightParen) {
        do {
            arguments.push_back(ParseExpression());
            if (currentToken.type != TokenType::Comma) break;
            Advance();
        } while (true);
    }
    Consume(TokenType::RightParen);
    return arguments;
}

std::unique_ptr<Expression> Parser::ParseCall(std::unique_ptr<Expression> callee, size_t offset) {
    auto* identifier = dynamic_cast<IdentifierExpr*>(callee.get());
    auto* member = dynamic_cast<MemberExpr*>(callee.get());
    if (!identifier && !member) throw std::runtime_error("Function call requires a function name");
    if (identifier && identifier->name == "move") {
        auto move = std::make_unique<MoveExpr>();
        move->offset = offset;
        move->operand = ParseParenthesizedExpression();
        return move;
    }

    auto call = std::make_unique<CallExpr>();
    if (identifier) {
        call->name = std::move(identifier->name);
    } else {
        call->name = std::move(member->name);
        call->object = std::move(member->object);
    }
    call->arguments = ParseArguments();
    return call;
}

std::unique_ptr<Expression> Parser::ParseExpression(int minPrecedence) {
    const auto offset = currentToken.offset;
    auto left = ParsePrimary();
    left->offset = offset;
    while (true) {
        if (minPrecedence <= PostfixPrecedence) {
            if (currentToken.type == TokenType::Dot) {
                Advance();
                auto member = std::make_unique<MemberExpr>();
                member->object = std::move(left);
                member->name = Consume(TokenType::Identifier).value;
                member->offset = offset;
                left = std::move(member);
                continue;
            }
            if (currentToken.type == TokenType::LeftParen) {
                left = ParseCall(std::move(left), offset);
                continue;
            }
            if (currentToken.type == TokenType::Operator && (currentToken.value == "++" || currentToken.value == "--")) {
                auto unary = std::make_unique<UnaryExpr>();
                unary->op = Consume(TokenType::Operator).value;
                unary->postfix = true;
                unary->operand = std::move(left);
                unary->offset = offset;
                left = std::move(unary);
                continue;
            }
        }

        const int precedence = Precedence(currentToken.value);
        if (!precedence || precedence < minPrecedence) break;
        auto op = currentToken.value;
        Advance();
        if (IsComparison(precedence)) {
            auto chain = std::make_unique<ComparisonExpr>();
            chain->operands.push_back(std::move(left));
            do {
                chain->operators.push_back(std::move(op));
                chain->operands.push_back(ParseExpression(OrderingPrecedence + 1));
                if (!IsComparison(Precedence(currentToken.value))) break;
                op = currentToken.value;
                Advance();
            } while (true);
            left = std::move(chain);
        } else {
            auto binary = std::make_unique<BinaryExpr>();
            const bool rightAssociative = precedence == AssignmentPrecedence || op == "`";
            binary->op = std::move(op);
            binary->left = std::move(left);
            binary->right = ParseExpression(rightAssociative ? precedence : precedence + 1);
            left = std::move(binary);
        }
        left->offset = offset;
    }
    left->offset = offset;
    return left;
}
