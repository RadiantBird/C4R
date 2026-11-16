#include "../source/OwnershipChecker.h"
#include <cassert>

int main() {
    Program program;
    auto function = std::make_unique<FunctionDeclaration>();
    function->name = "main";
    function->returnType = "int";
    auto variable = std::make_unique<VariableDeclaration>();
    variable->name = "x"; variable->type = "int";
    auto number = std::make_unique<NumericLiteral>(); number->value = "1";
    variable->initialValue = std::move(number);
    function->body.push_back(std::move(variable));
    auto firstRead = std::make_unique<IdentifierExpr>(); firstRead->name = "x";
    auto move = std::make_unique<MoveExpr>(); move->operand = std::move(firstRead);
    function->body.push_back(std::move(move));
    auto statement = std::make_unique<ReturnStatement>();
    auto identifier = std::make_unique<IdentifierExpr>(); identifier->name = "x";
    statement->value = std::move(identifier);
    function->body.push_back(std::move(statement));
    auto* entry = function.get();
    program.children.push_back(std::move(function));
    Semantic semantic;
    semantic.Collect(program);
    semantic.CheckBodies(program);
    auto violations = OwnershipChecker(semantic).checkFunction(*entry);
    assert(violations.size() == 1 && violations[0].action == OwnershipFlow::Action::Read);
}
