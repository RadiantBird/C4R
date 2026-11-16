#pragma once

#include "Semantic.h"

class SemanticBodyChecker {
public:
    explicit SemanticBodyChecker(const Semantic& semantic) : semantic(semantic), borrows(semantic) {}
    void Check(const Program& program);

private:
    using Kind = Semantic::Kind;
    using Symbol = Semantic::Symbol;
    using ResolvedReference = Semantic::ResolvedReference;
    using Statements = std::vector<std::unique_ptr<ASTNode>>;
    using Arguments = std::vector<std::unique_ptr<Expression>>;

    const Semantic& semantic;
    struct Scope {
        std::unordered_map<std::string, std::string> variables;
        std::unordered_map<std::string, bool> mutableVariables;
        std::unordered_map<std::string, bool> receiverVariables;
        std::unordered_map<std::string, size_t> bindingIds;
        std::unordered_set<size_t> availableBindings;
        std::unordered_map<const void*, const void*> borrowOrigins;
        std::unordered_map<const void*, const void*> borrowParents;
        std::unordered_map<const void*, std::vector<std::pair<const void*, bool>>> activeBorrows;
        std::unordered_map<const void*, const void*> pointerOrigins;
        std::unordered_set<std::string> localFunctions;
        std::unordered_map<std::string, const FunctionDeclaration*> localSignatures;
        std::unordered_set<std::string> shadowedMethods;
        std::unordered_set<std::string> globalBindings;
        std::unordered_set<std::string> inaccessible;
        std::string owner;
        std::string returnType;
        bool valueScope = false;
        bool allowCapture = false;
        bool mayEvaluate = true;
        bool directConstruction = false;
        std::string* valueType = nullptr;
        bool* valueSawReturn = nullptr;
        bool* valueUnknown = nullptr;
        size_t loopDepth = 0;
        size_t valueLoopDepth = 0;
        const FunctionDeclaration* method = nullptr;
        const FunctionDeclaration* function = nullptr;
    };

    class BorrowChecker {
    public:
        struct Origin {
            const void* root = nullptr;
            const void* binding = nullptr;

            explicit operator bool() const { return root && binding; }
        };

        explicit BorrowChecker(const Semantic& semantic) : semantic(semantic) {}
        Origin Find(const Expression& value, const Scope& scope) const;
        void CheckAccess(const Expression& value, const Scope& scope, bool write) const;
        void CheckAccess(Origin origin, const Scope& scope, bool write) const;
        void Bind(const VariableDeclaration& variable, Scope& scope) const;
        void RecordReturn(const FunctionDeclaration& declaration, const Arguments& arguments,
            const Scope& scope, const Expression& call);
        const void* PointerOrigin(const Expression& value, const Scope& scope) const;
        bool PointerMayReferenceLocal(const Expression& value, const Scope& scope) const;
        void TrackPointer(const void* binding, const Expression& value, Scope& scope);

    private:
        const Semantic& semantic;
        std::unordered_map<const Expression*, Origin> returnOrigins;
        std::unordered_set<const void*> unsafePointerVariables;
    };

    BorrowChecker borrows;
    std::unordered_map<const FunctionDeclaration*, bool> mutates;
    std::unordered_map<const FunctionDeclaration*, std::vector<std::pair<std::string, size_t>>> capturedBindings;
    size_t nextBindingId = 0;
    std::unordered_map<size_t, const void*> bindingOrigins;
    std::unordered_set<size_t> globalBindingIds;
    std::unordered_map<std::string, size_t> globalIdsByName;
    std::unordered_map<const FunctionDeclaration*, std::vector<const FunctionDeclaration*>> calls;
    struct DeferredCall { const FunctionDeclaration* method; size_t offset; std::string file; };
    std::vector<DeferredCall> immutableCalls;

    void CheckStatements(const Statements& nodes, Scope scope);
    void CheckMethodMutability();
    std::string CheckExpression(const Expression& value, Scope& scope);
    std::string CheckCallResult(const FunctionDeclaration& declaration, const Scope& scope);
    bool IsDirectValue(const Expression* value) const;
    bool IsConsumableValue(const Expression* value) const;
    void RequireAssignableFields(const std::string& type, const std::string& owner);
    void RequireDestructionAccess(const std::string& type, const std::string& owner);
    bool Terminates(const Statements& nodes);
    void CheckFunction(const FunctionDeclaration& declaration, Scope scope, bool local);
    std::optional<bool> MutableValue(const Expression& value, Scope& scope);
    bool IsReceiver(const Expression& value, const Scope& scope);
    void BindReference(const VariableDeclaration& variable, Scope& scope);
    void RecordReturnOrigin(const FunctionDeclaration& declaration, const Arguments& arguments, Scope& scope, const Expression& call);
    void CheckReferenceArguments(const FunctionDeclaration& declaration, const Arguments& arguments, Scope& scope, const std::string& name);
    void CheckVariable(const VariableDeclaration* variable, Scope& scope, std::unordered_set<std::string>& declared);
    void CheckLocalFunction(const FunctionDeclaration* local, Scope& scope, std::unordered_set<std::string>& declared);
    void CheckFormat(const FormatStringStatement* format, Scope& scope);
    void CheckReturn(const ReturnStatement* result, Scope& scope);
    void CheckIf(const IfStatement* branch, Scope& scope);
    void CheckLoop(const LoopStatement* loop, const Scope& scope);
    std::string CheckMove(const MoveExpr* move, Scope& scope);
    std::string CheckConstruction(const ConstructExpr* construct, Scope& scope);
    std::string CheckIdentifier(const IdentifierExpr* identifier, Scope& scope);
    std::string CheckCall(const CallExpr* call, Scope& scope);
    std::string CheckMember(const MemberExpr* member, Scope& scope);
    std::string CheckUnary(const UnaryExpr* unary, Scope& scope);
    std::string CheckBinary(const BinaryExpr* binary, Scope& scope);
    std::string CheckComparison(const ComparisonExpr* comparison, Scope& scope);
    std::string CheckBlockValue(const BlockExpr* block, Scope& scope);
    std::string CheckIfValue(const IfExpr* branch, Scope& scope);
};
