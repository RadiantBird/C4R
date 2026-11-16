#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

class Semantic;
struct Program;

class LLVM {
public:
    LLVM();

    void Generate(const Program& program, const Semantic& semantic);
    void Print() const;
    size_t DiagnosticOffset() const { return diagnosticOffset; }
    llvm::Module& GetModule() { return Module; }

private:
    llvm::LLVMContext Context;
    llvm::Module Module;
    size_t diagnosticOffset = 0;
};
