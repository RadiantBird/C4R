#include "Compilation.h"

#include "Lexer.h"
#include "LLVM.h"
#include "OwnershipChecker.h"
#include "Parser.h"

#include <stdexcept>
#include <utility>

namespace {
using std::string;

// AST offsets are relative to their source file, so retain the file on every
// nested declaration and expression as well as on top-level declarations.
void AssignSourceFile(ASTNode& node, const string& filename) {
    node.sourceFile = filename;
    auto visit = [&](auto& child) { if (child) AssignSourceFile(*child, filename); };
    auto visitList = [&](auto& children) { for (auto& child : children) visit(child); };
    if (auto* program = dynamic_cast<Program*>(&node)) visitList(program->children);
    else if (auto* type = dynamic_cast<TypeDeclaration*>(&node)) for (auto& member : type->members) visit(member.declaration);
    else if (auto* function = dynamic_cast<FunctionDeclaration*>(&node)) {
        for (auto& parameter : function->parameters) parameter.sourceFile = filename;
        visitList(function->body);
    }
    else if (auto* block = dynamic_cast<Block*>(&node)) visitList(block->statements);
    else if (auto* variable = dynamic_cast<VariableDeclaration*>(&node)) visit(variable->initialValue);
    else if (auto* result = dynamic_cast<ReturnStatement*>(&node)) visit(result->value);
    else if (auto* member = dynamic_cast<MemberExpr*>(&node)) visit(member->object);
    else if (auto* call = dynamic_cast<CallExpr*>(&node)) { visit(call->object); visitList(call->arguments); }
    else if (auto* construct = dynamic_cast<ConstructExpr*>(&node)) visitList(construct->arguments);
    else if (auto* move = dynamic_cast<MoveExpr*>(&node)) visit(move->operand);
    else if (auto* unary = dynamic_cast<UnaryExpr*>(&node)) visit(unary->operand);
    else if (auto* binary = dynamic_cast<BinaryExpr*>(&node)) { visit(binary->left); visit(binary->right); }
    else if (auto* comparison = dynamic_cast<ComparisonExpr*>(&node)) visitList(comparison->operands);
    else if (auto* branch = dynamic_cast<IfExpr*>(&node)) { visit(branch->condition); visit(branch->thenBranch); visit(branch->elseBranch); }
    else if (auto* statementBranch = dynamic_cast<IfStatement*>(&node)) { visit(statementBranch->condition); visit(statementBranch->thenBranch); visit(statementBranch->elseBranch); }
    else if (auto* blockExpression = dynamic_cast<BlockExpr*>(&node)) visit(blockExpression->block);
    else if (auto* loop = dynamic_cast<LoopStatement*>(&node)) { visit(loop->initializer); visit(loop->condition); visit(loop->step); visit(loop->body); }
    else if (auto* format = dynamic_cast<FormatStringStatement*>(&node)) visitList(format->parts);
}
}

void Compilation::DiscoverTypes() {
    for (const auto& source : sources) {
        Lexer scan(source.text, &typeLocations);
        for (Token token = scan.NextToken(); token.type != TokenType::EndOfFile; token = scan.NextToken()) {
            if (token.type != TokenType::Reserved || (token.value != "struct" && token.value != "class")) continue;
            Token name = scan.NextToken();
            if (name.type != TokenType::Identifier) {
                diagnosticOffset = name.offset;
                auto previous = typeLocations.find(name.value);
                throw std::runtime_error("Invalid or duplicate type name: " + name.value + " (" + source.name +
                    (previous == typeLocations.end() ? "" : ", previously " + previous->second) + ")");
            }
            typeLocations.emplace(name.value, source.name);
        }
    }
}

void Compilation::Parse() {
    for (const auto& source : sources) {
        Parser parser(Lexer(source.text, &typeLocations));
        Program file;
        try {
            file = parser.ParseProgram();
        } catch (...) {
            diagnosticOffset = parser.DiagnosticOffset();
            diagnosticFile = source.name;
            throw;
        }
        AssignSourceFile(file, source.name);
        for (auto& node : file.children) program.children.push_back(std::move(node));
    }
}

void Compilation::Analyze() {
    try {
        semantic.Collect(program, sources.size() > 1);
        semantic.CheckBodies(program);
    } catch (...) {
        diagnosticOffset = semantic.DiagnosticOffset();
        diagnosticFile = semantic.DiagnosticFile();
        throw;
    }

    OwnershipChecker checker(semantic);
    for (const auto* function : semantic.FunctionsWithMoves()) {
        auto violations = checker.checkFunction(*function);
        if (violations.empty()) continue;
        const auto& violation = violations.front();
        diagnosticOffset = violation.offset;
        diagnosticFile = violation.site ? violation.site->sourceFile : function->sourceFile;
        throw std::runtime_error(violation.action == OwnershipFlow::Action::Reinitialize
            ? "Copying a destructible value is not permitted: target is not definitely moved"
            : "Object may have already been moved or be uninitialized");
    }
}

void Compilation::Generate(LLVM& llvm) {
    try {
        llvm.Generate(program, semantic);
    } catch (...) {
        diagnosticOffset = llvm.DiagnosticOffset();
        throw;
    }
}
