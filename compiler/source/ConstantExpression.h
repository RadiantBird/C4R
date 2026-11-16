#pragma once

#include "AST.h"

#include <llvm/ADT/APInt.h>
#include <optional>

class ConstantExpression {
public:
    static void ValidateNumber(const NumericLiteral& number);
    static void Check(const Expression& expression, bool evaluated = true);
    static std::optional<llvm::APInt> Integer(const Expression& expression);
    static std::optional<bool> Boolean(const Expression& expression);
    static std::optional<bool> Compare(const Expression& left, const std::string& op, const Expression& right);

private:
    static std::optional<float> Float(const Expression& expression);
};
