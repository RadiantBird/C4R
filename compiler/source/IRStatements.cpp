#include "IRFunctionEmitter.h"

#include "Semantic.h"

#include <stdexcept>
#include <utility>

namespace c4r::ir {

llvm::Value* IRFunctionEmitter::CreateString(const std::string& value) {
    auto* global = Builder.CreateGlobalString(
        value,
        "c4r.str",
        0,
        &module.Module
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

llvm::FunctionCallee IRFunctionEmitter::Printf() {
    llvm::Type* arguments[] = {llvm::PointerType::getUnqual(module.Context)};
    auto* type = llvm::FunctionType::get(Builder.getInt32Ty(), arguments, true);
    return module.Module.getOrInsertFunction("printf", type);
}

void IRFunctionEmitter::EmitString(const StringStatement& statement) {
    auto printfFunction = Printf();
    auto* format = CreateString("%s");
    auto* value = CreateString(statement.value);
    Builder.CreateCall(printfFunction, {format, value});
}

void IRFunctionEmitter::EmitBody(
    const std::vector<std::unique_ptr<ASTNode>>& body
) {
    cleanupScopes.emplace_back();
    auto outerVariables = variables;
    auto outerBindings = bindings;
    auto outerFunctions = functions;
    auto outerInaccessible = inaccessibleVariables;
    std::unordered_set<std::string> localNames;

    auto printfFunction = Printf();

    for (const auto& node : body) {
        module.diagnosticOffset = node->offset;

        if (dynamic_cast<TypeDeclaration*>(node.get())) continue;

        if (auto* declaration = dynamic_cast<FunctionDeclaration*>(node.get())) {
            auto existing = functions.find(declaration->name);
            if (existing != functions.end() && existing->second->declaration == declaration) continue;
            if (!localNames.insert(declaration->name).second) throw std::runtime_error("Duplicate declaration: " + declaration->name);
            variables.erase(declaration->name);
            inaccessibleVariables.erase(declaration->name);
            auto symbol = DeclareFunction(*declaration, true);
            functions[declaration->name] = symbol;
            functionDeclarations[declaration] = symbol;
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
        if (auto* result = dynamic_cast<const ReturnStatement*>(node.get())) {
            EmitReturn(*result);
        } else if (auto* block = dynamic_cast<const Block*>(node.get())) {
            EmitBody(block->statements);
        } else if (auto* branch = dynamic_cast<const IfStatement*>(node.get())) {
            EmitIf(*branch);
        } else if (auto* loop = dynamic_cast<const LoopStatement*>(node.get())) {
            EmitLoop(*loop);
        } else if (auto* control = dynamic_cast<const LoopControl*>(node.get())) {
            if (loops.empty()) throw std::runtime_error("break or continue outside loop");
            if (!valueScopes.empty() && loops.size() <= valueScopes.back()->loopDepth)
                throw std::runtime_error("Cannot leave a value scope with break or continue");
            EmitCleanups(loopCleanupDepths.back());
            Builder.CreateBr(control->isBreak ? loops.back().first : loops.back().second);
        } else if (auto* string = dynamic_cast<const StringStatement*>(node.get())) {
            EmitString(*string);
        } else if (auto* declaration = dynamic_cast<const VariableDeclaration*>(node.get())) {
            if (!localNames.insert(declaration->name).second)
                throw std::runtime_error("Duplicate variable: " + declaration->name);
            EmitVariable(*declaration);
        } else if (auto* expression = dynamic_cast<const Expression*>(node.get())) {
            EmitFullExpression(*expression);
        } else if (auto* format = dynamic_cast<const FormatStringStatement*>(node.get())) {
            EmitFormat(*format, printfFunction);
        } else {
            throw std::runtime_error("Unsupported AST node in function");
        }
    }
    if (!Builder.GetInsertBlock()->hasTerminator()) EmitCleanups(cleanupScopes.size() - 1);
    cleanupScopes.pop_back();
    variables = std::move(outerVariables);
    bindings = std::move(outerBindings);
    functions = std::move(outerFunctions);
    inaccessibleVariables = std::move(outerInaccessible);
}

void IRFunctionEmitter::EmitReturn(const ReturnStatement& statement) {
    if (!valueScopes.empty()) {
        if (!statement.value) throw std::runtime_error("Value scope requires a return value");
        auto& scope = *valueScopes.back();
        const auto& returnType = semanticAnalysis->ReturnValueType(statement);
        if (semanticAnalysis->NeedsDestruction(returnType)) {
            auto& type = module.types.at(returnType);
            if (scope.type && scope.type != type.type) throw std::runtime_error("Value scope return types do not match");
            if (!scope.objectStorage) {
                auto& entry = Builder.GetInsertBlock()->getParent()->getEntryBlock();
                llvm::IRBuilder<> entryBuilder(&entry, entry.begin());
                scope.objectStorage = entryBuilder.CreateAlloca(type.type, nullptr, "value.object");
                scope.objectActive = entryBuilder.CreateAlloca(Builder.getInt1Ty(), nullptr, "value.active");
                scope.objectType = &type;
                llvm::IRBuilder<> initializer(temporaryFrames.at(scope.temporaryFrame).marker);
                initializer.CreateStore(Builder.getFalse(), scope.objectActive);
                temporaryFrames.at(scope.temporaryFrame).cleanups.push_back({scope.objectStorage, &type, scope.objectActive});
            }
            scope.type = type.type;
            BeginFullExpression();
            EmitDirectValue(*statement.value, scope.objectStorage, type);
            EndFullExpression();
            if (scope.objectActive) Builder.CreateStore(Builder.getTrue(), scope.objectActive);
            EmitCleanups(scope.cleanupDepth);
            scope.returns.emplace_back(nullptr, Builder.GetInsertBlock());
            Builder.CreateBr(scope.exit);
            return;
        }
        auto* value = EmitFullExpression(*statement.value);
        if (value->getType()->isVoidTy()) throw std::runtime_error("Value scope cannot return void");
        if (scope.type && scope.type != value->getType()) throw std::runtime_error("Value scope return types do not match");
        scope.type = value->getType();
        EmitCleanups(scope.cleanupDepth);
        scope.returns.emplace_back(value, Builder.GetInsertBlock());
        Builder.CreateBr(scope.exit);
        return;
    }
    if (semanticAnalysis->IsReferenceReturn(statement)) {
        auto result = ResolveLValue(*statement.value);
        EmitCleanups(0);
        Builder.CreateRet(result.storage);
        return;
    }
    auto* resultType = Builder.GetInsertBlock()->getParent()->getReturnType();
    if (currentReturnStorage) {
        auto* construct = statement.value ? dynamic_cast<const ConstructExpr*>(statement.value.get()) : nullptr;
        auto* move = statement.value ? dynamic_cast<const MoveExpr*>(statement.value.get()) : nullptr;
        if (!construct && !move) throw std::runtime_error("Destructible return requires direct construction");
        BeginFullExpression();
        if (construct) EmitConstruction(currentReturnStorage, *construct);
        else Builder.CreateStore(EmitMove(*move), currentReturnStorage);
        EndFullExpression();
        EmitCleanups(0);
        Builder.CreateRetVoid();
    } else if (!statement.value) {
        if (!resultType->isVoidTy()) throw std::runtime_error("Return value required");
        EmitCleanups(0);
        Builder.CreateRetVoid();
    } else {
        auto* value = EmitFullExpression(*statement.value);
        if (value->getType() != resultType) throw std::runtime_error("Invalid return value type");
        EmitCleanups(0);
        Builder.CreateRet(value);
    }
}

void IRFunctionEmitter::EmitIf(const IfStatement& branch) {
    auto* condition = EmitCondition(*branch.condition);
    auto* fn = Builder.GetInsertBlock()->getParent();
    auto* thenBlock = llvm::BasicBlock::Create(module.Context, "if.then", fn);
    auto* elseBlock = llvm::BasicBlock::Create(module.Context, "if.else", fn);
    auto* done = llvm::BasicBlock::Create(module.Context, "if.end", fn);
    Builder.CreateCondBr(condition, thenBlock, elseBlock);
    Builder.SetInsertPoint(thenBlock);
    EmitBody(branch.thenBranch->statements);
    bool thenTerminated = Builder.GetInsertBlock()->hasTerminator();
    if (!thenTerminated) Builder.CreateBr(done);
    Builder.SetInsertPoint(elseBlock);
    if (branch.elseBranch) EmitBody(branch.elseBranch->statements);
    bool elseTerminated = Builder.GetInsertBlock()->hasTerminator();
    if (!elseTerminated) Builder.CreateBr(done);
    Builder.SetInsertPoint(done);
    if (thenTerminated && elseTerminated) Builder.CreateUnreachable();
}

void IRFunctionEmitter::EmitLoop(const LoopStatement& loop) {
    auto savedVariables = variables;
    auto savedBindings = bindings;
    cleanupScopes.emplace_back();
    if (auto* declaration = dynamic_cast<VariableDeclaration*>(loop.initializer.get())) EmitVariable(*declaration);
    else if (auto* initializer = dynamic_cast<Expression*>(loop.initializer.get())) EmitFullExpression(*initializer);
    auto* fn = Builder.GetInsertBlock()->getParent();
    auto* conditionBlock = llvm::BasicBlock::Create(module.Context, "loop.condition", fn);
    auto* bodyBlock = llvm::BasicBlock::Create(module.Context, "loop.body", fn);
    auto* stepBlock = llvm::BasicBlock::Create(module.Context, "loop.step", fn);
    auto* done = llvm::BasicBlock::Create(module.Context, "loop.end", fn);
    Builder.CreateBr(loop.kind == LoopStatement::Kind::DoWhile ? bodyBlock : conditionBlock);
    Builder.SetInsertPoint(conditionBlock);
    auto* condition = loop.condition ? EmitCondition(*loop.condition) : Builder.getTrue();
    Builder.CreateCondBr(condition, bodyBlock, done);
    Builder.SetInsertPoint(bodyBlock);
    loops.emplace_back(done, stepBlock);
    loopCleanupDepths.push_back(cleanupScopes.size());
    EmitBody(loop.body->statements);
    loopCleanupDepths.pop_back();
    loops.pop_back();
    if (!Builder.GetInsertBlock()->hasTerminator()) Builder.CreateBr(stepBlock);
    Builder.SetInsertPoint(stepBlock);
    if (loop.step) EmitFullExpression(*loop.step);
    Builder.CreateBr(conditionBlock);
    Builder.SetInsertPoint(done);
    EmitCleanups(cleanupScopes.size() - 1);
    cleanupScopes.pop_back();
    variables = std::move(savedVariables);
    bindings = std::move(savedBindings);
}

void IRFunctionEmitter::EmitFormat(const FormatStringStatement& statement, llvm::FunctionCallee printfFunction) {
    std::string format;
    std::vector<llvm::Value*> arguments;

    for (const auto& part : statement.parts) {

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

            if (it == variables.end() && (!currentMethod || !module.types.at(currentMethod->owner).fields.contains(identifier->name))) {
                throw std::runtime_error(
                    "Undefined variable: " + identifier->name
                );
            }

            llvm::Value* value = EmitExpression(*identifier);
            auto* type = value->getType();
            if (type->isFloatTy()) {
                format += "%g";
                value = Builder.CreateFPExt(value, Builder.getDoubleTy());
            } else if (type->isIntegerTy(32) || type->isIntegerTy(1)) {
                format += "%d";
                if (type->isIntegerTy(1)) value = Builder.CreateZExt(value, Builder.getInt32Ty());
            } else throw std::runtime_error("Unsupported format string type: " + identifier->name);
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

}
