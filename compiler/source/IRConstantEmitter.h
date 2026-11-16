#pragma once

#include "AST.h"
#include "IROperatorEmitter.h"

namespace c4r::ir {
class IRConstantEmitter {
public:
    explicit IRConstantEmitter(llvm::LLVMContext& context) : Builder(context), operators(Builder) {}
    llvm::Constant* Emit(const Expression& expression);

private:
    llvm::IRBuilder<> Builder;
    IROperatorEmitter operators;
};
}
