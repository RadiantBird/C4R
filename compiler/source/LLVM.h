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

class LLVM {
public:
    LLVM();

    void Generate(const Program& program);
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
    struct Variable { llvm::Value* storage; llvm::Type* type; bool isMutable; size_t id; bool isReceiver = false; };
    std::unordered_map<std::string, Variable> variables;
    std::unordered_map<size_t, Variable> bindings;
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
    };
    std::unordered_map<std::string, TypeInfo> types;
    std::shared_ptr<FunctionSymbol> currentMethod;
    std::vector<std::shared_ptr<FunctionSymbol>> immutableCalls;
    Variable ResolveLValue(const Expression& expression);
    llvm::Value* EmitMethodCall(const CallExpr& call);
    void InitializeObject(llvm::Value* storage, TypeInfo& type);
    std::unordered_map<std::string, std::shared_ptr<FunctionSymbol>> functions;
    std::unordered_set<std::string> inaccessibleVariables;
    std::unordered_set<const CaptureDeclaration*> validCaptures;
    struct ValueScope {
        llvm::BasicBlock* exit;
        llvm::Type* type = nullptr;
        std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> returns;
        size_t loopDepth;
    };
    std::vector<ValueScope*> valueScopes;
    std::vector<std::pair<llvm::BasicBlock*, llvm::BasicBlock*>> loops;

    llvm::Value* EmitExpression(const Expression& expression);
    llvm::Value* EmitBinary(const std::string& op, llvm::Value* left, llvm::Value* right);
    Variable MutableVariable(const Expression& expression);
    void EmitVariable(const VariableDeclaration& declaration);
    llvm::Value* EmitValueBlock(const Block& block);
    llvm::Value* FinishValueScope(ValueScope& scope);
    llvm::Value* EmitCondition(const Expression& expression);
    llvm::Value* EmitCall(const std::string& name, const std::vector<std::unique_ptr<Expression>>& arguments);
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
