#include <iostream>
#include <LLVM.h>
#include <print>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <filesystem>
// #include <cstdlib>
// #include <utility>

enum class TokenType {
    EndOfStatement,
    LeftParen,
    RightParen,
    String,
    EndOfFile,
    Unknown,

    TypeName,
    Identifier,
    LeftBrace,
    RightBrace,
    Blank,

    Number,
    Assign,
    Reserved,
    FormatString,

    Comma,
    Dot,
    Operator,
};

using std::print;
using std::string;
using std::unordered_map;
using std::vector;
using std::unique_ptr;

// {"", true}, <- for Copy and Paste

unordered_map<string, bool> StandardType = {
    {"int32", true},
    {"int", true},
    {"bool", true},
    {"void", true},
};

unordered_map<string, bool> ReservedWord = {
    {"if", true},
    {"else", true},
    {"for", true},
    {"while", true},
    {"do", true},
    {"return", true},
    {"capture", true},
    {"break", true},
    {"continue", true},

    {"const", true},
    {"mutable", true},    
};

std::string tokenTypeToString(TokenType type) {
    switch (type) {
    case TokenType::EndOfStatement:
        return "EndOfStatement";
    case TokenType::LeftParen:
        return "LeftParen";
    case TokenType::RightParen:
        return "RightParen";
    case TokenType::String:
        return "String";
    case TokenType::EndOfFile:
        return "EndOfFile";
    case TokenType::Unknown:
        return "Unknown";

    case TokenType::TypeName:
        return "TypeName";
    case TokenType::Identifier:
        return "Identifier";
    case TokenType::LeftBrace:
        return "LeftBrace";
    case TokenType::RightBrace:
        return "RightBrace";
    case TokenType::Blank:
        return "Blank";

    case TokenType::Number:
        return "Number";
    case TokenType::Assign:
        return "Assign";
    case TokenType::Reserved:
        return "Reserved";
    case TokenType::FormatString:
        return "FormatString";
    case TokenType::Comma:
        return "Comma";
    case TokenType::Dot:
        return "Dot";

    default:
        return "Invalid";
    }
}

bool isIdentifier(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isWhitespace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool isNumeric(const string& s) {
    // TODO: support decimal and hex
    if (s.empty()) return false;

    for (unsigned char c : s) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    return true;
}

struct Token {
    TokenType type;
    string value;
};

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: C4R <source.c4r>\n";
        return 1;
    }
    string source = "";
    string target = argv[1];

    {
        std::ifstream file(target);

        if (!file) {
            print("Failed to open source file\n");
            return -1;
        }

        source.assign(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()
        );
    }

    class Lexer {
    private:
        const std::string& source;
        size_t position = 0;

        Token ParseString(TokenType type) {
            string value;

            while (position < source.size() && source[position] != '"') {
                char c = source[position++];

                if (c == '\\' && position < source.size()) {
                    char escaped = source[position++];

                    switch (escaped) {
                    case 'n':  value += '\n'; break;
                    case 't':  value += '\t'; break;
                    case 'r':  value += '\r'; break;
                    case '\\': value += '\\'; break;
                    case '"':  value += '"'; break;

                    default:
                        throw std::runtime_error("Unknown escape sequence");
                    }
                }
                else {
                    value += c;
                }
            }

            if (position >= source.size()) {
                throw std::runtime_error("Unterminated string literal");
            }

            ++position;
            return { type, value };
        }
    public:
        Lexer(const std::string& source)
            : source(source) {}

        Token NextToken() {
            while (position < source.size()) {
                char c = source[position++];

                if (isWhitespace(c)) {
                    continue;
                }

                if (c == '/' && position < source.size() && source[position] == '/') {
                    while (position < source.size() && source[position] != '\n') ++position;
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    string number(1, c);
                    while (position < source.size() && std::isdigit(static_cast<unsigned char>(source[position])))
                        number += source[position++];
                    return {TokenType::Number, number};
                }
                if (string("+-*/%`!~<>=&|^").find(c) != string::npos) {
                    string op(1, c);
                    static const vector<string> ops = {"!&&", "!||", "<<=", ">>=", "++", "--", "==", "!=", "<=", ">=", "<<", ">>", "&&", "||", "^^", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^="};
                    for (const auto& candidate : ops) {
                        if (candidate[0] == c && source.compare(position, candidate.size() - 1, candidate, 1, candidate.size() - 1) == 0) {
                            position += candidate.size() - 1;
                            return {TokenType::Operator, candidate};
                        }
                    }
                    return {c == '=' ? TokenType::Assign : TokenType::Operator, op};
                }

                switch (c) {
                case ';':
                    return { TokenType::EndOfStatement, ";" };

                case ',':
                    return { TokenType::Comma, "," };

                case '.':
                    return { TokenType::Dot, "." };

                case '(':
                    return { TokenType::LeftParen, "(" };

                case ')':
                    return { TokenType::RightParen, ")" };

                case '"':
                    return ParseString(TokenType::String);

                case '{':
                    return {TokenType::LeftBrace, "{"};

                case '}':
                    return {TokenType::RightBrace, "}"};

                default:
                    if (c == 'f' && position < source.size() && source[position] == '"') {
                        ++position; // " を消費
                        return ParseString(TokenType::FormatString);
                    }
                    else if (isIdentifier(c)) {
                        string name(1, c);

                        while (
                            position < source.size() && (
                                isIdentifier(source[position]) ||
                                std::isdigit(static_cast<unsigned char>(source[position]))
                            )
                        ) {
                            name += source[position++];
                        }

                        if (ReservedWord.contains(name)) {
                            return {TokenType::Reserved, name};
                        }
                        else if (StandardType.contains(name)) {
                            return {TokenType::TypeName, name};
                        }
                        else {
                            return {TokenType::Identifier, name};
                        }
                    }
                    else {
                        string name(1, c);
                        while (
                            position < source.size() && !isWhitespace(source[position]) &&
                            source[position] != ';'
                        ) {
                            name += source[position++];
                        }

                        if (isNumeric(name)) {
                            return { TokenType::Number, name };
                        }
                        else {
                            return { TokenType::Unknown, name };
                        }
                    }
                }
            }
            return { TokenType::EndOfFile, "" };
        }
    };

    class Parser {
    private:
        Lexer& lexer;
        Token currentToken;

        void Advance() {
            try {
                currentToken = lexer.NextToken();
            }
            catch (const std::exception& e) {
                throw std::runtime_error(std::format("Lexer Error: {}\n", e.what()));
            }
        }

        void Expect(TokenType type) {
            if (currentToken.type != type) {
                throw std::runtime_error("Unexpected token");
            }
        }

        static int Precedence(const string& op) {
            if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=") return 1;
            if (op == "!||") return 2;
            if (op == "!&&") return 3;
            if (op == "||") return 4;
            if (op == "^^") return 5;
            if (op == "&&") return 6;
            if (op == "|") return 7;
            if (op == "^") return 8;
            if (op == "&") return 9;
            if (op == "==" || op == "!=") return 10;
            if (op == "<" || op == "<=" || op == ">" || op == ">=") return 11;
            if (op == "<<" || op == ">>") return 12;
            if (op == "+" || op == "-") return 13;
            if (op == "*" || op == "/" || op == "%") return 14;
            if (op == "`") return 15;
            return 0;
        }

        unique_ptr<Expression> ParseExpression(int minPrecedence = 1) {
            unique_ptr<Expression> left;
            if (currentToken.type == TokenType::Operator &&
                (currentToken.value == "+" || currentToken.value == "-" || currentToken.value == "!" || currentToken.value == "~" || currentToken.value == "++" || currentToken.value == "--")) {
                auto unary = std::make_unique<UnaryExpr>();
                unary->op = currentToken.value;
                Advance();
                unary->operand = ParseExpression(16);
                left = std::move(unary);
            } else if (currentToken.value == "if") {
                auto value = std::make_unique<IfExpr>();
                Advance();
                Expect(TokenType::LeftParen); Advance();
                value->condition = ParseExpression();
                Expect(TokenType::RightParen); Advance();
                value->thenBranch = ParseBlock();
                if (currentToken.value != "else") throw std::runtime_error("if expression requires else");
                Advance();
                if (currentToken.value == "if") {
                    value->elseBranch = std::make_unique<Block>();
                    ParseStatement(value->elseBranch->statements);
                } else value->elseBranch = ParseBlock();
                left = std::move(value);
            } else if (currentToken.type == TokenType::LeftBrace) {
                auto value = std::make_unique<BlockExpr>();
                value->block = ParseBlock();
                left = std::move(value);
            } else if (currentToken.type == TokenType::LeftParen) {
                Advance();
                left = ParseExpression();
                Expect(TokenType::RightParen);
                Advance();
            } else if (currentToken.type == TokenType::Number) {
                auto value = std::make_unique<NumericLiteral>();
                value->value = currentToken.value;
                Advance();
                left = std::move(value);
            } else if (currentToken.type == TokenType::Identifier) {
                if (currentToken.value == "true" || currentToken.value == "false") {
                    auto value = std::make_unique<BooleanLiteral>();
                    value->value = currentToken.value == "true";
                    left = std::move(value);
                } else {
                    auto value = std::make_unique<IdentifierExpr>();
                    value->name = currentToken.value;
                    left = std::move(value);
                }
                Advance();
            } else {
                throw std::runtime_error("Expected expression, found: " + currentToken.value);
            }
            while (true) {
                if (currentToken.type == TokenType::Operator && (currentToken.value == "++" || currentToken.value == "--") && minPrecedence <= 17) {
                    auto unary = std::make_unique<UnaryExpr>();
                    unary->op = currentToken.value;
                    unary->postfix = true;
                    unary->operand = std::move(left);
                    Advance();
                    left = std::move(unary);
                    continue;
                }
                const int precedence = Precedence(currentToken.value);
                if (!precedence || precedence < minPrecedence) break;
                string op = currentToken.value;
                Advance();
                if (precedence == 10 || precedence == 11) {
                    auto chain = std::make_unique<ComparisonExpr>();
                    chain->operands.push_back(std::move(left));
                    do {
                        chain->operators.push_back(op);
                        chain->operands.push_back(ParseExpression(12));
                        if (Precedence(currentToken.value) != 10 && Precedence(currentToken.value) != 11) break;
                        op = currentToken.value;
                        Advance();
                    } while (true);
                    left = std::move(chain);
                } else {
                    auto binary = std::make_unique<BinaryExpr>();
                    binary->op = op;
                    binary->left = std::move(left);
                    binary->right = ParseExpression(precedence + (precedence == 1 || op == "`" ? 0 : 1));
                    left = std::move(binary);
                }
            }
            return left;
        }

        void ParseStringStatement(vector<unique_ptr<ASTNode>>& destination) {
            Expect(TokenType::String);
            string v = currentToken.value;
            Advance();

            Expect(TokenType::EndOfStatement);
            Advance();

            StringStatement s;
            s.value = v;
            destination.push_back(std::make_unique<StringStatement>(std::move(s)));
        }

        void ParseFormatStringStatement(vector<unique_ptr<ASTNode>>& destination) {
            Expect(TokenType::FormatString);

            const string& source = currentToken.value;
            FormatStringStatement statement;

            size_t position = 0;

            while (position < source.size()) {
                // 通常テキストを読み取る
                size_t open = source.find('{', position);

                if (open == string::npos) {
                    if (position < source.size()) {
                        auto text = std::make_unique<FormatText>();
                        text->value = source.substr(position);
                        statement.parts.push_back(std::move(text));
                    }
                    break;
                }

                // { より前のテキスト
                if (open > position) {
                    auto text = std::make_unique<FormatText>();
                    text->value = source.substr(position, open - position);
                    statement.parts.push_back(std::move(text));
                }

                // 対応する } を探す
                size_t close = source.find('}', open + 1);

                if (close == string::npos) {
                    throw std::runtime_error("Unclosed format expression");
                }

                string name = source.substr(open + 1, close - open - 1);

                if (name.empty() || !isIdentifier(name[0])) {
                    throw std::runtime_error("Expected identifier in format expression");
                }

                for (size_t i = 1; i < name.size(); ++i) {
                    if (!isIdentifier(name[i]) &&
                        !std::isdigit(static_cast<unsigned char>(name[i])))
                    {
                        throw std::runtime_error("Invalid identifier in format expression");
                    }
                }

                auto identifier = std::make_unique<IdentifierExpr>();
                identifier->name = std::move(name);
                statement.parts.push_back(std::move(identifier));

                position = close + 1;
            }

            Advance();

            Expect(TokenType::EndOfStatement);
            Advance();

            destination.push_back(
                std::make_unique<FormatStringStatement>(std::move(statement))
            );
        }

        std::unique_ptr<Block> ParseBlock() {
            Expect(TokenType::LeftBrace); Advance();
            auto block = std::make_unique<Block>();
            while (currentToken.type != TokenType::RightBrace) {
                if (currentToken.type == TokenType::EndOfFile) throw std::runtime_error("Unexpected EOF in block");
                ParseStatement(block->statements);
            }
            Advance();
            return block;
        }

        void ParseStatement(vector<unique_ptr<ASTNode>>& destination) {
            if (currentToken.type == TokenType::String) { ParseStringStatement(destination); return; }
            if (currentToken.type == TokenType::FormatString) { ParseFormatStringStatement(destination); return; }
            if (currentToken.type == TokenType::TypeName || currentToken.value == "const") { ParseVariableDeclaration(destination); return; }
            if (currentToken.type == TokenType::LeftBrace) { destination.push_back(ParseBlock()); return; }
            if (currentToken.value == "return") {
                Advance();
                auto statement = std::make_unique<ReturnStatement>();
                if (currentToken.type != TokenType::EndOfStatement) statement->value = ParseExpression();
                Expect(TokenType::EndOfStatement); Advance();
                destination.push_back(std::move(statement)); return;
            }
            if (currentToken.value == "break" || currentToken.value == "continue") {
                auto control = std::make_unique<LoopControl>();
                control->isBreak = currentToken.value == "break";
                Advance(); Expect(TokenType::EndOfStatement); Advance();
                destination.push_back(std::move(control)); return;
            }
            if (currentToken.value == "if") {
                auto statement = std::make_unique<IfStatement>();
                Advance(); Expect(TokenType::LeftParen); Advance();
                statement->condition = ParseExpression();
                Expect(TokenType::RightParen); Advance();
                statement->thenBranch = ParseBlock();
                if (currentToken.value == "else") {
                    Advance();
                    if (currentToken.value == "if") {
                        statement->elseBranch = std::make_unique<Block>();
                        ParseStatement(statement->elseBranch->statements);
                    } else statement->elseBranch = ParseBlock();
                }
                destination.push_back(std::move(statement)); return;
            }
            if (currentToken.value == "while" || currentToken.value == "for" || currentToken.value == "do") {
                auto loop = std::make_unique<LoopStatement>();
                const string kind = currentToken.value;
                loop->kind = kind == "for" ? LoopStatement::Kind::For : kind == "do" ? LoopStatement::Kind::DoWhile : LoopStatement::Kind::While;
                Advance();
                if (kind != "do") {
                    Expect(TokenType::LeftParen); Advance();
                    if (kind == "for") {
                        if (currentToken.type == TokenType::TypeName || currentToken.value == "const") {
                            vector<unique_ptr<ASTNode>> init;
                            ParseVariableDeclaration(init);
                            loop->initializer = std::move(init.front());
                        } else {
                            if (currentToken.type != TokenType::EndOfStatement) loop->initializer = ParseExpression();
                            Expect(TokenType::EndOfStatement); Advance();
                        }
                    }
                    if (kind != "for" || currentToken.type != TokenType::EndOfStatement) loop->condition = ParseExpression();
                    if (kind == "for") {
                        Expect(TokenType::EndOfStatement); Advance();
                        if (currentToken.type != TokenType::RightParen) loop->step = ParseExpression();
                    }
                    Expect(TokenType::RightParen); Advance();
                }
                loop->body = ParseBlock();
                if (kind == "do") {
                    if (currentToken.value != "while") throw std::runtime_error("Expected while after do block");
                    Advance(); Expect(TokenType::LeftParen); Advance();
                    loop->condition = ParseExpression();
                    Expect(TokenType::RightParen); Advance();
                    Expect(TokenType::EndOfStatement); Advance();
                }
                destination.push_back(std::move(loop)); return;
            }
            auto expression = ParseExpression();
            Expect(TokenType::EndOfStatement); Advance();
            destination.push_back(std::move(expression));
        }

        void ParseFunctionStatement() {
            Expect(TokenType::TypeName);
            string typev = currentToken.value;
            Advance();

            Expect(TokenType::Identifier);
            string ident = currentToken.value;
            Advance();

            Expect(TokenType::LeftParen);
            Advance();

            Expect(TokenType::RightParen);
            Advance();

            FunctionDeclaration function;
            function.name = ident;
            function.returnType = typev;

            function.body = std::move(ParseBlock()->statements);

            program.children.push_back(
                std::make_unique<FunctionDeclaration>(std::move(function))
            );
        }

        void ParseVariableDeclaration(vector<unique_ptr<ASTNode>>& destination) {
            VariableDeclaration variable;

            if (currentToken.type == TokenType::Reserved &&
                currentToken.value == "const") {
                variable.isConst = true;
                Advance();
            }

            Expect(TokenType::TypeName);
            variable.type = currentToken.value;
            Advance();

            if (currentToken.type == TokenType::Reserved &&
                currentToken.value == "mutable") {
                if (variable.isConst) {
                    throw std::runtime_error("const and mutable cannot coexist");
                }

                variable.isMutable = true;
                Advance();
            }

            Expect(TokenType::Identifier);
            variable.name = currentToken.value;
            Advance();

            Expect(TokenType::Assign);
            Advance();

            variable.initialValue = ParseExpression();

            Expect(TokenType::EndOfStatement);
            Advance();

            destination.push_back(
                std::make_unique<VariableDeclaration>(std::move(variable))
            );
        }

    public:
        Program program;
        Parser(Lexer& lexer)
            : lexer(lexer) {
            Advance();
        }

        void ParseProgram() {
            while (currentToken.type != TokenType::EndOfFile) {
                if (currentToken.type == TokenType::String) {
                    ParseStringStatement(program.children);
                }
                else if (currentToken.type == TokenType::TypeName) {
                    Lexer lookahead = lexer;
                    Token next = lookahead.NextToken();
                    if (next.value == "mutable") next = lookahead.NextToken();
                    if (next.type != TokenType::Identifier) throw std::runtime_error("Expected identifier");
                    if (lookahead.NextToken().type == TokenType::LeftParen) ParseFunctionStatement();
                    else ParseVariableDeclaration(program.children);
                }
                else if (currentToken.value == "const") {
                    ParseVariableDeclaration(program.children);
                }
                else if (currentToken.type == TokenType::FormatString) {
                    ParseFormatStringStatement(program.children);
                }
                else {
                    ParseStatement(program.children);
                }
            }
        }
    };

    try {
        print("Compiling {}...\n", target);

        print("Parsing program...\n");
        Lexer lexer(source);
        Parser parser(lexer);

        parser.ParseProgram();

        print("Generating IR...\n");
        LLVM llvm;
        llvm.Generate(parser.program);
        llvm.Print();

        print("Generating Binary...\n");
        auto objectPath = std::filesystem::path(target);
        objectPath.replace_extension(".obj");

        auto executablePath = std::filesystem::path(target);
        executablePath.replace_extension(".exe");

        llvm.WriteObject(objectPath);
        llvm.LinkExecutable(objectPath, executablePath);

        print("Generated {}\n", executablePath.string());
    }
    catch (const std::exception& e) {
        print("Compilation Error: {}\n", e.what());
        return -1;
    }

    print("Compiled!\n");

    return 0;
}
