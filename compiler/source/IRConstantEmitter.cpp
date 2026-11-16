#include "IRConstantEmitter.h"

#include <stdexcept>

namespace c4r::ir {

llvm::Constant* IRConstantEmitter::Emit(const Expression& expression) {
    if (auto* number = dynamic_cast<const NumericLiteral*>(&expression)) {
        if (number->value.find('.') != std::string::npos)
            return llvm::ConstantFP::get(Builder.getFloatTy(), std::stod(number->value));
        auto value = std::stoll(number->value);
        if (value > INT32_MAX) throw std::runtime_error("Integer literal out of range");
        return Builder.getInt32(static_cast<int32_t>(value));
    }
    if (auto* boolean = dynamic_cast<const BooleanLiteral*>(&expression)) return Builder.getInt1(boolean->value);
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
        auto* value = Emit(*unary->operand);
        llvm::Value* result = nullptr;
        if (unary->op == "+" && (value->getType()->isIntegerTy(32) || value->getType()->isFloatTy())) result = value;
        else if (unary->op == "-" && value->getType()->isIntegerTy(32)) result = Builder.CreateNeg(value);
        else if (unary->op == "-" && value->getType()->isFloatTy()) result = Builder.CreateFNeg(value);
        else if (unary->op == "~" && value->getType()->isIntegerTy(32)) result = Builder.CreateNot(value);
        else if (unary->op == "!" && value->getType()->isIntegerTy(1)) result = Builder.CreateNot(value);
        if (auto* constant = llvm::dyn_cast_or_null<llvm::Constant>(result)) return constant;
        throw std::runtime_error("Invalid constant unary expression");
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        auto* left = Emit(*binary->left);
        const auto& op = binary->op;
        if (op == "&&" || op == "||" || op == "!&&" || op == "!||") {
            if (!left->getType()->isIntegerTy(1)) throw std::runtime_error("Logical operator requires bool operands");
            auto* truth = llvm::dyn_cast<llvm::ConstantInt>(left);
            if (!truth) throw std::runtime_error("Invalid global constant initializer");
            bool isAnd = op == "&&" || op == "!&&";
            bool negate = op == "!&&" || op == "!||";
            bool leftValue = !truth->isZero();
            if ((isAnd && !leftValue) || (!isAnd && leftValue)) return Builder.getInt1(negate ? !leftValue : leftValue);
            auto* right = Emit(*binary->right);
            if (!right->getType()->isIntegerTy(1)) throw std::runtime_error("Logical operator requires bool operands");
            auto* result = op == "&&" || op == "!&&" ? Builder.CreateAnd(left, right) : Builder.CreateOr(left, right);
            if (op == "!&&" || op == "!||") result = Builder.CreateNot(result);
            return llvm::cast<llvm::Constant>(result);
        }
        auto* right = Emit(*binary->right);
        if (op == "`") {
            auto* base = llvm::dyn_cast<llvm::ConstantInt>(left);
            auto* exponent = llvm::dyn_cast<llvm::ConstantInt>(right);
            if (!base || !exponent || !base->getType()->isIntegerTy(32) || !exponent->getType()->isIntegerTy(32))
                throw std::runtime_error("Operator requires int32 operands: `");
            if (exponent->isNegative()) throw std::runtime_error("Integer exponent must be nonnegative");
            auto n = exponent->getZExtValue();
            llvm::APInt result(32, 1), factor = base->getValue();
            while (n) { if (n & 1) result *= factor; factor *= factor; n >>= 1; }
            return llvm::ConstantInt::get(Builder.getContext(), result);
        }
        if (op == "=" || op.ends_with("=")) throw std::runtime_error("Unsupported operator in global constant initializer: " + op);
        auto* value = llvm::dyn_cast<llvm::Constant>(operators.Emit(op, left, right));
        if (!value || llvm::isa<llvm::PoisonValue>(value)) throw std::runtime_error("Invalid global constant initializer");
        return value;
    }
    if (auto* chain = dynamic_cast<const ComparisonExpr*>(&expression)) {
        auto* left = Emit(*chain->operands.front());
        llvm::Constant* result = Builder.getTrue();
        for (size_t i = 0; i < chain->operators.size(); ++i) {
            auto* right = Emit(*chain->operands[i + 1]);
            auto* comparison = llvm::dyn_cast<llvm::Constant>(operators.Emit(chain->operators[i], left, right));
            if (!comparison || llvm::isa<llvm::PoisonValue>(comparison)) throw std::runtime_error("Invalid global constant initializer");
            auto* truth = llvm::dyn_cast<llvm::ConstantInt>(comparison);
            if (truth && truth->isZero()) return Builder.getFalse();
            result = llvm::cast<llvm::Constant>(Builder.CreateAnd(result, comparison));
            left = right;
        }
        return result;
    }
    throw std::runtime_error("Global initializer requires a constant expression");
}

}
