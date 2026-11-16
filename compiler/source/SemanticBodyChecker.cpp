#include "SemanticBodyChecker.h"

#include "ConstantExpression.h"

#include <stdexcept>
#include <utility>


void SemanticBodyChecker::Check(const Program& program) {
    Scope globalScope;
    const bool explicitEntry = !semantic.entryPoint.empty();
    if (explicitEntry) for (const auto& [name, symbol] : semantic.globals)
        if (symbol.kind == Kind::GlobalVariable) {
            globalScope.variables[name] = Semantic::CanonicalType(symbol.type);
            globalScope.mutableVariables[name] = static_cast<const VariableDeclaration*>(symbol.declaration)->isMutable;
            globalScope.globalBindings.insert(name);
            size_t id = nextBindingId++;
            bindingOrigins[id] = symbol.declaration;
            globalScope.bindingIds[name] = id;
            globalScope.availableBindings.insert(id);
            globalBindingIds.insert(id);
            globalIdsByName[name] = id;
        }
    if (explicitEntry) for (const auto& node : program.children) {
        auto* variable = dynamic_cast<const VariableDeclaration*>(node.get());
        if (!variable || !variable->initialValue) continue;
        semantic.diagnosticFile = variable->sourceFile;
        auto initialType = CheckExpression(*variable->initialValue, globalScope);
        if (!initialType.empty() && initialType != Semantic::CanonicalType(variable->type)) throw std::runtime_error("Initializer type mismatch: " + variable->name);
    }
    for (const auto& node : program.children) {
        if (auto* declaration = dynamic_cast<const FunctionDeclaration*>(node.get())) CheckFunction(*declaration, globalScope, false);
        else if (auto* type = dynamic_cast<const TypeDeclaration*>(node.get())) {
            semantic.diagnosticFile = type->sourceFile;
            Scope initializerScope;
            initializerScope.owner = type->name;
            for (const auto& member : type->members) {
                auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
                if (!field) continue;
                if (field->isConsumable) throw std::runtime_error("Consumable fields are not supported: " + field->name);
                if (!field->initialValue) continue;
                Scope fieldScope = initializerScope;
                fieldScope.directConstruction = dynamic_cast<const ConstructExpr*>(field->initialValue.get()) != nullptr;
                auto initial = CheckExpression(*field->initialValue, fieldScope);
                if (semantic.NeedsDestruction(field->type) && !IsDirectValue(field->initialValue.get()))
                    throw std::runtime_error("Copying a destructible value is not permitted: " + field->name);
                if (!initial.empty() && initial != Semantic::CanonicalType(field->type)) throw std::runtime_error("Initializer type mismatch: " + field->name);
            }
            Scope scope = globalScope;
            scope.owner = type->name;
            for (const auto& member : type->members) {
                if (auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get())) {
                    scope.variables[field->name] = Semantic::CanonicalType(field->type);
                    scope.mutableVariables[field->name] = field->isMutable;
                    scope.receiverVariables[field->name] = true;
                    scope.globalBindings.erase(field->name);
                }
                if (auto* method = dynamic_cast<const FunctionDeclaration*>(member.declaration.get()); method &&
                    !dynamic_cast<const ConstructorDeclaration*>(method) && !dynamic_cast<const DestructorDeclaration*>(method))
                    scope.localFunctions.insert(method->name);
            }
            for (const auto& member : type->members)
                if (auto* method = dynamic_cast<const FunctionDeclaration*>(member.declaration.get())) {
                    Scope methodScope = scope;
                    methodScope.method = method;
                    if (dynamic_cast<const ConstructorDeclaration*>(method)) {
                        for (const auto& fieldMember : type->members)
                            if (auto* field = dynamic_cast<const VariableDeclaration*>(fieldMember.declaration.get()))
                                methodScope.mutableVariables[field->name] = true;
                    }
                    CheckFunction(*method, std::move(methodScope), false);
                }
        }
    }
    if (!explicitEntry) CheckStatements(program.children, globalScope);
    CheckMethodMutability();
}

void SemanticBodyChecker::CheckMethodMutability() {
    bool changed;
    do {
        changed = false;
        for (const auto& [caller, callees] : calls) {
            if (mutates[caller]) continue;
            for (const auto* callee : callees) if (mutates[callee]) { mutates[caller] = true; changed = true; break; }
        }
    } while (changed);
    for (const auto& call : immutableCalls) if (mutates[call.method]) {
        semantic.diagnosticOffset = call.offset;
        semantic.diagnosticFile = call.file;
        throw std::runtime_error("Cannot call mutating method on immutable instance: " + call.method->name);
    }
}

bool SemanticBodyChecker::IsDirectValue(const Expression* value) const {
    if (dynamic_cast<const NumericLiteral*>(value) || dynamic_cast<const BooleanLiteral*>(value)) return true;
    if (auto* identifier = dynamic_cast<const IdentifierExpr*>(value)) return semantic.bareCalls.contains(identifier);
    if (auto* member = dynamic_cast<const MemberExpr*>(value)) return semantic.bareMemberCalls.contains(member);
    return dynamic_cast<const MoveExpr*>(value) || dynamic_cast<const ConstructExpr*>(value) || dynamic_cast<const CallExpr*>(value) ||
        dynamic_cast<const BlockExpr*>(value) || dynamic_cast<const IfExpr*>(value);
}

bool SemanticBodyChecker::IsConsumableValue(const Expression* value) const {
    return IsDirectValue(value) || dynamic_cast<const UnaryExpr*>(value) ||
        dynamic_cast<const BinaryExpr*>(value) || dynamic_cast<const ComparisonExpr*>(value);
}

void SemanticBodyChecker::RequireAssignableFields(const std::string& type, const std::string& owner) {
    const auto& symbols = semantic.members.at(type);
    for (const auto& [name, symbol] : symbols) {
        if (symbol.kind != Kind::Field) continue;
        auto* field = static_cast<const VariableDeclaration*>(symbol.declaration);
        if (!field->isMutable) throw std::runtime_error("Assignment to a user-defined value would modify an immutable field: " + name);
        if (!symbol.isPublic && owner != type) throw std::runtime_error("Private member: " + name);
        auto nested = semantic.globals.find(field->type);
        if (nested != semantic.globals.end() && nested->second.kind == Kind::Type) RequireAssignableFields(field->type, owner);
    }
}

void SemanticBodyChecker::RequireDestructionAccess(const std::string& type, const std::string& owner) {
    auto found = semantic.globals.find(type);
    if (found == semantic.globals.end() || found->second.kind != Kind::Type) return;
    const auto& lifecycle = semantic.specialMembers.at(type);
    if (lifecycle.destructor && !lifecycle.destructor->isPublic && owner != type)
        throw std::runtime_error("Private destructor: " + type);
    auto* declaration = static_cast<const TypeDeclaration*>(found->second.declaration);
    for (const auto& member : declaration->members) {
        auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
        if (!field) continue;
        auto nested = semantic.globals.find(field->type);
        if (nested != semantic.globals.end() && nested->second.kind == Kind::Type) {
            if (!field->initialValue) {
                if (auto* constructor = semantic.SelectDefaultConstructor(field->type, type)) semantic.defaultConstructors[field] = constructor;
            }
            RequireDestructionAccess(field->type, type);
        }
    }
}

bool SemanticBodyChecker::Terminates(const Statements& nodes) {
    for (const auto& node : nodes) {
        if (dynamic_cast<const ReturnStatement*>(node.get()) || dynamic_cast<const LoopControl*>(node.get())) return true;
        if (auto* block = dynamic_cast<const Block*>(node.get()); block && Terminates(block->statements)) return true;
        if (auto* branch = dynamic_cast<const IfStatement*>(node.get()); branch && branch->elseBranch &&
            Terminates(branch->thenBranch->statements) && Terminates(branch->elseBranch->statements)) return true;
    }
    return false;
}

void SemanticBodyChecker::CheckFunction(const FunctionDeclaration& declaration, Scope scope, bool local) {
    if (!declaration.sourceFile.empty()) semantic.diagnosticFile = declaration.sourceFile;
    semantic.diagnosticOffset = declaration.offset;
    Scope outer = scope;
    capturedBindings[&declaration].clear();
    if (local) {
        scope.availableBindings = globalBindingIds;
        for (auto it = scope.variables.begin(); it != scope.variables.end();) {
            if (scope.globalBindings.contains(it->first)) {
                scope.availableBindings.insert(scope.bindingIds.at(it->first));
                ++it; continue;
            }
            scope.inaccessible.insert(it->first);
            scope.mutableVariables.erase(it->first);
            scope.receiverVariables.erase(it->first);
            scope.bindingIds.erase(it->first);
            it = scope.variables.erase(it);
        }
    }
    std::unordered_set<std::string> bindings;
    for (const auto& parameter : declaration.parameters) {
        semantic.diagnosticOffset = parameter.offset;
        if (!parameter.sourceFile.empty()) semantic.diagnosticFile = parameter.sourceFile;
        if (!bindings.insert(parameter.name).second) throw std::runtime_error("Duplicate parameter: " + parameter.name);
        if (parameter.type == "void") throw std::runtime_error("Parameter cannot have void type");
        if (parameter.isConsumable && parameter.isReference) throw std::runtime_error("A parameter cannot be both borrowed and consumable");
        if (semantic.NeedsDestruction(parameter.type)) RequireDestructionAccess(parameter.type, scope.owner);
    }
    for (const auto& node : declaration.body) {
        auto* capture = dynamic_cast<const CaptureDeclaration*>(node.get());
        if (!capture) continue;
        if (!local) throw std::runtime_error("capture requires a local function");
        for (const auto& name : capture->names) {
            if (!bindings.insert(name).second) throw std::runtime_error("Duplicate capture or parameter: " + name);
            auto found = outer.variables.find(name);
            bool isField = found != outer.variables.end() && outer.receiverVariables.contains(name) && outer.receiverVariables.at(name);
            if (isField && globalIdsByName.contains(name)) {
                auto& global = semantic.globals.at(name);
                scope.variables[name] = Semantic::CanonicalType(global.type);
                scope.mutableVariables[name] = static_cast<const VariableDeclaration*>(global.declaration)->isMutable;
                scope.receiverVariables[name] = false;
                size_t id = globalIdsByName.at(name);
                scope.bindingIds[name] = id;
                scope.availableBindings.insert(id);
                scope.globalBindings.insert(name);
                capturedBindings[&declaration].emplace_back(name, id);
                scope.inaccessible.erase(name);
                continue;
            }
            if (found == outer.variables.end() || isField) {
                if (outer.inaccessible.contains(name)) throw std::runtime_error("Variable requires capture in enclosing function: " + name);
                throw std::runtime_error("Cannot capture undefined variable: " + name);
            }
            scope.variables[name] = found->second;
            scope.mutableVariables[name] = outer.mutableVariables.at(name);
            scope.receiverVariables[name] = false;
            size_t id = outer.bindingIds.at(name);
            scope.bindingIds[name] = id;
            scope.availableBindings.insert(id);
            capturedBindings[&declaration].emplace_back(name, id);
            scope.inaccessible.erase(name);
        }
    }
    scope.returnType = Semantic::CanonicalType(declaration.returnType);
    scope.function = &declaration;
    scope.valueScope = false;
    scope.allowCapture = true;
    scope.mayEvaluate = true;
    scope.valueType = nullptr; scope.valueSawReturn = nullptr; scope.valueUnknown = nullptr;
    scope.loopDepth = 0;
    for (const auto& parameter : declaration.parameters) {
        scope.variables[parameter.name] = Semantic::CanonicalType(parameter.type);
        scope.mutableVariables[parameter.name] = parameter.isMutable;
        scope.receiverVariables[parameter.name] = false;
        size_t id = nextBindingId++;
        bindingOrigins[id] = &parameter;
        scope.bindingIds[parameter.name] = id;
        scope.availableBindings.insert(id);
        if (parameter.isReference) scope.borrowOrigins[&parameter] = &parameter;
        if (parameter.isReference) scope.borrowParents[&parameter] = &parameter;
        scope.globalBindings.erase(parameter.name);
        scope.inaccessible.erase(parameter.name);
    }
    for (const auto& node : declaration.body) {
        if (auto* variable = dynamic_cast<const VariableDeclaration*>(node.get()); variable && bindings.contains(variable->name))
            throw std::runtime_error("Duplicate variable: " + variable->name);
        if (auto* nested = dynamic_cast<const FunctionDeclaration*>(node.get()); nested && bindings.contains(nested->name))
            throw std::runtime_error("Duplicate declaration: " + nested->name);
    }
    CheckStatements(declaration.body, std::move(scope));
    const bool implicitEntryReturn = !local && outer.method == nullptr && declaration.parameters.empty() &&
        Semantic::CanonicalType(declaration.returnType) == "int32" &&
        (declaration.name == "main" || declaration.name == "start" || declaration.name == "entry");
    if (Semantic::CanonicalType(declaration.returnType) != "void" && !implicitEntryReturn && !Terminates(declaration.body))
        throw std::runtime_error("Function must return a value: " + declaration.name);
}

void Semantic::CheckBodies(const Program& program) const {
    SemanticBodyChecker(*this).Check(program);
}
