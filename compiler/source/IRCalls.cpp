#include "IRFunctionEmitter.h"

#include "Semantic.h"

#include <stdexcept>
#include <utility>

namespace c4r::ir {

llvm::Value* IRFunctionEmitter::EmitCall(const std::string& name, const std::vector<std::unique_ptr<Expression>>& arguments,
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

llvm::Value* IRFunctionEmitter::EmitFunctionCall(const std::shared_ptr<FunctionSymbol>& symbol,
    std::vector<llvm::Value*> values, llvm::Value* destination) {
    const auto& resultType = symbol->declaration->returnType;
    if (symbol->declaration->returnsReference) return Builder.CreateCall(symbol->function, values);
    if (!semanticAnalysis->NeedsDestruction(resultType)) return Builder.CreateCall(symbol->function, values);
    llvm::AllocaInst* active = nullptr;
    auto& type = module.types.at(resultType);
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

void IRFunctionEmitter::EmitArguments(const std::vector<std::unique_ptr<Expression>>& arguments,
    const std::vector<FunctionDeclaration::Parameter>& parameters, std::vector<llvm::Value*>& values) {
    if (arguments.size() != parameters.size()) throw std::runtime_error("Wrong number of arguments");
    for (size_t i = 0; i < arguments.size(); ++i) {
        auto* type = module.GetType(parameters[i].type);
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
            EmitDirectValue(*arguments[i], storage, module.types.at(parameters[i].type));
            values.push_back(storage);
        } else {
            auto* value = EmitExpression(*arguments[i]);
            if (value->getType() != type) throw std::runtime_error("Argument type mismatch");
            values.push_back(value);
        }
    }
}

llvm::Value* IRFunctionEmitter::EmitMethodCall(const CallExpr& call, llvm::Value* destination) {
    Variable object;
    if (call.object) object = ResolveLValue(*call.object);
    else object = variables.at("$self");
    if (auto entry = module.typesByLayout.find(object.type); entry != module.typesByLayout.end()) {
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

llvm::Value* IRFunctionEmitter::EmitCallExpression(const CallExpr& call, llvm::Value* destination) {
    if (semanticAnalysis) if (auto* target = semanticAnalysis->CallTarget(call)) return EmitCall(call.name, call.arguments, target, destination);
    if (call.object || (currentMethod && module.types.at(currentMethod->owner).methods.contains(call.name) && !functions.contains(call.name) && !variables.contains(call.name)))
        return EmitMethodCall(call, destination);
    return EmitCall(call.name, call.arguments, nullptr, destination);
}

llvm::Value* IRFunctionEmitter::EmitBareCall(const IdentifierExpr& expression, llvm::Value* destination) {
    auto* target = semanticAnalysis->CallTarget(expression);
    if (!target) throw std::runtime_error("Destructible value requires direct construction");
    if (currentMethod) {
        auto& methods = module.types.at(currentMethod->owner).methods;
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

llvm::Value* IRFunctionEmitter::EmitBareMemberCall(const MemberExpr& expression, llvm::Value* destination) {
    auto* target = semanticAnalysis->CallTarget(expression);
    if (!target) throw std::runtime_error("Destructible value requires direct construction");
    auto object = ResolveLValue(*expression.object);
    auto found = module.typesByLayout.find(object.type);
    if (found == module.typesByLayout.end()) throw std::runtime_error("Method call requires a user-defined type");
    auto& type = *found->second;
    auto method = type.methods.find(expression.name);
    if (method == type.methods.end() || method->second.first->declaration != target) throw std::runtime_error("Unknown method: " + expression.name);
    if (!method->second.second && (!currentMethod || currentMethod->owner != type.declaration->name)) throw std::runtime_error("Private member: " + expression.name);
    auto symbol = method->second.first;
    if (!object.isMutable) immutableCalls.push_back(symbol);
    else if (object.isReceiver && currentMethod) currentMethod->calls.push_back(symbol);
    return EmitFunctionCall(symbol, {object.storage}, destination);
}


}
