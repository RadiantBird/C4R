#pragma once

#include "AST.h"
#include "OwnershipFlow.h"
#include <functional>
#include <stdexcept>

// Lowers straight-line expressions into ordered ownership events. Control-flow
// expressions must be lowered by the enclosing CFG builder, not flattened.
class OwnershipExpressionFlow {
public:
    using Resolve = std::function<OwnershipState::Binding(const IdentifierExpr&)>;
    explicit OwnershipExpressionFlow(Resolve resolve) : resolve(std::move(resolve)) {}
    OwnershipState::Binding binding(const IdentifierExpr& identifier) const { return resolve(identifier); }

    void append(const Expression& expression, OwnershipFlow::Block& block) const {
        if (auto* id = dynamic_cast<const IdentifierExpr*>(&expression)) {
            if (auto binding = resolve(*id)) block.operations.push_back({OwnershipFlow::Action::Read, binding, id->offset, id});
        } else if (auto* move = dynamic_cast<const MoveExpr*>(&expression)) {
            auto* id = dynamic_cast<const IdentifierExpr*>(move->operand.get());
            if (!id) throw std::runtime_error("move requires an owned variable; field moves are not supported");
            auto binding = resolve(*id);
            if (!binding) throw std::runtime_error("move requires an owned variable");
            block.operations.push_back({OwnershipFlow::Action::Move, binding, move->offset, move});
        } else if (auto* member = dynamic_cast<const MemberExpr*>(&expression)) {
            append(*member->object, block);
        } else if (auto* call = dynamic_cast<const CallExpr*>(&expression)) {
            if (call->object) append(*call->object, block);
            for (const auto& arg : call->arguments) append(*arg, block);
        } else if (auto* construct = dynamic_cast<const ConstructExpr*>(&expression)) {
            for (const auto& arg : construct->arguments) append(*arg, block);
        } else if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
            append(*unary->operand, block);
        } else if (dynamic_cast<const NumericLiteral*>(&expression) || dynamic_cast<const BooleanLiteral*>(&expression)) {
            return;
        } else {
            throw std::runtime_error("Expression requires ownership control-flow lowering");
        }
    }
private:
    Resolve resolve;
};
