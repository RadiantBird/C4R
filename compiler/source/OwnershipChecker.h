#pragma once

#include "OwnershipGraphBuilder.h"
#include "Semantic.h"

// Per-function ownership analysis consumes the bindings resolved by Semantic.
// Field references deliberately do not resolve as independently owned values.
class OwnershipChecker {
public:
    explicit OwnershipChecker(const Semantic& semantic) : semantic(semantic) {}

    std::vector<OwnershipFlow::Violation> checkFunction(const FunctionDeclaration& function) const {
        OwnershipGraphBuilder builder([&](const IdentifierExpr& identifier) -> OwnershipState::Binding {
            const auto* reference = semantic.Reference(identifier);
            return reference && reference->kind == Semantic::ResolvedReference::Kind::Variable
                ? reference->declaration : nullptr;
        }, [&](const BinaryExpr& expression) { return semantic.IsReinitialization(expression); });
        OwnershipState initial;
        for (const auto& [name, symbol] : semantic.Globals()) {
            if (symbol.kind == Semantic::Kind::GlobalVariable)
                initial.set(symbol.declaration, OwnershipState::State::Available);
        }
        return builder.buildFunction(function).validate(initial);
    }
private:
    const Semantic& semantic;
};
