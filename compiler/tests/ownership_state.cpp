#include "../source/OwnershipState.h"
#include "../source/OwnershipFlow.h"
#include "../source/OwnershipExpressionFlow.h"
#include "../source/OwnershipGraphBuilder.h"
#include "../source/OwnershipChecker.h"
#include <cassert>

int main() {
    int first = 0, second = 0;
    OwnershipState before;
    before.set(&first, OwnershipState::State::Available);
    OwnershipState moved = before;
    moved.set(&first, OwnershipState::State::Moved);
    moved.set(&second, OwnershipState::State::Available);
    assert(before.merge(moved));
    assert(!before.merge(moved));
    assert(!before.definitelyAvailable(&first));
    assert(!before.definitelyAvailable(&second));
    before.set(&first, OwnershipState::State::Available);
    assert(before.definitelyAvailable(&first));
    before.forget(&first);
    assert(before.possibilities(&first) == static_cast<OwnershipState::Mask>(OwnershipState::State::Uninitialized));

    OwnershipFlow flow;
    flow.blocks.resize(4);
    flow.blocks[0].successors = {1, 2};
    flow.blocks[1].effects.push_back({&first, OwnershipState::State::Moved});
    flow.blocks[1].successors = {3};
    flow.blocks[2].successors = {3};
    flow.blocks[3].successors = {1}; // A back edge must converge.
    OwnershipState initial;
    initial.set(&first, OwnershipState::State::Available);
    auto incoming = flow.solve(initial);
    assert(incoming[3] && !incoming[3]->definitelyAvailable(&first));
    assert(incoming[2] && incoming[2]->definitelyAvailable(&first));

    OwnershipFlow checks;
    checks.blocks.resize(4);
    checks.blocks[0].successors = {1, 2};
    checks.blocks[1].operations.push_back({OwnershipFlow::Action::Move, &first, 10});
    checks.blocks[1].successors = {3};
    checks.blocks[2].successors = {3};
    checks.blocks[3].operations.push_back({OwnershipFlow::Action::Read, &first, 20});
    auto violations = checks.validate(initial);
    assert(violations.size() == 1 && violations[0].offset == 20);
    checks.blocks[3].operations.insert(checks.blocks[3].operations.begin(), {OwnershipFlow::Action::Initialize, &first, 19});
    assert(checks.validate(initial).empty());

    OwnershipExpressionFlow lower([&](const IdentifierExpr& id) -> const void* {
        return id.name == "value" ? &first : nullptr;
    });
    MoveExpr move;
    move.offset = 42;
    auto operand = std::make_unique<IdentifierExpr>();
    operand->name = "value";
    move.operand = std::move(operand);
    OwnershipFlow lowered;
    lowered.blocks.resize(1);
    lower.append(move, lowered.blocks[0]);
    IdentifierExpr read;
    read.name = "value"; read.offset = 50; read.sourceFile = "example.c4r";
    lower.append(read, lowered.blocks[0]);
    auto movedRead = lowered.validate(initial);
    assert(movedRead.size() == 1 && movedRead[0].offset == 50);
    assert(movedRead[0].site == &read && movedRead[0].site->sourceFile == "example.c4r");
    assert(movedRead[0].action == OwnershipFlow::Action::Read);

    std::vector<std::unique_ptr<ASTNode>> body;
    auto declaration = std::make_unique<VariableDeclaration>();
    declaration->aggregateInit = true;
    auto* declared = declaration.get();
    body.push_back(std::move(declaration));
    auto branch = std::make_unique<IfStatement>();
    branch->condition = std::make_unique<BooleanLiteral>();
    branch->thenBranch = std::make_unique<Block>();
    auto branchMove = std::make_unique<MoveExpr>();
    auto branchId = std::make_unique<IdentifierExpr>();
    branchId->name = "local";
    branchMove->operand = std::move(branchId);
    branch->thenBranch->statements.push_back(std::move(branchMove));
    body.push_back(std::move(branch));
    auto after = std::make_unique<IdentifierExpr>();
    after->name = "local"; after->offset = 90;
    body.push_back(std::move(after));
    OwnershipGraphBuilder builder([&](const IdentifierExpr& id) -> const void* {
        return id.name == "local" ? declared : nullptr;
    });
    auto graph = builder.build(body);
    auto graphViolations = graph.validate(OwnershipState{});
    assert(graphViolations.size() == 1 && graphViolations[0].offset == 90);

    std::vector<std::unique_ptr<ASTNode>> loopBody;
    auto owned = std::make_unique<VariableDeclaration>();
    owned->aggregateInit = true;
    auto* ownedBinding = owned.get();
    loopBody.push_back(std::move(owned));
    auto loop = std::make_unique<LoopStatement>();
    loop->kind = LoopStatement::Kind::While;
    loop->condition = std::make_unique<BooleanLiteral>();
    loop->body = std::make_unique<Block>();
    auto loopMove = std::make_unique<MoveExpr>();
    auto loopId = std::make_unique<IdentifierExpr>(); loopId->name = "owner";
    loopMove->operand = std::move(loopId);
    loopMove->offset = 110;
    loop->body->statements.push_back(std::move(loopMove));
    auto next = std::make_unique<LoopControl>(); next->isBreak = false;
    loop->body->statements.push_back(std::move(next));
    loopBody.push_back(std::move(loop));
    OwnershipGraphBuilder loopBuilder([&](const IdentifierExpr& id) -> const void* {
        return id.name == "owner" ? ownedBinding : nullptr;
    });
    auto loopGraph = loopBuilder.build(loopBody);
    auto loopViolations = loopGraph.validate(OwnershipState{});
    assert(loopViolations.size() == 1 && loopViolations[0].offset == 110);

    std::vector<std::unique_ptr<ASTNode>> logicalBody;
    auto logicalOwner = std::make_unique<VariableDeclaration>();
    logicalOwner->aggregateInit = true;
    auto* logicalBinding = logicalOwner.get();
    logicalBody.push_back(std::move(logicalOwner));
    auto logical = std::make_unique<BinaryExpr>(); logical->op = "&&";
    auto left = std::make_unique<BooleanLiteral>(); left->value = false;
    logical->left = std::move(left);
    auto logicalMove = std::make_unique<MoveExpr>();
    auto logicalId = std::make_unique<IdentifierExpr>(); logicalId->name = "logical";
    logicalMove->operand = std::move(logicalId);
    logical->right = std::move(logicalMove);
    logicalBody.push_back(std::move(logical));
    auto logicalRead = std::make_unique<IdentifierExpr>(); logicalRead->name = "logical";
    logicalBody.push_back(std::move(logicalRead));
    OwnershipGraphBuilder logicalBuilder([&](const IdentifierExpr& id) -> const void* {
        return id.name == "logical" ? logicalBinding : nullptr;
    });
    assert(logicalBuilder.build(logicalBody).validate(OwnershipState{}).empty());

    std::vector<std::unique_ptr<ASTNode>> callBody;
    auto callOwner = std::make_unique<VariableDeclaration>();
    callOwner->aggregateInit = true;
    auto* callBinding = callOwner.get();
    callBody.push_back(std::move(callOwner));
    auto call = std::make_unique<CallExpr>(); call->name = "use";
    auto firstArg = std::make_unique<MoveExpr>();
    auto firstId = std::make_unique<IdentifierExpr>(); firstId->name = "argument";
    firstArg->operand = std::move(firstId);
    call->arguments.push_back(std::move(firstArg));
    auto secondArg = std::make_unique<IdentifierExpr>(); secondArg->name = "argument"; secondArg->offset = 120;
    call->arguments.push_back(std::move(secondArg));
    callBody.push_back(std::move(call));
    OwnershipGraphBuilder callBuilder([&](const IdentifierExpr& id) -> const void* {
        return id.name == "argument" ? callBinding : nullptr;
    });
    auto callViolations = callBuilder.build(callBody).validate(OwnershipState{});
    assert(callViolations.size() == 1 && callViolations[0].offset == 120);

    std::vector<std::unique_ptr<ASTNode>> comparisonBody;
    auto cmpOwner = std::make_unique<VariableDeclaration>(); cmpOwner->aggregateInit = true;
    auto* cmpBinding = cmpOwner.get(); comparisonBody.push_back(std::move(cmpOwner));
    auto comparison = std::make_unique<ComparisonExpr>();
    comparison->operators = {"<", "<"};
    comparison->operands.push_back(std::make_unique<NumericLiteral>());
    comparison->operands.push_back(std::make_unique<NumericLiteral>());
    auto cmpMove = std::make_unique<MoveExpr>();
    auto cmpId = std::make_unique<IdentifierExpr>(); cmpId->name = "cmp";
    cmpMove->operand = std::move(cmpId);
    comparison->operands.push_back(std::move(cmpMove));
    comparisonBody.push_back(std::move(comparison));
    auto cmpRead = std::make_unique<IdentifierExpr>(); cmpRead->name = "cmp"; cmpRead->offset = 130;
    comparisonBody.push_back(std::move(cmpRead));
    OwnershipGraphBuilder cmpBuilder([&](const IdentifierExpr& id) -> const void* { return id.name == "cmp" ? cmpBinding : nullptr; });
    auto cmpViolations = cmpBuilder.build(comparisonBody).validate(OwnershipState{});
    assert(cmpViolations.size() == 1 && cmpViolations[0].offset == 130);

    FunctionDeclaration function;
    function.parameters.push_back({"int", "parameter"});
    auto readParameter = std::make_unique<IdentifierExpr>();
    readParameter->name = "parameter";
    function.body.push_back(std::move(readParameter));
    OwnershipGraphBuilder parameterBuilder([&](const IdentifierExpr& id) -> const void* {
        return id.name == "parameter" ? &function.parameters[0] : nullptr;
    });
    assert(parameterBuilder.buildFunction(function).validate(OwnershipState{}).empty());
}
