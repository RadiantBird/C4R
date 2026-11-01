#pragma once

#include <string>
#include <vector>
#include <memory>

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct StringStatement : ASTNode {
    std::string value;
};

struct FunctionDeclaration : ASTNode {
    std::string name;
    std::string returnType = "void";
    std::vector<std::unique_ptr<ASTNode>> body;
};

struct Program : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> children;
};