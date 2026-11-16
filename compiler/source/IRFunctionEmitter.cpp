#include "IRFunctionEmitter.h"

#include "Semantic.h"

#include <stdexcept>
#include <utility>

namespace c4r::ir {

IRFunctionEmitter::IRFunctionEmitter(IRModuleEmitter& module)
    : module(module), Builder(module.Context), operators(Builder), semanticAnalysis(&module.semantic) {}

void IRFunctionEmitter::DeclareGlobalFunction(const FunctionDeclaration& declaration) {
    auto symbol = DeclareFunction(declaration, false);
    functions[declaration.name] = symbol;
    functionDeclarations[&declaration] = std::move(symbol);
}

void IRFunctionEmitter::RegisterGlobal(const VariableDeclaration& declaration, llvm::Value* storage, llvm::Type* type) {
    Variable variable{storage, type, declaration.isMutable, nextVariableId++};
    globalVariables.emplace(declaration.name, variable);
    bindingDeclarations[&declaration] = variable.id;
}

void IRFunctionEmitter::EmitGlobalFunction(const std::string& name) {
    EmitFunctionBody(functions.at(name));
}

void IRFunctionEmitter::EmitEntryPoint(const Program& program, const std::string& name) {
    if (name.empty()) {
        EmitImplicitMain(program);
    } else if (name != "main") {
        auto* type = llvm::FunctionType::get(Builder.getInt32Ty(), false);
        auto* wrapper = llvm::Function::Create(type, llvm::Function::ExternalLinkage, "main", module.Module);
        Builder.SetInsertPoint(llvm::BasicBlock::Create(module.Context, "entry", wrapper));
        Builder.CreateRet(Builder.CreateCall(functions.at(name)->function));
    }
}

void IRFunctionEmitter::CheckMethodMutability() {
    bool changed;
    do {
        changed = false;
        for (auto& [name, type] : module.types) for (auto& [methodName, entry] : type.methods) {
            auto& method = entry.first;
            if (!method->mutates) for (const auto& callee : method->calls) if (callee->mutates) { method->mutates = true; changed = true; break; }
        }
    } while (changed);
    for (const auto& method : immutableCalls) if (method->mutates) throw std::runtime_error("Cannot call mutating method on immutable instance: " + method->declaration->name);
}

std::shared_ptr<FunctionSymbol> IRFunctionEmitter::DeclareFunction(const FunctionDeclaration& declaration, bool local) {
    std::vector<Capture> captures;
    std::unordered_set<std::string> names;
    for (const auto& parameter : declaration.parameters) {
        if (!names.insert(parameter.name).second) throw std::runtime_error("Duplicate parameter: " + parameter.name);
        if (module.GetType(parameter.type)->isVoidTy()) throw std::runtime_error("Parameter cannot have void type");
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
    if (!declaration.returnsReference && semanticAnalysis->NeedsDestruction(declaration.returnType)) parameterTypes.push_back(llvm::PointerType::getUnqual(module.Context));
    parameterTypes.insert(parameterTypes.end(), captures.size(), llvm::PointerType::getUnqual(module.Context));
    for (const auto& parameter : declaration.parameters)
        parameterTypes.push_back(parameter.isReference || semanticAnalysis->NeedsDestruction(parameter.type)
            ? llvm::PointerType::getUnqual(module.Context) : module.GetType(parameter.type));
    auto* resultType = declaration.returnsReference ? static_cast<llvm::Type*>(llvm::PointerType::getUnqual(module.Context)) :
        semanticAnalysis->NeedsDestruction(declaration.returnType) ? static_cast<llvm::Type*>(Builder.getVoidTy()) : module.GetType(declaration.returnType);
    auto* functionType = llvm::FunctionType::get(resultType, parameterTypes, false);
    auto name = local ? "__c4r_local_" + std::to_string(module.nextFunctionId++) : declaration.name;
    if (!local && module.Module.getNamedValue(name)) throw std::runtime_error("Duplicate function: " + name);
    auto* function = llvm::Function::Create(functionType, local ? llvm::Function::InternalLinkage : llvm::Function::ExternalLinkage, name, module.Module);
    return std::make_shared<FunctionSymbol>(FunctionSymbol{function, &declaration, std::move(captures)});
}

void IRFunctionEmitter::EmitFunctionBody(const std::shared_ptr<FunctionSymbol>& symbol) {
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
    auto* entry = llvm::BasicBlock::Create(module.Context, "entry", symbol->function);
    Builder.SetInsertPoint(entry);
    auto argument = symbol->function->arg_begin();
    if (!declaration.returnsReference && semanticAnalysis->NeedsDestruction(declaration.returnType)) currentReturnStorage = &*argument++;
    if (currentMethod) {
        Variable self{&*argument++, module.types.at(symbol->owner).type, true, nextVariableId++, true};
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
        auto* type = module.GetType(parameter.type);
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
            cleanupScopes.back().push_back({storage, &module.types.at(parameter.type), active});
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

void IRFunctionEmitter::EmitImplicitMain(const Program& program) {
    auto* functionType = llvm::FunctionType::get(
        Builder.getInt32Ty(),
        false
    );

    auto* function = llvm::Function::Create(
        functionType,
        llvm::Function::ExternalLinkage,
        "main",
        module.Module
    );

    auto* entry = llvm::BasicBlock::Create(
        module.Context,
        "entry",
        function
    );

    Builder.SetInsertPoint(entry);

    variables.clear();
    EmitBody(program.children);

    if (!Builder.GetInsertBlock()->hasTerminator()) Builder.CreateRet(Builder.getInt32(0));
}

}
