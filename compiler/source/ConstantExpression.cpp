#include "ConstantExpression.h"

#include <stdexcept>

void ConstantExpression::ValidateNumber(const NumericLiteral& number) {
    try {
        if (number.value.find('.') != std::string::npos) (void)std::stod(number.value);
        else if (std::stoll(number.value) > INT32_MAX) throw std::out_of_range("literal");
    } catch (const std::out_of_range&) {
        throw std::runtime_error(number.value.find('.') == std::string::npos ? "Integer literal out of range" : "Floating literal out of range");
    }
}

std::optional<llvm::APInt> ConstantExpression::Integer(const Expression& expression) {
    if (auto* number = dynamic_cast<const NumericLiteral*>(&expression)) {
        if (number->value.find('.') != std::string::npos) return std::nullopt;
        long long value;
        try { value = std::stoll(number->value); }
        catch (const std::out_of_range&) { throw std::runtime_error("Integer literal out of range"); }
        if (value > INT32_MAX) throw std::runtime_error("Integer literal out of range");
        return llvm::APInt(32, static_cast<uint64_t>(value));
    }
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
        auto value = Integer(*unary->operand);
        if (!value) return std::nullopt;
        if (unary->op == "+") return *value;
        if (unary->op == "-") return -*value;
        if (unary->op == "~") return ~*value;
        return std::nullopt;
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        const auto& op = binary->op;
        if (op != "+" && op != "-" && op != "*" && op != "/" && op != "%" && op != "&" && op != "|" && op != "^" && op != "<<" && op != ">>" && op != "`") return std::nullopt;
        auto left = Integer(*binary->left), right = Integer(*binary->right);
        if (!left || !right) return std::nullopt;
        if (op == "+") return *left + *right;
        if (op == "-") return *left - *right;
        if (op == "*") return *left * *right;
        if (op == "/" || op == "%") {
            if (right->isZero() || (left->isMinSignedValue() && right->isAllOnes())) throw std::runtime_error("Invalid global constant initializer");
            return op == "/" ? left->sdiv(*right) : left->srem(*right);
        }
        if (op == "&") return *left & *right;
        if (op == "|") return *left | *right;
        if (op == "^") return *left ^ *right;
        if (op == "<<" || op == ">>") {
            if (right->uge(32)) throw std::runtime_error("Integer shift count out of range");
            return op == "<<" ? left->shl(static_cast<unsigned>(right->getZExtValue())) : left->ashr(static_cast<unsigned>(right->getZExtValue()));
        }
        if (op == "`") {
            if (right->isNegative()) throw std::runtime_error("Integer exponent must be nonnegative");
            uint64_t count = right->getZExtValue();
            llvm::APInt result(32, 1), factor = *left;
            while (count) { if (count & 1) result *= factor; factor *= factor; count >>= 1; }
            return result;
        }
    }
    return std::nullopt;
}

std::optional<float> ConstantExpression::Float(const Expression& expression) {
    if (auto* number = dynamic_cast<const NumericLiteral*>(&expression)) {
        if (number->value.find('.') == std::string::npos) return std::nullopt;
        return static_cast<float>(std::stod(number->value));
    }
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
        if (unary->op != "+" && unary->op != "-") return std::nullopt;
        auto value = Float(*unary->operand);
        if (!value) return std::nullopt;
        return unary->op == "-" ? -*value : *value;
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        if (binary->op != "+" && binary->op != "-" && binary->op != "*" && binary->op != "/") return std::nullopt;
        auto left = Float(*binary->left);
        if (!left) return std::nullopt;
        auto right = Float(*binary->right);
        if (!right) return std::nullopt;
        if (binary->op == "+") return *left + *right;
        if (binary->op == "-") return *left - *right;
        if (binary->op == "*") return *left * *right;
        return *left / *right;
    }
    return std::nullopt;
}

std::optional<bool> ConstantExpression::Compare(const Expression& left, const std::string& op, const Expression& right) {
    auto compare = [&](auto a, auto b) -> std::optional<bool> {
        if (op == "==") return a == b;
        if (op == "!=") return a != b;
        if (op == "<") return a < b;
        if (op == "<=") return a <= b;
        if (op == ">") return a > b;
        if (op == ">=") return a >= b;
        return std::nullopt;
    };
    if (auto a = Integer(left)) {
        if (auto b = Integer(right)) return compare(a->getSExtValue(), b->getSExtValue());
        return std::nullopt;
    }
    if (auto a = Float(left)) {
        if (auto b = Float(right)) return compare(*a, *b);
        return std::nullopt;
    }
    if (auto a = Boolean(left)) {
        if (auto b = Boolean(right); b && (op == "==" || op == "!=")) return compare(*a, *b);
    }
    return std::nullopt;
}

std::optional<bool> ConstantExpression::Boolean(const Expression& expression) {
    if (auto* literal = dynamic_cast<const BooleanLiteral*>(&expression)) return literal->value;
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression); unary && unary->op == "!") {
        auto value = Boolean(*unary->operand);
        return value ? std::optional<bool>(!*value) : std::nullopt;
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        const auto& op = binary->op;
        if (op != "&&" && op != "||" && op != "!&&" && op != "!||" && op != "^^") return std::nullopt;
        auto left = Boolean(*binary->left);
        if (!left) return std::nullopt;
        bool isAnd = op == "&&" || op == "!&&";
        bool negate = op == "!&&" || op == "!||";
        if (op != "^^" && ((isAnd && !*left) || (!isAnd && *left))) return negate ? !*left : *left;
        auto right = Boolean(*binary->right);
        if (!right) return std::nullopt;
        bool result = op == "^^" ? *left != *right : isAnd ? *left && *right : *left || *right;
        return negate ? !result : result;
    }
    if (auto* chain = dynamic_cast<const ComparisonExpr*>(&expression)) {
        for (size_t i = 0; i < chain->operators.size(); ++i) {
            auto result = Compare(*chain->operands[i], chain->operators[i], *chain->operands[i + 1]);
            if (!result) return std::nullopt;
            if (!*result) return false;
        }
        return true;
    }
    return std::nullopt;
}

void ConstantExpression::Check(const Expression& expression, bool evaluated) {
    if (auto* number = dynamic_cast<const NumericLiteral*>(&expression)) { ValidateNumber(*number); return; }
    if (dynamic_cast<const BooleanLiteral*>(&expression)) return;
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
        if (unary->op != "+" && unary->op != "-" && unary->op != "~" && unary->op != "!")
            throw std::runtime_error("Global initializer requires a constant expression");
        Check(*unary->operand, evaluated);
        return;
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        if (binary->op == "=" || binary->op.ends_with("=")) throw std::runtime_error("Global initializer requires a constant expression");
        Check(*binary->left, evaluated);
        bool evaluateRight = evaluated;
        if (evaluated && (binary->op == "&&" || binary->op == "!&&" || binary->op == "||" || binary->op == "!||")) {
            auto left = Boolean(*binary->left);
            bool isAnd = binary->op == "&&" || binary->op == "!&&";
            if (left && ((isAnd && !*left) || (!isAnd && *left))) evaluateRight = false;
        }
        Check(*binary->right, evaluateRight);
        if (evaluated && binary->op != "&&" && binary->op != "!&&" && binary->op != "||" && binary->op != "!||") (void)Integer(expression);
        return;
    }
    if (auto* chain = dynamic_cast<const ComparisonExpr*>(&expression)) {
        Check(*chain->operands.front(), evaluated);
        bool active = evaluated;
        for (size_t i = 0; i < chain->operators.size(); ++i) {
            Check(*chain->operands[i + 1], active);
            if (active) {
                auto result = Compare(*chain->operands[i], chain->operators[i], *chain->operands[i + 1]);
                if (result && !*result) active = false;
            }
        }
        return;
    }
    throw std::runtime_error("Global initializer requires a constant expression");
}
