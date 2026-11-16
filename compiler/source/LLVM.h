#pragma once

#include "AST.h"
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Verifier.h>

class Semantic;

class LLVM {
public:
    LLVM();

    void Generate(const Program& program, const Semantic& semantic);
    void Print();
    size_t DiagnosticOffset() const { return diagnosticOffset; }
    void WriteObject(const std::filesystem::path& path);
    void LinkExecutable(
        const std::filesystem::path& objectPath,
        const std::filesystem::path& executablePath
    );

private:
    llvm::LLVMContext Context;
    llvm::Module Module;
    llvm::IRBuilder<> Builder;
    size_t diagnosticOffset = 0;
    struct Variable { llvm::Value* storage; llvm::Type* type; bool isMutable; size_t id; bool isReceiver = false; llvm::AllocaInst* ownershipActive = nullptr; };
    std::unordered_map<std::string, Variable> variables;
    std::unordered_map<std::string, Variable> globalVariables;
    std::unordered_map<size_t, Variable> bindings;
    const Semantic* semanticAnalysis = nullptr;
    std::unordered_map<const void*, size_t> bindingDeclarations;
    size_t nextVariableId = 0;
    size_t nextFunctionId = 0;
    struct Capture { std::string name; Variable variable; };
    struct FunctionSymbol {
        llvm::Function* function;
        const FunctionDeclaration* declaration;
        std::vector<Capture> captures;
        std::string owner;
        bool mutates = false;
        std::vector<std::shared_ptr<FunctionSymbol>> calls;
    };
    struct FieldInfo { const VariableDeclaration* declaration; bool isPublic; unsigned index; };
    struct TypeInfo {
        const TypeDeclaration* declaration;
        llvm::StructType* type;
        std::unordered_map<std::string, FieldInfo> fields;
        std::unordered_map<std::string, std::pair<std::shared_ptr<FunctionSymbol>, bool>> methods;
        std::unordered_map<const ConstructorDeclaration*, std::shared_ptr<FunctionSymbol>> constructors;
        std::shared_ptr<FunctionSymbol> destructor;
    };
    std::unordered_map<std::string, TypeInfo> types;
    std::unordered_map<llvm::Type*, TypeInfo*> typesByLayout;
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
    llvm::Constant* ConstantInitializer(const Expression& expression);
    llvm::Constant* ConstantObject(TypeInfo& type);
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
    llvm::Value* EmitBinary(const std::string& op, llvm::Value* left, llvm::Value* right);
    Variable MutableVariable(const Expression& expression);
    void EmitVariable(const VariableDeclaration& declaration);
    llvm::Value* EmitValueBlock(const Block& block, llvm::Value* destination = nullptr, TypeInfo* type = nullptr);
    llvm::Value* EmitIfValue(const IfExpr& expression, llvm::Value* destination = nullptr, TypeInfo* type = nullptr);
    llvm::Value* FinishValueScope(ValueScope& scope);
    llvm::Value* EmitCondition(const Expression& expression, bool fullExpression = true);
    llvm::Value* EmitCall(const std::string& name, const std::vector<std::unique_ptr<Expression>>& arguments,
        const FunctionDeclaration* target = nullptr, llvm::Value* destination = nullptr);
    std::shared_ptr<FunctionSymbol> DeclareFunction(const FunctionDeclaration& declaration, bool local);
    void EmitFunctionBody(const std::shared_ptr<FunctionSymbol>& symbol);

    llvm::Type* GetType(const std::string& name);

    llvm::Value* CreateString(const std::string& value);

    void EmitString(const StringStatement& statement);

    void EmitBody(
        const std::vector<std::unique_ptr<ASTNode>>& body
    );

    void EmitImplicitMain(const Program& program);
};
