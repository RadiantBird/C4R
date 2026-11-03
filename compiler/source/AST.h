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

struct Expression : ASTNode {
    virtual ~Expression() = default;
};

struct NumericLiteral : Expression {
    std::string value;
};

struct BooleanLiteral : Expression { bool value = false; };
struct UnaryExpr : Expression {
    std::string op;
    std::unique_ptr<Expression> operand;
    bool postfix = false;
};
struct BinaryExpr : Expression {
    std::string op;
    std::unique_ptr<Expression> left, right;
};
struct ComparisonExpr : Expression {
    std::vector<std::string> operators;
    std::vector<std::unique_ptr<Expression>> operands;
};

struct VariableDeclaration : ASTNode {
    std::string name;
    std::string type;
    bool isMutable = false;
    bool isConst = false;
    std::unique_ptr<Expression> initialValue;
};

struct IdentifierExpr : Expression {
    std::string name;
};

struct ReturnStatement : ASTNode {
    std::unique_ptr<Expression> value;
};

struct Block : ASTNode { std::vector<std::unique_ptr<ASTNode>> statements; };
struct IfExpr : Expression {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> thenBranch, elseBranch;
};
struct BlockExpr : Expression { std::unique_ptr<Block> block; };
struct IfStatement : ASTNode {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> thenBranch, elseBranch;
};
struct LoopStatement : ASTNode {
    enum class Kind { While, DoWhile, For } kind;
    std::unique_ptr<ASTNode> initializer;
    std::unique_ptr<Expression> condition, step;
    std::unique_ptr<Block> body;
};
struct LoopControl : ASTNode { bool isBreak; };

struct FormatText : ASTNode {
    std::string value;
};

struct FormatStringStatement : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> parts;
};
