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
    void WriteObject(const std::filesystem::path& path);
    void LinkExecutable(
        const std::filesystem::path& objectPath,
        const std::filesystem::path& executablePath
    );

private:
    llvm::LLVMContext Context;
    llvm::Module Module;
    llvm::IRBuilder<> Builder;
    struct Variable { llvm::AllocaInst* storage; llvm::Type* type; bool isMutable; };
    std::unordered_map<std::string, Variable> variables;
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
    Variable& MutableVariable(const Expression& expression);
    void EmitVariable(const VariableDeclaration& declaration);
    llvm::Value* EmitValueBlock(const Block& block);
    llvm::Value* FinishValueScope(ValueScope& scope);
    llvm::Value* EmitCondition(const Expression& expression);

    llvm::Type* GetType(const std::string& name);

    llvm::Value* CreateString(const std::string& value);

    void EmitString(const StringStatement& statement);

    void EmitBody(
        const std::vector<std::unique_ptr<ASTNode>>& body
    );

    void EmitFunction(const FunctionDeclaration& declaration);

    void EmitImplicitMain(const Program& program);
};
