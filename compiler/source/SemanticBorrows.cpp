#include "SemanticBodyChecker.h"

#include <stdexcept>

SemanticBodyChecker::BorrowChecker::Origin SemanticBodyChecker::BorrowChecker::Find(
    const Expression& value, const Scope& scope
) const {
    if (auto origin = returnOrigins.find(&value); origin != returnOrigins.end()) return origin->second;
    if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
        const auto* reference = semantic.Reference(*identifier);
        if (!reference || reference->kind != ResolvedReference::Kind::Variable) return {};
        auto root = scope.borrowOrigins.find(reference->declaration);
        return {root == scope.borrowOrigins.end() ? reference->declaration : root->second, reference->declaration};
    }
    if (auto* member = dynamic_cast<const MemberExpr*>(&value)) return Find(*member->object, scope);
    return {};
}

void SemanticBodyChecker::BorrowChecker::CheckAccess(const Expression& value, const Scope& scope, bool write) const {
    CheckAccess(Find(value, scope), scope, write);
}

void SemanticBodyChecker::BorrowChecker::CheckAccess(Origin origin, const Scope& scope, bool write) const {
    if (!origin) return;
    auto loans = scope.activeBorrows.find(origin.root);
    if (loans == scope.activeBorrows.end()) return;

    auto isAncestor = [&](const void* ancestor, const void* child) {
        std::unordered_set<const void*> seen;
        while (child && seen.insert(child).second) {
            if (child == ancestor) return true;
            auto parent = scope.borrowParents.find(child);
            child = parent == scope.borrowParents.end() ? nullptr : parent->second;
        }
        return false;
    };
    for (const auto& [borrower, isMutable] : loans->second) {
        if (isAncestor(borrower, origin.binding)) continue;
        if (isMutable || write) {
            throw std::runtime_error(isMutable
                ? "Cannot access an object while it is mutably borrowed"
                : "Cannot modify an object while it is immutably borrowed");
        }
    }
}

void SemanticBodyChecker::BorrowChecker::Bind(const VariableDeclaration& variable, Scope& scope) const {
    const auto origin = Find(*variable.initialValue, scope);
    if (!origin) throw std::runtime_error("Borrow requires an addressable object: " + variable.name);
    CheckAccess(origin, scope, variable.isMutableReference);
    scope.activeBorrows[origin.root].emplace_back(&variable, variable.isMutableReference);
    scope.borrowOrigins[&variable] = origin.root;
    scope.borrowParents[&variable] = origin.binding;
}

void SemanticBodyChecker::BorrowChecker::RecordReturn(
    const FunctionDeclaration& declaration, const Arguments& arguments, const Scope& scope, const Expression& call
) {
    for (size_t i = 0; i < declaration.parameters.size(); ++i) {
        const auto& parameter = declaration.parameters[i];
        if (!parameter.isReference || parameter.lifetime != declaration.returnLifetime) continue;
        const auto origin = Find(*arguments[i], scope);
        if (origin) returnOrigins[&call] = origin;
        return;
    }
    throw std::runtime_error("Reference return origin could not be resolved");
}

const void* SemanticBodyChecker::BorrowChecker::PointerOrigin(const Expression& value, const Scope& scope) const {
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&value); unary && unary->op == "&")
        return Find(*unary->operand, scope).root;
    if (auto* move = dynamic_cast<const MoveExpr*>(&value)) return PointerOrigin(*move->operand, scope);
    if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
        const auto* reference = semantic.Reference(*identifier);
        if (!reference || reference->kind != ResolvedReference::Kind::Variable) return nullptr;
        auto found = scope.pointerOrigins.find(reference->declaration);
        return found == scope.pointerOrigins.end() ? nullptr : found->second;
    }
    const auto origin = returnOrigins.find(&value);
    return origin == returnOrigins.end() ? nullptr : origin->second.root;
}

bool SemanticBodyChecker::BorrowChecker::PointerMayReferenceLocal(const Expression& value, const Scope& scope) const {
    if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
        const auto* reference = semantic.Reference(*identifier);
        return reference && unsafePointerVariables.contains(reference->declaration);
    }
    if (auto* move = dynamic_cast<const MoveExpr*>(&value)) return PointerMayReferenceLocal(*move->operand, scope);
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&value); unary && unary->op == "&") {
        const void* origin = Find(*unary->operand, scope).root;
        if (!origin) return true;
        for (const auto& [name, symbol] : semantic.Globals()) {
            if (symbol.kind == Kind::GlobalVariable && symbol.declaration == origin) return false;
        }
        if (scope.function) {
            for (const auto& parameter : scope.function->parameters) {
                if (&parameter == origin) return !parameter.isReference;
            }
        }
        return true;
    }
    return dynamic_cast<const IfExpr*>(&value) || dynamic_cast<const BlockExpr*>(&value) ||
        dynamic_cast<const CallExpr*>(&value) || dynamic_cast<const MemberExpr*>(&value) ||
        dynamic_cast<const BinaryExpr*>(&value);
}

void SemanticBodyChecker::BorrowChecker::TrackPointer(const void* binding, const Expression& value, Scope& scope) {
    scope.pointerOrigins[binding] = PointerOrigin(value, scope);
    if (PointerMayReferenceLocal(value, scope)) unsafePointerVariables.insert(binding);
}

void SemanticBodyChecker::BindReference(const VariableDeclaration& variable, Scope& scope) {
    if (!variable.initialValue) throw std::runtime_error("Reference variable requires an initializer: " + variable.name);
    const auto mutableSource = MutableValue(*variable.initialValue, scope);
    if (!mutableSource) throw std::runtime_error("Borrow requires an addressable object: " + variable.name);
    if (variable.isMutableReference && !*mutableSource)
        throw std::runtime_error("Mutable borrow requires a mutable object: " + variable.name);
    borrows.Bind(variable, scope);
}

void SemanticBodyChecker::RecordReturnOrigin(
    const FunctionDeclaration& declaration, const Arguments& arguments, Scope& scope, const Expression& call
) {
    if (!declaration.returnsReference) return;
    semantic.referenceTargets[&call] = &declaration;
    borrows.RecordReturn(declaration, arguments, scope, call);
}

void SemanticBodyChecker::CheckReferenceArguments(
    const FunctionDeclaration& declaration, const Arguments& arguments, Scope& scope, const std::string& name
) {
    std::vector<std::pair<const void*, bool>> active;
    for (size_t i = 0; i < declaration.parameters.size(); ++i) {
        const auto& parameter = declaration.parameters[i];
        const auto& argument = arguments[i];
        if (!parameter.isReference) {
            if (parameter.isConsumable && !IsConsumableValue(argument.get()))
                throw std::runtime_error("Consumable argument requires direct construction or explicit move: " + name);
            if (semantic.NeedsDestruction(parameter.type) && !IsDirectValue(argument.get()))
                throw std::runtime_error("Destructible value argument requires direct construction: " + name);
            continue;
        }
        const auto mutableArgument = MutableValue(*argument, scope);
        if (!mutableArgument) throw std::runtime_error("Reference argument requires an addressable object: " + name);
        if (parameter.isMutableReference && !*mutableArgument)
            throw std::runtime_error("Mutable reference requires a mutable object: " + name);
        const auto origin = borrows.Find(*argument, scope);
        if (!origin) throw std::runtime_error("Reference argument requires an addressable object: " + name);
        borrows.CheckAccess(origin, scope, parameter.isMutableReference);
        for (const auto& [previousRoot, previousMutable] : active) {
            if (origin.root == previousRoot && (parameter.isMutableReference || previousMutable))
                throw std::runtime_error("Conflicting borrows of the same object in call: " + name);
        }
        active.emplace_back(origin.root, parameter.isMutableReference);
    }
}
