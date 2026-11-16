#pragma once

#include "OwnershipState.h"
#include "AST.h"
#include <deque>
#include <optional>
#include <vector>

// A small forward data-flow engine. A missing entry state means unreachable;
// it is distinct from a reachable state with an uninitialized binding.
class OwnershipFlow {
public:
    struct Effect { OwnershipState::Binding binding; OwnershipState::State state; };
    enum class Action { Initialize, Read, Move, Reinitialize };
    struct Operation { Action action; OwnershipState::Binding binding; std::size_t offset; const ASTNode* site = nullptr; };
    struct Block { std::vector<Effect> effects; std::vector<std::size_t> successors; std::vector<Operation> operations; };
    struct Violation { std::size_t block; std::size_t offset; OwnershipState::Binding binding; const ASTNode* site; Action action; };
    std::vector<Block> blocks;

    std::vector<std::optional<OwnershipState>> solve(const OwnershipState& initial) const {
        std::vector<std::optional<OwnershipState>> incoming(blocks.size());
        if (blocks.empty()) return incoming;
        incoming[0] = initial;
        std::deque<std::size_t> pending{0};
        while (!pending.empty()) {
            const auto index = pending.front(); pending.pop_front();
            auto output = *incoming.at(index);
            for (const auto& effect : blocks.at(index).effects) output.set(effect.binding, effect.state);
            for (const auto& op : blocks.at(index).operations) {
                if (op.action == Action::Initialize || op.action == Action::Reinitialize) output.set(op.binding, OwnershipState::State::Available);
                else if (op.action == Action::Move) output.set(op.binding, OwnershipState::State::Moved);
            }
            for (auto successor : blocks.at(index).successors) {
                auto& target = incoming.at(successor);
                if (!target) { target = output; pending.push_back(successor); }
                else if (target->merge(output)) pending.push_back(successor);
            }
        }
        return incoming;
    }

    std::vector<Violation> validate(const OwnershipState& initial) const {
        const auto incoming = solve(initial);
        std::vector<Violation> violations;
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            if (!incoming[index]) continue;
            auto state = *incoming[index];
            for (const auto& effect : blocks[index].effects) state.set(effect.binding, effect.state);
            for (const auto& op : blocks[index].operations) {
                if ((op.action == Action::Reinitialize && state.possibilities(op.binding) != static_cast<OwnershipState::Mask>(OwnershipState::State::Moved)) ||
                    ((op.action == Action::Read || op.action == Action::Move) && !state.definitelyAvailable(op.binding)))
                    violations.push_back({index, op.offset, op.binding, op.site, op.action});
                if (op.action == Action::Initialize || op.action == Action::Reinitialize) state.set(op.binding, OwnershipState::State::Available);
                else if (op.action == Action::Move) state.set(op.binding, OwnershipState::State::Moved);
            }
        }
        return violations;
    }
};
