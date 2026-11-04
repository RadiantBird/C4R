#include "LLVM.h"

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
#include <system_error>

#include <stdexcept>
#include <process.h>
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

LLVM::Variable& LLVM::MutableVariable(const Expression& expression) {
    auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression);
    if (!identifier) throw std::runtime_error("Assignment requires a variable");
    auto it = variables.find(identifier->name);
    if (it == variables.end()) throw std::runtime_error("Undefined variable: " + identifier->name);
    if (!it->second.isMutable) throw std::runtime_error("Cannot modify immutable variable: " + identifier->name);
    return it->second;
}

llvm::Value* LLVM::EmitBinary(const std::string& op, llvm::Value* left, llvm::Value* right) {
    if (left->getType()->isVoidTy() || right->getType()->isVoidTy()) throw std::runtime_error("Void expression cannot be used as an operand");
    if (left->getType() != right->getType()) throw std::runtime_error("Operand types do not match for " + op);
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
    if (op == "/") return Builder.CreateSDiv(left, right);
    if (op == "%") return Builder.CreateSRem(left, right);
    if (op == "&") return Builder.CreateAnd(left, right);
    if (op == "|") return Builder.CreateOr(left, right);
    if (op == "^") return Builder.CreateXor(left, right);
    if (op == "<<") return Builder.CreateShl(left, right);
    if (op == ">>") return Builder.CreateAShr(left, right);
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

llvm::Value* LLVM::EmitCondition(const Expression& expression) {
    auto* value = EmitExpression(expression);
    if (!value->getType()->isIntegerTy(1)) throw std::runtime_error("Condition must be bool");
    return value;
}

void LLVM::EmitVariable(const VariableDeclaration& declaration) {
    auto* type = GetType(declaration.type);
    if (type->isVoidTy()) throw std::runtime_error("Variable cannot have void type");
    auto* value = EmitExpression(*declaration.initialValue);
    if (value->getType() != type) throw std::runtime_error("Initializer type mismatch: " + declaration.name);
    auto& entry = Builder.GetInsertBlock()->getParent()->getEntryBlock();
    llvm::IRBuilder<> entryBuilder(&entry, entry.begin());
    auto* storage = entryBuilder.CreateAlloca(type, nullptr, declaration.name);
    Builder.CreateStore(value, storage);
    Variable variable{storage, type, declaration.isMutable, nextVariableId++};
    variables.insert_or_assign(declaration.name, variable);
    bindings.insert_or_assign(variable.id, variable);
}

llvm::Value* LLVM::EmitCall(const std::string& name, const std::vector<std::unique_ptr<Expression>>& arguments) {
    if (variables.contains(name)) throw std::runtime_error("Variable is not callable: " + name);
    if (inaccessibleVariables.contains(name)) throw std::runtime_error("Variable requires capture: " + name);
    auto it = functions.find(name);
    if (it == functions.end()) throw std::runtime_error("Undefined function: " + name);
    auto symbol = it->second;
    const auto& parameters = symbol->declaration->parameters;
    if (arguments.size() != parameters.size()) throw std::runtime_error("Wrong number of arguments for function: " + name);
    std::vector<llvm::Value*> values;
    for (const auto& capture : symbol->captures) {
        auto binding = bindings.find(capture.variable.id);
        if (binding == bindings.end()) throw std::runtime_error("Capture is not available in this function: " + capture.name);
        values.push_back(binding->second.storage);
    }
    for (size_t i = 0; i < arguments.size(); ++i) {
        auto* value = EmitExpression(*arguments[i]);
        if (value->getType() != GetType(parameters[i].type)) throw std::runtime_error("Argument type mismatch for function: " + name);
        values.push_back(value);
    }
    return Builder.CreateCall(symbol->function, values);
}

llvm::Value* LLVM::FinishValueScope(ValueScope& scope) {
    if (scope.returns.empty()) throw std::runtime_error("Value scope requires return");
    Builder.SetInsertPoint(scope.exit);
    auto* result = Builder.CreatePHI(scope.type, static_cast<unsigned>(scope.returns.size()), "scope.value");
    for (const auto& item : scope.returns) result->addIncoming(item.first, item.second);
    return result;
}

llvm::Value* LLVM::EmitValueBlock(const Block& block) {
    auto* fn = Builder.GetInsertBlock()->getParent();
    ValueScope scope{llvm::BasicBlock::Create(Context, "value.end", fn), nullptr, {}, loops.size()};
    valueScopes.push_back(&scope);
    EmitBody(block.statements);
    if (!Builder.GetInsertBlock()->hasTerminator()) throw std::runtime_error("Every path in a value scope must return a value");
    valueScopes.pop_back();
    return FinishValueScope(scope);
}

llvm::Value* LLVM::EmitExpression(const Expression& expression) {
    diagnosticOffset = expression.offset;
    if (auto* call = dynamic_cast<const CallExpr*>(&expression)) return EmitCall(call->name, call->arguments);
    if (auto* block = dynamic_cast<const BlockExpr*>(&expression)) return EmitValueBlock(*block->block);
    if (auto* branch = dynamic_cast<const IfExpr*>(&expression)) {
        auto* condition = EmitCondition(*branch->condition);
        auto* fn = Builder.GetInsertBlock()->getParent();
        auto* thenBlock = llvm::BasicBlock::Create(Context, "if.value.then", fn);
        auto* elseBlock = llvm::BasicBlock::Create(Context, "if.value.else", fn);
        ValueScope scope{llvm::BasicBlock::Create(Context, "if.value.end", fn), nullptr, {}, loops.size()};
        Builder.CreateCondBr(condition, thenBlock, elseBlock);
        valueScopes.push_back(&scope);
        Builder.SetInsertPoint(thenBlock);
        EmitBody(branch->thenBranch->statements);
        if (!Builder.GetInsertBlock()->hasTerminator()) throw std::runtime_error("Every if expression branch must return a value");
        Builder.SetInsertPoint(elseBlock);
        EmitBody(branch->elseBranch->statements);
        if (!Builder.GetInsertBlock()->hasTerminator()) throw std::runtime_error("Every if expression branch must return a value");
        valueScopes.pop_back();
        return FinishValueScope(scope);
    }
    if (auto* literal = dynamic_cast<const NumericLiteral*>(&expression)) {
        auto value = std::stoll(literal->value);
        if (value > INT32_MAX) throw std::runtime_error("Integer literal out of range");
        return Builder.getInt32(static_cast<int32_t>(value));
    }
    if (auto* literal = dynamic_cast<const BooleanLiteral*>(&expression)) return Builder.getInt1(literal->value);
    if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression)) {
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
        if (unary->op == "++" || unary->op == "--") {
            auto& var = MutableVariable(*unary->operand);
            if (!var.type->isIntegerTy(32)) throw std::runtime_error("Increment requires int32");
            auto* old = Builder.CreateLoad(var.type, var.storage);
            auto* value = unary->op == "++" ? Builder.CreateAdd(old, Builder.getInt32(1)) : Builder.CreateSub(old, Builder.getInt32(1));
            Builder.CreateStore(value, var.storage);
            return unary->postfix ? old : value;
        }
        auto* value = EmitExpression(*unary->operand);
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

        if (auto* declaration = dynamic_cast<FunctionDeclaration*>(node.get())) {
            auto existing = functions.find(declaration->name);
            if (existing != functions.end() && existing->second->declaration == declaration) continue;
            if (!localNames.insert(declaration->name).second) throw std::runtime_error("Duplicate declaration: " + declaration->name);
            variables.erase(declaration->name);
            inaccessibleVariables.erase(declaration->name);
            auto symbol = DeclareFunction(*declaration, true);
            functions[declaration->name] = symbol;
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
                auto* value = EmitExpression(*statement->value);
                if (value->getType()->isVoidTy()) throw std::runtime_error("Value scope cannot return void");
                auto& scope = *valueScopes.back();
                if (scope.type && scope.type != value->getType()) throw std::runtime_error("Value scope return types do not match");
                scope.type = value->getType();
                scope.returns.emplace_back(value, Builder.GetInsertBlock());
                Builder.CreateBr(scope.exit);
                continue;
            }
            auto* resultType = Builder.GetInsertBlock()->getParent()->getReturnType();
            if (!statement->value) {
                if (!resultType->isVoidTy()) throw std::runtime_error("Return value required");
                Builder.CreateRetVoid();
            } else {
                auto* value = EmitExpression(*statement->value);
                if (value->getType() != resultType) throw std::runtime_error("Invalid return value type");
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
            if (auto* declaration = dynamic_cast<VariableDeclaration*>(loop->initializer.get())) EmitVariable(*declaration);
            else if (auto* initializer = dynamic_cast<Expression*>(loop->initializer.get())) EmitExpression(*initializer);
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
            EmitBody(loop->body->statements);
            loops.pop_back();
            if (!Builder.GetInsertBlock()->hasTerminator()) Builder.CreateBr(stepBlock);
            Builder.SetInsertPoint(stepBlock);
            if (loop->step) EmitExpression(*loop->step);
            Builder.CreateBr(conditionBlock);
            Builder.SetInsertPoint(done);
            variables = std::move(savedVariables);
            bindings = std::move(savedBindings);
        }
        else if (auto* control = dynamic_cast<LoopControl*>(node.get())) {
            if (loops.empty()) throw std::runtime_error("break or continue outside loop");
            if (!valueScopes.empty() && loops.size() <= valueScopes.back()->loopDepth)
                throw std::runtime_error("Cannot leave a value scope with break or continue");
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
            EmitExpression(*expression);
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

                    if (it == variables.end()) {
                        throw std::runtime_error(
                            "Undefined variable: " + identifier->name
                        );
                    }

                    format += "%d";

                    llvm::Value* value = Builder.CreateLoad(it->second.type, it->second.storage, identifier->name + ".value");
                    if (it->second.type->isIntegerTy(1)) value = Builder.CreateZExt(value, Builder.getInt32Ty());
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
    std::vector<llvm::Type*> parameterTypes(captures.size(), llvm::PointerType::getUnqual(Context));
    for (const auto& parameter : declaration.parameters) parameterTypes.push_back(GetType(parameter.type));
    auto* functionType = llvm::FunctionType::get(GetType(declaration.returnType), parameterTypes, false);
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
    variables.clear(); bindings.clear(); valueScopes.clear(); loops.clear(); validCaptures.clear();
    for (const auto& [name, variable] : outerVariables) inaccessibleVariables.insert(name);
    const auto& declaration = *symbol->declaration;
    for (const auto& node : declaration.body) {
        if (auto* capture = dynamic_cast<const CaptureDeclaration*>(node.get())) validCaptures.insert(capture);
    }
    auto* entry = llvm::BasicBlock::Create(Context, "entry", symbol->function);
    Builder.SetInsertPoint(entry);
    auto argument = symbol->function->arg_begin();
    for (const auto& capture : symbol->captures) {
        Variable variable{&*argument++, capture.variable.type, capture.variable.isMutable, capture.variable.id};
        variables.insert_or_assign(capture.name, variable);
        bindings.insert_or_assign(variable.id, variable);
        inaccessibleVariables.erase(capture.name);
    }
    for (const auto& parameter : declaration.parameters) {
        auto* type = GetType(parameter.type);
        auto* storage = Builder.CreateAlloca(type, nullptr, parameter.name);
        Builder.CreateStore(&*argument++, storage);
        Variable variable{storage, type, parameter.isMutable, nextVariableId++};
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

void LLVM::Generate(const Program& program) {
    bool hasTopLevelStatements = false;

    for (const auto& node : program.children) {
        if (auto* function = dynamic_cast<FunctionDeclaration*>(node.get())) {
            if (functions.contains(function->name)) throw std::runtime_error("Duplicate function: " + function->name);
            if (function->name == "printf") throw std::runtime_error("Reserved function name: printf");
            functions[function->name] = DeclareFunction(*function, false);
        }
        else if (!dynamic_cast<FunctionDeclaration*>(node.get())) {
            hasTopLevelStatements = true;
        }
        else {
            throw std::runtime_error(
                "Unsupported AST node in program"
            );
        }
    }

    std::string entryName;
    if (functions.contains("main")) {
        const auto& main = *functions.at("main")->declaration;
        if (!main.parameters.empty() || !GetType(main.returnType)->isIntegerTy(32)) throw std::runtime_error("main must return int and have no parameters");
        entryName = "main";
    } else {
        for (const auto* name : {"start", "entry"}) {
            auto it = functions.find(name);
            if (it != functions.end() && it->second->declaration->parameters.empty() && GetType(it->second->declaration->returnType)->isIntegerTy(32)) {
                entryName = name; break;
            }
        }
    }
    if (!entryName.empty() && hasTopLevelStatements) {
        throw std::runtime_error(
            "Top-level statements with explicit entry point are not supported yet"
        );
    }

    for (const auto& node : program.children) {
        if (auto* function = dynamic_cast<FunctionDeclaration*>(node.get())) {
            EmitFunctionBody(functions.at(function->name));
        }
    }

    if (entryName.empty()) {
        EmitImplicitMain(program);
    } else if (entryName != "main") {
        auto* type = llvm::FunctionType::get(Builder.getInt32Ty(), false);
        auto* wrapper = llvm::Function::Create(type, llvm::Function::ExternalLinkage, "main", Module);
        Builder.SetInsertPoint(llvm::BasicBlock::Create(Context, "entry", wrapper));
        Builder.CreateRet(Builder.CreateCall(functions.at(entryName)->function));
    }

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

    const auto result = _wspawnvp(
        _P_WAIT,
        L"link.exe",
        arguments.data()
    );

    if (result == -1)
        throw std::system_error(
            errno,
            std::generic_category(),
            "Failed to start link.exe"
        );

    if (result != 0) {
        throw std::runtime_error(
            std::format("link.exe failed with exit code {}", result)
        );
    }
}
