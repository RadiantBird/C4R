#include "Semantic.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <functional>
#include <optional>
#include <llvm/ADT/APInt.h>

namespace {
void ValidateNumber(const NumericLiteral& number) {
    try {
        if (number.value.find('.') != std::string::npos) (void)std::stod(number.value);
        else if (std::stoll(number.value) > INT32_MAX) throw std::out_of_range("literal");
    } catch (const std::out_of_range&) {
        throw std::runtime_error(number.value.find('.') == std::string::npos ? "Integer literal out of range" : "Floating literal out of range");
    }
}

std::optional<llvm::APInt> ConstantInteger(const Expression& expression) {
    if (auto* number = dynamic_cast<const NumericLiteral*>(&expression)) {
        if (number->value.find('.') != std::string::npos) return std::nullopt;
        long long value;
        try { value = std::stoll(number->value); }
        catch (const std::out_of_range&) { throw std::runtime_error("Integer literal out of range"); }
        if (value > INT32_MAX) throw std::runtime_error("Integer literal out of range");
        return llvm::APInt(32, static_cast<uint64_t>(value));
    }
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
        auto value = ConstantInteger(*unary->operand);
        if (!value) return std::nullopt;
        if (unary->op == "+") return *value;
        if (unary->op == "-") return -*value;
        if (unary->op == "~") return ~*value;
        return std::nullopt;
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        const auto& op = binary->op;
        if (op != "+" && op != "-" && op != "*" && op != "/" && op != "%" && op != "&" && op != "|" && op != "^" && op != "<<" && op != ">>" && op != "`") return std::nullopt;
        auto left = ConstantInteger(*binary->left), right = ConstantInteger(*binary->right);
        if (!left || !right) return std::nullopt;
        if (op == "+") return *left + *right;
        if (op == "-") return *left - *right;
        if (op == "*") return *left * *right;
        if (op == "/" || op == "%") {
            if (right->isZero() || (left->isMinSignedValue() && right->isAllOnes())) throw std::runtime_error("Invalid global constant initializer");
            return op == "/" ? left->sdiv(*right) : left->srem(*right);
        }
        if (op == "&") return *left & *right;
        if (op == "|") return *left | *right;
        if (op == "^") return *left ^ *right;
        if (op == "<<" || op == ">>") {
            if (right->uge(32)) throw std::runtime_error("Integer shift count out of range");
            return op == "<<" ? left->shl(static_cast<unsigned>(right->getZExtValue())) : left->ashr(static_cast<unsigned>(right->getZExtValue()));
        }
        if (op == "`") {
            if (right->isNegative()) throw std::runtime_error("Integer exponent must be nonnegative");
            uint64_t count = right->getZExtValue();
            llvm::APInt result(32, 1), factor = *left;
            while (count) { if (count & 1) result *= factor; factor *= factor; count >>= 1; }
            return result;
        }
    }
    return std::nullopt;
}

std::optional<float> ConstantFloat(const Expression& expression) {
    if (auto* number = dynamic_cast<const NumericLiteral*>(&expression)) {
        if (number->value.find('.') == std::string::npos) return std::nullopt;
        return static_cast<float>(std::stod(number->value));
    }
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
        if (unary->op != "+" && unary->op != "-") return std::nullopt;
        auto value = ConstantFloat(*unary->operand);
        if (!value) return std::nullopt;
        return unary->op == "-" ? -*value : *value;
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        if (binary->op != "+" && binary->op != "-" && binary->op != "*" && binary->op != "/") return std::nullopt;
        auto left = ConstantFloat(*binary->left);
        if (!left) return std::nullopt;
        auto right = ConstantFloat(*binary->right);
        if (!right) return std::nullopt;
        if (binary->op == "+") return *left + *right;
        if (binary->op == "-") return *left - *right;
        if (binary->op == "*") return *left * *right;
        return *left / *right;
    }
    return std::nullopt;
}

std::optional<bool> ConstantBoolean(const Expression& expression);
std::optional<bool> ConstantComparison(const Expression& left, const std::string& op, const Expression& right) {
    auto compare = [&](auto a, auto b) -> std::optional<bool> {
        if (op == "==") return a == b;
        if (op == "!=") return a != b;
        if (op == "<") return a < b;
        if (op == "<=") return a <= b;
        if (op == ">") return a > b;
        if (op == ">=") return a >= b;
        return std::nullopt;
    };
    if (auto a = ConstantInteger(left)) {
        if (auto b = ConstantInteger(right)) return compare(a->getSExtValue(), b->getSExtValue());
        return std::nullopt;
    }
    if (auto a = ConstantFloat(left)) {
        if (auto b = ConstantFloat(right)) return compare(*a, *b);
        return std::nullopt;
    }
    if (auto a = ConstantBoolean(left)) {
        if (auto b = ConstantBoolean(right); b && (op == "==" || op == "!=")) return compare(*a, *b);
    }
    return std::nullopt;
}

std::optional<bool> ConstantBoolean(const Expression& expression) {
    if (auto* literal = dynamic_cast<const BooleanLiteral*>(&expression)) return literal->value;
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression); unary && unary->op == "!") {
        auto value = ConstantBoolean(*unary->operand);
        return value ? std::optional<bool>(!*value) : std::nullopt;
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        const auto& op = binary->op;
        if (op != "&&" && op != "||" && op != "!&&" && op != "!||" && op != "^^") return std::nullopt;
        auto left = ConstantBoolean(*binary->left);
        if (!left) return std::nullopt;
        bool isAnd = op == "&&" || op == "!&&";
        bool negate = op == "!&&" || op == "!||";
        if (op != "^^" && ((isAnd && !*left) || (!isAnd && *left))) return negate ? !*left : *left;
        auto right = ConstantBoolean(*binary->right);
        if (!right) return std::nullopt;
        bool result = op == "^^" ? *left != *right : isAnd ? *left && *right : *left || *right;
        return negate ? !result : result;
    }
    if (auto* chain = dynamic_cast<const ComparisonExpr*>(&expression)) {
        for (size_t i = 0; i < chain->operators.size(); ++i) {
            auto result = ConstantComparison(*chain->operands[i], chain->operators[i], *chain->operands[i + 1]);
            if (!result) return std::nullopt;
            if (!*result) return false;
        }
        return true;
    }
    return std::nullopt;
}

void CheckConstantExpression(const Expression& expression, bool evaluated = true) {
    if (auto* number = dynamic_cast<const NumericLiteral*>(&expression)) { ValidateNumber(*number); return; }
    if (dynamic_cast<const BooleanLiteral*>(&expression)) return;
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
        if (unary->op != "+" && unary->op != "-" && unary->op != "~" && unary->op != "!")
            throw std::runtime_error("Global initializer requires a constant expression");
        CheckConstantExpression(*unary->operand, evaluated);
        return;
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        if (binary->op == "=" || binary->op.ends_with("=")) throw std::runtime_error("Global initializer requires a constant expression");
        CheckConstantExpression(*binary->left, evaluated);
        bool evaluateRight = evaluated;
        if (evaluated && (binary->op == "&&" || binary->op == "!&&" || binary->op == "||" || binary->op == "!||")) {
            auto left = ConstantBoolean(*binary->left);
            bool isAnd = binary->op == "&&" || binary->op == "!&&";
            if (left && ((isAnd && !*left) || (!isAnd && *left))) evaluateRight = false;
        }
        CheckConstantExpression(*binary->right, evaluateRight);
        if (evaluated && binary->op != "&&" && binary->op != "!&&" && binary->op != "||" && binary->op != "!||") (void)ConstantInteger(expression);
        return;
    }
    if (auto* chain = dynamic_cast<const ComparisonExpr*>(&expression)) {
        CheckConstantExpression(*chain->operands.front(), evaluated);
        bool active = evaluated;
        for (size_t i = 0; i < chain->operators.size(); ++i) {
            CheckConstantExpression(*chain->operands[i + 1], active);
            if (active) {
                auto result = ConstantComparison(*chain->operands[i], chain->operators[i], *chain->operands[i + 1]);
                if (result && !*result) active = false;
            }
        }
        return;
    }
    throw std::runtime_error("Global initializer requires a constant expression");
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
    for (const auto& node : program.children) {
        diagnosticOffset = node->offset;
        diagnosticFile = node->sourceFile;
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
        auto [previous, inserted] = globals.emplace(name, symbol);
        if (!inserted) {
            std::string kind = symbol.kind == Kind::Function && previous->second.kind == Kind::Function ? "function" : "declaration";
            throw std::runtime_error("Duplicate " + kind + ": " + name +
                (node->sourceFile.empty() ? "" : " (" + node->sourceFile + ", previously " + previous->second.declaration->sourceFile + ")"));
        }
    }
    bool explicitEntry = false;
    unsigned entryCount = 0;
    for (const auto* name : {"main", "start", "entry"}) {
        auto found = globals.find(name);
        if (found == globals.end() || found->second.kind != Kind::Function) continue;
        auto* function = static_cast<const FunctionDeclaration*>(found->second.declaration);
        if (function->parameters.empty() && (function->returnType == "int" || function->returnType == "int32")) {
            explicitEntry = true;
            if (entryPoint.empty()) entryPoint = name;
            ++entryCount;
        }
    }
    if (requireExplicitEntry && entryCount != 1) throw std::runtime_error("Multiple input files require exactly one explicit entry point");
    if (auto main = globals.find("main"); main != globals.end() && main->second.kind == Kind::Function) {
        auto* function = static_cast<const FunctionDeclaration*>(main->second.declaration);
        if (!function->parameters.empty() || (function->returnType != "int" && function->returnType != "int32"))
            throw std::runtime_error("main must return int and have no parameters");
    }
    if (explicitEntry) for (const auto& node : program.children)
        if (!dynamic_cast<const TypeDeclaration*>(node.get()) && !dynamic_cast<const FunctionDeclaration*>(node.get()) && !dynamic_cast<const VariableDeclaration*>(node.get()))
            throw std::runtime_error("Top-level statements with explicit entry point are not supported yet");
    if (explicitEntry) {
        for (const auto& node : program.children) {
            auto* variable = dynamic_cast<const VariableDeclaration*>(node.get());
            if (variable && variable->initialValue) {
                diagnosticOffset = variable->offset;
                diagnosticFile = variable->sourceFile;
                CheckConstantExpression(*variable->initialValue);
            }
        }
    }
    std::function<bool(const std::string&)> knownType = [&](const std::string& name) {
        if (!name.empty() && name.back() == '*') return knownType(name.substr(0, name.size() - 1));
        if (name == "void" || name == "int" || name == "int32" || name == "bool" || name == "float") return true;
        auto found = globals.find(name);
        return found != globals.end() && found->second.kind == Kind::Type;
    };
    auto validateType = [&](const std::string& name, bool allowVoid) {
        if (!knownType(name)) throw std::runtime_error("Unknown type: " + name);
        if (!name.empty() && name.back() == '*') return;
        if (!allowVoid && name == "void") throw std::runtime_error("Variable or parameter cannot have void type");
    };
    auto validateFunction = [&](const FunctionDeclaration& function) {
        validateType(function.returnType, true);
        if (function.returnsMutableReference && !function.returnsReference)
            throw std::runtime_error("mutable return requires a reference type");
        if (!function.returnsReference && !function.returnLifetime.empty())
            throw std::runtime_error("Lifetime specified on a non-reference return");
        if (function.returnsReference && function.returnLifetime.empty())
            throw std::runtime_error("Reference return requires an explicit lifetime");
        std::unordered_set<std::string> names;
        size_t returnLifetimeParameters = 0;
        for (const auto& parameter : function.parameters) {
            diagnosticOffset = parameter.offset;
            if (!parameter.sourceFile.empty()) diagnosticFile = parameter.sourceFile;
            validateType(parameter.type, false);
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
    };
    for (const auto& node : program.children) {
        diagnosticOffset = node->offset;
        diagnosticFile = node->sourceFile;
        if (auto* function = dynamic_cast<const FunctionDeclaration*>(node.get())) validateFunction(*function);
        else if (auto* variable = dynamic_cast<const VariableDeclaration*>(node.get())) {
            validateType(variable->type, false);
            if (variable->isReference) throw std::runtime_error("Global references are not implemented yet: " + variable->name);
            if (variable->isConsumable) throw std::runtime_error("Global consumables are not implemented yet: " + variable->name);
        }
        else if (auto* type = dynamic_cast<const TypeDeclaration*>(node.get())) {
            auto& table = members[type->name];
            auto& lifecycle = specialMembers[type->name];
            for (const auto& member : type->members) {
                diagnosticOffset = member.declaration->offset;
                if (auto* constructor = dynamic_cast<const ConstructorDeclaration*>(member.declaration.get())) {
                    validateFunction(*constructor);
                    std::function<std::string(std::string)> canonical = [&](std::string name) {
                        if (!name.empty() && name.back() == '*') return canonical(name.substr(0, name.size() - 1)) + "*";
                        return name == "int" ? std::string("int32") : name;
                    };
                    for (const auto& existing : lifecycle.constructors) {
                        const auto* previous = static_cast<const ConstructorDeclaration*>(existing.declaration);
                        if (previous->parameters.size() != constructor->parameters.size()) continue;
                        bool same = true;
                        for (size_t i = 0; i < previous->parameters.size(); ++i)
                            if (canonical(previous->parameters[i].type) != canonical(constructor->parameters[i].type)) { same = false; break; }
                        if (same) throw std::runtime_error("Duplicate constructor signature: " + type->name);
                    }
                    lifecycle.constructors.push_back(Symbol{Kind::Constructor, constructor, "void", type->name, member.isPublic});
                    continue;
                }
                if (auto* destructor = dynamic_cast<const DestructorDeclaration*>(member.declaration.get())) {
                    if (lifecycle.destructor) throw std::runtime_error("Duplicate destructor: " + type->name);
                    validateFunction(*destructor);
                    lifecycle.destructor = Symbol{Kind::Destructor, destructor, "void", type->name, member.isPublic};
                    continue;
                }
                std::string name;
                Symbol symbol{Kind::Field, member.declaration.get(), {}, type->name, member.isPublic};
                if (auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get())) {
                    name = field->name; symbol.type = field->type; validateType(field->type, false);
                    if (field->isReference) throw std::runtime_error("Reference fields are not implemented yet: " + field->name);
                } else if (auto* method = dynamic_cast<const FunctionDeclaration*>(member.declaration.get())) {
                    name = method->name; symbol.kind = Kind::Method; symbol.type = method->returnType; validateFunction(*method);
                } else throw std::runtime_error("Unsupported type member");
                if (!table.emplace(name, std::move(symbol)).second) throw std::runtime_error("Duplicate member: " + name);
            }
        }
    }
    std::unordered_set<std::string> completeTypes;
    std::unordered_set<std::string> activeTypes;
    std::function<void(const TypeDeclaration&)> validateLayout = [&](const TypeDeclaration& type) {
        if (completeTypes.contains(type.name)) return;
        if (!activeTypes.insert(type.name).second) throw std::runtime_error("Invalid or incomplete field type: " + type.name);
        for (const auto& member : type.members) {
            auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
            if (!field) continue;
            auto nested = globals.find(field->type);
            if (nested != globals.end() && nested->second.kind == Kind::Type)
                validateLayout(*static_cast<const TypeDeclaration*>(nested->second.declaration));
        }
        activeTypes.erase(type.name);
        completeTypes.insert(type.name);
    };
    for (const auto& node : program.children) if (auto* type = dynamic_cast<const TypeDeclaration*>(node.get())) {
        diagnosticOffset = type->offset;
        diagnosticFile = type->sourceFile;
        validateLayout(*type);
    }
    if (explicitEntry) for (const auto& node : program.children) {
        auto* variable = dynamic_cast<const VariableDeclaration*>(node.get());
        if (variable && NeedsDestruction(variable->type)) {
            diagnosticOffset = variable->offset;
            diagnosticFile = variable->sourceFile;
            throw std::runtime_error("Global variables with destructors are not supported: " + variable->name);
        }
    }
    if (explicitEntry) {
        std::unordered_set<std::string> visiting;
        std::function<void(const std::string&)> checkObject = [&](const std::string& typeName) {
            auto type = globals.find(typeName);
            if (type == globals.end() || type->second.kind != Kind::Type) throw std::runtime_error("Brace initialization requires a user-defined type");
            if (!specialMembers.at(typeName).constructors.empty()) throw std::runtime_error("Global construction with constructors is not supported yet: " + typeName);
            if (!visiting.insert(typeName).second) throw std::runtime_error("Invalid or incomplete field type: " + typeName);
            auto* declaration = static_cast<const TypeDeclaration*>(type->second.declaration);
            for (const auto& member : declaration->members) {
                auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
                if (!field) continue;
                if (field->initialValue) CheckConstantExpression(*field->initialValue);
                else if (auto nested = globals.find(field->type); nested != globals.end() && nested->second.kind == Kind::Type) checkObject(field->type);
            }
            visiting.erase(typeName);
        };
        for (const auto& node : program.children) {
            auto* variable = dynamic_cast<const VariableDeclaration*>(node.get());
            if (variable && variable->aggregateInit) {
                diagnosticOffset = variable->offset;
                diagnosticFile = variable->sourceFile;
                checkObject(variable->type);
            }
        }
    }
}

bool Semantic::NeedsDestruction(const std::string& type) const {
    auto found = globals.find(type);
    if (found == globals.end() || found->second.kind != Kind::Type) return false;
    if (auto cached = destructionCache.find(type); cached != destructionCache.end()) return cached->second;
    const auto& lifecycle = specialMembers.at(type);
    if (lifecycle.destructor) return destructionCache[type] = true;
    const auto* declaration = static_cast<const TypeDeclaration*>(found->second.declaration);
    for (const auto& member : declaration->members)
        if (auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get()); field && NeedsDestruction(field->type))
            return destructionCache[type] = true;
    return destructionCache[type] = false;
}

const ConstructorDeclaration* Semantic::SelectDefaultConstructor(const std::string& type, const std::string& owner) const {
    const auto& lifecycle = specialMembers.at(type);
    if (lifecycle.constructors.empty()) return nullptr;
    for (const auto& candidate : lifecycle.constructors) {
        auto* declaration = static_cast<const ConstructorDeclaration*>(candidate.declaration);
        if (!declaration->parameters.empty()) continue;
        if (!candidate.isPublic && owner != type) throw std::runtime_error("Private constructor: " + type);
        return declaration;
    }
    throw std::runtime_error("No matching constructor: " + type);
}

void Semantic::CheckBodies(const Program& program) const {
    struct Scope {
        std::unordered_map<std::string, std::string> variables;
        std::unordered_map<std::string, bool> mutableVariables;
        std::unordered_map<std::string, bool> receiverVariables;
        std::unordered_map<std::string, size_t> bindingIds;
        std::unordered_set<size_t> availableBindings;
        std::unordered_map<const void*, const void*> borrowOrigins;
        std::unordered_map<const void*, const void*> borrowParents;
        std::unordered_map<const void*, std::vector<std::pair<const void*, bool>>> activeBorrows;
        std::unordered_map<const void*, const void*> pointerOrigins;
        std::unordered_set<std::string> localFunctions;
        std::unordered_map<std::string, const FunctionDeclaration*> localSignatures;
        std::unordered_set<std::string> shadowedMethods;
        std::unordered_set<std::string> globalBindings;
        std::unordered_set<std::string> inaccessible;
        std::string owner;
        std::string returnType;
        bool valueScope = false;
        bool allowCapture = false;
        bool mayEvaluate = true;
        bool directConstruction = false;
        std::string* valueType = nullptr;
        bool* valueSawReturn = nullptr;
        bool* valueUnknown = nullptr;
        size_t loopDepth = 0;
        size_t valueLoopDepth = 0;
        const FunctionDeclaration* method = nullptr;
        const FunctionDeclaration* function = nullptr;
    };
    std::function<std::string(std::string)> canonical = [&](std::string type) {
        if (!type.empty() && type.back() == '*') return canonical(type.substr(0, type.size() - 1)) + "*";
        return type == "int" ? std::string("int32") : type;
    };
    auto directValue = [&](const Expression* value) {
        if (dynamic_cast<const NumericLiteral*>(value) || dynamic_cast<const BooleanLiteral*>(value)) return true;
        if (auto* identifier = dynamic_cast<const IdentifierExpr*>(value)) return bareCalls.contains(identifier);
        if (auto* member = dynamic_cast<const MemberExpr*>(value)) return bareMemberCalls.contains(member);
        return dynamic_cast<const MoveExpr*>(value) || dynamic_cast<const ConstructExpr*>(value) || dynamic_cast<const CallExpr*>(value) ||
            dynamic_cast<const BlockExpr*>(value) || dynamic_cast<const IfExpr*>(value);
    };
    auto consumableValue = [&](const Expression* value) {
        return directValue(value) || dynamic_cast<const UnaryExpr*>(value) ||
            dynamic_cast<const BinaryExpr*>(value) || dynamic_cast<const ComparisonExpr*>(value);
    };
    std::function<void(const std::string&, const std::string&)> requireAssignableFields;
    requireAssignableFields = [&](const std::string& type, const std::string& owner) {
        const auto& symbols = members.at(type);
        for (const auto& [name, symbol] : symbols) {
            if (symbol.kind != Kind::Field) continue;
            auto* field = static_cast<const VariableDeclaration*>(symbol.declaration);
            if (!field->isMutable) throw std::runtime_error("Assignment to a user-defined value would modify an immutable field: " + name);
            if (!symbol.isPublic && owner != type) throw std::runtime_error("Private member: " + name);
            auto nested = globals.find(field->type);
            if (nested != globals.end() && nested->second.kind == Kind::Type) requireAssignableFields(field->type, owner);
        }
    };
    std::function<void(const std::string&, const std::string&)> requireDestructionAccess;
    requireDestructionAccess = [&](const std::string& type, const std::string& owner) {
        auto found = globals.find(type);
        if (found == globals.end() || found->second.kind != Kind::Type) return;
        const auto& lifecycle = specialMembers.at(type);
        if (lifecycle.destructor && !lifecycle.destructor->isPublic && owner != type)
            throw std::runtime_error("Private destructor: " + type);
        auto* declaration = static_cast<const TypeDeclaration*>(found->second.declaration);
        for (const auto& member : declaration->members) {
            auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
            if (!field) continue;
            auto nested = globals.find(field->type);
            if (nested != globals.end() && nested->second.kind == Kind::Type) {
                if (!field->initialValue) {
                    if (auto* constructor = SelectDefaultConstructor(field->type, type)) defaultConstructors[field] = constructor;
                }
                requireDestructionAccess(field->type, type);
            }
        }
    };
    std::function<std::string(const Expression&, Scope&)> expression;
    std::function<std::optional<bool>(const Expression&, Scope&)> mutableValue;
    std::function<bool(const Expression&, const Scope&)> isReceiver;
    std::function<void(const std::vector<std::unique_ptr<ASTNode>>&, Scope)> body;
    std::function<void(const FunctionDeclaration&, const std::vector<std::unique_ptr<Expression>>&, Scope&, const std::string&)> checkReferenceArguments;
    std::function<const void*(const Expression&, Scope&)> borrowRoot;
    std::function<const void*(const Expression&, Scope&)> borrowBinding;
    std::function<void(const Expression&, Scope&, bool)> checkBorrowAccess;
    std::function<const void*(const Expression&, Scope&)> pointerOrigin;
    std::unordered_map<const Expression*, std::pair<const void*, const void*>> returnOrigins;
    std::unordered_set<const void*> unsafePointerVariables;
    std::function<void(const FunctionDeclaration&, const std::vector<std::unique_ptr<Expression>>&, Scope&, const Expression&)> recordReturnOrigin;
    std::function<bool(const Expression&, Scope&)> pointerMayReferenceLocal;
    std::function<void(const FunctionDeclaration&, Scope, bool)> function;
    std::function<bool(const std::vector<std::unique_ptr<ASTNode>>&)> terminates;
    terminates = [&](const std::vector<std::unique_ptr<ASTNode>>& nodes) {
        for (const auto& node : nodes) {
            if (dynamic_cast<const ReturnStatement*>(node.get()) || dynamic_cast<const LoopControl*>(node.get())) return true;
            if (auto* block = dynamic_cast<const Block*>(node.get()); block && terminates(block->statements)) return true;
            if (auto* branch = dynamic_cast<const IfStatement*>(node.get()); branch && branch->elseBranch &&
                terminates(branch->thenBranch->statements) && terminates(branch->elseBranch->statements)) return true;
        }
        return false;
    };
    std::unordered_map<const FunctionDeclaration*, bool> mutates;
    std::unordered_map<const FunctionDeclaration*, std::vector<std::pair<std::string, size_t>>> capturedBindings;
    size_t nextBindingId = 0;
    std::unordered_map<size_t, const void*> bindingOrigins;
    std::unordered_set<size_t> globalBindingIds;
    std::unordered_map<std::string, size_t> globalIdsByName;
    std::unordered_map<const FunctionDeclaration*, std::vector<const FunctionDeclaration*>> calls;
    struct DeferredCall { const FunctionDeclaration* method; size_t offset; std::string file; };
    std::vector<DeferredCall> immutableCalls;
    function = [&](const FunctionDeclaration& declaration, Scope scope, bool local) {
        if (!declaration.sourceFile.empty()) diagnosticFile = declaration.sourceFile;
        diagnosticOffset = declaration.offset;
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
            diagnosticOffset = parameter.offset;
            if (!parameter.sourceFile.empty()) diagnosticFile = parameter.sourceFile;
            if (!bindings.insert(parameter.name).second) throw std::runtime_error("Duplicate parameter: " + parameter.name);
            if (parameter.type == "void") throw std::runtime_error("Parameter cannot have void type");
            if (parameter.isConsumable && parameter.isReference) throw std::runtime_error("A parameter cannot be both borrowed and consumable");
            if (NeedsDestruction(parameter.type)) requireDestructionAccess(parameter.type, scope.owner);
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
                    auto& global = globals.at(name);
                    scope.variables[name] = canonical(global.type);
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
        scope.returnType = canonical(declaration.returnType);
        scope.function = &declaration;
        scope.valueScope = false;
        scope.allowCapture = true;
        scope.mayEvaluate = true;
        scope.valueType = nullptr; scope.valueSawReturn = nullptr; scope.valueUnknown = nullptr;
        scope.loopDepth = 0;
        for (const auto& parameter : declaration.parameters) {
            scope.variables[parameter.name] = canonical(parameter.type);
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
        body(declaration.body, std::move(scope));
        const bool implicitEntryReturn = !local && outer.method == nullptr && declaration.parameters.empty() &&
            canonical(declaration.returnType) == "int32" &&
            (declaration.name == "main" || declaration.name == "start" || declaration.name == "entry");
        if (canonical(declaration.returnType) != "void" && !implicitEntryReturn && !terminates(declaration.body))
            throw std::runtime_error("Function must return a value: " + declaration.name);
    };
    expression = [&](const Expression& value, Scope& scope) -> std::string {
        diagnosticOffset = value.offset;
        if (auto* move = dynamic_cast<const MoveExpr*>(&value)) {
            if (scope.function) functionsWithMoves.insert(scope.function);
            auto type = expression(*move->operand, scope);
            diagnosticOffset = value.offset;
            auto* identifier = dynamic_cast<const IdentifierExpr*>(move->operand.get());
            auto* reference = identifier ? Reference(*identifier) : nullptr;
            if (!reference || reference->kind != ResolvedReference::Kind::Variable)
                throw std::runtime_error("move requires an owned variable; field moves are not supported");
            if (scope.function) for (const auto& parameter : scope.function->parameters)
                if (parameter.isReference && reference->declaration == &parameter)
                    throw std::runtime_error("Cannot move a borrowed parameter");
            auto borrowed = scope.borrowOrigins.find(reference->declaration);
            if (borrowed != scope.borrowOrigins.end()) throw std::runtime_error("Cannot move a borrowed variable");
            auto active = scope.activeBorrows.find(reference->declaration);
            if (active != scope.activeBorrows.end() && !active->second.empty())
                throw std::runtime_error("Cannot move an object while it is borrowed");
            return type;
        }
        auto callResult = [&](const FunctionDeclaration& declaration) {
            auto type = canonical(declaration.returnType);
            if (!declaration.returnsReference && NeedsDestruction(type)) requireDestructionAccess(type, scope.owner);
            return type;
        };
        if (auto* construct = dynamic_cast<const ConstructExpr*>(&value)) {
            auto type = globals.find(construct->type);
            if (type == globals.end() || type->second.kind != Kind::Type)
                throw std::runtime_error("Constructor requires a user-defined type: " + construct->type);
            std::vector<std::string> argumentTypes;
            Scope argumentScope = scope;
            argumentScope.directConstruction = false;
            for (const auto& argument : construct->arguments) {
                auto argumentType = expression(*argument, argumentScope);
                argumentTypes.push_back(argumentType);
            }
            auto lifecycle = specialMembers.find(construct->type);
            const Symbol* match = nullptr;
            if (lifecycle != specialMembers.end()) for (const auto& candidate : lifecycle->second.constructors) {
                auto* declaration = static_cast<const ConstructorDeclaration*>(candidate.declaration);
                if (declaration->parameters.size() != argumentTypes.size()) continue;
                bool same = true;
                for (size_t i = 0; i < argumentTypes.size(); ++i)
                    if (argumentTypes[i] != canonical(declaration->parameters[i].type)) { same = false; break; }
                if (!same) continue;
                if (match) throw std::runtime_error("Ambiguous constructor: " + construct->type);
                match = &candidate;
            }
            if (!match) throw std::runtime_error("No matching constructor: " + construct->type);
            if (!match->isPublic && scope.owner != construct->type) throw std::runtime_error("Private constructor: " + construct->type);
            checkReferenceArguments(*static_cast<const FunctionDeclaration*>(match->declaration), construct->arguments, scope, construct->type);
            requireDestructionAccess(construct->type, scope.owner);
            constructorCalls[construct] = static_cast<const ConstructorDeclaration*>(match->declaration);
            return construct->type;
        }
        if (auto* number = dynamic_cast<const NumericLiteral*>(&value)) {
            ValidateNumber(*number);
            if (number->value.find('.') != std::string::npos) return "float";
            return "int32";
        }
        if (dynamic_cast<const BooleanLiteral*>(&value)) return "bool";
        if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
            auto found = scope.variables.find(identifier->name);
            if (found != scope.variables.end()) {
                checkBorrowAccess(value, scope, false);
                if (scope.receiverVariables.contains(identifier->name) && scope.receiverVariables.at(identifier->name)) {
                    auto type = members.find(scope.owner);
                    if (type != members.end()) {
                        auto field = type->second.find(identifier->name);
                        if (field != type->second.end() && field->second.kind == Kind::Field)
                            references[identifier] = {ResolvedReference::Kind::Field, field->second.declaration};
                    }
                } else if (auto binding = scope.bindingIds.find(identifier->name); binding != scope.bindingIds.end()) {
                    references[identifier] = {ResolvedReference::Kind::Variable, bindingOrigins.at(binding->second)};
                }
                return found->second;
            }
            if (scope.inaccessible.contains(identifier->name)) throw std::runtime_error("Variable requires capture: " + identifier->name);
            if (scope.method && !globals.contains(identifier->name) && !scope.shadowedMethods.contains(identifier->name)) {
                auto type = members.find(scope.owner);
                if (type != members.end()) {
                    auto entry = type->second.find(identifier->name);
                    if (entry != type->second.end() && entry->second.kind == Kind::Method) {
                        auto* method = static_cast<const FunctionDeclaration*>(entry->second.declaration);
                        if (!method->parameters.empty()) throw std::runtime_error("Wrong number of arguments for method: " + identifier->name);
                        calls[scope.method].push_back(method);
                        bareCalls[identifier] = method;
                        return callResult(*method);
                    }
                }
            }
            if (scope.localFunctions.contains(identifier->name)) {
                auto local = scope.localSignatures.find(identifier->name);
                if (local != scope.localSignatures.end()) {
                    if (!local->second->parameters.empty()) throw std::runtime_error("Wrong number of arguments for function: " + identifier->name);
                    for (const auto& [name, id] : capturedBindings[local->second])
                        if (!scope.availableBindings.contains(id)) throw std::runtime_error("Capture is not available in this function: " + name);
                    bareCalls[identifier] = local->second;
                    return callResult(*local->second);
                }
                if (!globals.contains(identifier->name)) return "";
            }
            auto symbol = globals.find(identifier->name);
            if (symbol != globals.end() && symbol->second.kind == Kind::Function) {
                auto* function = static_cast<const FunctionDeclaration*>(symbol->second.declaration);
                if (!function->parameters.empty()) throw std::runtime_error("Wrong number of arguments for function: " + identifier->name);
                bareCalls[identifier] = function;
                return callResult(*function);
            }
            throw std::runtime_error("Undefined variable: " + identifier->name);
        }
        if (auto* call = dynamic_cast<const CallExpr*>(&value)) {
            std::string objectType;
            if (call->object) objectType = expression(*call->object, scope);
            std::vector<std::string> argumentTypes;
            for (const auto& argument : call->arguments) {
                auto type = expression(*argument, scope);
                argumentTypes.push_back(type);
            }
            if (call->object) {
                if (objectType.empty()) return "";
                auto type = members.find(objectType);
                if (type == members.end()) throw std::runtime_error("Method call requires a user-defined type");
                auto method = type->second.find(call->name);
                if (method == type->second.end() || method->second.kind != Kind::Method) throw std::runtime_error("Unknown method: " + call->name);
                if (!method->second.isPublic && scope.owner != objectType) throw std::runtime_error("Private member: " + call->name);
                const auto* declaration = static_cast<const FunctionDeclaration*>(method->second.declaration);
                if (argumentTypes.size() != declaration->parameters.size()) throw std::runtime_error("Wrong number of arguments for method: " + call->name);
                for (size_t i = 0; i < argumentTypes.size(); ++i)
                    if (!argumentTypes[i].empty() && argumentTypes[i] != canonical(declaration->parameters[i].type))
                        throw std::runtime_error("Argument type mismatch for method: " + call->name);
                checkReferenceArguments(*declaration, call->arguments, scope, call->name);
                auto objectMutable = mutableValue(*call->object, scope);
                if (!objectMutable) throw std::runtime_error("Method call requires an instance variable");
                if (objectMutable && !*objectMutable) immutableCalls.push_back({declaration, value.offset, diagnosticFile});
                else if (objectMutable && scope.method && isReceiver(*call->object, scope)) calls[scope.method].push_back(declaration);
                if (const void* root = borrowRoot(*call->object, scope)) {
                    auto loans = scope.activeBorrows.find(root);
                    if (loans != scope.activeBorrows.end() && std::any_of(loans->second.begin(), loans->second.end(), [](const auto& loan) { return !loan.second; }))
                        immutableCalls.push_back({declaration, value.offset, diagnosticFile});
                }
                recordReturnOrigin(*declaration, call->arguments, scope, value);
                return callResult(*declaration);
            }
            if (scope.variables.contains(call->name)) throw std::runtime_error("Variable is not callable: " + call->name);
            if (scope.inaccessible.contains(call->name) && !scope.localFunctions.contains(call->name)) throw std::runtime_error("Variable requires capture: " + call->name);
            if (scope.localFunctions.contains(call->name)) {
                auto local = scope.localSignatures.find(call->name);
                if (local != scope.localSignatures.end() && (scope.shadowedMethods.contains(call->name) || scope.owner.empty())) {
                    auto* declaration = local->second;
                    if (argumentTypes.size() != declaration->parameters.size()) throw std::runtime_error("Wrong number of arguments for function: " + call->name);
                    for (size_t i = 0; i < argumentTypes.size(); ++i)
                        if (!argumentTypes[i].empty() && argumentTypes[i] != canonical(declaration->parameters[i].type))
                            throw std::runtime_error("Argument type mismatch for function: " + call->name);
                    checkReferenceArguments(*declaration, call->arguments, scope, call->name);
                    for (const auto& [name, id] : capturedBindings[declaration])
                        if (!scope.availableBindings.contains(id)) throw std::runtime_error("Capture is not available in this function: " + name);
                    resolvedCalls[call] = declaration;
                    recordReturnOrigin(*declaration, call->arguments, scope, value);
                    return callResult(*declaration);
                }
                if (scope.method && !globals.contains(call->name) && !scope.shadowedMethods.contains(call->name)) {
                    auto type = members.find(scope.owner);
                    if (type != members.end()) {
                        auto method = type->second.find(call->name);
                        if (method != type->second.end() && method->second.kind == Kind::Method) {
                            auto* declaration = static_cast<const FunctionDeclaration*>(method->second.declaration);
                            if (argumentTypes.size() != declaration->parameters.size()) throw std::runtime_error("Wrong number of arguments for method: " + call->name);
                            for (size_t i = 0; i < argumentTypes.size(); ++i)
                                if (!argumentTypes[i].empty() && argumentTypes[i] != canonical(declaration->parameters[i].type))
                                    throw std::runtime_error("Argument type mismatch for method: " + call->name);
                            checkReferenceArguments(*declaration, call->arguments, scope, call->name);
                            calls[scope.method].push_back(declaration);
                            recordReturnOrigin(*declaration, call->arguments, scope, value);
                            return callResult(*declaration);
                        }
                    }
                }
                if (!globals.contains(call->name)) return "";
            }
            auto symbol = globals.find(call->name);
            if (symbol == globals.end()) throw std::runtime_error("Undefined function: " + call->name);
            if (symbol->second.kind != Kind::Function) throw std::runtime_error("Undefined function: " + call->name);
            const auto& declaration = *static_cast<const FunctionDeclaration*>(symbol->second.declaration);
            if (argumentTypes.size() != declaration.parameters.size()) throw std::runtime_error("Wrong number of arguments for function: " + call->name);
            for (size_t i = 0; i < argumentTypes.size(); ++i)
                if (!argumentTypes[i].empty() && argumentTypes[i] != canonical(declaration.parameters[i].type))
                    throw std::runtime_error("Argument type mismatch for function: " + call->name);
            checkReferenceArguments(declaration, call->arguments, scope, call->name);
            resolvedCalls[call] = &declaration;
            recordReturnOrigin(declaration, call->arguments, scope, value);
            return callResult(declaration);
        }
        if (auto* member = dynamic_cast<const MemberExpr*>(&value)) {
            auto objectType = expression(*member->object, scope);
            if (objectType.empty()) return "";
            auto type = members.find(objectType);
            if (type == members.end()) throw std::runtime_error("Member access requires a user-defined type");
            auto found = type->second.find(member->name);
            if (found == type->second.end()) throw std::runtime_error("Unknown field: " + member->name);
            if (!found->second.isPublic && scope.owner != objectType) throw std::runtime_error("Private member: " + member->name);
            auto objectMutable = mutableValue(*member->object, scope);
            if (!objectMutable) throw std::runtime_error("Member access requires an instance variable");
            if (found->second.kind == Kind::Method) {
                auto* method = static_cast<const FunctionDeclaration*>(found->second.declaration);
                bareMemberCalls[member] = method;
                if (!method->parameters.empty()) throw std::runtime_error("Wrong number of arguments for method: " + member->name);
                if (objectMutable && !*objectMutable) immutableCalls.push_back({method, value.offset, diagnosticFile});
                else if (objectMutable && scope.method && isReceiver(*member->object, scope)) calls[scope.method].push_back(method);
                if (const void* root = borrowRoot(*member->object, scope)) {
                    auto loans = scope.activeBorrows.find(root);
                    if (loans != scope.activeBorrows.end() && std::any_of(loans->second.begin(), loans->second.end(), [](const auto& loan) { return !loan.second; }))
                        immutableCalls.push_back({method, value.offset, diagnosticFile});
                }
                return callResult(*method);
            }
            return canonical(found->second.type);
        }
        if (auto* unary = dynamic_cast<const UnaryExpr*>(&value)) {
            auto type = expression(*unary->operand, scope);
            if (unary->op == "&") {
                if (!borrowBinding(*unary->operand, scope)) throw std::runtime_error("Address-of requires an addressable object");
                return type.empty() ? std::string{} : canonical(type) + "*";
            }
            if (unary->op == "++" || unary->op == "--") {
                auto mutableTarget = mutableValue(*unary->operand, scope);
                if (!mutableTarget) throw std::runtime_error("Assignment requires a variable");
                if (mutableTarget && !*mutableTarget) throw std::runtime_error("Cannot modify immutable variable or field");
                checkBorrowAccess(*unary->operand, scope, true);
                if (scope.method && isReceiver(*unary->operand, scope)) mutates[scope.method] = true;
                if (!type.empty() && type != "int32") throw std::runtime_error("Increment requires int32");
            }
            else if (unary->op == "!" && !type.empty() && type != "bool") throw std::runtime_error("Logical NOT requires bool");
            else if ((unary->op == "+" || unary->op == "-") && !type.empty() && type != "int32" && type != "float") throw std::runtime_error("Unary operator requires int32");
            else if (unary->op == "~" && !type.empty() && type != "int32") throw std::runtime_error("Unary operator requires int32");
            return type;
        }
        if (auto* binary = dynamic_cast<const BinaryExpr*>(&value)) {
            auto left = expression(*binary->left, scope);
            Scope rightScope = scope;
            if (scope.mayEvaluate && (binary->op == "&&" || binary->op == "!&&" || binary->op == "||" || binary->op == "!||")) {
                auto known = ConstantBoolean(*binary->left);
                bool isAnd = binary->op == "&&" || binary->op == "!&&";
                if (known && ((isAnd && !*known) || (!isAnd && *known))) rightScope.mayEvaluate = false;
            }
            auto right = expression(*binary->right, rightScope);
            if (binary->op == "=" || binary->op.ends_with("=")) {
                auto mutableTarget = mutableValue(*binary->left, scope);
                if (!mutableTarget) throw std::runtime_error("Assignment requires a variable");
                if (mutableTarget && !*mutableTarget) throw std::runtime_error("Cannot modify immutable variable or field");
                checkBorrowAccess(*binary->left, scope, true);
                if (scope.method && isReceiver(*binary->left, scope)) mutates[scope.method] = true;
                if (!left.empty() && !right.empty() && left != right) throw std::runtime_error("Assignment type mismatch");
                if (binary->op == "=" && !left.empty() && left.back() == '*') {
                    auto* target = dynamic_cast<const IdentifierExpr*>(binary->left.get());
                    const auto* reference = target ? Reference(*target) : nullptr;
                    if (reference && reference->kind == ResolvedReference::Kind::Variable) {
                        scope.pointerOrigins[reference->declaration] = pointerOrigin(*binary->right, scope);
                        if (pointerMayReferenceLocal(*binary->right, scope)) unsafePointerVariables.insert(reference->declaration);
                    }
                }
                auto aggregate = globals.find(left);
                if (aggregate != globals.end() && aggregate->second.kind == Kind::Type) {
                    if (binary->op != "=") throw std::runtime_error("Compound assignment requires a scalar value");
                    if (NeedsDestruction(left)) {
                        auto* target = dynamic_cast<const IdentifierExpr*>(binary->left.get());
                        auto* reference = target ? Reference(*target) : nullptr;
                        if (!scope.function || !reference || reference->kind != ResolvedReference::Kind::Variable || !directValue(binary->right.get()))
                            throw std::runtime_error("Copying a destructible value is not permitted");
                        reinitializations.insert(binary);
                        functionsWithMoves.insert(scope.function);
                    } else requireAssignableFields(left, scope.owner);
                }
            }
            if (left.empty() || right.empty()) return "";
            if (scope.mayEvaluate && left == "int32" && right == "int32" &&
                (binary->op == "/" || binary->op == "%" || binary->op == "/=" || binary->op == "%=")) {
                auto divisor = ConstantInteger(*binary->right);
                if (divisor && divisor->isZero()) throw std::runtime_error("Integer division or remainder by zero");
                if (divisor && divisor->isAllOnes() && (binary->op == "/" || binary->op == "%")) {
                    auto dividend = ConstantInteger(*binary->left);
                    if (dividend && dividend->isMinSignedValue()) throw std::runtime_error("Integer division or remainder overflow");
                }
            }
            if (scope.mayEvaluate && left == "int32" && right == "int32" &&
                (binary->op == "<<" || binary->op == ">>" || binary->op == "<<=" || binary->op == ">>=")) {
                auto count = ConstantInteger(*binary->right);
                if (count && count->uge(32)) throw std::runtime_error("Integer shift count out of range");
            }
            if (binary->op == "`" && right == "int32") {
                auto exponent = ConstantInteger(*binary->right);
                if (exponent && exponent->isNegative()) throw std::runtime_error("Integer exponent must be nonnegative");
            }
            if (left == "void" || right == "void") throw std::runtime_error("Void expression cannot be used as an operand");
            if (left != right) throw std::runtime_error("Operand types do not match for " + binary->op);
            if (binary->op == "&&" || binary->op == "||" || binary->op == "^^" || binary->op == "!&&" || binary->op == "!||") {
                if (left != "bool") throw std::runtime_error("Logical operator requires bool operands");
                return "bool";
            }
            if (binary->op != "=" && left != "int32" && !(left == "float" && (binary->op == "+" || binary->op == "-" || binary->op == "*" || binary->op == "/" || binary->op == "+=" || binary->op == "-=" || binary->op == "*=" || binary->op == "/=")))
                throw std::runtime_error("Operator requires int32 operands: " + binary->op);
            return left;
        }
        if (auto* comparison = dynamic_cast<const ComparisonExpr*>(&value)) {
            auto left = expression(*comparison->operands.front(), scope);
            Scope operandScope = scope;
            for (size_t i = 0; i < comparison->operators.size(); ++i) {
                auto right = expression(*comparison->operands[i + 1], operandScope);
                if (!left.empty() && !right.empty()) {
                    if (left == "void" || right == "void") throw std::runtime_error("Void expression cannot be used as an operand");
                    if (left != right) throw std::runtime_error("Operand types do not match for " + comparison->operators[i]);
                    if (left != "int32" && left != "float" && !(left == "bool" && (comparison->operators[i] == "==" || comparison->operators[i] == "!=")))
                        throw std::runtime_error("Unsupported operand type for " + comparison->operators[i]);
                }
                if (operandScope.mayEvaluate) {
                    auto known = ConstantComparison(*comparison->operands[i], comparison->operators[i], *comparison->operands[i + 1]);
                    if (known && !*known) operandScope.mayEvaluate = false;
                }
                left = right;
            }
            return "bool";
        }
        if (auto* block = dynamic_cast<const BlockExpr*>(&value)) {
            if (scope.function && scope.function->returnsReference)
                throw std::runtime_error("Reference return cannot use a value block yet");
            std::string result; bool sawReturn = false, unknown = false;
            Scope inner = scope; inner.valueScope = true; inner.valueType = &result; inner.valueSawReturn = &sawReturn; inner.valueUnknown = &unknown; inner.valueLoopDepth = scope.loopDepth;
            body(block->block->statements, inner);
            if (!terminates(block->block->statements)) throw std::runtime_error("Every path in a value scope must return a value");
            if (!sawReturn) throw std::runtime_error("Value scope requires return");
            return unknown ? "" : result;
        }
        if (auto* branch = dynamic_cast<const IfExpr*>(&value)) {
            if (scope.function && scope.function->returnsReference)
                throw std::runtime_error("Reference return cannot use an if value yet");
            auto condition = expression(*branch->condition, scope);
            if (!condition.empty() && condition != "bool") throw std::runtime_error("Condition must be bool");
            std::string result; bool sawReturn = false, unknown = false;
            Scope inner = scope; inner.valueScope = true; inner.valueType = &result; inner.valueSawReturn = &sawReturn; inner.valueUnknown = &unknown; inner.valueLoopDepth = scope.loopDepth;
            Scope otherwise = inner;
            if (scope.mayEvaluate) if (auto known = ConstantBoolean(*branch->condition)) {
                if (*known) otherwise.mayEvaluate = false;
                else inner.mayEvaluate = false;
            }
            body(branch->thenBranch->statements, inner);
            body(branch->elseBranch->statements, otherwise);
            if (!terminates(branch->thenBranch->statements) || !terminates(branch->elseBranch->statements)) throw std::runtime_error("Every if expression branch must return a value");
            if (!sawReturn) throw std::runtime_error("Value scope requires return");
            return unknown ? "" : result;
        }
        return "";
    };
    mutableValue = [&](const Expression& value, Scope& scope) -> std::optional<bool> {
        if (dynamic_cast<const ConstructExpr*>(&value) || dynamic_cast<const BlockExpr*>(&value) || dynamic_cast<const IfExpr*>(&value)) return false;
        if (auto* call = dynamic_cast<const CallExpr*>(&value)) {
            auto target = referenceTargets.find(call);
            return target != referenceTargets.end()
                ? std::optional<bool>(target->second->returnsMutableReference) : std::optional<bool>(false);
        }
        if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
            auto found = scope.mutableVariables.find(identifier->name);
            if (found == scope.mutableVariables.end() && bareCalls.contains(identifier))
                return bareCalls.at(identifier)->returnsReference ? std::optional<bool>(bareCalls.at(identifier)->returnsMutableReference) : std::optional<bool>(false);
            return found == scope.mutableVariables.end() ? std::nullopt : std::optional<bool>(found->second);
        }
        if (auto* member = dynamic_cast<const MemberExpr*>(&value)) {
            if (bareMemberCalls.contains(member))
                return bareMemberCalls.at(member)->returnsReference ? std::optional<bool>(bareMemberCalls.at(member)->returnsMutableReference) : std::optional<bool>(false);
            auto objectType = expression(*member->object, scope);
            auto type = members.find(objectType);
            if (type == members.end()) return std::nullopt;
            auto field = type->second.find(member->name);
            if (field == type->second.end() || field->second.kind != Kind::Field) return std::nullopt;
            auto object = mutableValue(*member->object, scope);
            if (!object) return std::nullopt;
            return *object && static_cast<const VariableDeclaration*>(field->second.declaration)->isMutable;
        }
        return std::nullopt;
    };
    isReceiver = [&](const Expression& value, const Scope& scope) -> bool {
        if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
            auto found = scope.receiverVariables.find(identifier->name);
            return found != scope.receiverVariables.end() && found->second;
        }
        if (auto* member = dynamic_cast<const MemberExpr*>(&value)) return isReceiver(*member->object, scope);
        return false;
    };
    borrowRoot = [&](const Expression& value, Scope& scope) -> const void* {
        if (auto origin = returnOrigins.find(&value); origin != returnOrigins.end()) return origin->second.first;
        if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
            const auto* reference = Reference(*identifier);
            if (!reference || reference->kind != ResolvedReference::Kind::Variable) return nullptr;
            auto origin = scope.borrowOrigins.find(reference->declaration);
            return origin == scope.borrowOrigins.end() ? reference->declaration : origin->second;
        }
        if (auto* member = dynamic_cast<const MemberExpr*>(&value)) return borrowRoot(*member->object, scope);
        return nullptr;
    };
    borrowBinding = [&](const Expression& value, Scope& scope) -> const void* {
        if (auto origin = returnOrigins.find(&value); origin != returnOrigins.end()) return origin->second.second;
        if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
            const auto* reference = Reference(*identifier);
            return reference && reference->kind == ResolvedReference::Kind::Variable ? reference->declaration : nullptr;
        }
        if (auto* member = dynamic_cast<const MemberExpr*>(&value)) return borrowBinding(*member->object, scope);
        return nullptr;
    };
    pointerOrigin = [&](const Expression& value, Scope& scope) -> const void* {
        if (auto* unary = dynamic_cast<const UnaryExpr*>(&value); unary && unary->op == "&")
            return borrowRoot(*unary->operand, scope);
        if (auto* move = dynamic_cast<const MoveExpr*>(&value)) return pointerOrigin(*move->operand, scope);
        if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
            const auto* reference = Reference(*identifier);
            if (!reference || reference->kind != ResolvedReference::Kind::Variable) return nullptr;
            auto found = scope.pointerOrigins.find(reference->declaration);
            return found == scope.pointerOrigins.end() ? nullptr : found->second;
        }
        if (returnOrigins.contains(&value)) return borrowRoot(value, scope);
        return nullptr;
    };
    pointerMayReferenceLocal = [&](const Expression& value, Scope& scope) {
        if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
            const auto* reference = Reference(*identifier);
            return reference && unsafePointerVariables.contains(reference->declaration);
        }
        if (auto* move = dynamic_cast<const MoveExpr*>(&value)) return pointerMayReferenceLocal(*move->operand, scope);
        if (auto* unary = dynamic_cast<const UnaryExpr*>(&value); unary && unary->op == "&") {
            const void* origin = borrowRoot(*unary->operand, scope);
            if (!origin) return true;
            for (const auto& [name, symbol] : globals)
                if (symbol.kind == Kind::GlobalVariable && symbol.declaration == origin) return false;
            if (scope.function) for (const auto& parameter : scope.function->parameters)
                if (&parameter == origin) return !parameter.isReference;
            for (const auto& [name, id] : scope.bindingIds)
                if (!scope.globalBindings.contains(name) && bindingOrigins.at(id) == origin) return true;
            return true;
        }
        if (dynamic_cast<const IfExpr*>(&value) || dynamic_cast<const BlockExpr*>(&value) ||
            dynamic_cast<const CallExpr*>(&value) || dynamic_cast<const MemberExpr*>(&value) ||
            dynamic_cast<const BinaryExpr*>(&value)) return true;
        return false;
    };
    recordReturnOrigin = [&](const FunctionDeclaration& declaration,
        const std::vector<std::unique_ptr<Expression>>& arguments, Scope& scope, const Expression& call) {
        if (!declaration.returnsReference) return;
        referenceTargets[&call] = &declaration;
        for (size_t i = 0; i < declaration.parameters.size(); ++i) {
            const auto& parameter = declaration.parameters[i];
            if (!parameter.isReference || parameter.lifetime != declaration.returnLifetime) continue;
            const void* root = borrowRoot(*arguments[i], scope);
            const void* view = borrowBinding(*arguments[i], scope);
            if (root && view) returnOrigins[&call] = {root, view};
            return;
        }
        throw std::runtime_error("Reference return origin could not be resolved");
    };
    checkBorrowAccess = [&](const Expression& value, Scope& scope, bool write) {
        const void* root = borrowRoot(value, scope);
        const void* view = borrowBinding(value, scope);
        if (!root || !view) return;
        auto loans = scope.activeBorrows.find(root);
        if (loans == scope.activeBorrows.end()) return;
        auto isAncestor = [&](const void* ancestor, const void* child) {
            std::unordered_set<const void*> seen;
            while (child && seen.insert(child).second) {
                if (child == ancestor) return true;
                auto parent = scope.borrowParents.find(child);
                child = parent == scope.borrowParents.end() || parent->second == child ? nullptr : parent->second;
            }
            return false;
        };
        for (const auto& [borrower, isMutable] : loans->second) {
            if (isAncestor(borrower, view)) continue;
            if (isMutable || write)
                throw std::runtime_error(isMutable
                    ? "Cannot access an object while it is mutably borrowed"
                    : "Cannot modify an object while it is immutably borrowed");
        }
    };
    checkReferenceArguments = [&](const FunctionDeclaration& declaration,
        const std::vector<std::unique_ptr<Expression>>& arguments, Scope& scope, const std::string& name) {
        std::vector<std::pair<const void*, bool>> active;
        for (size_t i = 0; i < declaration.parameters.size(); ++i) {
            const auto& parameter = declaration.parameters[i];
            if (!parameter.isReference) {
                if (parameter.isConsumable && !consumableValue(arguments[i].get()))
                    throw std::runtime_error("Consumable argument requires direct construction or explicit move: " + name);
                if (NeedsDestruction(parameter.type) && !directValue(arguments[i].get()))
                    throw std::runtime_error("Destructible value argument requires direct construction: " + name);
                continue;
            }
            auto mutableArgument = mutableValue(*arguments[i], scope);
            if (!mutableArgument) throw std::runtime_error("Reference argument requires an addressable object: " + name);
            if (parameter.isMutableReference && !*mutableArgument)
                throw std::runtime_error("Mutable reference requires a mutable object: " + name);
            checkBorrowAccess(*arguments[i], scope, parameter.isMutableReference);
            const void* root = borrowRoot(*arguments[i], scope);
            const void* view = borrowBinding(*arguments[i], scope);
            if (!root || !view) throw std::runtime_error("Reference argument requires an addressable object: " + name);
            for (const auto& [previousRoot, previousMutable] : active)
                if (root && root == previousRoot && (parameter.isMutableReference || previousMutable))
                    throw std::runtime_error("Conflicting borrows of the same object in call: " + name);
            active.emplace_back(root, parameter.isMutableReference);
        }
    };
    body = [&](const std::vector<std::unique_ptr<ASTNode>>& nodes, Scope scope) {
        std::unordered_set<std::string> declared;
        bool directCapture = scope.allowCapture;
        scope.allowCapture = false;
        bool terminated = false;
        for (const auto& node : nodes) {
            diagnosticOffset = node->offset;
            if (terminated && !dynamic_cast<const FunctionDeclaration*>(node.get()) && !dynamic_cast<const CaptureDeclaration*>(node.get()))
                throw std::runtime_error("Unreachable statement after return");
            if (auto* variable = dynamic_cast<const VariableDeclaration*>(node.get())) {
                if (!declared.insert(variable->name).second) throw std::runtime_error("Duplicate variable: " + variable->name);
                if (variable->type == "void") throw std::runtime_error("Variable cannot have void type");
                if (variable->isConsumable && variable->isReference) throw std::runtime_error("A variable cannot be both borrowed and consumable");
                if (globals.contains(variable->type) && globals.at(variable->type).kind == Kind::Type)
                    requireDestructionAccess(variable->type, scope.owner);
                if (variable->aggregateInit && (!globals.contains(variable->type) || globals.at(variable->type).kind != Kind::Type))
                    throw std::runtime_error("Brace initialization requires a user-defined type");
                if (variable->aggregateInit) {
                    if (auto* constructor = SelectDefaultConstructor(variable->type, scope.owner)) defaultConstructors[variable] = constructor;
                }
                if (variable->initialValue) {
                    Scope initializerScope = scope;
                    auto initial = expression(*variable->initialValue, initializerScope);
                    if (variable->isConsumable && !consumableValue(variable->initialValue.get()))
                        throw std::runtime_error("Consumable initializer requires direct construction or explicit move: " + variable->name);
                    if (NeedsDestruction(variable->type) && !variable->isReference && !directValue(variable->initialValue.get())) throw std::runtime_error("Copying a destructible value is not permitted: " + variable->name);
                    if (!initial.empty() && initial != canonical(variable->type)) throw std::runtime_error("Initializer type mismatch: " + variable->name);
                }
                if (variable->isReference) {
                    if (!variable->initialValue) throw std::runtime_error("Reference variable requires an initializer: " + variable->name);
                    auto mutableSource = mutableValue(*variable->initialValue, scope);
                    if (!mutableSource) throw std::runtime_error("Borrow requires an addressable object: " + variable->name);
                    if (variable->isMutableReference && !*mutableSource)
                        throw std::runtime_error("Mutable borrow requires a mutable object: " + variable->name);
                    const void* root = borrowRoot(*variable->initialValue, scope);
                    const void* view = borrowBinding(*variable->initialValue, scope);
                    if (!root || !view) throw std::runtime_error("Borrow requires an addressable object: " + variable->name);
                    checkBorrowAccess(*variable->initialValue, scope, variable->isMutableReference);
                    scope.activeBorrows[root].emplace_back(variable, variable->isMutableReference);
                    scope.borrowOrigins[variable] = root;
                    scope.borrowParents[variable] = view;
                }
                if (!variable->type.empty() && variable->type.back() == '*' && variable->initialValue)
                    scope.pointerOrigins[variable] = pointerOrigin(*variable->initialValue, scope);
                if (!variable->type.empty() && variable->type.back() == '*' && variable->initialValue &&
                    pointerMayReferenceLocal(*variable->initialValue, scope))
                    unsafePointerVariables.insert(variable);
                scope.variables[variable->name] = canonical(variable->type);
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
            } else if (auto* local = dynamic_cast<const FunctionDeclaration*>(node.get())) {
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
                if (auto type = members.find(localScope.owner); type != members.end()) {
                    for (const auto& [name, member] : type->second)
                        if (member.kind == Kind::Method && !localScope.shadowedMethods.contains(name)) localScope.localFunctions.erase(name);
                }
                localScope.method = nullptr;
                localScope.owner.clear();
                function(*local, std::move(localScope), true);
            } else if (dynamic_cast<const CaptureDeclaration*>(node.get())) {
                if (!directCapture) throw std::runtime_error("capture must be declared directly in a local function");
            } else if (auto* format = dynamic_cast<const FormatStringStatement*>(node.get())) {
                for (const auto& part : format->parts) {
                    auto* identifier = dynamic_cast<const IdentifierExpr*>(part.get());
                    if (!identifier) continue;
                    auto found = scope.variables.find(identifier->name);
                    if (found == scope.variables.end()) throw std::runtime_error("Undefined variable: " + identifier->name);
                    if (found->second != "int32" && found->second != "bool" && found->second != "float")
                        throw std::runtime_error("Unsupported format string type: " + identifier->name);
                    (void)expression(*identifier, scope);
                }
            } else if (auto* expr = dynamic_cast<const Expression*>(node.get())) expression(*expr, scope);
            else if (auto* result = dynamic_cast<const ReturnStatement*>(node.get())) {
                if (result->value) {
                    if (!scope.valueScope && NeedsDestruction(scope.returnType) && !(scope.function && scope.function->returnsReference) &&
                        !dynamic_cast<const ConstructExpr*>(result->value.get()) && !dynamic_cast<const MoveExpr*>(result->value.get()))
                        throw std::runtime_error("Destructible return requires direct construction");
                    auto resultType = expression(*result->value, scope);
                    returnValueTypes[result] = resultType;
                    if (scope.valueScope) {
                        if (NeedsDestruction(resultType) && !directValue(result->value.get()))
                            throw std::runtime_error("Destructible value scope requires direct construction");
                        *scope.valueSawReturn = true;
                        if (resultType.empty()) *scope.valueUnknown = true;
                        else if (resultType == "void") throw std::runtime_error("Value scope cannot return void");
                        else if (!scope.valueType->empty() && *scope.valueType != resultType) throw std::runtime_error("Value scope return types do not match");
                        else *scope.valueType = resultType;
                    } else {
                        if (scope.function && scope.function->returnsConsumable && !consumableValue(result->value.get()))
                            throw std::runtime_error("Consumable return requires direct construction or explicit move");
                        if (scope.function && scope.function->returnsReference) {
                            if (!resultType.empty() && resultType != canonical(scope.returnType))
                                throw std::runtime_error("Invalid reference return type");
                            const void* origin = borrowRoot(*result->value, scope);
                            bool validOrigin = false;
                            for (const auto& parameter : scope.function->parameters)
                                if (parameter.isReference && parameter.lifetime == scope.function->returnLifetime && origin == &parameter)
                                    validOrigin = true;
                            if (!validOrigin) throw std::runtime_error("Returning a reference to an expired or unrelated object");
                            if (scope.function->returnsMutableReference) {
                                auto mutability = mutableValue(*result->value, scope);
                                if (!mutability || !*mutability) throw std::runtime_error("Mutable reference return requires a mutable source");
                            }
                            referenceReturns.insert(result);
                        } else if (scope.function && !scope.returnType.empty() && scope.returnType.back() == '*') {
                            if (!resultType.empty() && resultType != scope.returnType)
                                throw std::runtime_error("Invalid return value type");
                            if (pointerMayReferenceLocal(*result->value, scope))
                                throw std::runtime_error("Returning a pointer to an expired local object");
                            const void* origin = pointerOrigin(*result->value, scope);
                            if (origin) {
                                bool isGlobal = false, isReferenceParameter = false, isLocal = false;
                                for (const auto& [name, symbol] : globals)
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
            else if (auto* block = dynamic_cast<const Block*>(node.get())) body(block->statements, scope);
            else if (auto* branch = dynamic_cast<const IfStatement*>(node.get())) {
                auto condition = expression(*branch->condition, scope);
                if (!condition.empty() && condition != "bool") throw std::runtime_error("Condition must be bool");
                Scope thenScope = scope, elseScope = scope;
                if (scope.mayEvaluate) if (auto known = ConstantBoolean(*branch->condition)) {
                    if (*known) elseScope.mayEvaluate = false;
                    else thenScope.mayEvaluate = false;
                }
                body(branch->thenBranch->statements, thenScope);
                if (branch->elseBranch) body(branch->elseBranch->statements, elseScope);
            } else if (auto* loop = dynamic_cast<const LoopStatement*>(node.get())) {
                Scope inner = scope;
                if (auto* loopVariable = dynamic_cast<const VariableDeclaration*>(loop->initializer.get())) {
                    if (loopVariable->type == "void") throw std::runtime_error("Variable cannot have void type");
                    if (globals.contains(loopVariable->type) && globals.at(loopVariable->type).kind == Kind::Type)
                        requireDestructionAccess(loopVariable->type, inner.owner);
                    if (loopVariable->aggregateInit && (!globals.contains(loopVariable->type) || globals.at(loopVariable->type).kind != Kind::Type))
                        throw std::runtime_error("Brace initialization requires a user-defined type");
                    if (loopVariable->aggregateInit) {
                        if (auto* constructor = SelectDefaultConstructor(loopVariable->type, inner.owner)) defaultConstructors[loopVariable] = constructor;
                    }
                    if (loopVariable->initialValue) {
                        Scope initializerScope = inner;
                        auto initial = expression(*loopVariable->initialValue, initializerScope);
                        if (NeedsDestruction(loopVariable->type) && !loopVariable->isReference && !directValue(loopVariable->initialValue.get())) throw std::runtime_error("Copying a destructible value is not permitted: " + loopVariable->name);
                        if (!initial.empty() && initial != canonical(loopVariable->type)) throw std::runtime_error("Initializer type mismatch: " + loopVariable->name);
                    }
                    if (loopVariable->isReference) {
                        if (!loopVariable->initialValue) throw std::runtime_error("Reference variable requires an initializer: " + loopVariable->name);
                        auto mutableSource = mutableValue(*loopVariable->initialValue, inner);
                        if (!mutableSource) throw std::runtime_error("Borrow requires an addressable object: " + loopVariable->name);
                        if (loopVariable->isMutableReference && !*mutableSource)
                            throw std::runtime_error("Mutable borrow requires a mutable object: " + loopVariable->name);
                        const void* root = borrowRoot(*loopVariable->initialValue, inner);
                        const void* view = borrowBinding(*loopVariable->initialValue, inner);
                        if (!root || !view) throw std::runtime_error("Borrow requires an addressable object: " + loopVariable->name);
                        checkBorrowAccess(*loopVariable->initialValue, inner, loopVariable->isMutableReference);
                        inner.activeBorrows[root].emplace_back(loopVariable, loopVariable->isMutableReference);
                        inner.borrowOrigins[loopVariable] = root;
                        inner.borrowParents[loopVariable] = view;
                    }
                    inner.variables[loopVariable->name] = canonical(loopVariable->type);
                    inner.mutableVariables[loopVariable->name] = loopVariable->isReference ? loopVariable->isMutableReference : loopVariable->isMutable;
                    inner.receiverVariables[loopVariable->name] = false;
                    size_t id = nextBindingId++;
                    bindingOrigins[id] = loopVariable;
                    inner.bindingIds[loopVariable->name] = id;
                    inner.availableBindings.insert(id);
                    inner.globalBindings.erase(loopVariable->name);
                    inner.inaccessible.erase(loopVariable->name);
                } else if (auto* init = dynamic_cast<const Expression*>(loop->initializer.get())) expression(*init, inner);
                bool skipLoop = false;
                if (loop->condition) {
                    auto condition = expression(*loop->condition, inner);
                    if (!condition.empty() && condition != "bool") throw std::runtime_error("Condition must be bool");
                    if (inner.mayEvaluate && loop->kind != LoopStatement::Kind::DoWhile) {
                        auto known = ConstantBoolean(*loop->condition);
                        skipLoop = known && !*known;
                    }
                }
                if (skipLoop) inner.mayEvaluate = false;
                if (loop->step) expression(*loop->step, inner);
                ++inner.loopDepth;
                body(loop->body->statements, inner);
            } else if (dynamic_cast<const LoopControl*>(node.get())) {
                if (!scope.loopDepth) throw std::runtime_error("break or continue outside loop");
                if (scope.valueScope && scope.loopDepth <= scope.valueLoopDepth) throw std::runtime_error("Cannot leave a value scope with break or continue");
            }
            if (dynamic_cast<const ReturnStatement*>(node.get()) || dynamic_cast<const LoopControl*>(node.get())) terminated = true;
            else if (auto* block = dynamic_cast<const Block*>(node.get()); block && terminates(block->statements)) terminated = true;
            else if (auto* branch = dynamic_cast<const IfStatement*>(node.get()); branch && branch->elseBranch &&
                terminates(branch->thenBranch->statements) && terminates(branch->elseBranch->statements)) terminated = true;
        }
    };
    Scope globalScope;
    const bool explicitEntry = !entryPoint.empty();
    if (explicitEntry) for (const auto& [name, symbol] : globals)
        if (symbol.kind == Kind::GlobalVariable) {
            globalScope.variables[name] = canonical(symbol.type);
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
        diagnosticFile = variable->sourceFile;
        auto initialType = expression(*variable->initialValue, globalScope);
        if (!initialType.empty() && initialType != canonical(variable->type)) throw std::runtime_error("Initializer type mismatch: " + variable->name);
    }
    for (const auto& node : program.children) {
        if (auto* declaration = dynamic_cast<const FunctionDeclaration*>(node.get())) function(*declaration, globalScope, false);
        else if (auto* type = dynamic_cast<const TypeDeclaration*>(node.get())) {
            diagnosticFile = type->sourceFile;
            Scope initializerScope;
            initializerScope.owner = type->name;
            for (const auto& member : type->members) {
                auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
                if (!field) continue;
                if (field->isConsumable) throw std::runtime_error("Consumable fields are not supported: " + field->name);
                if (!field->initialValue) continue;
                Scope fieldScope = initializerScope;
                fieldScope.directConstruction = dynamic_cast<const ConstructExpr*>(field->initialValue.get()) != nullptr;
                auto initial = expression(*field->initialValue, fieldScope);
                if (NeedsDestruction(field->type) && !directValue(field->initialValue.get()))
                    throw std::runtime_error("Copying a destructible value is not permitted: " + field->name);
                if (!initial.empty() && initial != canonical(field->type)) throw std::runtime_error("Initializer type mismatch: " + field->name);
            }
            Scope scope = globalScope;
            scope.owner = type->name;
            for (const auto& member : type->members) {
                if (auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get())) {
                    scope.variables[field->name] = canonical(field->type);
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
                    function(*method, std::move(methodScope), false);
                }
        }
    }
    if (!explicitEntry) body(program.children, globalScope);
    bool changed;
    do {
        changed = false;
        for (const auto& [caller, callees] : calls) {
            if (mutates[caller]) continue;
            for (const auto* callee : callees) if (mutates[callee]) { mutates[caller] = true; changed = true; break; }
        }
    } while (changed);
    for (const auto& call : immutableCalls) if (mutates[call.method]) {
        diagnosticOffset = call.offset;
        diagnosticFile = call.file;
        throw std::runtime_error("Cannot call mutating method on immutable instance: " + call.method->name);
    }
}
