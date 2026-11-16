#pragma once

#include <cstdint>
#include <unordered_map>

// A binding may have different states on different incoming control-flow paths.
// Keep all possibilities so a later use is accepted only when every path owns it.
class OwnershipState {
public:
    enum class State : std::uint8_t { Uninitialized = 1, Available = 2, Moved = 4 };
    using Binding = const void*;
    using Mask = std::uint8_t;

    void set(Binding binding, State state) { states[binding] = static_cast<Mask>(state); }
    Mask possibilities(Binding binding) const {
        auto found = states.find(binding);
        return found == states.end() ? static_cast<Mask>(State::Uninitialized) : found->second;
    }
    bool definitelyAvailable(Binding binding) const {
        return possibilities(binding) == static_cast<Mask>(State::Available);
    }
    void forget(Binding binding) { states.erase(binding); }
    // Return whether the data-flow fact changed, for loop fixed-point analysis.
    bool merge(const OwnershipState& other) {
        bool changed = false;
        for (auto& [binding, mask] : states) {
            const Mask joined = mask | other.possibilities(binding);
            changed |= joined != mask;
            mask = joined;
        }
        for (const auto& [binding, mask] : other.states) {
            if (!states.contains(binding)) {
                const Mask joined = mask | static_cast<Mask>(State::Uninitialized);
                if (joined != static_cast<Mask>(State::Uninitialized)) {
                    states.emplace(binding, joined);
                    changed = true;
                }
            }
        }
        return changed;
    }

private:
    std::unordered_map<Binding, Mask> states;
};
