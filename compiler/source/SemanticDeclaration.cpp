#include "Semantic.h"

#include "ConstantExpression.h"

#include <stdexcept>
#include <utility>

class SemanticDeclarationCollector {
public:
    explicit SemanticDeclarationCollector(Semantic& semantic) : semantic(semantic) {}
    void Collect(const Program& program, bool requireExplicitEntry);

private:
    using Kind = Semantic::Kind;
    using Symbol = Semantic::Symbol;

    Semantic& semantic;
    std::unordered_set<std::string> completeTypes;
    std::unordered_set<std::string> activeTypes;

    void RegisterGlobals(const Program& program);
    bool SelectEntryPoint(const Program& program, bool requireExplicitEntry);
    void CheckGlobalInitializers(const Program& program);
    bool KnownType(const std::string& name) const;
    void ValidateType(const std::string& name, bool allowVoid) const;
    void ValidateFunction(const FunctionDeclaration& function);
    void CollectMembers(const Program& program);
    void ValidateLayout(const TypeDeclaration& type);
    void ValidateLayouts(const Program& program);
    void CheckGlobalDestruction(const Program& program);
    void CheckObject(const std::string& typeName, std::unordered_set<std::string>& visiting);
    void CheckGlobalObjects(const Program& program);
};

void SemanticDeclarationCollector::Collect(const Program& program, bool requireExplicitEntry) {
    RegisterGlobals(program);
    const bool explicitEntry = SelectEntryPoint(program, requireExplicitEntry);
    if (explicitEntry) CheckGlobalInitializers(program);
    CollectMembers(program);
    ValidateLayouts(program);
    if (explicitEntry) {
        CheckGlobalDestruction(program);
        CheckGlobalObjects(program);
    }
}

void SemanticDeclarationCollector::RegisterGlobals(const Program& program) {
    for (const auto& node : program.children) {
        semantic.diagnosticOffset = node->offset;
        semantic.diagnosticFile = node->sourceFile;
        std::string name;
        Symbol symbol{Kind::Type, node.get(), {}, {}, true};
        if (auto* type = dynamic_cast<const TypeDeclaration*>(node.get())) {
            name = type->name;
            symbol.kind = Kind::Type;
            symbol.type = type->name;
        } else if (auto* function = dynamic_cast<const FunctionDeclaration*>(node.get())) {
            name = function->name;
            if (name == "printf") throw std::runtime_error("Reserved function name: printf");
            symbol.kind = Kind::Function;
            symbol.type = function->returnType;
        } else if (auto* variable = dynamic_cast<const VariableDeclaration*>(node.get())) {
            name = variable->name;
            symbol.kind = Kind::GlobalVariable;
            symbol.type = variable->type;
        } else continue;
        auto [previous, inserted] = semantic.globals.emplace(name, symbol);
        if (!inserted) {
            std::string kind = symbol.kind == Kind::Function && previous->second.kind == Kind::Function ? "function" : "declaration";
            throw std::runtime_error("Duplicate " + kind + ": " + name +
                (node->sourceFile.empty() ? "" : " (" + node->sourceFile + ", previously " + previous->second.declaration->sourceFile + ")"));
        }
    }
}

bool SemanticDeclarationCollector::SelectEntryPoint(const Program& program, bool requireExplicitEntry) {
    bool explicitEntry = false;
    unsigned entryCount = 0;
    for (const auto* name : {"main", "start", "entry"}) {
        auto found = semantic.globals.find(name);
        if (found == semantic.globals.end() || found->second.kind != Kind::Function) continue;
        auto* function = static_cast<const FunctionDeclaration*>(found->second.declaration);
        if (function->parameters.empty() && (function->returnType == "int" || function->returnType == "int32")) {
            explicitEntry = true;
            if (semantic.entryPoint.empty()) semantic.entryPoint = name;
            ++entryCount;
        }
    }
    if (requireExplicitEntry && entryCount != 1) throw std::runtime_error("Multiple input files require exactly one explicit entry point");
    if (auto main = semantic.globals.find("main"); main != semantic.globals.end() && main->second.kind == Kind::Function) {
        auto* function = static_cast<const FunctionDeclaration*>(main->second.declaration);
        if (!function->parameters.empty() || (function->returnType != "int" && function->returnType != "int32"))
            throw std::runtime_error("main must return int and have no parameters");
    }
    if (explicitEntry) for (const auto& node : program.children)
        if (!dynamic_cast<const TypeDeclaration*>(node.get()) && !dynamic_cast<const FunctionDeclaration*>(node.get()) && !dynamic_cast<const VariableDeclaration*>(node.get()))
            throw std::runtime_error("Top-level statements with explicit entry point are not supported yet");
    return explicitEntry;
}

void SemanticDeclarationCollector::CheckGlobalInitializers(const Program& program) {
    for (const auto& node : program.children) {
        auto* variable = dynamic_cast<const VariableDeclaration*>(node.get());
        if (variable && variable->initialValue) {
            semantic.diagnosticOffset = variable->offset;
            semantic.diagnosticFile = variable->sourceFile;
            ConstantExpression::Check(*variable->initialValue);
        }
    }
}

bool SemanticDeclarationCollector::KnownType(const std::string& name) const {
    if (!name.empty() && name.back() == '*') return KnownType(name.substr(0, name.size() - 1));
    if (name == "void" || name == "int" || name == "int32" || name == "bool" || name == "float") return true;
    auto found = semantic.globals.find(name);
    return found != semantic.globals.end() && found->second.kind == Kind::Type;
}

void SemanticDeclarationCollector::ValidateType(const std::string& name, bool allowVoid) const {
    if (!KnownType(name)) throw std::runtime_error("Unknown type: " + name);
    if (!name.empty() && name.back() == '*') return;
    if (!allowVoid && name == "void") throw std::runtime_error("Variable or parameter cannot have void type");
}

void SemanticDeclarationCollector::ValidateFunction(const FunctionDeclaration& function) {
    ValidateType(function.returnType, true);
    if (function.returnsMutableReference && !function.returnsReference)
        throw std::runtime_error("mutable return requires a reference type");
    if (!function.returnsReference && !function.returnLifetime.empty())
        throw std::runtime_error("Lifetime specified on a non-reference return");
    if (function.returnsReference && function.returnLifetime.empty())
        throw std::runtime_error("Reference return requires an explicit lifetime");
    std::unordered_set<std::string> names;
    size_t returnLifetimeParameters = 0;
    for (const auto& parameter : function.parameters) {
        semantic.diagnosticOffset = parameter.offset;
        if (!parameter.sourceFile.empty()) semantic.diagnosticFile = parameter.sourceFile;
        ValidateType(parameter.type, false);
        if (!parameter.isReference && !parameter.lifetime.empty())
            throw std::runtime_error("Lifetime specified on a non-reference parameter: " + parameter.name);
        if (function.returnsReference) {
            if (!parameter.isReference) throw std::runtime_error("Reference-returning function requires reference parameters to declare lifetimes");
            if (parameter.lifetime.empty()) throw std::runtime_error("Reference-returning function requires explicit parameter lifetimes");
            if (parameter.lifetime == function.returnLifetime) ++returnLifetimeParameters;
        }
        if (!names.insert(parameter.name).second) throw std::runtime_error("Duplicate parameter: " + parameter.name);
    }
    if (function.returnsReference && returnLifetimeParameters == 0)
        throw std::runtime_error("Return lifetime is not tied to a reference parameter");
    if (function.returnsReference && returnLifetimeParameters > 1)
        throw std::runtime_error("Reference return lifetime must identify exactly one reference parameter");
}

void SemanticDeclarationCollector::CollectMembers(const Program& program) {
    for (const auto& node : program.children) {
        semantic.diagnosticOffset = node->offset;
        semantic.diagnosticFile = node->sourceFile;
        if (auto* function = dynamic_cast<const FunctionDeclaration*>(node.get())) ValidateFunction(*function);
        else if (auto* variable = dynamic_cast<const VariableDeclaration*>(node.get())) {
            ValidateType(variable->type, false);
            if (variable->isReference) throw std::runtime_error("Global references are not implemented yet: " + variable->name);
            if (variable->isConsumable) throw std::runtime_error("Global consumables are not implemented yet: " + variable->name);
        }
        else if (auto* type = dynamic_cast<const TypeDeclaration*>(node.get())) {
            auto& table = semantic.members[type->name];
            auto& lifecycle = semantic.specialMembers[type->name];
            for (const auto& member : type->members) {
                semantic.diagnosticOffset = member.declaration->offset;
                if (auto* constructor = dynamic_cast<const ConstructorDeclaration*>(member.declaration.get())) {
                    ValidateFunction(*constructor);
                    for (const auto& existing : lifecycle.constructors) {
                        const auto* previous = static_cast<const ConstructorDeclaration*>(existing.declaration);
                        if (previous->parameters.size() != constructor->parameters.size()) continue;
                        bool same = true;
                        for (size_t i = 0; i < previous->parameters.size(); ++i)
                            if (Semantic::CanonicalType(previous->parameters[i].type) != Semantic::CanonicalType(constructor->parameters[i].type)) { same = false; break; }
                        if (same) throw std::runtime_error("Duplicate constructor signature: " + type->name);
                    }
                    lifecycle.constructors.push_back(Symbol{Kind::Constructor, constructor, "void", type->name, member.isPublic});
                    continue;
                }
                if (auto* destructor = dynamic_cast<const DestructorDeclaration*>(member.declaration.get())) {
                    if (lifecycle.destructor) throw std::runtime_error("Duplicate destructor: " + type->name);
                    ValidateFunction(*destructor);
                    lifecycle.destructor = Symbol{Kind::Destructor, destructor, "void", type->name, member.isPublic};
                    continue;
                }
                std::string name;
                Symbol symbol{Kind::Field, member.declaration.get(), {}, type->name, member.isPublic};
                if (auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get())) {
                    name = field->name; symbol.type = field->type; ValidateType(field->type, false);
                    if (field->isReference) throw std::runtime_error("Reference fields are not implemented yet: " + field->name);
                } else if (auto* method = dynamic_cast<const FunctionDeclaration*>(member.declaration.get())) {
                    name = method->name; symbol.kind = Kind::Method; symbol.type = method->returnType; ValidateFunction(*method);
                } else throw std::runtime_error("Unsupported type member");
                if (!table.emplace(name, std::move(symbol)).second) throw std::runtime_error("Duplicate member: " + name);
            }
        }
    }
}

void SemanticDeclarationCollector::ValidateLayout(const TypeDeclaration& type) {
    if (completeTypes.contains(type.name)) return;
    if (!activeTypes.insert(type.name).second) throw std::runtime_error("Invalid or incomplete field type: " + type.name);
    for (const auto& member : type.members) {
        auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
        if (!field) continue;
        auto nested = semantic.globals.find(field->type);
        if (nested != semantic.globals.end() && nested->second.kind == Kind::Type)
            ValidateLayout(*static_cast<const TypeDeclaration*>(nested->second.declaration));
    }
    activeTypes.erase(type.name);
    completeTypes.insert(type.name);
}

void SemanticDeclarationCollector::ValidateLayouts(const Program& program) {
    for (const auto& node : program.children) if (auto* type = dynamic_cast<const TypeDeclaration*>(node.get())) {
        semantic.diagnosticOffset = type->offset;
        semantic.diagnosticFile = type->sourceFile;
        ValidateLayout(*type);
    }
}

void SemanticDeclarationCollector::CheckGlobalDestruction(const Program& program) {
    for (const auto& node : program.children) {
        auto* variable = dynamic_cast<const VariableDeclaration*>(node.get());
        if (variable && semantic.NeedsDestruction(variable->type)) {
            semantic.diagnosticOffset = variable->offset;
            semantic.diagnosticFile = variable->sourceFile;
            throw std::runtime_error("Global variables with destructors are not supported: " + variable->name);
        }
    }
}

void SemanticDeclarationCollector::CheckObject(const std::string& typeName, std::unordered_set<std::string>& visiting) {
    auto type = semantic.globals.find(typeName);
    if (type == semantic.globals.end() || type->second.kind != Kind::Type) throw std::runtime_error("Brace initialization requires a user-defined type");
    if (!semantic.specialMembers.at(typeName).constructors.empty()) throw std::runtime_error("Global construction with constructors is not supported yet: " + typeName);
    if (!visiting.insert(typeName).second) throw std::runtime_error("Invalid or incomplete field type: " + typeName);
    auto* declaration = static_cast<const TypeDeclaration*>(type->second.declaration);
    for (const auto& member : declaration->members) {
        auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
        if (!field) continue;
        if (field->initialValue) ConstantExpression::Check(*field->initialValue);
        else if (auto nested = semantic.globals.find(field->type); nested != semantic.globals.end() && nested->second.kind == Kind::Type) CheckObject(field->type, visiting);
    }
    visiting.erase(typeName);
}

void SemanticDeclarationCollector::CheckGlobalObjects(const Program& program) {
    std::unordered_set<std::string> visiting;
    for (const auto& node : program.children) {
        auto* variable = dynamic_cast<const VariableDeclaration*>(node.get());
        if (variable && variable->aggregateInit) {
            semantic.diagnosticOffset = variable->offset;
            semantic.diagnosticFile = variable->sourceFile;
            CheckObject(variable->type, visiting);
        }
    }
}

void Semantic::Collect(const Program& program, bool requireExplicitEntry) {
    globals.clear();
    members.clear();
    specialMembers.clear();
    references.clear();
    resolvedCalls.clear();
    bareCalls.clear();
    bareMemberCalls.clear();
    referenceTargets.clear();
    constructorCalls.clear();
    defaultConstructors.clear();
    returnValueTypes.clear();
    referenceReturns.clear();
    destructionCache.clear();
    functionsWithMoves.clear();
    reinitializations.clear();
    entryPoint.clear();
    SemanticDeclarationCollector(*this).Collect(program, requireExplicitEntry);
}
