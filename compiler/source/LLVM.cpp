#include "LLVM.h"
#include "Semantic.h"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/raw_ostream.h>

#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/CodeGen.h>

#include <memory>
#include <functional>
#include <algorithm>
#include <system_error>

#include <stdexcept>
#include <process.h>
#include <io.h>
#include <cstdio>
#include <iostream>
#include <unordered_map>


LLVM::LLVM()
    : Module("C4R", Context),
      Builder(Context)
{}

llvm::Type* LLVM::GetType(const std::string& name) {
    if (name == "void") {
        return Builder.getVoidTy();
    }

    if (name == "int32" || name == "int") {
        return Builder.getInt32Ty();
    }
    if (name == "bool") return Builder.getInt1Ty();
    if (name == "float") return Builder.getFloatTy();
    if (!name.empty() && name.back() == '*') return llvm::PointerType::getUnqual(GetType(name.substr(0, name.size() - 1)));
    if (types.contains(name)) return types.at(name).type;

    throw std::runtime_error(
        "Unknown type: " + name
    );
}

llvm::Value* LLVM::CreateString(const std::string& value) {
    auto* global = Builder.CreateGlobalString(
        value,
        "c4r.str",
        0,
        &Module
    );

    llvm::Value* indices[] = {
        Builder.getInt32(0),
        Builder.getInt32(0)
    };

    return Builder.CreateInBoundsGEP(
        global->getValueType(),
        global,
        indices,
        "str.ptr"
    );
}

void LLVM::EmitString(const StringStatement& statement) {
    auto* pointerType = llvm::PointerType::getUnqual(Context);

    llvm::Type* arguments[] = {
        pointerType
    };

    auto* printfType = llvm::FunctionType::get(
        Builder.getInt32Ty(),
        arguments,
        true
    );

    auto printfFunction = Module.getOrInsertFunction(
        "printf",
        printfType
    );

    auto* format = CreateString("%s");
    auto* value = CreateString(statement.value);

    Builder.CreateCall(
        printfFunction,
        { format, value }
    );
}

LLVM::Variable LLVM::ResolveLValue(const Expression& expression) {
    if (semanticAnalysis) if (auto* target = semanticAnalysis->ReferenceTarget(expression); target && target->returnsReference) {
        llvm::Value* storage = nullptr;
        if (auto* call = dynamic_cast<const CallExpr*>(&expression)) storage = EmitCallExpression(*call);
        else if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression)) storage = EmitBareCall(*identifier);
        else if (auto* member = dynamic_cast<const MemberExpr*>(&expression)) storage = EmitBareMemberCall(*member);
        if (storage) return {storage, GetType(target->returnType), target->returnsMutableReference, 0};
    }
    auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression);
    if (identifier) {
        if (semanticAnalysis) if (auto* target = semanticAnalysis->CallTarget(*identifier)) {
            if (target->returnsReference)
                return {EmitBareCall(*identifier), GetType(target->returnType), target->returnsMutableReference, 0};
            return MaterializeTemporary(expression);
        }
        const auto* resolved = semanticAnalysis ? semanticAnalysis->Reference(*identifier) : nullptr;
        if (resolved && resolved->kind == Semantic::ResolvedReference::Kind::Variable) {
            auto declaration = bindingDeclarations.find(resolved->declaration);
            if (declaration == bindingDeclarations.end() || !bindings.contains(declaration->second))
                throw std::runtime_error("Resolved variable is not available: " + identifier->name);
            return bindings.at(declaration->second);
        }
        if (resolved && resolved->kind == Semantic::ResolvedReference::Kind::Field) {
            MemberExpr member;
            auto self = std::make_unique<IdentifierExpr>(); self->name = "$self";
            member.object = std::move(self); member.name = identifier->name;
            return ResolveLValue(member);
        }
        auto it = variables.find(identifier->name);
        auto global = globalVariables.find(identifier->name);
        bool isGlobal = it != variables.end() && global != globalVariables.end() && it->second.id == global->second.id;
        if (currentMethod && (it == variables.end() || isGlobal) && types.at(currentMethod->owner).fields.contains(identifier->name)) {
            MemberExpr member;
            auto self = std::make_unique<IdentifierExpr>(); self->name = "$self";
            member.object = std::move(self); member.name = identifier->name;
            return ResolveLValue(member);
        }
        if (it != variables.end()) return it->second;
        throw std::runtime_error("Undefined variable: " + identifier->name);
    }
    auto* member = dynamic_cast<const MemberExpr*>(&expression);
    if (!member) return MaterializeTemporary(expression);
        if (semanticAnalysis) if (auto* target = semanticAnalysis->CallTarget(*member)) {
        if (target->returnsReference)
            return {EmitBareMemberCall(*member), GetType(target->returnType), target->returnsMutableReference, 0};
        return MaterializeTemporary(expression);
    }
    return ResolveMember(ResolveLValue(*member->object), member->name);
}

LLVM::Variable LLVM::ResolveMember(const Variable& object, const std::string& name) {
    if (auto found = typesByLayout.find(object.type); found != typesByLayout.end()) {
        auto& type = *found->second;
        auto field = type.fields.find(name);
        if (field == type.fields.end()) throw std::runtime_error("Unknown field: " + name);
        if (!field->second.isPublic && (!currentMethod || currentMethod->owner != type.declaration->name))
            throw std::runtime_error("Private member: " + name);
        auto* fieldType = GetType(field->second.declaration->type);
        bool constructorInitialization = currentMethod &&
            dynamic_cast<const ConstructorDeclaration*>(currentMethod->declaration) &&
            object.isReceiver && object.type == types.at(currentMethod->owner).type;
        return {Builder.CreateStructGEP(type.type, object.storage, field->second.index), fieldType,
                object.isMutable && (field->second.declaration->isMutable || constructorInitialization), 0, object.isReceiver};
    }
    throw std::runtime_error("Member access requires a user-defined type");
}

LLVM::Variable LLVM::MutableVariable(const Expression& expression) {
    auto variable = ResolveLValue(expression);
    if (!variable.isMutable) throw std::runtime_error("Cannot modify immutable variable or field");
    if (variable.isReceiver && currentMethod) currentMethod->mutates = true;
    return variable;
}

llvm::Value* LLVM::EmitBinary(const std::string& op, llvm::Value* left, llvm::Value* right) {
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
            auto* valid = llvm::BasicBlock::Create(Context, "division.valid", fn);
            auto* invalid = llvm::BasicBlock::Create(Context, "division.invalid", fn);
            auto* nonzero = Builder.CreateICmpNE(right, Builder.getInt32(0));
            auto* minimum = Builder.CreateICmpEQ(left, Builder.getInt32(static_cast<uint32_t>(INT32_MIN)));
            auto* minusOne = Builder.CreateICmpEQ(right, Builder.getInt32(UINT32_MAX));
            auto* overflow = Builder.CreateAnd(minimum, minusOne);
            Builder.CreateCondBr(Builder.CreateAnd(nonzero, Builder.CreateNot(overflow)), valid, invalid);
            Builder.SetInsertPoint(invalid);
            Builder.CreateCall(llvm::Intrinsic::getOrInsertDeclaration(&Module, llvm::Intrinsic::trap));
            Builder.CreateUnreachable();
            Builder.SetInsertPoint(valid);
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
            auto* valid = llvm::BasicBlock::Create(Context, "shift.valid", fn);
            auto* invalid = llvm::BasicBlock::Create(Context, "shift.invalid", fn);
            Builder.CreateCondBr(Builder.CreateICmpULT(right, Builder.getInt32(32)), valid, invalid);
            Builder.SetInsertPoint(invalid);
            Builder.CreateCall(llvm::Intrinsic::getOrInsertDeclaration(&Module, llvm::Intrinsic::trap));
            Builder.CreateUnreachable();
            Builder.SetInsertPoint(valid);
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
        auto* valid = llvm::BasicBlock::Create(Context, "pow.valid", fn);
        auto* invalid = llvm::BasicBlock::Create(Context, "pow.invalid", fn);
        auto* loop = llvm::BasicBlock::Create(Context, "pow.loop", fn);
        auto* body = llvm::BasicBlock::Create(Context, "pow.body", fn);
        auto* done = llvm::BasicBlock::Create(Context, "pow.done", fn);
        Builder.CreateCondBr(Builder.CreateICmpSGE(right, Builder.getInt32(0)), valid, invalid);
        Builder.SetInsertPoint(invalid);
        Builder.CreateCall(llvm::Intrinsic::getOrInsertDeclaration(&Module, llvm::Intrinsic::trap));
        Builder.CreateUnreachable();
        Builder.SetInsertPoint(valid);
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

llvm::Value* LLVM::EmitCondition(const Expression& expression, bool fullExpression) {
    auto* value = fullExpression ? EmitFullExpression(expression) : EmitExpression(expression);
    if (!value->getType()->isIntegerTy(1)) throw std::runtime_error("Condition must be bool");
    return value;
}

void LLVM::EmitVariable(const VariableDeclaration& declaration) {
    auto* type = GetType(declaration.type);
    if (type->isVoidTy()) throw std::runtime_error("Variable cannot have void type");
    if (declaration.isReference) {
        if (!declaration.initialValue) throw std::runtime_error("Reference variable requires an initializer: " + declaration.name);
        auto source = ResolveLValue(*declaration.initialValue);
        if (source.type != type) throw std::runtime_error("Reference initializer type mismatch: " + declaration.name);
        Variable variable{source.storage, type, declaration.isMutableReference, nextVariableId++};
        bindingDeclarations[&declaration] = variable.id;
        variables.insert_or_assign(declaration.name, variable);
        bindings.insert_or_assign(variable.id, variable);
        return;
    }
    auto& entry = Builder.GetInsertBlock()->getParent()->getEntryBlock();
    llvm::IRBuilder<> entryBuilder(&entry, entry.begin());
    auto* storage = entryBuilder.CreateAlloca(type, nullptr, declaration.name);
    BeginFullExpression();
    if (declaration.aggregateInit) {
        if (!types.contains(declaration.type)) throw std::runtime_error("Brace initialization requires a user-defined type");
        InitializeObject(storage, types.at(declaration.type));
        if (auto* constructor = semanticAnalysis->ConstructorTarget(declaration))
            Builder.CreateCall(types.at(declaration.type).constructors.at(constructor)->function, {storage});
    } else if (auto* construct = dynamic_cast<const ConstructExpr*>(declaration.initialValue.get())) {
        if (construct->type != declaration.type) throw std::runtime_error("Initializer type mismatch: " + declaration.name);
        EmitConstruction(storage, *construct);
    } else if (semanticAnalysis->NeedsDestruction(declaration.type)) {
        EmitDirectValue(*declaration.initialValue, storage, types.at(declaration.type));
    } else {
        auto* value = EmitExpression(*declaration.initialValue);
        if (value->getType() != type) throw std::runtime_error("Initializer type mismatch: " + declaration.name);
        Builder.CreateStore(value, storage);
    }
    EndFullExpression();
    Variable variable{storage, type, declaration.isMutable, nextVariableId++};
    if (currentHasMoves && semanticAnalysis->NeedsDestruction(declaration.type)) {
        variable.ownershipActive = entryBuilder.CreateAlloca(Builder.getInt1Ty(), nullptr, declaration.name + ".owned");
        Builder.CreateStore(Builder.getTrue(), variable.ownershipActive);
    }
    bindingDeclarations[&declaration] = variable.id;
    variables.insert_or_assign(declaration.name, variable);
    bindings.insert_or_assign(variable.id, variable);
    if (auto found = types.find(declaration.type); found != types.end())
        cleanupScopes.back().push_back({storage, &found->second, variable.ownershipActive});
}

void LLVM::EmitConstruction(llvm::Value* storage, const ConstructExpr& expression) {
    auto& type = types.at(expression.type);
    auto* declaration = semanticAnalysis->ConstructorTarget(expression);
    if (!declaration) throw std::runtime_error("Constructor was not resolved: " + expression.type);
    std::vector<llvm::Value*> args{storage};
    EmitArguments(expression.arguments, declaration->parameters, args);
    InitializeObject(storage, type);
    Builder.CreateCall(type.constructors.at(declaration)->function, args);
}

llvm::Value* LLVM::EmitDirectValue(const Expression& expression, llvm::Value* destination, TypeInfo& type) {
    if (auto* move = dynamic_cast<const MoveExpr*>(&expression)) {
        auto* value = EmitMove(*move);
        if (value->getType() != type.type) throw std::runtime_error("Move type mismatch");
        Builder.CreateStore(value, destination);
        return value;
    }
    if (auto* construct = dynamic_cast<const ConstructExpr*>(&expression)) {
        if (construct->type != type.declaration->name) throw std::runtime_error("Initializer type mismatch");
        EmitConstruction(destination, *construct);
        return Builder.CreateLoad(type.type, destination);
    }
    if (auto* call = dynamic_cast<const CallExpr*>(&expression)) return EmitCallExpression(*call, destination);
    if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression)) return EmitBareCall(*identifier, destination);
    if (auto* member = dynamic_cast<const MemberExpr*>(&expression)) return EmitBareMemberCall(*member, destination);
    if (auto* block = dynamic_cast<const BlockExpr*>(&expression)) return EmitValueBlock(*block->block, destination, &type);
    if (auto* branch = dynamic_cast<const IfExpr*>(&expression)) return EmitIfValue(*branch, destination, &type);
    throw std::runtime_error("Destructible value requires direct construction");
}

llvm::Value* LLVM::EmitMove(const MoveExpr& expression) {
    if (!currentHasMoves) throw std::runtime_error("Ownership analysis is unavailable for this move");
    auto source = ResolveLValue(*expression.operand);
    for (const auto& [name, global] : globalVariables)
        if (global.id == source.id) throw std::runtime_error("Moving global variables is not implemented yet");
    if (source.type->isStructTy() && !source.ownershipActive &&
        semanticAnalysis->NeedsDestruction(typesByLayout.at(source.type)->declaration->name))
        throw std::runtime_error("Moving this object is not implemented yet");
    auto* result = Builder.CreateLoad(source.type, source.storage, "move.value");
    if (source.ownershipActive) Builder.CreateStore(Builder.getFalse(), source.ownershipActive);
    return result;
}

llvm::Constant* LLVM::ConstantInitializer(const Expression& expression) {
    if (auto* number = dynamic_cast<const NumericLiteral*>(&expression)) {
        if (number->value.find('.') != std::string::npos)
            return llvm::ConstantFP::get(Builder.getFloatTy(), std::stod(number->value));
        auto value = std::stoll(number->value);
        if (value > INT32_MAX) throw std::runtime_error("Integer literal out of range");
        return Builder.getInt32(static_cast<int32_t>(value));
    }
    if (auto* boolean = dynamic_cast<const BooleanLiteral*>(&expression)) return Builder.getInt1(boolean->value);
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
        auto* value = ConstantInitializer(*unary->operand);
        llvm::Value* result = nullptr;
        if (unary->op == "+" && (value->getType()->isIntegerTy(32) || value->getType()->isFloatTy())) result = value;
        else if (unary->op == "-" && value->getType()->isIntegerTy(32)) result = Builder.CreateNeg(value);
        else if (unary->op == "-" && value->getType()->isFloatTy()) result = Builder.CreateFNeg(value);
        else if (unary->op == "~" && value->getType()->isIntegerTy(32)) result = Builder.CreateNot(value);
        else if (unary->op == "!" && value->getType()->isIntegerTy(1)) result = Builder.CreateNot(value);
        if (auto* constant = llvm::dyn_cast_or_null<llvm::Constant>(result)) return constant;
        throw std::runtime_error("Invalid constant unary expression");
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        auto* left = ConstantInitializer(*binary->left);
        const auto& op = binary->op;
        if (op == "&&" || op == "||" || op == "!&&" || op == "!||") {
            if (!left->getType()->isIntegerTy(1)) throw std::runtime_error("Logical operator requires bool operands");
            auto* truth = llvm::dyn_cast<llvm::ConstantInt>(left);
            if (!truth) throw std::runtime_error("Invalid global constant initializer");
            bool isAnd = op == "&&" || op == "!&&";
            bool negate = op == "!&&" || op == "!||";
            bool leftValue = !truth->isZero();
            if ((isAnd && !leftValue) || (!isAnd && leftValue)) return Builder.getInt1(negate ? !leftValue : leftValue);
            auto* right = ConstantInitializer(*binary->right);
            if (!right->getType()->isIntegerTy(1)) throw std::runtime_error("Logical operator requires bool operands");
            auto* result = op == "&&" || op == "!&&" ? Builder.CreateAnd(left, right) : Builder.CreateOr(left, right);
            if (op == "!&&" || op == "!||") result = Builder.CreateNot(result);
            return llvm::cast<llvm::Constant>(result);
        }
        auto* right = ConstantInitializer(*binary->right);
        if (op == "`") {
            auto* base = llvm::dyn_cast<llvm::ConstantInt>(left);
            auto* exponent = llvm::dyn_cast<llvm::ConstantInt>(right);
            if (!base || !exponent || !base->getType()->isIntegerTy(32) || !exponent->getType()->isIntegerTy(32))
                throw std::runtime_error("Operator requires int32 operands: `");
            if (exponent->isNegative()) throw std::runtime_error("Integer exponent must be nonnegative");
            auto n = exponent->getZExtValue();
            llvm::APInt result(32, 1), factor = base->getValue();
            while (n) { if (n & 1) result *= factor; factor *= factor; n >>= 1; }
            return llvm::ConstantInt::get(Context, result);
        }
        if (op == "=" || op.ends_with("=")) throw std::runtime_error("Unsupported operator in global constant initializer: " + op);
        auto* value = llvm::dyn_cast<llvm::Constant>(EmitBinary(op, left, right));
        if (!value || llvm::isa<llvm::PoisonValue>(value)) throw std::runtime_error("Invalid global constant initializer");
        return value;
    }
    if (auto* chain = dynamic_cast<const ComparisonExpr*>(&expression)) {
        auto* left = ConstantInitializer(*chain->operands.front());
        llvm::Constant* result = Builder.getTrue();
        for (size_t i = 0; i < chain->operators.size(); ++i) {
            auto* right = ConstantInitializer(*chain->operands[i + 1]);
            auto* comparison = llvm::dyn_cast<llvm::Constant>(EmitBinary(chain->operators[i], left, right));
            if (!comparison || llvm::isa<llvm::PoisonValue>(comparison)) throw std::runtime_error("Invalid global constant initializer");
            auto* truth = llvm::dyn_cast<llvm::ConstantInt>(comparison);
            if (truth && truth->isZero()) return Builder.getFalse();
            result = llvm::cast<llvm::Constant>(Builder.CreateAnd(result, comparison));
            left = right;
        }
        return result;
    }
    throw std::runtime_error("Global initializer requires a constant expression");
}

llvm::Constant* LLVM::ConstantObject(TypeInfo& type) {
    std::vector<llvm::Constant*> values;
    for (const auto& member : type.declaration->members) {
        auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
        if (!field) continue;
        auto* fieldType = GetType(field->type);
        llvm::Constant* value = field->initialValue ? ConstantInitializer(*field->initialValue) :
            types.contains(field->type) ? ConstantObject(types.at(field->type)) : llvm::Constant::getNullValue(fieldType);
        if (value->getType() != fieldType) throw std::runtime_error("Initializer type mismatch: " + field->name);
        values.push_back(value);
    }
    return llvm::ConstantStruct::get(type.type, values);
}

llvm::Value* LLVM::EmitCall(const std::string& name, const std::vector<std::unique_ptr<Expression>>& arguments,
    const FunctionDeclaration* target, llvm::Value* destination) {
    std::shared_ptr<FunctionSymbol> symbol;
    if (target) {
        auto it = functionDeclarations.find(target);
        if (it == functionDeclarations.end()) throw std::runtime_error("Resolved function is not available: " + name);
        symbol = it->second;
    } else {
        if (variables.contains(name)) throw std::runtime_error("Variable is not callable: " + name);
        if (inaccessibleVariables.contains(name)) throw std::runtime_error("Variable requires capture: " + name);
        auto it = functions.find(name);
        if (it == functions.end()) throw std::runtime_error("Undefined function: " + name);
        symbol = it->second;
    }
    const auto& parameters = symbol->declaration->parameters;
    if (arguments.size() != parameters.size()) throw std::runtime_error("Wrong number of arguments for function: " + name);
    std::vector<llvm::Value*> values;
    for (const auto& capture : symbol->captures) {
        auto binding = bindings.find(capture.variable.id);
        if (binding == bindings.end()) throw std::runtime_error("Capture is not available in this function: " + capture.name);
        values.push_back(binding->second.storage);
    }
    EmitArguments(arguments, parameters, values);
    return EmitFunctionCall(symbol, std::move(values), destination);
}

llvm::Value* LLVM::EmitFunctionCall(const std::shared_ptr<FunctionSymbol>& symbol,
    std::vector<llvm::Value*> values, llvm::Value* destination) {
    const auto& resultType = symbol->declaration->returnType;
    if (symbol->declaration->returnsReference) return Builder.CreateCall(symbol->function, values);
    if (!semanticAnalysis->NeedsDestruction(resultType)) return Builder.CreateCall(symbol->function, values);
    llvm::AllocaInst* active = nullptr;
    auto& type = types.at(resultType);
    if (!destination) {
        if (temporaryFrames.empty()) throw std::runtime_error("Temporary outside a full expression");
        auto& entry = Builder.GetInsertBlock()->getParent()->getEntryBlock();
        llvm::IRBuilder<> entryBuilder(&entry, entry.begin());
        destination = entryBuilder.CreateAlloca(type.type, nullptr, "result.object");
        active = entryBuilder.CreateAlloca(Builder.getInt1Ty(), nullptr, "result.active");
        llvm::IRBuilder<> initializer(temporaryFrames.back().marker);
        initializer.CreateStore(Builder.getFalse(), active);
    }
    values.insert(values.begin(), destination);
    Builder.CreateCall(symbol->function, values);
    if (active) {
        Builder.CreateStore(Builder.getTrue(), active);
        temporaryFrames.back().cleanups.push_back({destination, &type, active});
    }
    auto* result = Builder.CreateLoad(type.type, destination);
    if (active) temporaryValues.insert_or_assign(result, Variable{destination, type.type, false, 0, false});
    return result;
}

void LLVM::EmitArguments(const std::vector<std::unique_ptr<Expression>>& arguments,
    const std::vector<FunctionDeclaration::Parameter>& parameters, std::vector<llvm::Value*>& values) {
    if (arguments.size() != parameters.size()) throw std::runtime_error("Wrong number of arguments");
    for (size_t i = 0; i < arguments.size(); ++i) {
        auto* type = GetType(parameters[i].type);
        if (parameters[i].isReference) {
            auto variable = ResolveLValue(*arguments[i]);
            if (variable.type != type) throw std::runtime_error("Reference argument type mismatch");
            if (parameters[i].isMutableReference && !variable.isMutable)
                throw std::runtime_error("Mutable reference requires a mutable object");
            values.push_back(variable.storage);
            continue;
        }
        if (semanticAnalysis->NeedsDestruction(parameters[i].type)) {
            auto& entry = Builder.GetInsertBlock()->getParent()->getEntryBlock();
            llvm::IRBuilder<> entryBuilder(&entry, entry.begin());
            auto* storage = entryBuilder.CreateAlloca(type, nullptr, "argument.object");
            EmitDirectValue(*arguments[i], storage, types.at(parameters[i].type));
            values.push_back(storage);
        } else {
            auto* value = EmitExpression(*arguments[i]);
            if (value->getType() != type) throw std::runtime_error("Argument type mismatch");
            values.push_back(value);
        }
    }
}

void LLVM::InitializeObject(llvm::Value* storage, TypeInfo& type) {
    for (const auto& member : type.declaration->members) {
        auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
        if (!field) continue;
        auto& info = type.fields.at(field->name);
        auto* address = Builder.CreateStructGEP(type.type, storage, info.index);
        auto* fieldType = GetType(field->type);
        if (auto* construct = dynamic_cast<const ConstructExpr*>(field->initialValue.get())) {
            EmitConstruction(address, *construct);
        } else if (field->initialValue) {
            auto savedVariables = variables;
            auto savedBindings = bindings;
            auto savedFunctions = functions;
            auto savedMethod = currentMethod;
            variables.clear(); bindings.clear(); currentMethod.reset();
            for (auto it = functions.begin(); it != functions.end();) {
                if (!it->second->function->hasExternalLinkage()) it = functions.erase(it);
                else ++it;
            }
            llvm::Value* value;
            if (semanticAnalysis->NeedsDestruction(field->type)) {
                value = EmitDirectValue(*field->initialValue, address, types.at(field->type));
            } else value = EmitExpression(*field->initialValue);
            variables = std::move(savedVariables);
            bindings = std::move(savedBindings);
            functions = std::move(savedFunctions);
            currentMethod = std::move(savedMethod);
            if (value->getType() != fieldType) throw std::runtime_error("Initializer type mismatch: " + field->name);
            if (!semanticAnalysis->NeedsDestruction(field->type)) Builder.CreateStore(value, address);
        } else if (types.contains(field->type)) {
            auto& nested = types.at(field->type);
            InitializeObject(address, nested);
            if (auto* constructor = semanticAnalysis->ConstructorTarget(*field))
                Builder.CreateCall(nested.constructors.at(constructor)->function, {address});
        }
        else Builder.CreateStore(llvm::Constant::getNullValue(fieldType), address);
    }
}

void LLVM::EmitDestruction(llvm::Value* storage, TypeInfo& type) {
    if (type.destructor) Builder.CreateCall(type.destructor->function, {storage});
    for (auto it = type.declaration->members.rbegin(); it != type.declaration->members.rend(); ++it) {
        auto* field = dynamic_cast<const VariableDeclaration*>(it->declaration.get());
        if (!field) continue;
        auto nested = types.find(field->type);
        if (nested == types.end()) continue;
        auto* address = Builder.CreateStructGEP(type.type, storage, type.fields.at(field->name).index);
        EmitDestruction(address, nested->second);
    }
}

void LLVM::EmitCleanups(size_t fromDepth) {
    for (size_t depth = cleanupScopes.size(); depth > fromDepth; --depth) {
        const auto& scope = cleanupScopes[depth - 1];
        for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
            if (!it->active) { EmitDestruction(it->storage, *it->type); continue; }
            auto* function = Builder.GetInsertBlock()->getParent();
            auto* destroy = llvm::BasicBlock::Create(Context, "owned.destroy", function);
            auto* done = llvm::BasicBlock::Create(Context, "owned.done", function);
            Builder.CreateCondBr(Builder.CreateLoad(Builder.getInt1Ty(), it->active), destroy, done);
            Builder.SetInsertPoint(destroy);
            EmitDestruction(it->storage, *it->type);
            Builder.CreateBr(done);
            Builder.SetInsertPoint(done);
        }
    }
}

void LLVM::BeginFullExpression() {
    auto* marker = Builder.CreateAlloca(Builder.getInt1Ty(), nullptr, "temporary.marker");
    temporaryFrames.push_back({marker, {}});
}

void LLVM::EndFullExpression() {
    auto frame = std::move(temporaryFrames.back());
    temporaryFrames.pop_back();
    for (auto it = frame.cleanups.rbegin(); it != frame.cleanups.rend(); ++it) {
        auto* fn = Builder.GetInsertBlock()->getParent();
        auto* destroy = llvm::BasicBlock::Create(Context, "temporary.destroy", fn);
        auto* next = llvm::BasicBlock::Create(Context, "temporary.done", fn);
        Builder.CreateCondBr(Builder.CreateLoad(Builder.getInt1Ty(), it->active), destroy, next);
        Builder.SetInsertPoint(destroy);
        EmitDestruction(it->storage, *it->type);
        Builder.CreateBr(next);
        Builder.SetInsertPoint(next);
    }
    frame.marker->eraseFromParent();
}

llvm::Value* LLVM::EmitFullExpression(const Expression& expression) {
    BeginFullExpression();
    auto* value = EmitExpression(expression);
    EndFullExpression();
    return value;
}

LLVM::Variable LLVM::MaterializeTemporary(const Expression& expression) {
    if (temporaryFrames.empty()) throw std::runtime_error("Temporary outside a full expression");
    auto& entry = Builder.GetInsertBlock()->getParent()->getEntryBlock();
    llvm::IRBuilder<> entryBuilder(&entry, entry.begin());
    const auto* construct = dynamic_cast<const ConstructExpr*>(&expression);
    llvm::Value* value = nullptr;
    llvm::Type* llvmType = nullptr;
    if (construct) llvmType = GetType(construct->type);
    else {
        value = EmitExpression(expression);
        if (auto found = temporaryValues.find(value); found != temporaryValues.end()) return found->second;
        llvmType = value->getType();
    }
    auto found = typesByLayout.find(llvmType);
    if (found == typesByLayout.end()) throw std::runtime_error("Member access requires a user-defined type");
    auto* storage = entryBuilder.CreateAlloca(llvmType, nullptr, "temporary.object");
    auto* type = found->second;
    llvm::AllocaInst* active = nullptr;
    if (semanticAnalysis->NeedsDestruction(type->declaration->name)) {
        active = entryBuilder.CreateAlloca(Builder.getInt1Ty(), nullptr, "temporary.active");
        llvm::IRBuilder<> initializer(temporaryFrames.back().marker);
        initializer.CreateStore(Builder.getFalse(), active);
    }
    if (construct) EmitConstruction(storage, *construct);
    else Builder.CreateStore(value, storage);
    if (active) {
        Builder.CreateStore(Builder.getTrue(), active);
        temporaryFrames.back().cleanups.push_back({storage, type, active});
    }
    return {storage, llvmType, false, 0, false};
}

llvm::Value* LLVM::EmitMethodCall(const CallExpr& call, llvm::Value* destination) {
    Variable object;
    if (call.object) object = ResolveLValue(*call.object);
    else object = variables.at("$self");
    if (auto entry = typesByLayout.find(object.type); entry != typesByLayout.end()) {
        auto& type = *entry->second;
        auto found = type.methods.find(call.name);
        if (found == type.methods.end()) throw std::runtime_error("Unknown method: " + call.name);
        auto symbol = found->second.first;
        if (!found->second.second && (!currentMethod || currentMethod->owner != type.declaration->name)) throw std::runtime_error("Private member: " + call.name);
        if (call.arguments.size() != symbol->declaration->parameters.size()) throw std::runtime_error("Wrong number of arguments for method: " + call.name);
        if (!object.isMutable) immutableCalls.push_back(symbol);
        else if (object.isReceiver && currentMethod) currentMethod->calls.push_back(symbol);
        std::vector<llvm::Value*> args{object.storage};
        EmitArguments(call.arguments, symbol->declaration->parameters, args);
        return EmitFunctionCall(symbol, std::move(args), destination);
    }
    throw std::runtime_error("Method call requires a user-defined type");
}

llvm::Value* LLVM::EmitCallExpression(const CallExpr& call, llvm::Value* destination) {
    if (semanticAnalysis) if (auto* target = semanticAnalysis->CallTarget(call)) return EmitCall(call.name, call.arguments, target, destination);
    if (call.object || (currentMethod && types.at(currentMethod->owner).methods.contains(call.name) && !functions.contains(call.name) && !variables.contains(call.name)))
        return EmitMethodCall(call, destination);
    return EmitCall(call.name, call.arguments, nullptr, destination);
}

llvm::Value* LLVM::EmitBareCall(const IdentifierExpr& expression, llvm::Value* destination) {
    auto* target = semanticAnalysis->CallTarget(expression);
    if (!target) throw std::runtime_error("Destructible value requires direct construction");
    if (currentMethod) {
        auto& methods = types.at(currentMethod->owner).methods;
        auto found = methods.find(expression.name);
        if (found != methods.end() && found->second.first->declaration == target) {
            auto symbol = found->second.first;
            currentMethod->calls.push_back(symbol);
            return EmitFunctionCall(symbol, {variables.at("$self").storage}, destination);
        }
    }
    std::vector<std::unique_ptr<Expression>> arguments;
    return EmitCall(expression.name, arguments, target, destination);
}

llvm::Value* LLVM::EmitBareMemberCall(const MemberExpr& expression, llvm::Value* destination) {
    auto* target = semanticAnalysis->CallTarget(expression);
    if (!target) throw std::runtime_error("Destructible value requires direct construction");
    auto object = ResolveLValue(*expression.object);
    auto found = typesByLayout.find(object.type);
    if (found == typesByLayout.end()) throw std::runtime_error("Method call requires a user-defined type");
    auto& type = *found->second;
    auto method = type.methods.find(expression.name);
    if (method == type.methods.end() || method->second.first->declaration != target) throw std::runtime_error("Unknown method: " + expression.name);
    if (!method->second.second && (!currentMethod || currentMethod->owner != type.declaration->name)) throw std::runtime_error("Private member: " + expression.name);
    auto symbol = method->second.first;
    if (!object.isMutable) immutableCalls.push_back(symbol);
    else if (object.isReceiver && currentMethod) currentMethod->calls.push_back(symbol);
    return EmitFunctionCall(symbol, {object.storage}, destination);
}

llvm::Value* LLVM::FinishValueScope(ValueScope& scope) {
    if (scope.returns.empty()) throw std::runtime_error("Value scope requires return");
    Builder.SetInsertPoint(scope.exit);
    if (scope.objectStorage) {
        auto* value = Builder.CreateLoad(scope.objectType->type, scope.objectStorage);
        temporaryValues.insert_or_assign(value, Variable{scope.objectStorage, scope.objectType->type, false, 0, false});
        return value;
    }
    auto* result = Builder.CreatePHI(scope.type, static_cast<unsigned>(scope.returns.size()), "scope.value");
    for (const auto& item : scope.returns) result->addIncoming(item.first, item.second);
    return result;
}

llvm::Value* LLVM::EmitValueBlock(const Block& block, llvm::Value* destination, TypeInfo* type) {
    auto* fn = Builder.GetInsertBlock()->getParent();
    ValueScope scope{llvm::BasicBlock::Create(Context, "value.end", fn), nullptr, {}, loops.size(), cleanupScopes.size()};
    scope.temporaryFrame = temporaryFrames.size() - 1;
    scope.objectStorage = destination;
    scope.objectType = type;
    if (type) scope.type = type->type;
    valueScopes.push_back(&scope);
    EmitBody(block.statements);
    if (!Builder.GetInsertBlock()->hasTerminator()) throw std::runtime_error("Every path in a value scope must return a value");
    valueScopes.pop_back();
    return FinishValueScope(scope);
}

llvm::Value* LLVM::EmitIfValue(const IfExpr& branch, llvm::Value* destination, TypeInfo* type) {
    auto* condition = EmitCondition(*branch.condition, false);
    auto* fn = Builder.GetInsertBlock()->getParent();
    auto* thenBlock = llvm::BasicBlock::Create(Context, "if.value.then", fn);
    auto* elseBlock = llvm::BasicBlock::Create(Context, "if.value.else", fn);
    ValueScope scope{llvm::BasicBlock::Create(Context, "if.value.end", fn), nullptr, {}, loops.size(), cleanupScopes.size()};
    scope.temporaryFrame = temporaryFrames.size() - 1;
    scope.objectStorage = destination;
    scope.objectType = type;
    if (type) scope.type = type->type;
    Builder.CreateCondBr(condition, thenBlock, elseBlock);
    valueScopes.push_back(&scope);
    Builder.SetInsertPoint(thenBlock);
    EmitBody(branch.thenBranch->statements);
    if (!Builder.GetInsertBlock()->hasTerminator()) throw std::runtime_error("Every if expression branch must return a value");
    Builder.SetInsertPoint(elseBlock);
    EmitBody(branch.elseBranch->statements);
    if (!Builder.GetInsertBlock()->hasTerminator()) throw std::runtime_error("Every if expression branch must return a value");
    valueScopes.pop_back();
    return FinishValueScope(scope);
}

llvm::Value* LLVM::EmitExpression(const Expression& expression) {
    diagnosticOffset = expression.offset;
    if (auto* move = dynamic_cast<const MoveExpr*>(&expression)) {
        auto* identifier = dynamic_cast<const IdentifierExpr*>(move->operand.get());
        auto variable = identifier ? ResolveLValue(*identifier) : Variable{};
        if (variable.type && variable.type->isStructTy())
            throw std::runtime_error("Object move requires a direct destination");
        return EmitMove(*move);
    }
    if (dynamic_cast<const ConstructExpr*>(&expression)) {
        auto temporary = MaterializeTemporary(expression);
        return Builder.CreateLoad(temporary.type, temporary.storage);
    }
    if (auto* call = dynamic_cast<const CallExpr*>(&expression)) {
        auto* result = EmitCallExpression(*call);
        if (semanticAnalysis) if (auto* target = semanticAnalysis->ReferenceTarget(*call); target && target->returnsReference)
            return Builder.CreateLoad(GetType(target->returnType), result);
        return result;
    }
    if (auto* member = dynamic_cast<const MemberExpr*>(&expression)) {
        if (auto* referenceTarget = semanticAnalysis->ReferenceTarget(*member); referenceTarget && referenceTarget->returnsReference) {
            auto* result = EmitBareMemberCall(*member);
            return Builder.CreateLoad(GetType(referenceTarget->returnType), result);
        }
        if (auto* target = semanticAnalysis->CallTarget(*member)) {
            auto* result = EmitBareMemberCall(*member);
            if (target->returnsReference) return Builder.CreateLoad(GetType(target->returnType), result);
            return result;
        }
        auto object = ResolveLValue(*member->object);
        if (auto entry = typesByLayout.find(object.type); entry != typesByLayout.end() && entry->second->methods.contains(member->name)) {
            auto& type = *entry->second;
            auto symbol = type.methods.at(member->name).first;
            if (!type.methods.at(member->name).second && (!currentMethod || currentMethod->owner != type.declaration->name)) throw std::runtime_error("Private member: " + member->name);
            if (!symbol->declaration->parameters.empty()) throw std::runtime_error("Wrong number of arguments for method: " + member->name);
            if (!object.isMutable) immutableCalls.push_back(symbol);
            else if (object.isReceiver && currentMethod) currentMethod->calls.push_back(symbol);
            return EmitFunctionCall(symbol, {object.storage}, nullptr);
        }
        auto field = ResolveMember(object, member->name);
        return Builder.CreateLoad(field.type, field.storage);
    }
    if (auto* block = dynamic_cast<const BlockExpr*>(&expression)) return EmitValueBlock(*block->block);
    if (auto* branch = dynamic_cast<const IfExpr*>(&expression)) return EmitIfValue(*branch);
    if (auto* literal = dynamic_cast<const NumericLiteral*>(&expression)) {
        if (literal->value.find('.') != std::string::npos)
            return llvm::ConstantFP::get(Builder.getFloatTy(), std::stod(literal->value));
        auto value = std::stoll(literal->value);
        if (value > INT32_MAX) throw std::runtime_error("Integer literal out of range");
        return Builder.getInt32(static_cast<int32_t>(value));
    }
    if (auto* literal = dynamic_cast<const BooleanLiteral*>(&expression)) return Builder.getInt1(literal->value);
    if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression)) {
        if (semanticAnalysis && semanticAnalysis->Reference(*identifier)) {
            auto variable = ResolveLValue(expression);
            return Builder.CreateLoad(variable.type, variable.storage, identifier->name + ".value");
        }
        if (semanticAnalysis) if (auto* target = semanticAnalysis->ReferenceTarget(*identifier); target && target->returnsReference) {
            auto* result = EmitBareCall(*identifier);
            return Builder.CreateLoad(GetType(target->returnType), result);
        }
        if (semanticAnalysis) if (auto* target = semanticAnalysis->CallTarget(*identifier)) {
            auto* result = EmitBareCall(*identifier);
            if (target->returnsReference) return Builder.CreateLoad(GetType(target->returnType), result);
            return result;
        }
        auto variable = variables.find(identifier->name);
        auto global = globalVariables.find(identifier->name);
        bool isGlobal = variable != variables.end() && global != globalVariables.end() && variable->second.id == global->second.id;
        if ((variable == variables.end() || isGlobal) && currentMethod && types.at(currentMethod->owner).fields.contains(identifier->name)) {
            auto field = ResolveLValue(expression);
            return Builder.CreateLoad(field.type, field.storage);
        }
        if (!variables.contains(identifier->name) && !functions.contains(identifier->name) && currentMethod && types.at(currentMethod->owner).methods.contains(identifier->name)) {
            CallExpr call; call.name = identifier->name;
            return EmitMethodCall(call);
        }
        auto it = variables.find(identifier->name);
        if (it == variables.end()) {
            if (inaccessibleVariables.contains(identifier->name)) throw std::runtime_error("Variable requires capture: " + identifier->name);
            if (functions.contains(identifier->name)) {
                std::vector<std::unique_ptr<Expression>> noArguments;
                return EmitCall(identifier->name, noArguments);
            }
            throw std::runtime_error("Undefined variable: " + identifier->name);
        }
        return Builder.CreateLoad(it->second.type, it->second.storage, identifier->name + ".value");
    }
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
        if (unary->op == "&") return ResolveLValue(*unary->operand).storage;
        if (unary->op == "++" || unary->op == "--") {
            auto var = MutableVariable(*unary->operand);
            if (!var.type->isIntegerTy(32)) throw std::runtime_error("Increment requires int32");
            auto* old = Builder.CreateLoad(var.type, var.storage);
            auto* value = unary->op == "++" ? Builder.CreateAdd(old, Builder.getInt32(1)) : Builder.CreateSub(old, Builder.getInt32(1));
            Builder.CreateStore(value, var.storage);
            return unary->postfix ? old : value;
        }
        auto* value = EmitExpression(*unary->operand);
        if (value->getType()->isFloatTy() && (unary->op == "+" || unary->op == "-"))
            return unary->op == "+" ? value : Builder.CreateFNeg(value);
        if (unary->op == "!") {
            if (!value->getType()->isIntegerTy(1)) throw std::runtime_error("Logical NOT requires bool");
            return Builder.CreateNot(value);
        }
        if (!value->getType()->isIntegerTy(32)) throw std::runtime_error("Unary operator requires int32");
        if (unary->op == "+") return value;
        if (unary->op == "-") return Builder.CreateNeg(value);
        if (unary->op == "~") return Builder.CreateNot(value);
    }
    if (auto* chain = dynamic_cast<const ComparisonExpr*>(&expression)) {
        auto* fn = Builder.GetInsertBlock()->getParent();
        auto* done = llvm::BasicBlock::Create(Context, "compare.end", fn);
        std::vector<llvm::BasicBlock*> failures;
        auto* left = EmitExpression(*chain->operands[0]);
        for (size_t i = 0; i < chain->operators.size(); ++i) {
            auto* right = EmitExpression(*chain->operands[i + 1]);
            auto* comparison = EmitBinary(chain->operators[i], left, right);
            auto* next = llvm::BasicBlock::Create(Context, "compare.next", fn);
            failures.push_back(Builder.GetInsertBlock());
            Builder.CreateCondBr(comparison, next, done);
            Builder.SetInsertPoint(next);
            left = right;
        }
        auto* success = Builder.GetInsertBlock();
        Builder.CreateBr(done);
        Builder.SetInsertPoint(done);
        auto* result = Builder.CreatePHI(Builder.getInt1Ty(), static_cast<unsigned>(failures.size() + 1));
        for (auto* block : failures) result->addIncoming(Builder.getFalse(), block);
        result->addIncoming(Builder.getTrue(), success);
        return result;
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        const auto& op = binary->op;
        if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=") {
            auto var = MutableVariable(*binary->left);
            if (semanticAnalysis->IsReinitialization(*binary)) {
                auto found = typesByLayout.find(var.type);
                if (found == typesByLayout.end() || !var.ownershipActive)
                    throw std::runtime_error("Object reinitialization requires tracked ownership");
                auto* value = EmitDirectValue(*binary->right, var.storage, *found->second);
                Builder.CreateStore(Builder.getTrue(), var.ownershipActive);
                return value;
            }
            if (var.type->isStructTy() && op != "=") throw std::runtime_error("Compound assignment requires a scalar value");
            auto* old = op == "=" ? nullptr : Builder.CreateLoad(var.type, var.storage);
            auto* value = EmitExpression(*binary->right);
            if (value->getType() != var.type) throw std::runtime_error("Assignment type mismatch");
            if (old) value = EmitBinary(op.substr(0, op.size() - 1), old, value);
            Builder.CreateStore(value, var.storage);
            return value;
        }
        auto* left = EmitExpression(*binary->left);
        if (op == "&&" || op == "||" || op == "!&&" || op == "!||") {
            if (!left->getType()->isIntegerTy(1)) throw std::runtime_error("Logical operator requires bool operands");
            const bool isAnd = op == "&&" || op == "!&&";
            const bool negate = op == "!&&" || op == "!||";
            auto* fn = Builder.GetInsertBlock()->getParent();
            auto* start = Builder.GetInsertBlock();
            auto* rhs = llvm::BasicBlock::Create(Context, "logic.rhs", fn);
            auto* done = llvm::BasicBlock::Create(Context, "logic.end", fn);
            Builder.CreateCondBr(left, isAnd ? rhs : done, isAnd ? done : rhs);
            Builder.SetInsertPoint(rhs);
            auto* right = EmitExpression(*binary->right);
            if (!right->getType()->isIntegerTy(1)) throw std::runtime_error("Logical operator requires bool operands");
            auto* endRhs = Builder.GetInsertBlock();
            Builder.CreateBr(done);
            Builder.SetInsertPoint(done);
            auto* result = Builder.CreatePHI(Builder.getInt1Ty(), 2);
            result->addIncoming(Builder.getInt1(!isAnd), start);
            result->addIncoming(right, endRhs);
            return negate ? Builder.CreateNot(result) : result;
        }
        return EmitBinary(op, left, EmitExpression(*binary->right));
    }
    throw std::runtime_error("Unsupported expression");
}

void LLVM::EmitBody(
    const std::vector<std::unique_ptr<ASTNode>>& body
) {
    cleanupScopes.emplace_back();
    auto outerVariables = variables;
    auto outerBindings = bindings;
    auto outerFunctions = functions;
    auto outerInaccessible = inaccessibleVariables;
    std::unordered_set<std::string> localNames;

    auto* pointerType = llvm::PointerType::getUnqual(Context);

    llvm::Type* printfArguments[] = {
        pointerType
    };

    auto* printfType = llvm::FunctionType::get(
        Builder.getInt32Ty(),
        printfArguments,
        true
    );

    auto printfFunction = Module.getOrInsertFunction(
        "printf",
        printfType
    );

    for (const auto& node : body) {
        diagnosticOffset = node->offset;

        if (dynamic_cast<TypeDeclaration*>(node.get())) continue;

        if (auto* declaration = dynamic_cast<FunctionDeclaration*>(node.get())) {
            auto existing = functions.find(declaration->name);
            if (existing != functions.end() && existing->second->declaration == declaration) continue;
            if (!localNames.insert(declaration->name).second) throw std::runtime_error("Duplicate declaration: " + declaration->name);
            variables.erase(declaration->name);
            inaccessibleVariables.erase(declaration->name);
            auto symbol = DeclareFunction(*declaration, true);
            functions[declaration->name] = symbol;
            functionDeclarations[declaration] = symbol;
            EmitFunctionBody(symbol);
            continue;
        }
        if (auto* capture = dynamic_cast<CaptureDeclaration*>(node.get())) {
            if (!validCaptures.contains(capture)) throw std::runtime_error("capture must be declared directly in a local function");
            continue;
        }
        if (Builder.GetInsertBlock()->hasTerminator()) {
            throw std::runtime_error("Unreachable statement after return");
        }
        if (auto* statement = dynamic_cast<ReturnStatement*>(node.get())) {
            if (!valueScopes.empty()) {
                if (!statement->value) throw std::runtime_error("Value scope requires a return value");
                auto& scope = *valueScopes.back();
                const auto& returnType = semanticAnalysis->ReturnValueType(*statement);
                if (semanticAnalysis->NeedsDestruction(returnType)) {
                    auto& type = types.at(returnType);
                    if (scope.type && scope.type != type.type) throw std::runtime_error("Value scope return types do not match");
                    if (!scope.objectStorage) {
                        auto& entry = Builder.GetInsertBlock()->getParent()->getEntryBlock();
                        llvm::IRBuilder<> entryBuilder(&entry, entry.begin());
                        scope.objectStorage = entryBuilder.CreateAlloca(type.type, nullptr, "value.object");
                        scope.objectActive = entryBuilder.CreateAlloca(Builder.getInt1Ty(), nullptr, "value.active");
                        scope.objectType = &type;
                        llvm::IRBuilder<> initializer(temporaryFrames.at(scope.temporaryFrame).marker);
                        initializer.CreateStore(Builder.getFalse(), scope.objectActive);
                        temporaryFrames.at(scope.temporaryFrame).cleanups.push_back({scope.objectStorage, &type, scope.objectActive});
                    }
                    scope.type = type.type;
                    BeginFullExpression();
                    EmitDirectValue(*statement->value, scope.objectStorage, type);
                    EndFullExpression();
                    if (scope.objectActive) Builder.CreateStore(Builder.getTrue(), scope.objectActive);
                    EmitCleanups(scope.cleanupDepth);
                    scope.returns.emplace_back(nullptr, Builder.GetInsertBlock());
                    Builder.CreateBr(scope.exit);
                    continue;
                }
                auto* value = EmitFullExpression(*statement->value);
                if (value->getType()->isVoidTy()) throw std::runtime_error("Value scope cannot return void");
                if (scope.type && scope.type != value->getType()) throw std::runtime_error("Value scope return types do not match");
                scope.type = value->getType();
                EmitCleanups(scope.cleanupDepth);
                scope.returns.emplace_back(value, Builder.GetInsertBlock());
                Builder.CreateBr(scope.exit);
                continue;
            }
            if (semanticAnalysis->IsReferenceReturn(*statement)) {
                auto result = ResolveLValue(*statement->value);
                EmitCleanups(0);
                Builder.CreateRet(result.storage);
                continue;
            }
            auto* resultType = Builder.GetInsertBlock()->getParent()->getReturnType();
            if (currentReturnStorage) {
                auto* construct = statement->value ? dynamic_cast<const ConstructExpr*>(statement->value.get()) : nullptr;
                auto* move = statement->value ? dynamic_cast<const MoveExpr*>(statement->value.get()) : nullptr;
                if (!construct && !move) throw std::runtime_error("Destructible return requires direct construction");
                BeginFullExpression();
                if (construct) EmitConstruction(currentReturnStorage, *construct);
                else Builder.CreateStore(EmitMove(*move), currentReturnStorage);
                EndFullExpression();
                EmitCleanups(0);
                Builder.CreateRetVoid();
            } else if (!statement->value) {
                if (!resultType->isVoidTy()) throw std::runtime_error("Return value required");
                EmitCleanups(0);
                Builder.CreateRetVoid();
            } else {
                auto* value = EmitFullExpression(*statement->value);
                if (value->getType() != resultType) throw std::runtime_error("Invalid return value type");
                EmitCleanups(0);
                Builder.CreateRet(value);
            }
        }
        else if (auto* block = dynamic_cast<Block*>(node.get())) {
            EmitBody(block->statements);
        }
        else if (auto* branch = dynamic_cast<IfStatement*>(node.get())) {
            auto* condition = EmitCondition(*branch->condition);
            auto* fn = Builder.GetInsertBlock()->getParent();
            auto* thenBlock = llvm::BasicBlock::Create(Context, "if.then", fn);
            auto* elseBlock = llvm::BasicBlock::Create(Context, "if.else", fn);
            auto* done = llvm::BasicBlock::Create(Context, "if.end", fn);
            Builder.CreateCondBr(condition, thenBlock, elseBlock);
            Builder.SetInsertPoint(thenBlock);
            EmitBody(branch->thenBranch->statements);
            bool thenTerminated = Builder.GetInsertBlock()->hasTerminator();
            if (!thenTerminated) Builder.CreateBr(done);
            Builder.SetInsertPoint(elseBlock);
            if (branch->elseBranch) EmitBody(branch->elseBranch->statements);
            bool elseTerminated = Builder.GetInsertBlock()->hasTerminator();
            if (!elseTerminated) Builder.CreateBr(done);
            Builder.SetInsertPoint(done);
            if (thenTerminated && elseTerminated) Builder.CreateUnreachable();
        }
        else if (auto* loop = dynamic_cast<LoopStatement*>(node.get())) {
            auto savedVariables = variables;
            auto savedBindings = bindings;
            cleanupScopes.emplace_back();
            if (auto* declaration = dynamic_cast<VariableDeclaration*>(loop->initializer.get())) EmitVariable(*declaration);
            else if (auto* initializer = dynamic_cast<Expression*>(loop->initializer.get())) EmitFullExpression(*initializer);
            auto* fn = Builder.GetInsertBlock()->getParent();
            auto* conditionBlock = llvm::BasicBlock::Create(Context, "loop.condition", fn);
            auto* bodyBlock = llvm::BasicBlock::Create(Context, "loop.body", fn);
            auto* stepBlock = llvm::BasicBlock::Create(Context, "loop.step", fn);
            auto* done = llvm::BasicBlock::Create(Context, "loop.end", fn);
            Builder.CreateBr(loop->kind == LoopStatement::Kind::DoWhile ? bodyBlock : conditionBlock);
            Builder.SetInsertPoint(conditionBlock);
            auto* condition = loop->condition ? EmitCondition(*loop->condition) : Builder.getTrue();
            Builder.CreateCondBr(condition, bodyBlock, done);
            Builder.SetInsertPoint(bodyBlock);
            loops.emplace_back(done, stepBlock);
            loopCleanupDepths.push_back(cleanupScopes.size());
            EmitBody(loop->body->statements);
            loopCleanupDepths.pop_back();
            loops.pop_back();
            if (!Builder.GetInsertBlock()->hasTerminator()) Builder.CreateBr(stepBlock);
            Builder.SetInsertPoint(stepBlock);
            if (loop->step) EmitFullExpression(*loop->step);
            Builder.CreateBr(conditionBlock);
            Builder.SetInsertPoint(done);
            EmitCleanups(cleanupScopes.size() - 1);
            cleanupScopes.pop_back();
            variables = std::move(savedVariables);
            bindings = std::move(savedBindings);
        }
        else if (auto* control = dynamic_cast<LoopControl*>(node.get())) {
            if (loops.empty()) throw std::runtime_error("break or continue outside loop");
            if (!valueScopes.empty() && loops.size() <= valueScopes.back()->loopDepth)
                throw std::runtime_error("Cannot leave a value scope with break or continue");
            EmitCleanups(loopCleanupDepths.back());
            Builder.CreateBr(control->isBreak ? loops.back().first : loops.back().second);
        }
        // String statement
        else if (auto* statement = dynamic_cast<StringStatement*>(node.get())) {
            EmitString(*statement);
        }

        // Variable declaration
        else if (auto* declaration =
            dynamic_cast<VariableDeclaration*>(node.get()))
        {
            if (!localNames.insert(declaration->name).second) {
                throw std::runtime_error(
                    "Duplicate variable: " + declaration->name
                );
            }

            EmitVariable(*declaration);
        }

        else if (auto* expression = dynamic_cast<Expression*>(node.get())) {
            EmitFullExpression(*expression);
        }

        // Format string
        else if (auto* statement =
            dynamic_cast<FormatStringStatement*>(node.get()))
        {
            std::string format;
            std::vector<llvm::Value*> arguments;

            for (const auto& part : statement->parts) {

                if (auto* text =
                    dynamic_cast<FormatText*>(part.get()))
                {
                    for (char c : text->value) {
                        if (c == '%') {
                            format += "%%";
                        }
                        else {
                            format += c;
                        }
                    }
                }

                else if (auto* identifier =
                    dynamic_cast<IdentifierExpr*>(part.get()))
                {
                    auto it = variables.find(identifier->name);

                    if (it == variables.end() && (!currentMethod || !types.at(currentMethod->owner).fields.contains(identifier->name))) {
                        throw std::runtime_error(
                            "Undefined variable: " + identifier->name
                        );
                    }

                    llvm::Value* value = EmitExpression(*identifier);
                    auto* type = value->getType();
                    if (type->isFloatTy()) {
                        format += "%g";
                        value = Builder.CreateFPExt(value, Builder.getDoubleTy());
                    } else if (type->isIntegerTy(32) || type->isIntegerTy(1)) {
                        format += "%d";
                        if (type->isIntegerTy(1)) value = Builder.CreateZExt(value, Builder.getInt32Ty());
                    } else throw std::runtime_error("Unsupported format string type: " + identifier->name);
                    arguments.push_back(value);
                }

                else {
                    throw std::runtime_error(
                        "Unsupported format string expression"
                    );
                }
            }

            std::vector<llvm::Value*> callArguments;

            callArguments.push_back(CreateString(format));

            for (auto* argument : arguments) {
                callArguments.push_back(argument);
            }

            Builder.CreateCall(
                printfFunction,
                callArguments
            );
        }

        else {
            throw std::runtime_error(
                "Unsupported AST node in function"
            );
        }
    }
    if (!Builder.GetInsertBlock()->hasTerminator()) EmitCleanups(cleanupScopes.size() - 1);
    cleanupScopes.pop_back();
    variables = std::move(outerVariables);
    bindings = std::move(outerBindings);
    functions = std::move(outerFunctions);
    inaccessibleVariables = std::move(outerInaccessible);
}

std::shared_ptr<LLVM::FunctionSymbol> LLVM::DeclareFunction(const FunctionDeclaration& declaration, bool local) {
    std::vector<Capture> captures;
    std::unordered_set<std::string> names;
    for (const auto& parameter : declaration.parameters) {
        if (!names.insert(parameter.name).second) throw std::runtime_error("Duplicate parameter: " + parameter.name);
        if (GetType(parameter.type)->isVoidTy()) throw std::runtime_error("Parameter cannot have void type");
    }
    for (const auto& node : declaration.body) {
        auto* capture = dynamic_cast<const CaptureDeclaration*>(node.get());
        if (!capture) continue;
        if (!local) throw std::runtime_error("capture requires a local function");
        for (const auto& name : capture->names) {
            if (!names.insert(name).second) throw std::runtime_error("Duplicate capture or parameter: " + name);
            auto variable = variables.find(name);
            if (variable == variables.end()) {
                if (inaccessibleVariables.contains(name)) throw std::runtime_error("Variable requires capture in enclosing function: " + name);
                throw std::runtime_error("Cannot capture undefined variable: " + name);
            }
            captures.push_back(Capture{name, variable->second});
        }
    }
    for (const auto& node : declaration.body) {
        if (auto* variable = dynamic_cast<const VariableDeclaration*>(node.get())) {
            if (names.contains(variable->name)) throw std::runtime_error("Duplicate variable: " + variable->name);
        }
        if (auto* function = dynamic_cast<const FunctionDeclaration*>(node.get())) {
            if (names.contains(function->name)) throw std::runtime_error("Duplicate declaration: " + function->name);
        }
    }
    std::vector<llvm::Type*> parameterTypes;
    if (!declaration.returnsReference && semanticAnalysis->NeedsDestruction(declaration.returnType)) parameterTypes.push_back(llvm::PointerType::getUnqual(Context));
    parameterTypes.insert(parameterTypes.end(), captures.size(), llvm::PointerType::getUnqual(Context));
    for (const auto& parameter : declaration.parameters)
        parameterTypes.push_back(parameter.isReference || semanticAnalysis->NeedsDestruction(parameter.type)
            ? llvm::PointerType::getUnqual(Context) : GetType(parameter.type));
    auto* resultType = declaration.returnsReference ? static_cast<llvm::Type*>(llvm::PointerType::getUnqual(Context)) :
        semanticAnalysis->NeedsDestruction(declaration.returnType) ? static_cast<llvm::Type*>(Builder.getVoidTy()) : GetType(declaration.returnType);
    auto* functionType = llvm::FunctionType::get(resultType, parameterTypes, false);
    auto name = local ? "__c4r_local_" + std::to_string(nextFunctionId++) : declaration.name;
    if (!local && Module.getNamedValue(name)) throw std::runtime_error("Duplicate function: " + name);
    auto* function = llvm::Function::Create(functionType, local ? llvm::Function::InternalLinkage : llvm::Function::ExternalLinkage, name, Module);
    return std::make_shared<FunctionSymbol>(FunctionSymbol{function, &declaration, std::move(captures)});
}

void LLVM::EmitFunctionBody(const std::shared_ptr<FunctionSymbol>& symbol) {
    auto savedInsert = Builder.saveIP();
    auto outerVariables = variables;
    auto outerBindings = bindings;
    auto outerInaccessible = inaccessibleVariables;
    auto outerCaptures = validCaptures;
    auto outerValueScopes = valueScopes;
    auto outerLoops = loops;
    auto outerLoopCleanupDepths = loopCleanupDepths;
    auto outerCleanupScopes = std::move(cleanupScopes);
    auto outerTemporaryFrames = std::move(temporaryFrames);
    auto outerTemporaryValues = std::move(temporaryValues);
    auto* outerReturnStorage = currentReturnStorage;
    bool outerHasMoves = currentHasMoves;
    auto outerMethod = currentMethod;
    currentMethod = symbol->owner.empty() ? nullptr : symbol;
    variables = globalVariables; bindings.clear(); valueScopes.clear(); loops.clear(); loopCleanupDepths.clear(); cleanupScopes.clear(); temporaryFrames.clear(); temporaryValues.clear(); currentReturnStorage = nullptr; validCaptures.clear();
    for (const auto& [name, variable] : globalVariables) bindings.insert_or_assign(variable.id, variable);
    for (const auto& [name, variable] : outerVariables) {
        auto global = globalVariables.find(name);
        if (global == globalVariables.end() || global->second.id != variable.id) {
            inaccessibleVariables.insert(name);
            variables.erase(name);
        }
    }
    const auto& declaration = *symbol->declaration;
    currentHasMoves = semanticAnalysis->FunctionsWithMoves().contains(&declaration);
    for (const auto& node : declaration.body) {
        if (auto* capture = dynamic_cast<const CaptureDeclaration*>(node.get())) validCaptures.insert(capture);
    }
    auto* entry = llvm::BasicBlock::Create(Context, "entry", symbol->function);
    Builder.SetInsertPoint(entry);
    auto argument = symbol->function->arg_begin();
    if (!declaration.returnsReference && semanticAnalysis->NeedsDestruction(declaration.returnType)) currentReturnStorage = &*argument++;
    if (currentMethod) {
        Variable self{&*argument++, types.at(symbol->owner).type, true, nextVariableId++, true};
        variables.insert_or_assign("$self", self);
        bindings.insert_or_assign(self.id, self);
    }
    for (const auto& capture : symbol->captures) {
        Variable variable{&*argument++, capture.variable.type, capture.variable.isMutable, capture.variable.id};
        variables.insert_or_assign(capture.name, variable);
        bindings.insert_or_assign(variable.id, variable);
        inaccessibleVariables.erase(capture.name);
    }
    cleanupScopes.emplace_back();
    for (const auto& parameter : declaration.parameters) {
        auto* type = GetType(parameter.type);
        llvm::Value* storage;
        llvm::AllocaInst* active = nullptr;
        if (parameter.isReference) {
            storage = &*argument++;
        } else if (semanticAnalysis->NeedsDestruction(parameter.type)) {
            storage = &*argument++;
            if (currentHasMoves) {
                active = Builder.CreateAlloca(Builder.getInt1Ty(), nullptr, parameter.name + ".owned");
                Builder.CreateStore(Builder.getTrue(), active);
            }
            cleanupScopes.back().push_back({storage, &types.at(parameter.type), active});
        } else {
            storage = Builder.CreateAlloca(type, nullptr, parameter.name);
            Builder.CreateStore(&*argument++, storage);
        }
        Variable variable{storage, type, parameter.isReference ? parameter.isMutableReference : parameter.isMutable, nextVariableId++};
        variable.ownershipActive = active;
        bindingDeclarations[&parameter] = variable.id;
        variables.insert_or_assign(parameter.name, variable);
        bindings.insert_or_assign(variable.id, variable);
        inaccessibleVariables.erase(parameter.name);
    }
    for (const auto& node : declaration.body) {
        if (auto* variable = dynamic_cast<const VariableDeclaration*>(node.get())) {
            for (const auto& parameter : declaration.parameters)
                if (parameter.name == variable->name) throw std::runtime_error("Duplicate variable: " + variable->name);
        }
    }
    EmitBody(declaration.body);
    if (!Builder.GetInsertBlock()->hasTerminator()) {
        EmitCleanups(0);
        auto* returnType = symbol->function->getReturnType();
        if (returnType->isVoidTy()) Builder.CreateRetVoid();
        else if (symbol->function->hasExternalLinkage() && (declaration.name == "main" || declaration.name == "start" || declaration.name == "entry") && declaration.parameters.empty() && returnType->isIntegerTy(32)) Builder.CreateRet(Builder.getInt32(0));
        else throw std::runtime_error("Function must return a value: " + declaration.name);
    }
    variables = std::move(outerVariables);
    bindings = std::move(outerBindings);
    inaccessibleVariables = std::move(outerInaccessible);
    validCaptures = std::move(outerCaptures);
    valueScopes = std::move(outerValueScopes);
    loops = std::move(outerLoops);
    loopCleanupDepths = std::move(outerLoopCleanupDepths);
    cleanupScopes = std::move(outerCleanupScopes);
    temporaryFrames = std::move(outerTemporaryFrames);
    temporaryValues = std::move(outerTemporaryValues);
    currentReturnStorage = outerReturnStorage;
    currentHasMoves = outerHasMoves;
    currentMethod = std::move(outerMethod);
    Builder.restoreIP(savedInsert);
}

void LLVM::EmitImplicitMain(const Program& program) {
    auto* functionType = llvm::FunctionType::get(
        Builder.getInt32Ty(),
        false
    );

    auto* function = llvm::Function::Create(
        functionType,
        llvm::Function::ExternalLinkage,
        "main",
        Module
    );

    auto* entry = llvm::BasicBlock::Create(
        Context,
        "entry",
        function
    );

    Builder.SetInsertPoint(entry);

    variables.clear();
    EmitBody(program.children);

    if (!Builder.GetInsertBlock()->hasTerminator()) Builder.CreateRet(Builder.getInt32(0));
}

void LLVM::Generate(const Program& program, const Semantic& semantic) {
    semanticAnalysis = &semantic;
    const auto& globalSymbols = semantic.Globals();
    std::vector<std::string> names;
    names.reserve(globalSymbols.size());
    for (const auto& [name, symbol] : globalSymbols) names.push_back(name);
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        const auto& symbol = globalSymbols.at(name);
        if (symbol.kind != Semantic::Kind::Type) continue;
        auto* declaration = static_cast<const TypeDeclaration*>(symbol.declaration);
        types.emplace(name, TypeInfo{declaration, llvm::StructType::create(Context, name), {}, {}});
    }
    for (auto& [name, type] : types) typesByLayout.emplace(type.type, &type);
    std::unordered_set<std::string> layingOut;
    std::function<void(const TypeDeclaration*)> layoutType = [&](const TypeDeclaration* declaration) {
        auto& type = types.at(declaration->name);
        if (!type.type->isOpaque()) return;
        if (!layingOut.insert(declaration->name).second) throw std::runtime_error("Invalid or incomplete field type: " + declaration->name);
        std::vector<llvm::Type*> fields;
        const auto& memberSymbols = semantic.Members(declaration->name);
        for (const auto& member : declaration->members) {
            if (auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get())) {
                if (types.contains(field->type)) layoutType(types.at(field->type).declaration);
                auto* fieldType = GetType(field->type);
                if (fieldType->isVoidTy() || (fieldType->isStructTy() && !fieldType->isSized())) throw std::runtime_error("Invalid or incomplete field type: " + field->type);
                type.fields.emplace(field->name, FieldInfo{field, memberSymbols.at(field->name).isPublic, static_cast<unsigned>(fields.size())});
                fields.push_back(fieldType);
            }
        }
        type.type->setBody(fields);
        layingOut.erase(declaration->name);
        for (const auto& member : declaration->members) {
            auto* method = dynamic_cast<const FunctionDeclaration*>(member.declaration.get());
            if (!method) continue;
            if (dynamic_cast<const ConstructorDeclaration*>(method) || dynamic_cast<const DestructorDeclaration*>(method)) continue;
            std::vector<llvm::Type*> args;
            if (!method->returnsReference && semanticAnalysis->NeedsDestruction(method->returnType)) args.push_back(llvm::PointerType::getUnqual(Context));
            args.push_back(llvm::PointerType::getUnqual(Context));
            std::unordered_set<std::string> parameters;
            for (const auto& parameter : method->parameters) {
                if (!parameters.insert(parameter.name).second) throw std::runtime_error("Duplicate parameter: " + parameter.name);
                auto* parameterType = GetType(parameter.type);
                if (parameterType->isVoidTy()) throw std::runtime_error("Parameter cannot have void type");
                args.push_back(parameter.isReference || semanticAnalysis->NeedsDestruction(parameter.type)
                    ? llvm::PointerType::getUnqual(Context) : parameterType);
            }
            auto* resultType = method->returnsReference ? static_cast<llvm::Type*>(llvm::PointerType::getUnqual(Context)) :
                semanticAnalysis->NeedsDestruction(method->returnType) ? static_cast<llvm::Type*>(Builder.getVoidTy()) : GetType(method->returnType);
            auto* signature = llvm::FunctionType::get(resultType, args, false);
            auto* function = llvm::Function::Create(signature, llvm::Function::InternalLinkage, "__c4r_method_" + std::to_string(nextFunctionId++), Module);
            auto symbol = std::make_shared<FunctionSymbol>(FunctionSymbol{function, method, {}, declaration->name});
            type.methods.emplace(method->name, std::make_pair(symbol, memberSymbols.at(method->name).isPublic));
        }
        auto declareSpecial = [&](const FunctionDeclaration& declaration) {
            std::vector<llvm::Type*> args{llvm::PointerType::getUnqual(Context)};
            for (const auto& parameter : declaration.parameters)
                args.push_back(parameter.isReference || semanticAnalysis->NeedsDestruction(parameter.type)
                    ? llvm::PointerType::getUnqual(Context) : GetType(parameter.type));
            auto* signature = llvm::FunctionType::get(Builder.getVoidTy(), args, false);
            auto* function = llvm::Function::Create(signature, llvm::Function::InternalLinkage,
                "__c4r_lifecycle_" + std::to_string(nextFunctionId++), Module);
            return std::make_shared<FunctionSymbol>(FunctionSymbol{function, &declaration, {}, type.declaration->name});
        };
        const auto& lifecycle = semantic.Lifecycle(declaration->name);
        for (const auto& constructor : lifecycle.constructors) {
            auto* ast = static_cast<const ConstructorDeclaration*>(constructor.declaration);
            type.constructors.emplace(ast, declareSpecial(*ast));
        }
        if (lifecycle.destructor)
            type.destructor = declareSpecial(*static_cast<const DestructorDeclaration*>(lifecycle.destructor->declaration));
    };
    for (const auto& name : names) {
        const auto& symbol = globalSymbols.at(name);
        if (symbol.kind == Semantic::Kind::Type) layoutType(static_cast<const TypeDeclaration*>(symbol.declaration));
    }
    for (const auto& name : names) {
        const auto& symbol = globalSymbols.at(name);
        if (symbol.kind == Semantic::Kind::Function) {
            auto* declaration = static_cast<const FunctionDeclaration*>(symbol.declaration);
            functions[name] = DeclareFunction(*declaration, false);
            functionDeclarations[declaration] = functions[name];
        }
    }

    const auto& entryName = semantic.EntryPoint();
    if (!entryName.empty()) {
        for (const auto& name : names) {
            const auto& symbol = globalSymbols.at(name);
            if (symbol.kind != Semantic::Kind::GlobalVariable) continue;
            auto* declaration = static_cast<const VariableDeclaration*>(symbol.declaration);
            diagnosticOffset = declaration->offset;
            auto* type = GetType(declaration->type);
            if (type->isVoidTy()) throw std::runtime_error("Variable cannot have void type");
            llvm::Constant* initial = nullptr;
            if (declaration->aggregateInit) {
                if (!types.contains(declaration->type)) throw std::runtime_error("Brace initialization requires a user-defined type");
                initial = ConstantObject(types.at(declaration->type));
            } else initial = ConstantInitializer(*declaration->initialValue);
            if (initial->getType() != type) throw std::runtime_error("Initializer type mismatch: " + declaration->name);
            auto* storage = new llvm::GlobalVariable(Module, type, !declaration->isMutable,
                llvm::GlobalValue::InternalLinkage, initial, "__c4r_global_" + declaration->name);
            Variable variable{storage, type, declaration->isMutable, nextVariableId++};
            globalVariables.emplace(declaration->name, variable);
            bindingDeclarations[declaration] = variable.id;
        }
    }

    for (const auto& name : names)
        if (globalSymbols.at(name).kind == Semantic::Kind::Function) EmitFunctionBody(functions.at(name));

    for (auto& [name, type] : types)
    {
        for (auto& [methodName, method] : type.methods) EmitFunctionBody(method.first);
        for (auto& [constructor, function] : type.constructors) EmitFunctionBody(function);
        if (type.destructor) EmitFunctionBody(type.destructor);
    }

    if (entryName.empty()) {
        EmitImplicitMain(program);
    } else if (entryName != "main") {
        auto* type = llvm::FunctionType::get(Builder.getInt32Ty(), false);
        auto* wrapper = llvm::Function::Create(type, llvm::Function::ExternalLinkage, "main", Module);
        Builder.SetInsertPoint(llvm::BasicBlock::Create(Context, "entry", wrapper));
        Builder.CreateRet(Builder.CreateCall(functions.at(entryName)->function));
    }

    bool changed;
    do {
        changed = false;
        for (auto& [name, type] : types) for (auto& [methodName, entry] : type.methods) {
            auto& method = entry.first;
            if (!method->mutates) for (const auto& callee : method->calls) if (callee->mutates) { method->mutates = true; changed = true; break; }
        }
    } while (changed);
    for (const auto& method : immutableCalls) if (method->mutates) throw std::runtime_error("Cannot call mutating method on immutable instance: " + method->declaration->name);
    if (llvm::verifyModule(Module, &llvm::errs())) {
        throw std::runtime_error(
            "LLVM IR verification failed"
        );
    }
}

void LLVM::Print() {
    Module.print(llvm::outs(), nullptr);
}

void LLVM::WriteObject(const std::filesystem::path& path) {
    static const bool initialized = [] {
        if (llvm::InitializeNativeTarget())
            throw std::runtime_error("Failed to initialize native target");

        if (llvm::InitializeNativeTargetAsmPrinter())
            throw std::runtime_error("Failed to initialize native asm printer");

        return true;
    }();

    (void)initialized;

    const auto targetTriple = llvm::sys::getDefaultTargetTriple();
    const llvm::Triple triple{ llvm::StringRef(targetTriple) };

    std::string error;
    const llvm::Target* target =
        llvm::TargetRegistry::lookupTarget(triple, error);

    if (!target)
        throw std::runtime_error(error);

    llvm::TargetOptions options;

    std::unique_ptr<llvm::TargetMachine> targetMachine(
        target->createTargetMachine(
            triple,
            "generic",
            "",
            options,
            std::nullopt
        )
    );

    if (!targetMachine)
        throw std::runtime_error("Failed to create TargetMachine");

    this->Module.setTargetTriple(triple);
    this->Module.setDataLayout(targetMachine->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream output(
        path.string(),
        ec,
        llvm::sys::fs::OF_None
    );

    if (ec)
        throw std::system_error(ec, "Failed to create object file");

    llvm::legacy::PassManager pass;

    if (targetMachine->addPassesToEmitFile(
        pass,
        output,
        nullptr,
        llvm::CodeGenFileType::ObjectFile
    ))
    {
        throw std::runtime_error("Object file emission is unsupported");
    }

    pass.run(Module);
    output.flush();
}

void LLVM::LinkExecutable(
    const std::filesystem::path& objectPath,
    const std::filesystem::path& executablePath
) {
    std::vector<std::wstring> storage;

    storage.emplace_back(L"link.exe");
    storage.push_back(objectPath.wstring());

    storage.emplace_back(L"/NOLOGO");
    storage.emplace_back(L"/SUBSYSTEM:CONSOLE");
    storage.emplace_back(L"/MACHINE:X64");

    std::wstring outputArg = L"/OUT:";
    outputArg += executablePath.wstring();
    storage.push_back(std::move(outputArg));

    // Static MSVC CRT
    storage.emplace_back(L"/DEFAULTLIB:libcmt.lib");
    storage.emplace_back(L"/DEFAULTLIB:libvcruntime.lib");
    storage.emplace_back(L"/DEFAULTLIB:libucrt.lib");
    storage.emplace_back(L"/DEFAULTLIB:kernel32.lib");

    std::vector<const wchar_t*> arguments;

    for (const auto& arg : storage)
        arguments.push_back(arg.c_str());

    arguments.push_back(nullptr);

    // link.exe writes diagnostics to stdout; keep the compiler's diagnostics
    // on stderr while preserving stdout for its normal output and editor clients.
    llvm::outs().flush();
    std::cout.flush();
    std::fflush(stdout);
    int savedStdout = _dup(_fileno(stdout));
    if (savedStdout == -1) throw std::system_error(errno, std::generic_category(), "Failed to redirect linker output");
    if (_dup2(_fileno(stderr), _fileno(stdout)) != 0) {
        int error = errno;
        _close(savedStdout);
        throw std::system_error(error, std::generic_category(), "Failed to redirect linker output");
    }
    const auto result = _wspawnvp(_P_WAIT, L"link.exe", arguments.data());
    const int spawnError = errno;
    int restoreResult = _dup2(savedStdout, _fileno(stdout));
    int restoreError = errno;
    _close(savedStdout);
    if (restoreResult != 0) throw std::system_error(restoreError, std::generic_category(), "Failed to restore compiler output");

    if (result == -1)
        throw std::system_error(
            spawnError,
            std::generic_category(),
            "Failed to start link.exe"
        );

    if (result != 0) {
        throw std::runtime_error(
            std::format("link.exe failed with exit code {}", result)
        );
    }
}
