#include "SemanticBodyChecker.h"

#include "ConstantExpression.h"

#include <algorithm>
#include <stdexcept>
#include <utility>


std::string SemanticBodyChecker::CheckExpression(const Expression& value, Scope& scope) {
    semantic.diagnosticOffset = value.offset;
    if (auto* number = dynamic_cast<const NumericLiteral*>(&value)) {
        ConstantExpression::ValidateNumber(*number);
        return number->value.find('.') != std::string::npos ? "float" : "int32";
    }
    if (dynamic_cast<const BooleanLiteral*>(&value)) return "bool";
    if (auto* move = dynamic_cast<const MoveExpr*>(&value)) return CheckMove(move, scope);
    if (auto* construct = dynamic_cast<const ConstructExpr*>(&value)) return CheckConstruction(construct, scope);
    if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) return CheckIdentifier(identifier, scope);
    if (auto* call = dynamic_cast<const CallExpr*>(&value)) return CheckCall(call, scope);
    if (auto* member = dynamic_cast<const MemberExpr*>(&value)) return CheckMember(member, scope);
    if (auto* unary = dynamic_cast<const UnaryExpr*>(&value)) return CheckUnary(unary, scope);
    if (auto* binary = dynamic_cast<const BinaryExpr*>(&value)) return CheckBinary(binary, scope);
    if (auto* comparison = dynamic_cast<const ComparisonExpr*>(&value)) return CheckComparison(comparison, scope);
    if (auto* block = dynamic_cast<const BlockExpr*>(&value)) return CheckBlockValue(block, scope);
    if (auto* branch = dynamic_cast<const IfExpr*>(&value)) return CheckIfValue(branch, scope);
    return "";
}

std::string SemanticBodyChecker::CheckMove(const MoveExpr* move, Scope& scope) {
    if (scope.function) semantic.functionsWithMoves.insert(scope.function);
    auto type = CheckExpression(*move->operand, scope);
    semantic.diagnosticOffset = move->offset;
    auto* identifier = dynamic_cast<const IdentifierExpr*>(move->operand.get());
    auto* reference = identifier ? semantic.Reference(*identifier) : nullptr;
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

std::string SemanticBodyChecker::CheckConstruction(const ConstructExpr* construct, Scope& scope) {
    auto type = semantic.globals.find(construct->type);
    if (type == semantic.globals.end() || type->second.kind != Kind::Type)
        throw std::runtime_error("Constructor requires a user-defined type: " + construct->type);
    std::vector<std::string> argumentTypes;
    Scope argumentScope = scope;
    argumentScope.directConstruction = false;
    for (const auto& argument : construct->arguments) {
        auto argumentType = CheckExpression(*argument, argumentScope);
        argumentTypes.push_back(argumentType);
    }
    auto lifecycle = semantic.specialMembers.find(construct->type);
    const Symbol* match = nullptr;
    if (lifecycle != semantic.specialMembers.end()) for (const auto& candidate : lifecycle->second.constructors) {
        auto* declaration = static_cast<const ConstructorDeclaration*>(candidate.declaration);
        if (declaration->parameters.size() != argumentTypes.size()) continue;
        bool same = true;
        for (size_t i = 0; i < argumentTypes.size(); ++i)
            if (argumentTypes[i] != Semantic::CanonicalType(declaration->parameters[i].type)) { same = false; break; }
        if (!same) continue;
        if (match) throw std::runtime_error("Ambiguous constructor: " + construct->type);
        match = &candidate;
    }
    if (!match) throw std::runtime_error("No matching constructor: " + construct->type);
    if (!match->isPublic && scope.owner != construct->type) throw std::runtime_error("Private constructor: " + construct->type);
    CheckReferenceArguments(*static_cast<const FunctionDeclaration*>(match->declaration), construct->arguments, scope, construct->type);
    RequireDestructionAccess(construct->type, scope.owner);
    semantic.constructorCalls[construct] = static_cast<const ConstructorDeclaration*>(match->declaration);
    return construct->type;
}

std::string SemanticBodyChecker::CheckIdentifier(const IdentifierExpr* identifier, Scope& scope) {
    auto found = scope.variables.find(identifier->name);
    if (found != scope.variables.end()) {
        borrows.CheckAccess(*identifier, scope, false);
        if (scope.receiverVariables.contains(identifier->name) && scope.receiverVariables.at(identifier->name)) {
            auto type = semantic.members.find(scope.owner);
            if (type != semantic.members.end()) {
                auto field = type->second.find(identifier->name);
                if (field != type->second.end() && field->second.kind == Kind::Field)
                    semantic.references[identifier] = {ResolvedReference::Kind::Field, field->second.declaration};
            }
        } else if (auto binding = scope.bindingIds.find(identifier->name); binding != scope.bindingIds.end()) {
            semantic.references[identifier] = {ResolvedReference::Kind::Variable, bindingOrigins.at(binding->second)};
        }
        return found->second;
    }
    if (scope.inaccessible.contains(identifier->name)) throw std::runtime_error("Variable requires capture: " + identifier->name);
    if (scope.method && !semantic.globals.contains(identifier->name) && !scope.shadowedMethods.contains(identifier->name)) {
        auto type = semantic.members.find(scope.owner);
        if (type != semantic.members.end()) {
            auto entry = type->second.find(identifier->name);
            if (entry != type->second.end() && entry->second.kind == Kind::Method) {
                auto* method = static_cast<const FunctionDeclaration*>(entry->second.declaration);
                if (!method->parameters.empty()) throw std::runtime_error("Wrong number of arguments for method: " + identifier->name);
                calls[scope.method].push_back(method);
                semantic.bareCalls[identifier] = method;
                return CheckCallResult(*method, scope);
            }
        }
    }
    if (scope.localFunctions.contains(identifier->name)) {
        auto local = scope.localSignatures.find(identifier->name);
        if (local != scope.localSignatures.end()) {
            if (!local->second->parameters.empty()) throw std::runtime_error("Wrong number of arguments for function: " + identifier->name);
            for (const auto& [name, id] : capturedBindings[local->second])
                if (!scope.availableBindings.contains(id)) throw std::runtime_error("Capture is not available in this function: " + name);
            semantic.bareCalls[identifier] = local->second;
            return CheckCallResult(*local->second, scope);
        }
        if (!semantic.globals.contains(identifier->name)) return "";
    }
    auto symbol = semantic.globals.find(identifier->name);
    if (symbol != semantic.globals.end() && symbol->second.kind == Kind::Function) {
        auto* function = static_cast<const FunctionDeclaration*>(symbol->second.declaration);
        if (!function->parameters.empty()) throw std::runtime_error("Wrong number of arguments for function: " + identifier->name);
        semantic.bareCalls[identifier] = function;
        return CheckCallResult(*function, scope);
    }
    throw std::runtime_error("Undefined variable: " + identifier->name);
}

std::string SemanticBodyChecker::CheckCall(const CallExpr* call, Scope& scope) {
    std::string objectType;
    if (call->object) objectType = CheckExpression(*call->object, scope);
    std::vector<std::string> argumentTypes;
    for (const auto& argument : call->arguments) {
        auto type = CheckExpression(*argument, scope);
        argumentTypes.push_back(type);
    }
    if (call->object) {
        if (objectType.empty()) return "";
        auto type = semantic.members.find(objectType);
        if (type == semantic.members.end()) throw std::runtime_error("Method call requires a user-defined type");
        auto method = type->second.find(call->name);
        if (method == type->second.end() || method->second.kind != Kind::Method) throw std::runtime_error("Unknown method: " + call->name);
        if (!method->second.isPublic && scope.owner != objectType) throw std::runtime_error("Private member: " + call->name);
        const auto* declaration = static_cast<const FunctionDeclaration*>(method->second.declaration);
        if (argumentTypes.size() != declaration->parameters.size()) throw std::runtime_error("Wrong number of arguments for method: " + call->name);
        for (size_t i = 0; i < argumentTypes.size(); ++i)
            if (!argumentTypes[i].empty() && argumentTypes[i] != Semantic::CanonicalType(declaration->parameters[i].type))
                throw std::runtime_error("Argument type mismatch for method: " + call->name);
        CheckReferenceArguments(*declaration, call->arguments, scope, call->name);
        auto objectMutable = MutableValue(*call->object, scope);
        if (!objectMutable) throw std::runtime_error("Method call requires an instance variable");
        if (objectMutable && !*objectMutable) immutableCalls.push_back({declaration, call->offset, semantic.diagnosticFile});
        else if (objectMutable && scope.method && IsReceiver(*call->object, scope)) calls[scope.method].push_back(declaration);
        if (const void* root = borrows.Find(*call->object, scope).root) {
            auto loans = scope.activeBorrows.find(root);
            if (loans != scope.activeBorrows.end() && std::any_of(loans->second.begin(), loans->second.end(), [](const auto& loan) { return !loan.second; }))
                immutableCalls.push_back({declaration, call->offset, semantic.diagnosticFile});
        }
        RecordReturnOrigin(*declaration, call->arguments, scope, *call);
        return CheckCallResult(*declaration, scope);
    }
    if (scope.variables.contains(call->name)) throw std::runtime_error("Variable is not callable: " + call->name);
    if (scope.inaccessible.contains(call->name) && !scope.localFunctions.contains(call->name)) throw std::runtime_error("Variable requires capture: " + call->name);
    if (scope.localFunctions.contains(call->name)) {
        auto local = scope.localSignatures.find(call->name);
        if (local != scope.localSignatures.end() && (scope.shadowedMethods.contains(call->name) || scope.owner.empty())) {
            auto* declaration = local->second;
            if (argumentTypes.size() != declaration->parameters.size()) throw std::runtime_error("Wrong number of arguments for function: " + call->name);
            for (size_t i = 0; i < argumentTypes.size(); ++i)
                if (!argumentTypes[i].empty() && argumentTypes[i] != Semantic::CanonicalType(declaration->parameters[i].type))
                    throw std::runtime_error("Argument type mismatch for function: " + call->name);
            CheckReferenceArguments(*declaration, call->arguments, scope, call->name);
            for (const auto& [name, id] : capturedBindings[declaration])
                if (!scope.availableBindings.contains(id)) throw std::runtime_error("Capture is not available in this function: " + name);
            semantic.resolvedCalls[call] = declaration;
            RecordReturnOrigin(*declaration, call->arguments, scope, *call);
            return CheckCallResult(*declaration, scope);
        }
        if (scope.method && !semantic.globals.contains(call->name) && !scope.shadowedMethods.contains(call->name)) {
            auto type = semantic.members.find(scope.owner);
            if (type != semantic.members.end()) {
                auto method = type->second.find(call->name);
                if (method != type->second.end() && method->second.kind == Kind::Method) {
                    auto* declaration = static_cast<const FunctionDeclaration*>(method->second.declaration);
                    if (argumentTypes.size() != declaration->parameters.size()) throw std::runtime_error("Wrong number of arguments for method: " + call->name);
                    for (size_t i = 0; i < argumentTypes.size(); ++i)
                        if (!argumentTypes[i].empty() && argumentTypes[i] != Semantic::CanonicalType(declaration->parameters[i].type))
                            throw std::runtime_error("Argument type mismatch for method: " + call->name);
                    CheckReferenceArguments(*declaration, call->arguments, scope, call->name);
                    calls[scope.method].push_back(declaration);
                    RecordReturnOrigin(*declaration, call->arguments, scope, *call);
                    return CheckCallResult(*declaration, scope);
                }
            }
        }
        if (!semantic.globals.contains(call->name)) return "";
    }
    auto symbol = semantic.globals.find(call->name);
    if (symbol == semantic.globals.end()) throw std::runtime_error("Undefined function: " + call->name);
    if (symbol->second.kind != Kind::Function) throw std::runtime_error("Undefined function: " + call->name);
    const auto& declaration = *static_cast<const FunctionDeclaration*>(symbol->second.declaration);
    if (argumentTypes.size() != declaration.parameters.size()) throw std::runtime_error("Wrong number of arguments for function: " + call->name);
    for (size_t i = 0; i < argumentTypes.size(); ++i)
        if (!argumentTypes[i].empty() && argumentTypes[i] != Semantic::CanonicalType(declaration.parameters[i].type))
            throw std::runtime_error("Argument type mismatch for function: " + call->name);
    CheckReferenceArguments(declaration, call->arguments, scope, call->name);
    semantic.resolvedCalls[call] = &declaration;
    RecordReturnOrigin(declaration, call->arguments, scope, *call);
    return CheckCallResult(declaration, scope);
}

std::string SemanticBodyChecker::CheckMember(const MemberExpr* member, Scope& scope) {
    auto objectType = CheckExpression(*member->object, scope);
    if (objectType.empty()) return "";
    auto type = semantic.members.find(objectType);
    if (type == semantic.members.end()) throw std::runtime_error("Member access requires a user-defined type");
    auto found = type->second.find(member->name);
    if (found == type->second.end()) throw std::runtime_error("Unknown field: " + member->name);
    if (!found->second.isPublic && scope.owner != objectType) throw std::runtime_error("Private member: " + member->name);
    auto objectMutable = MutableValue(*member->object, scope);
    if (!objectMutable) throw std::runtime_error("Member access requires an instance variable");
    if (found->second.kind == Kind::Method) {
        auto* method = static_cast<const FunctionDeclaration*>(found->second.declaration);
        semantic.bareMemberCalls[member] = method;
        if (!method->parameters.empty()) throw std::runtime_error("Wrong number of arguments for method: " + member->name);
        if (objectMutable && !*objectMutable) immutableCalls.push_back({method, member->offset, semantic.diagnosticFile});
        else if (objectMutable && scope.method && IsReceiver(*member->object, scope)) calls[scope.method].push_back(method);
        if (const void* root = borrows.Find(*member->object, scope).root) {
            auto loans = scope.activeBorrows.find(root);
            if (loans != scope.activeBorrows.end() && std::any_of(loans->second.begin(), loans->second.end(), [](const auto& loan) { return !loan.second; }))
                immutableCalls.push_back({method, member->offset, semantic.diagnosticFile});
        }
        return CheckCallResult(*method, scope);
    }
    return Semantic::CanonicalType(found->second.type);
}

std::string SemanticBodyChecker::CheckUnary(const UnaryExpr* unary, Scope& scope) {
    auto type = CheckExpression(*unary->operand, scope);
    if (unary->op == "&") {
        if (!borrows.Find(*unary->operand, scope).binding) throw std::runtime_error("Address-of requires an addressable object");
        return type.empty() ? std::string{} : Semantic::CanonicalType(type) + "*";
    }
    if (unary->op == "++" || unary->op == "--") {
        auto mutableTarget = MutableValue(*unary->operand, scope);
        if (!mutableTarget) throw std::runtime_error("Assignment requires a variable");
        if (mutableTarget && !*mutableTarget) throw std::runtime_error("Cannot modify immutable variable or field");
        borrows.CheckAccess(*unary->operand, scope, true);
        if (scope.method && IsReceiver(*unary->operand, scope)) mutates[scope.method] = true;
        if (!type.empty() && type != "int32") throw std::runtime_error("Increment requires int32");
    }
    else if (unary->op == "!" && !type.empty() && type != "bool") throw std::runtime_error("Logical NOT requires bool");
    else if ((unary->op == "+" || unary->op == "-") && !type.empty() && type != "int32" && type != "float") throw std::runtime_error("Unary operator requires int32");
    else if (unary->op == "~" && !type.empty() && type != "int32") throw std::runtime_error("Unary operator requires int32");
    return type;
}

std::string SemanticBodyChecker::CheckBinary(const BinaryExpr* binary, Scope& scope) {
    auto left = CheckExpression(*binary->left, scope);
    Scope rightScope = scope;
    if (scope.mayEvaluate && (binary->op == "&&" || binary->op == "!&&" || binary->op == "||" || binary->op == "!||")) {
        auto known = ConstantExpression::Boolean(*binary->left);
        bool isAnd = binary->op == "&&" || binary->op == "!&&";
        if (known && ((isAnd && !*known) || (!isAnd && *known))) rightScope.mayEvaluate = false;
    }
    auto right = CheckExpression(*binary->right, rightScope);
    if (binary->op == "=" || binary->op.ends_with("=")) {
        auto mutableTarget = MutableValue(*binary->left, scope);
        if (!mutableTarget) throw std::runtime_error("Assignment requires a variable");
        if (mutableTarget && !*mutableTarget) throw std::runtime_error("Cannot modify immutable variable or field");
        borrows.CheckAccess(*binary->left, scope, true);
        if (scope.method && IsReceiver(*binary->left, scope)) mutates[scope.method] = true;
        if (!left.empty() && !right.empty() && left != right) throw std::runtime_error("Assignment type mismatch");
        if (binary->op == "=" && !left.empty() && left.back() == '*') {
            auto* target = dynamic_cast<const IdentifierExpr*>(binary->left.get());
            const auto* reference = target ? semantic.Reference(*target) : nullptr;
            if (reference && reference->kind == ResolvedReference::Kind::Variable) {
                borrows.TrackPointer(reference->declaration, *binary->right, scope);
            }
        }
        auto aggregate = semantic.globals.find(left);
        if (aggregate != semantic.globals.end() && aggregate->second.kind == Kind::Type) {
            if (binary->op != "=") throw std::runtime_error("Compound assignment requires a scalar value");
            if (semantic.NeedsDestruction(left)) {
                auto* target = dynamic_cast<const IdentifierExpr*>(binary->left.get());
                auto* reference = target ? semantic.Reference(*target) : nullptr;
                if (!scope.function || !reference || reference->kind != ResolvedReference::Kind::Variable || !IsDirectValue(binary->right.get()))
                    throw std::runtime_error("Copying a destructible value is not permitted");
                semantic.reinitializations.insert(binary);
                semantic.functionsWithMoves.insert(scope.function);
            } else RequireAssignableFields(left, scope.owner);
        }
    }
    if (left.empty() || right.empty()) return "";
    if (scope.mayEvaluate && left == "int32" && right == "int32" &&
        (binary->op == "/" || binary->op == "%" || binary->op == "/=" || binary->op == "%=")) {
        auto divisor = ConstantExpression::Integer(*binary->right);
        if (divisor && divisor->isZero()) throw std::runtime_error("Integer division or remainder by zero");
        if (divisor && divisor->isAllOnes() && (binary->op == "/" || binary->op == "%")) {
            auto dividend = ConstantExpression::Integer(*binary->left);
            if (dividend && dividend->isMinSignedValue()) throw std::runtime_error("Integer division or remainder overflow");
        }
    }
    if (scope.mayEvaluate && left == "int32" && right == "int32" &&
        (binary->op == "<<" || binary->op == ">>" || binary->op == "<<=" || binary->op == ">>=")) {
        auto count = ConstantExpression::Integer(*binary->right);
        if (count && count->uge(32)) throw std::runtime_error("Integer shift count out of range");
    }
    if (binary->op == "`" && right == "int32") {
        auto exponent = ConstantExpression::Integer(*binary->right);
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

std::string SemanticBodyChecker::CheckComparison(const ComparisonExpr* comparison, Scope& scope) {
    auto left = CheckExpression(*comparison->operands.front(), scope);
    Scope operandScope = scope;
    for (size_t i = 0; i < comparison->operators.size(); ++i) {
        auto right = CheckExpression(*comparison->operands[i + 1], operandScope);
        if (!left.empty() && !right.empty()) {
            if (left == "void" || right == "void") throw std::runtime_error("Void expression cannot be used as an operand");
            if (left != right) throw std::runtime_error("Operand types do not match for " + comparison->operators[i]);
            if (left != "int32" && left != "float" && !(left == "bool" && (comparison->operators[i] == "==" || comparison->operators[i] == "!=")))
                throw std::runtime_error("Unsupported operand type for " + comparison->operators[i]);
        }
        if (operandScope.mayEvaluate) {
            auto known = ConstantExpression::Compare(*comparison->operands[i], comparison->operators[i], *comparison->operands[i + 1]);
            if (known && !*known) operandScope.mayEvaluate = false;
        }
        left = right;
    }
    return "bool";
}

std::string SemanticBodyChecker::CheckBlockValue(const BlockExpr* block, Scope& scope) {
    if (scope.function && scope.function->returnsReference)
        throw std::runtime_error("Reference return cannot use a value block yet");
    std::string result; bool sawReturn = false, unknown = false;
    Scope inner = scope; inner.valueScope = true; inner.valueType = &result; inner.valueSawReturn = &sawReturn; inner.valueUnknown = &unknown; inner.valueLoopDepth = scope.loopDepth;
    CheckStatements(block->block->statements, inner);
    if (!Terminates(block->block->statements)) throw std::runtime_error("Every path in a value scope must return a value");
    if (!sawReturn) throw std::runtime_error("Value scope requires return");
    return unknown ? "" : result;
}

std::string SemanticBodyChecker::CheckIfValue(const IfExpr* branch, Scope& scope) {
    if (scope.function && scope.function->returnsReference)
        throw std::runtime_error("Reference return cannot use an if value yet");
    auto condition = CheckExpression(*branch->condition, scope);
    if (!condition.empty() && condition != "bool") throw std::runtime_error("Condition must be bool");
    std::string result; bool sawReturn = false, unknown = false;
    Scope inner = scope; inner.valueScope = true; inner.valueType = &result; inner.valueSawReturn = &sawReturn; inner.valueUnknown = &unknown; inner.valueLoopDepth = scope.loopDepth;
    Scope otherwise = inner;
    if (scope.mayEvaluate) if (auto known = ConstantExpression::Boolean(*branch->condition)) {
        if (*known) otherwise.mayEvaluate = false;
        else inner.mayEvaluate = false;
    }
    CheckStatements(branch->thenBranch->statements, inner);
    CheckStatements(branch->elseBranch->statements, otherwise);
    if (!Terminates(branch->thenBranch->statements) || !Terminates(branch->elseBranch->statements)) throw std::runtime_error("Every if expression branch must return a value");
    if (!sawReturn) throw std::runtime_error("Value scope requires return");
    return unknown ? "" : result;
}

std::string SemanticBodyChecker::CheckCallResult(const FunctionDeclaration& declaration, const Scope& scope) {
        auto type = Semantic::CanonicalType(declaration.returnType);
        if (!declaration.returnsReference && semantic.NeedsDestruction(type)) RequireDestructionAccess(type, scope.owner);
        return type;
}

std::optional<bool> SemanticBodyChecker::MutableValue(const Expression& value, Scope& scope) {
    if (dynamic_cast<const ConstructExpr*>(&value) || dynamic_cast<const BlockExpr*>(&value) || dynamic_cast<const IfExpr*>(&value)) return false;
    if (auto* call = dynamic_cast<const CallExpr*>(&value)) {
        auto target = semantic.referenceTargets.find(call);
        return target != semantic.referenceTargets.end()
            ? std::optional<bool>(target->second->returnsMutableReference) : std::optional<bool>(false);
    }
    if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
        auto found = scope.mutableVariables.find(identifier->name);
        if (found == scope.mutableVariables.end() && semantic.bareCalls.contains(identifier))
            return semantic.bareCalls.at(identifier)->returnsReference ? std::optional<bool>(semantic.bareCalls.at(identifier)->returnsMutableReference) : std::optional<bool>(false);
        return found == scope.mutableVariables.end() ? std::nullopt : std::optional<bool>(found->second);
    }
    if (auto* member = dynamic_cast<const MemberExpr*>(&value)) {
        if (semantic.bareMemberCalls.contains(member))
            return semantic.bareMemberCalls.at(member)->returnsReference ? std::optional<bool>(semantic.bareMemberCalls.at(member)->returnsMutableReference) : std::optional<bool>(false);
        auto objectType = CheckExpression(*member->object, scope);
        auto type = semantic.members.find(objectType);
        if (type == semantic.members.end()) return std::nullopt;
        auto field = type->second.find(member->name);
        if (field == type->second.end() || field->second.kind != Kind::Field) return std::nullopt;
        auto object = MutableValue(*member->object, scope);
        if (!object) return std::nullopt;
        return *object && static_cast<const VariableDeclaration*>(field->second.declaration)->isMutable;
    }
    return std::nullopt;
}

bool SemanticBodyChecker::IsReceiver(const Expression& value, const Scope& scope) {
    if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&value)) {
        auto found = scope.receiverVariables.find(identifier->name);
        return found != scope.receiverVariables.end() && found->second;
    }
    if (auto* member = dynamic_cast<const MemberExpr*>(&value)) return IsReceiver(*member->object, scope);
    return false;
}
