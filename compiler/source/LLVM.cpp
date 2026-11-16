#include "LLVM.h"

#include "IRModuleEmitter.h"

#include <llvm/Support/raw_ostream.h>

LLVM::LLVM() : Module("C4R", Context) {}

void LLVM::Generate(const Program& program, const Semantic& semantic) {
    c4r::ir::IRModuleEmitter(Module, semantic, diagnosticOffset).Emit(program);
}

void LLVM::Print() const {
    Module.print(llvm::outs(), nullptr);
}
