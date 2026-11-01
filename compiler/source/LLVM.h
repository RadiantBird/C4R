#pragma once

#include "AST.h"
#include <filesystem>

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

    llvm::Type* GetType(const std::string& name);

    llvm::Value* CreateString(const std::string& value);

    void EmitString(const StringStatement& statement);

    void EmitBody(
        const std::vector<std::unique_ptr<ASTNode>>& body
    );

    void EmitFunction(const FunctionDeclaration& declaration);

    void EmitImplicitMain(const Program& program);
};