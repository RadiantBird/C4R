#include "IRFunctionEmitter.h"

#include "Semantic.h"

#include <stdexcept>
#include <utility>

namespace c4r::ir {

Variable IRFunctionEmitter::ResolveLValue(const Expression& expression) {
    if (semanticAnalysis) if (auto* target = semanticAnalysis->ReferenceTarget(expression); target && target->returnsReference) {
        llvm::Value* storage = nullptr;
        if (auto* call = dynamic_cast<const CallExpr*>(&expression)) storage = EmitCallExpression(*call);
        else if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression)) storage = EmitBareCall(*identifier);
        else if (auto* member = dynamic_cast<const MemberExpr*>(&expression)) storage = EmitBareMemberCall(*member);
        if (storage) return {storage, module.GetType(target->returnType), target->returnsMutableReference, 0};
    }
    auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression);
    if (identifier) {
        if (semanticAnalysis) if (auto* target = semanticAnalysis->CallTarget(*identifier)) {
            if (target->returnsReference)
                return {EmitBareCall(*identifier), module.GetType(target->returnType), target->returnsMutableReference, 0};
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
        if (currentMethod && (it == variables.end() || isGlobal) && module.types.at(currentMethod->owner).fields.contains(identifier->name)) {
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
            return {EmitBareMemberCall(*member), module.GetType(target->returnType), target->returnsMutableReference, 0};
        return MaterializeTemporary(expression);
    }
    return ResolveMember(ResolveLValue(*member->object), member->name);
}

Variable IRFunctionEmitter::ResolveMember(const Variable& object, const std::string& name) {
    if (auto found = module.typesByLayout.find(object.type); found != module.typesByLayout.end()) {
        auto& type = *found->second;
        auto field = type.fields.find(name);
        if (field == type.fields.end()) throw std::runtime_error("Unknown field: " + name);
        if (!field->second.isPublic && (!currentMethod || currentMethod->owner != type.declaration->name))
            throw std::runtime_error("Private member: " + name);
        auto* fieldType = module.GetType(field->second.declaration->type);
        bool constructorInitialization = currentMethod &&
            dynamic_cast<const ConstructorDeclaration*>(currentMethod->declaration) &&
            object.isReceiver && object.type == module.types.at(currentMethod->owner).type;
        return {Builder.CreateStructGEP(type.type, object.storage, field->second.index), fieldType,
                object.isMutable && (field->second.declaration->isMutable || constructorInitialization), 0, object.isReceiver};
    }
    throw std::runtime_error("Member access requires a user-defined type");
}

Variable IRFunctionEmitter::MutableVariable(const Expression& expression) {
    auto variable = ResolveLValue(expression);
    if (!variable.isMutable) throw std::runtime_error("Cannot modify immutable variable or field");
    if (variable.isReceiver && currentMethod) currentMethod->mutates = true;
    return variable;
}

llvm::Value* IRFunctionEmitter::EmitCondition(const Expression& expression, bool fullExpression) {
    auto* value = fullExpression ? EmitFullExpression(expression) : EmitExpression(expression);
    if (!value->getType()->isIntegerTy(1)) throw std::runtime_error("Condition must be bool");
    return value;
}

llvm::Value* IRFunctionEmitter::FinishValueScope(ValueScope& scope) {
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

llvm::Value* IRFunctionEmitter::EmitValueBlock(const Block& block, llvm::Value* destination, TypeInfo* type) {
    auto* fn = Builder.GetInsertBlock()->getParent();
    ValueScope scope{llvm::BasicBlock::Create(module.Context, "value.end", fn), nullptr, {}, loops.size(), cleanupScopes.size()};
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

llvm::Value* IRFunctionEmitter::EmitIfValue(const IfExpr& branch, llvm::Value* destination, TypeInfo* type) {
    auto* condition = EmitCondition(*branch.condition, false);
    auto* fn = Builder.GetInsertBlock()->getParent();
    auto* thenBlock = llvm::BasicBlock::Create(module.Context, "if.value.then", fn);
    auto* elseBlock = llvm::BasicBlock::Create(module.Context, "if.value.else", fn);
    ValueScope scope{llvm::BasicBlock::Create(module.Context, "if.value.end", fn), nullptr, {}, loops.size(), cleanupScopes.size()};
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

llvm::Value* IRFunctionEmitter::EmitExpression(const Expression& expression) {
    module.diagnosticOffset = expression.offset;
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
            return Builder.CreateLoad(module.GetType(target->returnType), result);
        return result;
    }
    if (auto* member = dynamic_cast<const MemberExpr*>(&expression)) return EmitMemberValue(*member);
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
    if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression)) return EmitIdentifierValue(*identifier);
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) return EmitUnary(*unary);
    if (auto* chain = dynamic_cast<const ComparisonExpr*>(&expression)) return EmitComparison(*chain);
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) return EmitBinaryExpression(*binary);
    throw std::runtime_error("Unsupported expression");
}

llvm::Value* IRFunctionEmitter::EmitMemberValue(const MemberExpr& member) {
    if (auto* referenceTarget = semanticAnalysis->ReferenceTarget(member); referenceTarget && referenceTarget->returnsReference) {
        auto* result = EmitBareMemberCall(member);
        return Builder.CreateLoad(module.GetType(referenceTarget->returnType), result);
    }
    if (auto* target = semanticAnalysis->CallTarget(member)) {
        auto* result = EmitBareMemberCall(member);
        if (target->returnsReference) return Builder.CreateLoad(module.GetType(target->returnType), result);
        return result;
    }
    auto object = ResolveLValue(*member.object);
    if (auto entry = module.typesByLayout.find(object.type); entry != module.typesByLayout.end() && entry->second->methods.contains(member.name)) {
        auto& type = *entry->second;
        auto symbol = type.methods.at(member.name).first;
        if (!type.methods.at(member.name).second && (!currentMethod || currentMethod->owner != type.declaration->name)) throw std::runtime_error("Private member: " + member.name);
        if (!symbol->declaration->parameters.empty()) throw std::runtime_error("Wrong number of arguments for method: " + member.name);
        if (!object.isMutable) immutableCalls.push_back(symbol);
        else if (object.isReceiver && currentMethod) currentMethod->calls.push_back(symbol);
        return EmitFunctionCall(symbol, {object.storage}, nullptr);
    }
    auto field = ResolveMember(object, member.name);
    return Builder.CreateLoad(field.type, field.storage);
}

llvm::Value* IRFunctionEmitter::EmitIdentifierValue(const IdentifierExpr& identifier) {
    if (semanticAnalysis && semanticAnalysis->Reference(identifier)) {
        auto variable = ResolveLValue(identifier);
        return Builder.CreateLoad(variable.type, variable.storage, identifier.name + ".value");
    }
    if (semanticAnalysis) if (auto* target = semanticAnalysis->ReferenceTarget(identifier); target && target->returnsReference) {
        auto* result = EmitBareCall(identifier);
        return Builder.CreateLoad(module.GetType(target->returnType), result);
    }
    if (semanticAnalysis) if (auto* target = semanticAnalysis->CallTarget(identifier)) {
        auto* result = EmitBareCall(identifier);
        if (target->returnsReference) return Builder.CreateLoad(module.GetType(target->returnType), result);
        return result;
    }
    auto variable = variables.find(identifier.name);
    auto global = globalVariables.find(identifier.name);
    bool isGlobal = variable != variables.end() && global != globalVariables.end() && variable->second.id == global->second.id;
    if ((variable == variables.end() || isGlobal) && currentMethod && module.types.at(currentMethod->owner).fields.contains(identifier.name)) {
        auto field = ResolveLValue(identifier);
        return Builder.CreateLoad(field.type, field.storage);
    }
    if (!variables.contains(identifier.name) && !functions.contains(identifier.name) && currentMethod && module.types.at(currentMethod->owner).methods.contains(identifier.name)) {
        CallExpr call; call.name = identifier.name;
        return EmitMethodCall(call);
    }
    auto it = variables.find(identifier.name);
    if (it == variables.end()) {
        if (inaccessibleVariables.contains(identifier.name)) throw std::runtime_error("Variable requires capture: " + identifier.name);
        if (functions.contains(identifier.name)) {
            std::vector<std::unique_ptr<Expression>> noArguments;
            return EmitCall(identifier.name, noArguments);
        }
        throw std::runtime_error("Undefined variable: " + identifier.name);
    }
    return Builder.CreateLoad(it->second.type, it->second.storage, identifier.name + ".value");
}

llvm::Value* IRFunctionEmitter::EmitUnary(const UnaryExpr& unary) {
    if (unary.op == "&") return ResolveLValue(*unary.operand).storage;
    if (unary.op == "++" || unary.op == "--") {
        auto var = MutableVariable(*unary.operand);
        if (!var.type->isIntegerTy(32)) throw std::runtime_error("Increment requires int32");
        auto* old = Builder.CreateLoad(var.type, var.storage);
        auto* value = unary.op == "++" ? Builder.CreateAdd(old, Builder.getInt32(1)) : Builder.CreateSub(old, Builder.getInt32(1));
        Builder.CreateStore(value, var.storage);
        return unary.postfix ? old : value;
    }
    auto* value = EmitExpression(*unary.operand);
    if (value->getType()->isFloatTy() && (unary.op == "+" || unary.op == "-"))
        return unary.op == "+" ? value : Builder.CreateFNeg(value);
    if (unary.op == "!") {
        if (!value->getType()->isIntegerTy(1)) throw std::runtime_error("Logical NOT requires bool");
        return Builder.CreateNot(value);
    }
    if (!value->getType()->isIntegerTy(32)) throw std::runtime_error("Unary operator requires int32");
    if (unary.op == "+") return value;
    if (unary.op == "-") return Builder.CreateNeg(value);
    if (unary.op == "~") return Builder.CreateNot(value);
    throw std::runtime_error("Unsupported expression");
}

llvm::Value* IRFunctionEmitter::EmitComparison(const ComparisonExpr& chain) {
    auto* fn = Builder.GetInsertBlock()->getParent();
    auto* done = llvm::BasicBlock::Create(module.Context, "compare.end", fn);
    std::vector<llvm::BasicBlock*> failures;
    auto* left = EmitExpression(*chain.operands[0]);
    for (size_t i = 0; i < chain.operators.size(); ++i) {
        auto* right = EmitExpression(*chain.operands[i + 1]);
        auto* comparison = operators.Emit(chain.operators[i], left, right);
        auto* next = llvm::BasicBlock::Create(module.Context, "compare.next", fn);
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

llvm::Value* IRFunctionEmitter::EmitBinaryExpression(const BinaryExpr& binary) {
    const auto& op = binary.op;
    if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=") {
        auto var = MutableVariable(*binary.left);
        if (semanticAnalysis->IsReinitialization(binary)) {
            auto found = module.typesByLayout.find(var.type);
            if (found == module.typesByLayout.end() || !var.ownershipActive)
                throw std::runtime_error("Object reinitialization requires tracked ownership");
            auto* value = EmitDirectValue(*binary.right, var.storage, *found->second);
            Builder.CreateStore(Builder.getTrue(), var.ownershipActive);
            return value;
        }
        if (var.type->isStructTy() && op != "=") throw std::runtime_error("Compound assignment requires a scalar value");
        auto* old = op == "=" ? nullptr : Builder.CreateLoad(var.type, var.storage);
        auto* value = EmitExpression(*binary.right);
        if (value->getType() != var.type) throw std::runtime_error("Assignment type mismatch");
        if (old) value = operators.Emit(op.substr(0, op.size() - 1), old, value);
        Builder.CreateStore(value, var.storage);
        return value;
    }
    auto* left = EmitExpression(*binary.left);
    if (op == "&&" || op == "||" || op == "!&&" || op == "!||") {
        if (!left->getType()->isIntegerTy(1)) throw std::runtime_error("Logical operator requires bool operands");
        const bool isAnd = op == "&&" || op == "!&&";
        const bool negate = op == "!&&" || op == "!||";
        auto* fn = Builder.GetInsertBlock()->getParent();
        auto* start = Builder.GetInsertBlock();
        auto* rhs = llvm::BasicBlock::Create(module.Context, "logic.rhs", fn);
        auto* done = llvm::BasicBlock::Create(module.Context, "logic.end", fn);
        Builder.CreateCondBr(left, isAnd ? rhs : done, isAnd ? done : rhs);
        Builder.SetInsertPoint(rhs);
        auto* right = EmitExpression(*binary.right);
        if (!right->getType()->isIntegerTy(1)) throw std::runtime_error("Logical operator requires bool operands");
        auto* endRhs = Builder.GetInsertBlock();
        Builder.CreateBr(done);
        Builder.SetInsertPoint(done);
        auto* result = Builder.CreatePHI(Builder.getInt1Ty(), 2);
        result->addIncoming(Builder.getInt1(!isAnd), start);
        result->addIncoming(right, endRhs);
        return negate ? Builder.CreateNot(result) : result;
    }
    return operators.Emit(op, left, EmitExpression(*binary.right));
}

}
