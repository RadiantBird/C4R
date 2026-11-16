#include "SemanticBodyChecker.h"

#include "ConstantExpression.h"

#include <stdexcept>
#include <utility>


void SemanticBodyChecker::CheckStatements(const Statements& nodes, Scope scope) {
    std::unordered_set<std::string> declared;
    bool directCapture = scope.allowCapture;
    scope.allowCapture = false;
    bool terminated = false;
    for (const auto& node : nodes) {
        semantic.diagnosticOffset = node->offset;
        if (terminated && !dynamic_cast<const FunctionDeclaration*>(node.get()) && !dynamic_cast<const CaptureDeclaration*>(node.get()))
            throw std::runtime_error("Unreachable statement after return");
        if (auto* variable = dynamic_cast<const VariableDeclaration*>(node.get())) {
            CheckVariable(variable, scope, declared);
        } else if (auto* local = dynamic_cast<const FunctionDeclaration*>(node.get())) {
            CheckLocalFunction(local, scope, declared);
        } else if (dynamic_cast<const CaptureDeclaration*>(node.get())) {
            if (!directCapture) throw std::runtime_error("capture must be declared directly in a local function");
        } else if (auto* format = dynamic_cast<const FormatStringStatement*>(node.get())) {
            CheckFormat(format, scope);
        } else if (auto* expr = dynamic_cast<const Expression*>(node.get())) {
            CheckExpression(*expr, scope);
        } else if (auto* result = dynamic_cast<const ReturnStatement*>(node.get())) {
            CheckReturn(result, scope);
        } else if (auto* block = dynamic_cast<const Block*>(node.get())) {
            CheckStatements(block->statements, scope);
        } else if (auto* branch = dynamic_cast<const IfStatement*>(node.get())) {
            CheckIf(branch, scope);
        } else if (auto* loop = dynamic_cast<const LoopStatement*>(node.get())) {
            CheckLoop(loop, scope);
        } else if (dynamic_cast<const LoopControl*>(node.get())) {
            if (!scope.loopDepth) throw std::runtime_error("break or continue outside loop");
            if (scope.valueScope && scope.loopDepth <= scope.valueLoopDepth)
                throw std::runtime_error("Cannot leave a value scope with break or continue");
        }
        if (dynamic_cast<const ReturnStatement*>(node.get()) || dynamic_cast<const LoopControl*>(node.get())) terminated = true;
        else if (auto* block = dynamic_cast<const Block*>(node.get()); block && Terminates(block->statements)) terminated = true;
        else if (auto* branch = dynamic_cast<const IfStatement*>(node.get()); branch && branch->elseBranch &&
            Terminates(branch->thenBranch->statements) && Terminates(branch->elseBranch->statements)) terminated = true;
    }
}

void SemanticBodyChecker::CheckVariable(const VariableDeclaration* variable, Scope& scope, std::unordered_set<std::string>& declared) {
    if (!declared.insert(variable->name).second) throw std::runtime_error("Duplicate variable: " + variable->name);
    if (variable->type == "void") throw std::runtime_error("Variable cannot have void type");
    if (variable->isConsumable && variable->isReference) throw std::runtime_error("A variable cannot be both borrowed and consumable");
    if (semantic.globals.contains(variable->type) && semantic.globals.at(variable->type).kind == Kind::Type)
        RequireDestructionAccess(variable->type, scope.owner);
    if (variable->aggregateInit && (!semantic.globals.contains(variable->type) || semantic.globals.at(variable->type).kind != Kind::Type))
        throw std::runtime_error("Brace initialization requires a user-defined type");
    if (variable->aggregateInit) {
        if (auto* constructor = semantic.SelectDefaultConstructor(variable->type, scope.owner)) semantic.defaultConstructors[variable] = constructor;
    }
    if (variable->initialValue) {
        Scope initializerScope = scope;
        auto initial = CheckExpression(*variable->initialValue, initializerScope);
        if (variable->isConsumable && !IsConsumableValue(variable->initialValue.get()))
            throw std::runtime_error("Consumable initializer requires direct construction or explicit move: " + variable->name);
        if (semantic.NeedsDestruction(variable->type) && !variable->isReference && !IsDirectValue(variable->initialValue.get())) throw std::runtime_error("Copying a destructible value is not permitted: " + variable->name);
        if (!initial.empty() && initial != Semantic::CanonicalType(variable->type)) throw std::runtime_error("Initializer type mismatch: " + variable->name);
    }
    if (variable->isReference) BindReference(*variable, scope);
    if (variable->type.ends_with('*') && variable->initialValue)
        borrows.TrackPointer(variable, *variable->initialValue, scope);
    scope.variables[variable->name] = Semantic::CanonicalType(variable->type);
    scope.mutableVariables[variable->name] = variable->isReference ? variable->isMutableReference : variable->isMutable;
    scope.receiverVariables[variable->name] = false;
    size_t id = nextBindingId++;
    bindingOrigins[id] = variable;
    scope.bindingIds[variable->name] = id;
    scope.availableBindings.insert(id);
    scope.globalBindings.erase(variable->name);
    scope.inaccessible.erase(variable->name);
    scope.localFunctions.erase(variable->name);
    scope.localSignatures.erase(variable->name);
}

void SemanticBodyChecker::CheckLocalFunction(const FunctionDeclaration* local, Scope& scope, std::unordered_set<std::string>& declared) {
    if (!declared.insert(local->name).second) throw std::runtime_error("Duplicate declaration: " + local->name);
    scope.variables.erase(local->name);
    scope.mutableVariables.erase(local->name);
    scope.receiverVariables.erase(local->name);
    scope.bindingIds.erase(local->name);
    scope.globalBindings.erase(local->name);
    scope.inaccessible.erase(local->name);
    scope.localFunctions.insert(local->name);
    scope.localSignatures[local->name] = local;
    scope.shadowedMethods.insert(local->name);
    Scope localScope = scope;
    if (auto type = semantic.members.find(localScope.owner); type != semantic.members.end()) {
        for (const auto& [name, member] : type->second)
            if (member.kind == Kind::Method && !localScope.shadowedMethods.contains(name)) localScope.localFunctions.erase(name);
    }
    localScope.method = nullptr;
    localScope.owner.clear();
    CheckFunction(*local, std::move(localScope), true);
}

void SemanticBodyChecker::CheckFormat(const FormatStringStatement* format, Scope& scope) {
    for (const auto& part : format->parts) {
        auto* identifier = dynamic_cast<const IdentifierExpr*>(part.get());
        if (!identifier) continue;
        auto found = scope.variables.find(identifier->name);
        if (found == scope.variables.end()) throw std::runtime_error("Undefined variable: " + identifier->name);
        if (found->second != "int32" && found->second != "bool" && found->second != "float")
            throw std::runtime_error("Unsupported format string type: " + identifier->name);
        (void)CheckExpression(*identifier, scope);
    }
}

void SemanticBodyChecker::CheckReturn(const ReturnStatement* result, Scope& scope) {
    if (result->value) {
        if (!scope.valueScope && semantic.NeedsDestruction(scope.returnType) && !(scope.function && scope.function->returnsReference) &&
            !dynamic_cast<const ConstructExpr*>(result->value.get()) && !dynamic_cast<const MoveExpr*>(result->value.get()))
            throw std::runtime_error("Destructible return requires direct construction");
        auto resultType = CheckExpression(*result->value, scope);
        semantic.returnValueTypes[result] = resultType;
        if (scope.valueScope) {
            if (semantic.NeedsDestruction(resultType) && !IsDirectValue(result->value.get()))
                throw std::runtime_error("Destructible value scope requires direct construction");
            *scope.valueSawReturn = true;
            if (resultType.empty()) *scope.valueUnknown = true;
            else if (resultType == "void") throw std::runtime_error("Value scope cannot return void");
            else if (!scope.valueType->empty() && *scope.valueType != resultType) throw std::runtime_error("Value scope return types do not match");
            else *scope.valueType = resultType;
        } else {
            if (scope.function && scope.function->returnsConsumable && !IsConsumableValue(result->value.get()))
                throw std::runtime_error("Consumable return requires direct construction or explicit move");
            if (scope.function && scope.function->returnsReference) {
                if (!resultType.empty() && resultType != Semantic::CanonicalType(scope.returnType))
                    throw std::runtime_error("Invalid reference return type");
                const void* origin = borrows.Find(*result->value, scope).root;
                bool validOrigin = false;
                for (const auto& parameter : scope.function->parameters)
                    if (parameter.isReference && parameter.lifetime == scope.function->returnLifetime && origin == &parameter)
                        validOrigin = true;
                if (!validOrigin) throw std::runtime_error("Returning a reference to an expired or unrelated object");
                if (scope.function->returnsMutableReference) {
                    auto mutability = MutableValue(*result->value, scope);
                    if (!mutability || !*mutability) throw std::runtime_error("Mutable reference return requires a mutable source");
                }
                semantic.referenceReturns.insert(result);
            } else if (scope.function && !scope.returnType.empty() && scope.returnType.back() == '*') {
                if (!resultType.empty() && resultType != scope.returnType)
                    throw std::runtime_error("Invalid return value type");
                if (borrows.PointerMayReferenceLocal(*result->value, scope))
                    throw std::runtime_error("Returning a pointer to an expired local object");
                const void* origin = borrows.PointerOrigin(*result->value, scope);
                if (origin) {
                    bool isGlobal = false, isReferenceParameter = false, isLocal = false;
                    for (const auto& [name, symbol] : semantic.globals)
                        if (symbol.kind == Kind::GlobalVariable && symbol.declaration == origin) isGlobal = true;
                    for (const auto& parameter : scope.function->parameters)
                        if (&parameter == origin) {
                            isReferenceParameter = parameter.isReference;
                            isLocal = !parameter.isReference;
                        }
                    for (const auto& [name, id] : scope.bindingIds)
                        if (!scope.globalBindings.contains(name) && bindingOrigins.at(id) == origin) isLocal = true;
                    if (isLocal && !isGlobal && !isReferenceParameter)
                        throw std::runtime_error("Returning a pointer to an expired local object");
                }
            } else if (!scope.returnType.empty() && !resultType.empty() && resultType != scope.returnType)
                throw std::runtime_error("Invalid return value type");
        }
    } else if (scope.valueScope) throw std::runtime_error("Value scope requires a return value");
    else if (!scope.returnType.empty() && scope.returnType != "void") throw std::runtime_error("Return value required");
}

void SemanticBodyChecker::CheckIf(const IfStatement* branch, Scope& scope) {
    auto condition = CheckExpression(*branch->condition, scope);
    if (!condition.empty() && condition != "bool") throw std::runtime_error("Condition must be bool");
    Scope thenScope = scope, elseScope = scope;
    if (scope.mayEvaluate) if (auto known = ConstantExpression::Boolean(*branch->condition)) {
        if (*known) elseScope.mayEvaluate = false;
        else thenScope.mayEvaluate = false;
    }
    CheckStatements(branch->thenBranch->statements, thenScope);
    if (branch->elseBranch) CheckStatements(branch->elseBranch->statements, elseScope);
}

void SemanticBodyChecker::CheckLoop(const LoopStatement* loop, const Scope& scope) {
    Scope inner = scope;
    if (auto* loopVariable = dynamic_cast<const VariableDeclaration*>(loop->initializer.get())) {
        if (loopVariable->type == "void") throw std::runtime_error("Variable cannot have void type");
        if (semantic.globals.contains(loopVariable->type) && semantic.globals.at(loopVariable->type).kind == Kind::Type)
            RequireDestructionAccess(loopVariable->type, inner.owner);
        if (loopVariable->aggregateInit && (!semantic.globals.contains(loopVariable->type) || semantic.globals.at(loopVariable->type).kind != Kind::Type))
            throw std::runtime_error("Brace initialization requires a user-defined type");
        if (loopVariable->aggregateInit) {
            if (auto* constructor = semantic.SelectDefaultConstructor(loopVariable->type, inner.owner)) semantic.defaultConstructors[loopVariable] = constructor;
        }
        if (loopVariable->initialValue) {
            Scope initializerScope = inner;
            auto initial = CheckExpression(*loopVariable->initialValue, initializerScope);
            if (semantic.NeedsDestruction(loopVariable->type) && !loopVariable->isReference && !IsDirectValue(loopVariable->initialValue.get())) throw std::runtime_error("Copying a destructible value is not permitted: " + loopVariable->name);
            if (!initial.empty() && initial != Semantic::CanonicalType(loopVariable->type)) throw std::runtime_error("Initializer type mismatch: " + loopVariable->name);
        }
        if (loopVariable->isReference) BindReference(*loopVariable, inner);
        inner.variables[loopVariable->name] = Semantic::CanonicalType(loopVariable->type);
        inner.mutableVariables[loopVariable->name] = loopVariable->isReference ? loopVariable->isMutableReference : loopVariable->isMutable;
        inner.receiverVariables[loopVariable->name] = false;
        size_t id = nextBindingId++;
        bindingOrigins[id] = loopVariable;
        inner.bindingIds[loopVariable->name] = id;
        inner.availableBindings.insert(id);
        inner.globalBindings.erase(loopVariable->name);
        inner.inaccessible.erase(loopVariable->name);
    } else if (auto* init = dynamic_cast<const Expression*>(loop->initializer.get())) CheckExpression(*init, inner);
    bool skipLoop = false;
    if (loop->condition) {
        auto condition = CheckExpression(*loop->condition, inner);
        if (!condition.empty() && condition != "bool") throw std::runtime_error("Condition must be bool");
        if (inner.mayEvaluate && loop->kind != LoopStatement::Kind::DoWhile) {
            auto known = ConstantExpression::Boolean(*loop->condition);
            skipLoop = known && !*known;
        }
    }
    if (skipLoop) inner.mayEvaluate = false;
    if (loop->step) CheckExpression(*loop->step, inner);
    ++inner.loopDepth;
    CheckStatements(loop->body->statements, inner);
}
