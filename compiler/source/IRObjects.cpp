#include "IRFunctionEmitter.h"

#include "Semantic.h"

#include <stdexcept>
#include <utility>

namespace c4r::ir {

void IRFunctionEmitter::EmitVariable(const VariableDeclaration& declaration) {
    auto* type = module.GetType(declaration.type);
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
        if (!module.types.contains(declaration.type)) throw std::runtime_error("Brace initialization requires a user-defined type");
        InitializeObject(storage, module.types.at(declaration.type));
        if (auto* constructor = semanticAnalysis->ConstructorTarget(declaration))
            Builder.CreateCall(module.types.at(declaration.type).constructors.at(constructor)->function, {storage});
    } else if (auto* construct = dynamic_cast<const ConstructExpr*>(declaration.initialValue.get())) {
        if (construct->type != declaration.type) throw std::runtime_error("Initializer type mismatch: " + declaration.name);
        EmitConstruction(storage, *construct);
    } else if (semanticAnalysis->NeedsDestruction(declaration.type)) {
        EmitDirectValue(*declaration.initialValue, storage, module.types.at(declaration.type));
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
    if (auto found = module.types.find(declaration.type); found != module.types.end())
        cleanupScopes.back().push_back({storage, &found->second, variable.ownershipActive});
}

void IRFunctionEmitter::EmitConstruction(llvm::Value* storage, const ConstructExpr& expression) {
    auto& type = module.types.at(expression.type);
    auto* declaration = semanticAnalysis->ConstructorTarget(expression);
    if (!declaration) throw std::runtime_error("Constructor was not resolved: " + expression.type);
    std::vector<llvm::Value*> args{storage};
    EmitArguments(expression.arguments, declaration->parameters, args);
    InitializeObject(storage, type);
    Builder.CreateCall(type.constructors.at(declaration)->function, args);
}

llvm::Value* IRFunctionEmitter::EmitDirectValue(const Expression& expression, llvm::Value* destination, TypeInfo& type) {
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

llvm::Value* IRFunctionEmitter::EmitMove(const MoveExpr& expression) {
    if (!currentHasMoves) throw std::runtime_error("Ownership analysis is unavailable for this move");
    auto source = ResolveLValue(*expression.operand);
    for (const auto& [name, global] : globalVariables)
        if (global.id == source.id) throw std::runtime_error("Moving global variables is not implemented yet");
    if (source.type->isStructTy() && !source.ownershipActive &&
        semanticAnalysis->NeedsDestruction(module.typesByLayout.at(source.type)->declaration->name))
        throw std::runtime_error("Moving this object is not implemented yet");
    auto* result = Builder.CreateLoad(source.type, source.storage, "move.value");
    if (source.ownershipActive) Builder.CreateStore(Builder.getFalse(), source.ownershipActive);
    return result;
}

void IRFunctionEmitter::InitializeObject(llvm::Value* storage, TypeInfo& type) {
    for (const auto& member : type.declaration->members) {
        auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
        if (!field) continue;
        auto& info = type.fields.at(field->name);
        auto* address = Builder.CreateStructGEP(type.type, storage, info.index);
        auto* fieldType = module.GetType(field->type);
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
                value = EmitDirectValue(*field->initialValue, address, module.types.at(field->type));
            } else value = EmitExpression(*field->initialValue);
            variables = std::move(savedVariables);
            bindings = std::move(savedBindings);
            functions = std::move(savedFunctions);
            currentMethod = std::move(savedMethod);
            if (value->getType() != fieldType) throw std::runtime_error("Initializer type mismatch: " + field->name);
            if (!semanticAnalysis->NeedsDestruction(field->type)) Builder.CreateStore(value, address);
        } else if (module.types.contains(field->type)) {
            auto& nested = module.types.at(field->type);
            InitializeObject(address, nested);
            if (auto* constructor = semanticAnalysis->ConstructorTarget(*field))
                Builder.CreateCall(nested.constructors.at(constructor)->function, {address});
        }
        else Builder.CreateStore(llvm::Constant::getNullValue(fieldType), address);
    }
}

void IRFunctionEmitter::EmitDestruction(llvm::Value* storage, TypeInfo& type) {
    if (type.destructor) Builder.CreateCall(type.destructor->function, {storage});
    for (auto it = type.declaration->members.rbegin(); it != type.declaration->members.rend(); ++it) {
        auto* field = dynamic_cast<const VariableDeclaration*>(it->declaration.get());
        if (!field) continue;
        auto nested = module.types.find(field->type);
        if (nested == module.types.end()) continue;
        auto* address = Builder.CreateStructGEP(type.type, storage, type.fields.at(field->name).index);
        EmitDestruction(address, nested->second);
    }
}

void IRFunctionEmitter::EmitCleanups(size_t fromDepth) {
    for (size_t depth = cleanupScopes.size(); depth > fromDepth; --depth) {
        const auto& scope = cleanupScopes[depth - 1];
        for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
            if (!it->active) { EmitDestruction(it->storage, *it->type); continue; }
            auto* function = Builder.GetInsertBlock()->getParent();
            auto* destroy = llvm::BasicBlock::Create(module.Context, "owned.destroy", function);
            auto* done = llvm::BasicBlock::Create(module.Context, "owned.done", function);
            Builder.CreateCondBr(Builder.CreateLoad(Builder.getInt1Ty(), it->active), destroy, done);
            Builder.SetInsertPoint(destroy);
            EmitDestruction(it->storage, *it->type);
            Builder.CreateBr(done);
            Builder.SetInsertPoint(done);
        }
    }
}

void IRFunctionEmitter::BeginFullExpression() {
    auto* marker = Builder.CreateAlloca(Builder.getInt1Ty(), nullptr, "temporary.marker");
    temporaryFrames.push_back({marker, {}});
}

void IRFunctionEmitter::EndFullExpression() {
    auto frame = std::move(temporaryFrames.back());
    temporaryFrames.pop_back();
    for (auto it = frame.cleanups.rbegin(); it != frame.cleanups.rend(); ++it) {
        auto* fn = Builder.GetInsertBlock()->getParent();
        auto* destroy = llvm::BasicBlock::Create(module.Context, "temporary.destroy", fn);
        auto* next = llvm::BasicBlock::Create(module.Context, "temporary.done", fn);
        Builder.CreateCondBr(Builder.CreateLoad(Builder.getInt1Ty(), it->active), destroy, next);
        Builder.SetInsertPoint(destroy);
        EmitDestruction(it->storage, *it->type);
        Builder.CreateBr(next);
        Builder.SetInsertPoint(next);
    }
    frame.marker->eraseFromParent();
}

llvm::Value* IRFunctionEmitter::EmitFullExpression(const Expression& expression) {
    BeginFullExpression();
    auto* value = EmitExpression(expression);
    EndFullExpression();
    return value;
}

Variable IRFunctionEmitter::MaterializeTemporary(const Expression& expression) {
    if (temporaryFrames.empty()) throw std::runtime_error("Temporary outside a full expression");
    auto& entry = Builder.GetInsertBlock()->getParent()->getEntryBlock();
    llvm::IRBuilder<> entryBuilder(&entry, entry.begin());
    const auto* construct = dynamic_cast<const ConstructExpr*>(&expression);
    llvm::Value* value = nullptr;
    llvm::Type* llvmType = nullptr;
    if (construct) llvmType = module.GetType(construct->type);
    else {
        value = EmitExpression(expression);
        if (auto found = temporaryValues.find(value); found != temporaryValues.end()) return found->second;
        llvmType = value->getType();
    }
    auto found = module.typesByLayout.find(llvmType);
    if (found == module.typesByLayout.end()) throw std::runtime_error("Member access requires a user-defined type");
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


}
