#pragma once

#include "AST.h"
#include <string>
#include <optional>
#include <unordered_map>
#include <unordered_set>

// The compilation unit owns the AST; symbol records refer to those declarations.
class Semantic {
public:
    enum class Kind { Type, Function, GlobalVariable, Field, Method, Constructor, Destructor };
    struct Symbol {
        Kind kind;
        const ASTNode* declaration;
        std::string type;
        std::string owner;
        bool isPublic;
    };
    struct ResolvedReference {
        enum class Kind { Variable, Field } kind;
        const void* declaration;
    };
    struct SpecialMembers {
        std::vector<Symbol> constructors;
        std::optional<Symbol> destructor;
    };
    void Collect(const Program& program, bool requireExplicitEntry = false);
    void CheckBodies(const Program& program) const;
    const std::unordered_map<std::string, Symbol>& Globals() const { return globals; }
    const std::unordered_map<std::string, Symbol>& Members(const std::string& owner) const { return members.at(owner); }
    const std::string& EntryPoint() const { return entryPoint; }
    const SpecialMembers& Lifecycle(const std::string& owner) const { return specialMembers.at(owner); }
    bool NeedsDestruction(const std::string& type) const;
    const std::unordered_set<const FunctionDeclaration*>& FunctionsWithMoves() const { return functionsWithMoves; }
    bool IsReinitialization(const BinaryExpr& expression) const { return reinitializations.contains(&expression); }
    const std::string& ReturnValueType(const ReturnStatement& statement) const { return returnValueTypes.at(&statement); }
    bool IsReferenceReturn(const ReturnStatement& statement) const { return referenceReturns.contains(&statement); }
    const ResolvedReference* Reference(const IdentifierExpr& expression) const {
        auto found = references.find(&expression);
        return found == references.end() ? nullptr : &found->second;
    }
    const FunctionDeclaration* CallTarget(const CallExpr& expression) const {
        auto found = resolvedCalls.find(&expression);
        return found == resolvedCalls.end() ? nullptr : found->second;
    }
    const FunctionDeclaration* CallTarget(const IdentifierExpr& expression) const {
        auto found = bareCalls.find(&expression);
        return found == bareCalls.end() ? nullptr : found->second;
    }
    const FunctionDeclaration* CallTarget(const MemberExpr& expression) const {
        auto found = bareMemberCalls.find(&expression);
        return found == bareMemberCalls.end() ? nullptr : found->second;
    }
    const FunctionDeclaration* ReferenceTarget(const Expression& expression) const {
        auto found = referenceTargets.find(&expression);
        if (found != referenceTargets.end()) return found->second;
        if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression)) return CallTarget(*identifier);
        if (auto* member = dynamic_cast<const MemberExpr*>(&expression)) return CallTarget(*member);
        return nullptr;
    }
    const ConstructorDeclaration* ConstructorTarget(const ConstructExpr& expression) const {
        auto found = constructorCalls.find(&expression);
        return found == constructorCalls.end() ? nullptr : found->second;
    }
    const ConstructorDeclaration* ConstructorTarget(const VariableDeclaration& declaration) const {
        auto found = defaultConstructors.find(&declaration);
        return found == defaultConstructors.end() ? nullptr : found->second;
    }
    const ConstructorDeclaration* SelectDefaultConstructor(const std::string& type, const std::string& owner) const;
    size_t DiagnosticOffset() const { return diagnosticOffset; }
    const std::string& DiagnosticFile() const { return diagnosticFile; }
private:
    mutable size_t diagnosticOffset = 0;
    mutable std::string diagnosticFile;
    std::string entryPoint;
    std::unordered_map<std::string, Symbol> globals;
    std::unordered_map<std::string, std::unordered_map<std::string, Symbol>> members;
    std::unordered_map<std::string, SpecialMembers> specialMembers;
    mutable std::unordered_map<const IdentifierExpr*, ResolvedReference> references;
    mutable std::unordered_map<const CallExpr*, const FunctionDeclaration*> resolvedCalls;
    mutable std::unordered_map<const IdentifierExpr*, const FunctionDeclaration*> bareCalls;
    mutable std::unordered_map<const MemberExpr*, const FunctionDeclaration*> bareMemberCalls;
    mutable std::unordered_map<const Expression*, const FunctionDeclaration*> referenceTargets;
    mutable std::unordered_map<const ConstructExpr*, const ConstructorDeclaration*> constructorCalls;
    mutable std::unordered_map<const VariableDeclaration*, const ConstructorDeclaration*> defaultConstructors;
    mutable std::unordered_map<const ReturnStatement*, std::string> returnValueTypes;
    mutable std::unordered_set<const ReturnStatement*> referenceReturns;
    mutable std::unordered_map<std::string, bool> destructionCache;
    mutable std::unordered_set<const FunctionDeclaration*> functionsWithMoves;
    mutable std::unordered_set<const BinaryExpr*> reinitializations;
};
