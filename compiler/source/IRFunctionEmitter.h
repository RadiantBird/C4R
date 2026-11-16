#pragma once

#include "IRModuleEmitter.h"
#include "IROperatorEmitter.h"

namespace c4r::ir {
class IRFunctionEmitter {
public:
    explicit IRFunctionEmitter(IRModuleEmitter& module);

    void DeclareGlobalFunction(const FunctionDeclaration& declaration);
    void RegisterGlobal(const VariableDeclaration& declaration, llvm::Value* storage, llvm::Type* type);
    void EmitGlobalFunction(const std::string& name);
    void EmitFunctionBody(const std::shared_ptr<FunctionSymbol>& symbol);
    void EmitEntryPoint(const Program& program, const std::string& name);
    void CheckMethodMutability();

private:
    IRModuleEmitter& module;
    llvm::IRBuilder<> Builder;
    IROperatorEmitter operators;
    const Semantic* semanticAnalysis;
    std::unordered_map<std::string, Variable> variables;
    std::unordered_map<std::string, Variable> globalVariables;
    std::unordered_map<size_t, Variable> bindings;
    std::unordered_map<const void*, size_t> bindingDeclarations;
    size_t nextVariableId = 0;

    llvm::FunctionCallee Printf();
    void EmitFormat(const FormatStringStatement& statement, llvm::FunctionCallee printfFunction);
    void EmitReturn(const ReturnStatement& statement);
    void EmitIf(const IfStatement& branch);
    void EmitLoop(const LoopStatement& loop);

    llvm::Value* EmitMemberValue(const MemberExpr& member);
    llvm::Value* EmitIdentifierValue(const IdentifierExpr& identifier);
    llvm::Value* EmitUnary(const UnaryExpr& unary);
    llvm::Value* EmitComparison(const ComparisonExpr& chain);
    llvm::Value* EmitBinaryExpression(const BinaryExpr& binary);

    std::shared_ptr<FunctionSymbol> currentMethod;
    llvm::Value* currentReturnStorage = nullptr;
    bool currentHasMoves = false;
    std::unordered_map<llvm::Value*, Variable> temporaryValues;
    std::vector<std::shared_ptr<FunctionSymbol>> immutableCalls;
    Variable ResolveLValue(const Expression& expression);
    llvm::Value* EmitMethodCall(const CallExpr& call, llvm::Value* destination = nullptr);
    llvm::Value* EmitBareCall(const IdentifierExpr& expression, llvm::Value* destination = nullptr);
    llvm::Value* EmitBareMemberCall(const MemberExpr& expression, llvm::Value* destination = nullptr);
    void InitializeObject(llvm::Value* storage, TypeInfo& type);
    void EmitConstruction(llvm::Value* storage, const ConstructExpr& expression);
    llvm::Value* EmitMove(const MoveExpr& expression);
    llvm::Value* EmitDirectValue(const Expression& expression, llvm::Value* destination, TypeInfo& type);
    void EmitArguments(const std::vector<std::unique_ptr<Expression>>& arguments,
        const std::vector<FunctionDeclaration::Parameter>& parameters, std::vector<llvm::Value*>& values);
    std::unordered_map<std::string, std::shared_ptr<FunctionSymbol>> functions;
    std::unordered_map<const FunctionDeclaration*, std::shared_ptr<FunctionSymbol>> functionDeclarations;
    std::unordered_set<std::string> inaccessibleVariables;
    std::unordered_set<const CaptureDeclaration*> validCaptures;
    struct ValueScope {
        llvm::BasicBlock* exit;
        llvm::Type* type = nullptr;
        std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> returns;
        size_t loopDepth;
        size_t cleanupDepth;
        llvm::Value* objectStorage = nullptr;
        llvm::AllocaInst* objectActive = nullptr;
        TypeInfo* objectType = nullptr;
        size_t temporaryFrame = 0;
    };
    std::vector<ValueScope*> valueScopes;
    std::vector<std::pair<llvm::BasicBlock*, llvm::BasicBlock*>> loops;
    struct Cleanup { llvm::Value* storage; TypeInfo* type; llvm::AllocaInst* active = nullptr; };
    struct TemporaryCleanup { llvm::Value* storage; TypeInfo* type; llvm::AllocaInst* active; };
    struct TemporaryFrame { llvm::Instruction* marker; std::vector<TemporaryCleanup> cleanups; };
    std::vector<std::vector<Cleanup>> cleanupScopes;
    std::vector<TemporaryFrame> temporaryFrames;
    std::vector<size_t> loopCleanupDepths;
    void EmitDestruction(llvm::Value* storage, TypeInfo& type);
    void EmitCleanups(size_t fromDepth);
    void BeginFullExpression();
    void EndFullExpression();
    llvm::Value* EmitFullExpression(const Expression& expression);
    Variable MaterializeTemporary(const Expression& expression);
    llvm::Value* EmitFunctionCall(const std::shared_ptr<FunctionSymbol>& symbol,
        std::vector<llvm::Value*> values, llvm::Value* destination);
    llvm::Value* EmitCallExpression(const CallExpr& call, llvm::Value* destination = nullptr);
    Variable ResolveMember(const Variable& object, const std::string& name);

    llvm::Value* EmitExpression(const Expression& expression);
    Variable MutableVariable(const Expression& expression);
    void EmitVariable(const VariableDeclaration& declaration);
    llvm::Value* EmitValueBlock(const Block& block, llvm::Value* destination = nullptr, TypeInfo* type = nullptr);
    llvm::Value* EmitIfValue(const IfExpr& expression, llvm::Value* destination = nullptr, TypeInfo* type = nullptr);
    llvm::Value* FinishValueScope(ValueScope& scope);
    llvm::Value* EmitCondition(const Expression& expression, bool fullExpression = true);
    llvm::Value* EmitCall(const std::string& name, const std::vector<std::unique_ptr<Expression>>& arguments,
        const FunctionDeclaration* target = nullptr, llvm::Value* destination = nullptr);
    std::shared_ptr<FunctionSymbol> DeclareFunction(const FunctionDeclaration& declaration, bool local);


    llvm::Value* CreateString(const std::string& value);

    void EmitString(const StringStatement& statement);

    void EmitBody(
        const std::vector<std::unique_ptr<ASTNode>>& body
    );

    void EmitImplicitMain(const Program& program);
};
}
