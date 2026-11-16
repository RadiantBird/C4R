#include "IROperatorEmitter.h"

#include <llvm/IR/Intrinsics.h>
#include <stdexcept>

namespace c4r::ir {

void IROperatorEmitter::Guard(llvm::Value* condition, llvm::BasicBlock* valid, llvm::BasicBlock* invalid) {
    Builder.CreateCondBr(condition, valid, invalid);
    Builder.SetInsertPoint(invalid);
    Builder.CreateCall(llvm::Intrinsic::getOrInsertDeclaration(invalid->getModule(), llvm::Intrinsic::trap));
    Builder.CreateUnreachable();
    Builder.SetInsertPoint(valid);
}

llvm::Value* IROperatorEmitter::Emit(const std::string& op, llvm::Value* left, llvm::Value* right) {
    if (left->getType()->isVoidTy() || right->getType()->isVoidTy()) throw std::runtime_error("Void expression cannot be used as an operand");
    if (left->getType() != right->getType()) throw std::runtime_error("Operand types do not match for " + op);
    if (left->getType()->isFloatTy()) {
        if (op == "+") return Builder.CreateFAdd(left, right);
        if (op == "-") return Builder.CreateFSub(left, right);
        if (op == "*") return Builder.CreateFMul(left, right);
        if (op == "/") return Builder.CreateFDiv(left, right);
        if (op == "==") return Builder.CreateFCmpOEQ(left, right);
        if (op == "!=") return Builder.CreateFCmpUNE(left, right);
        if (op == "<") return Builder.CreateFCmpOLT(left, right);
        if (op == "<=") return Builder.CreateFCmpOLE(left, right);
        if (op == ">") return Builder.CreateFCmpOGT(left, right);
        if (op == ">=") return Builder.CreateFCmpOGE(left, right);
        throw std::runtime_error("Unsupported float operator: " + op);
    }
    if (!left->getType()->isIntegerTy()) throw std::runtime_error("Unsupported operand type for " + op);
    if (op == "==") return Builder.CreateICmpEQ(left, right);
    if (op == "!=") return Builder.CreateICmpNE(left, right);
    if (op == "^^") {
        if (!left->getType()->isIntegerTy(1)) throw std::runtime_error("Logical operator requires bool operands");
        return Builder.CreateXor(left, right);
    }
    if (!left->getType()->isIntegerTy(32)) throw std::runtime_error("Operator requires int32 operands: " + op);
    if (op == "+") return Builder.CreateAdd(left, right);
    if (op == "-") return Builder.CreateSub(left, right);
    if (op == "*") return Builder.CreateMul(left, right);
    if (op == "/" || op == "%") {
        auto* divisor = llvm::dyn_cast<llvm::ConstantInt>(right);
        auto* dividend = llvm::dyn_cast<llvm::ConstantInt>(left);
        bool needsGuard = !divisor || divisor->isZero() ||
            (divisor->isMinusOne() && (!dividend || dividend->getValue().isMinSignedValue()));
        if (needsGuard && Builder.GetInsertBlock()) {
            auto* fn = Builder.GetInsertBlock()->getParent();
            auto* valid = llvm::BasicBlock::Create(Builder.getContext(), "division.valid", fn);
            auto* invalid = llvm::BasicBlock::Create(Builder.getContext(), "division.invalid", fn);
            auto* nonzero = Builder.CreateICmpNE(right, Builder.getInt32(0));
            auto* minimum = Builder.CreateICmpEQ(left, Builder.getInt32(static_cast<uint32_t>(INT32_MIN)));
            auto* minusOne = Builder.CreateICmpEQ(right, Builder.getInt32(UINT32_MAX));
            auto* overflow = Builder.CreateAnd(minimum, minusOne);
            Guard(Builder.CreateAnd(nonzero, Builder.CreateNot(overflow)), valid, invalid);
        }
        return op == "/" ? Builder.CreateSDiv(left, right) : Builder.CreateSRem(left, right);
    }
    if (op == "&") return Builder.CreateAnd(left, right);
    if (op == "|") return Builder.CreateOr(left, right);
    if (op == "^") return Builder.CreateXor(left, right);
    if (op == "<<" || op == ">>") {
        auto* count = llvm::dyn_cast<llvm::ConstantInt>(right);
        if ((!count || count->getValue().uge(32)) && Builder.GetInsertBlock()) {
            auto* fn = Builder.GetInsertBlock()->getParent();
            auto* valid = llvm::BasicBlock::Create(Builder.getContext(), "shift.valid", fn);
            auto* invalid = llvm::BasicBlock::Create(Builder.getContext(), "shift.invalid", fn);
            Guard(Builder.CreateICmpULT(right, Builder.getInt32(32)), valid, invalid);
        }
        return op == "<<" ? Builder.CreateShl(left, right) : Builder.CreateAShr(left, right);
    }
    if (op == "<") return Builder.CreateICmpSLT(left, right);
    if (op == "<=") return Builder.CreateICmpSLE(left, right);
    if (op == ">") return Builder.CreateICmpSGT(left, right);
    if (op == ">=") return Builder.CreateICmpSGE(left, right);
    if (op == "`") {
        if (auto* exponent = llvm::dyn_cast<llvm::ConstantInt>(right); exponent && exponent->isNegative())
            throw std::runtime_error("Integer exponent must be nonnegative");
        auto* fn = Builder.GetInsertBlock()->getParent();
        auto* valid = llvm::BasicBlock::Create(Builder.getContext(), "pow.valid", fn);
        auto* invalid = llvm::BasicBlock::Create(Builder.getContext(), "pow.invalid", fn);
        auto* loop = llvm::BasicBlock::Create(Builder.getContext(), "pow.loop", fn);
        auto* body = llvm::BasicBlock::Create(Builder.getContext(), "pow.body", fn);
        auto* done = llvm::BasicBlock::Create(Builder.getContext(), "pow.done", fn);
        Guard(Builder.CreateICmpSGE(right, Builder.getInt32(0)), valid, invalid);
        Builder.CreateBr(loop);
        Builder.SetInsertPoint(loop);
        auto* count = Builder.CreatePHI(Builder.getInt32Ty(), 2);
        auto* result = Builder.CreatePHI(Builder.getInt32Ty(), 2);
        count->addIncoming(right, valid);
        result->addIncoming(Builder.getInt32(1), valid);
        Builder.CreateCondBr(Builder.CreateICmpSGT(count, Builder.getInt32(0)), body, done);
        Builder.SetInsertPoint(body);
        auto* nextResult = Builder.CreateMul(result, left);
        auto* nextCount = Builder.CreateSub(count, Builder.getInt32(1));
        Builder.CreateBr(loop);
        count->addIncoming(nextCount, body);
        result->addIncoming(nextResult, body);
        Builder.SetInsertPoint(done);
        return result;
    }
    throw std::runtime_error("Unknown operator: " + op);
}

}
