#pragma once

#include "OwnershipExpressionFlow.h"
#include <optional>

// Builds the ownership CFG separately from LLVM. Unsupported constructs fail
// closed until their evaluation order and scope exits have been lowered.
class OwnershipGraphBuilder {
public:
    explicit OwnershipGraphBuilder(OwnershipExpressionFlow::Resolve resolve,
        std::function<bool(const BinaryExpr&)> isReinitialization = {})
        : expressions(std::move(resolve)), isReinitialization(std::move(isReinitialization)) {}

    OwnershipFlow build(const std::vector<std::unique_ptr<ASTNode>>& statements) {
        flow = {};
        loops.clear();
        valueExits.clear();
        flow.blocks.emplace_back();
        appendBody(statements, 0);
        return std::move(flow);
    }
    OwnershipFlow buildFunction(const FunctionDeclaration& function) {
        flow = {};
        loops.clear();
        valueExits.clear();
        flow.blocks.emplace_back();
        for (const auto& parameter : function.parameters)
            flow.blocks[0].operations.push_back({OwnershipFlow::Action::Initialize, &parameter, parameter.offset});
        appendBody(function.body, 0);
        return std::move(flow);
    }
private:
    OwnershipExpressionFlow expressions;
    std::function<bool(const BinaryExpr&)> isReinitialization;
    OwnershipFlow flow;
    std::vector<std::pair<std::size_t, std::size_t>> loops; // break, continue
    std::vector<std::size_t> valueExits;

    std::size_t block() { flow.blocks.emplace_back(); return flow.blocks.size() - 1; }
    std::size_t appendExpression(const Expression& expression, std::size_t at) {
        if (auto* valueBlock = dynamic_cast<const BlockExpr*>(&expression)) {
            auto join = block();
            valueExits.push_back(join);
            auto end = appendBody(valueBlock->block->statements, at);
            valueExits.pop_back();
            if (end) throw std::runtime_error("Value scope requires a return value");
            return join;
        }
        if (auto* branch = dynamic_cast<const IfExpr*>(&expression)) {
            at = appendExpression(*branch->condition, at);
            auto yes = block(), no = block(), join = block();
            auto* literal = dynamic_cast<const BooleanLiteral*>(branch->condition.get());
            if (!literal || literal->value) flow.blocks[at].successors.push_back(yes);
            if (!literal || !literal->value) flow.blocks[at].successors.push_back(no);
            valueExits.push_back(join);
            auto yesEnd = appendBody(branch->thenBranch->statements, yes);
            auto noEnd = appendBody(branch->elseBranch->statements, no);
            valueExits.pop_back();
            if (yesEnd || noEnd) throw std::runtime_error("Value scope requires a return value");
            return join;
        }
        if (auto* comparison = dynamic_cast<const ComparisonExpr*>(&expression)) {
            if (comparison->operands.size() < 2) throw std::runtime_error("Invalid comparison expression");
            at = appendExpression(*comparison->operands[0], at);
            at = appendExpression(*comparison->operands[1], at);
            if (comparison->operands.size() == 2) return at;
            auto join = block();
            for (std::size_t i = 2; i < comparison->operands.size(); ++i) {
                auto next = block();
                flow.blocks[at].successors = {next, join};
                at = appendExpression(*comparison->operands[i], next);
            }
            flow.blocks[at].successors.push_back(join);
            return join;
        }
        if (auto* call = dynamic_cast<const CallExpr*>(&expression)) {
            if (call->object) at = appendExpression(*call->object, at);
            for (const auto& argument : call->arguments) at = appendExpression(*argument, at);
            return at;
        }
        if (auto* construct = dynamic_cast<const ConstructExpr*>(&expression)) {
            for (const auto& argument : construct->arguments) at = appendExpression(*argument, at);
            return at;
        }
        if (auto* member = dynamic_cast<const MemberExpr*>(&expression)) return appendExpression(*member->object, at);
        if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) return appendExpression(*unary->operand, at);
        if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
            if (binary->op == "=") {
                if (auto* target = dynamic_cast<const IdentifierExpr*>(binary->left.get())) {
                    at = appendExpression(*binary->right, at);
                    if (auto binding = expressions.binding(*target))
                        flow.blocks[at].operations.push_back({isReinitialization && isReinitialization(*binary)
                            ? OwnershipFlow::Action::Reinitialize : OwnershipFlow::Action::Initialize, binding, target->offset, target});
                    return at;
                }
            }
            at = appendExpression(*binary->left, at);
            if (binary->op == "&&" || binary->op == "||" || binary->op == "!&&" || binary->op == "!||") {
                auto rhs = block(), join = block();
                auto* literal = dynamic_cast<const BooleanLiteral*>(binary->left.get());
                const bool isAnd = binary->op == "&&" || binary->op == "!&&";
                if (!literal || literal->value == isAnd) flow.blocks[at].successors.push_back(rhs);
                if (!literal || literal->value != isAnd) flow.blocks[at].successors.push_back(join);
                auto end = appendExpression(*binary->right, rhs);
                flow.blocks[end].successors.push_back(join);
                return join;
            }
            return appendExpression(*binary->right, at);
        }
        expressions.append(expression, flow.blocks[at]);
        return at;
    }
    std::optional<std::size_t> appendBody(const std::vector<std::unique_ptr<ASTNode>>& nodes, std::optional<std::size_t> at) {
        for (const auto& node : nodes) {
            if (!at) break;
            if (auto* variable = dynamic_cast<const VariableDeclaration*>(node.get())) {
                if (variable->initialValue) at = appendExpression(*variable->initialValue, *at);
                if (variable->initialValue || variable->aggregateInit)
                    flow.blocks[*at].operations.push_back({OwnershipFlow::Action::Initialize, variable, variable->offset});
            } else if (auto* expression = dynamic_cast<const Expression*>(node.get())) {
                at = appendExpression(*expression, *at);
            } else if (auto* result = dynamic_cast<const ReturnStatement*>(node.get())) {
                if (result->value) at = appendExpression(*result->value, *at);
                if (!valueExits.empty()) flow.blocks[*at].successors.push_back(valueExits.back());
                at.reset();
            } else if (auto* nested = dynamic_cast<const Block*>(node.get())) {
                at = appendBody(nested->statements, at);
            } else if (auto* control = dynamic_cast<const LoopControl*>(node.get())) {
                if (loops.empty()) throw std::runtime_error("break or continue outside loop");
                flow.blocks[*at].successors.push_back(control->isBreak ? loops.back().first : loops.back().second);
                at.reset();
            } else if (auto* loop = dynamic_cast<const LoopStatement*>(node.get())) {
                if (auto* variable = dynamic_cast<const VariableDeclaration*>(loop->initializer.get())) {
                    if (variable->initialValue) at = appendExpression(*variable->initialValue, *at);
                    if (variable->initialValue || variable->aggregateInit)
                        flow.blocks[*at].operations.push_back({OwnershipFlow::Action::Initialize, variable, variable->offset});
                } else if (auto* initializer = dynamic_cast<const Expression*>(loop->initializer.get())) {
                    at = appendExpression(*initializer, *at);
                }
                auto condition = block(), body = block(), step = block(), done = block();
                flow.blocks[*at].successors.push_back(loop->kind == LoopStatement::Kind::DoWhile ? body : condition);
                auto conditionEnd = loop->condition ? appendExpression(*loop->condition, condition) : condition;
                auto* conditionLiteral = dynamic_cast<const BooleanLiteral*>(loop->condition.get());
                if (!conditionLiteral || conditionLiteral->value) flow.blocks[conditionEnd].successors.push_back(body);
                if (loop->condition && (!conditionLiteral || !conditionLiteral->value)) flow.blocks[conditionEnd].successors.push_back(done);
                loops.emplace_back(done, step);
                auto bodyEnd = appendBody(loop->body->statements, body);
                loops.pop_back();
                if (bodyEnd) flow.blocks[*bodyEnd].successors.push_back(step);
                auto stepEnd = loop->step ? appendExpression(*loop->step, step) : step;
                flow.blocks[stepEnd].successors.push_back(condition);
                at = done;
            } else if (auto* branch = dynamic_cast<const IfStatement*>(node.get())) {
                at = appendExpression(*branch->condition, *at);
                auto yes = block(), no = block();
                auto* conditionLiteral = dynamic_cast<const BooleanLiteral*>(branch->condition.get());
                if (!conditionLiteral || conditionLiteral->value) flow.blocks[*at].successors.push_back(yes);
                if (!conditionLiteral || !conditionLiteral->value) flow.blocks[*at].successors.push_back(no);
                auto yesEnd = appendBody(branch->thenBranch->statements, yes);
                auto noEnd = branch->elseBranch ? appendBody(branch->elseBranch->statements, no) : std::optional<std::size_t>(no);
                if (!yesEnd && !noEnd) { at.reset(); continue; }
                auto join = block();
                if (yesEnd) flow.blocks[*yesEnd].successors.push_back(join);
                if (noEnd) flow.blocks[*noEnd].successors.push_back(join);
                at = join;
            } else if (auto* format = dynamic_cast<const FormatStringStatement*>(node.get())) {
                for (const auto& part : format->parts)
                    if (auto* expression = dynamic_cast<const Expression*>(part.get())) at = appendExpression(*expression, *at);
            } else if (dynamic_cast<const StringStatement*>(node.get()) ||
                       dynamic_cast<const CaptureDeclaration*>(node.get())) {
                continue;
            } else if (auto* function = dynamic_cast<const FunctionDeclaration*>(node.get())) {
                for (const auto& child : function->body)
                    if (dynamic_cast<const CaptureDeclaration*>(child.get()))
                        throw std::runtime_error("Ownership analysis with captured variables is not implemented yet");
                // Without capture the nested function cannot access this scope.
                // Its own ownership graph is checked separately if it moves.
                continue;
            } else {
                throw std::runtime_error("Statement requires ownership control-flow lowering");
            }
        }
        return at;
    }
};
