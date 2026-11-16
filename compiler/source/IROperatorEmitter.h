#pragma once

#include <llvm/IR/IRBuilder.h>

namespace c4r::ir {
class IROperatorEmitter {
public:
    explicit IROperatorEmitter(llvm::IRBuilder<>& builder) : Builder(builder) {}
    llvm::Value* Emit(const std::string& op, llvm::Value* left, llvm::Value* right);

private:
    llvm::IRBuilder<>& Builder;
    void Guard(llvm::Value* condition, llvm::BasicBlock* valid, llvm::BasicBlock* invalid);
};
}
