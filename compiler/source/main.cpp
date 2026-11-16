#include <iostream>
#include <LLVM.h>
#include "Semantic.h"
#include "OwnershipChecker.h"
#include <print>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <filesystem>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <fcntl.h>
#include <io.h>
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
    Colon,
    At,
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
    {"float", true},
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
    {"struct", true}, {"class", true}, {"public", true}, {"private", true},
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
    size_t offset = 0;
};

constexpr int PostfixPrecedence = 17;

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

struct CommandLine {
    std::vector<string> inputs;
    string output;
    bool help = false, version = false;
};

CommandLine ParseCommandLine(int argc, char** argv) {
    CommandLine result;
    for (int i = 1; i < argc;) {
        string arg = argv[i++];
        if (arg == "-h" || arg == "--help") result.help = true;
        else if (arg == "-v" || arg == "--version") result.version = true;
        else if (arg == "-i" || arg == "--input") {
            size_t before = result.inputs.size();
            while (i < argc && argv[i][0] != '-') {
                if (argv[i][0] == '\0') throw std::runtime_error("Input option requires a non-empty file name");
                result.inputs.emplace_back(argv[i++]);
            }
            if (result.inputs.size() == before) throw std::runtime_error("Input option requires at least one file");
        } else if (arg == "-o" || arg == "--output") {
            if (!result.output.empty()) throw std::runtime_error("Output option specified more than once");
            if (i == argc || argv[i][0] == '-' || argv[i][0] == '\0') throw std::runtime_error("Output option requires a file");
            result.output = argv[i++];
        } else if (!arg.empty() && arg[0] != '-' && argc == 2) result.inputs.push_back(arg);
        else throw std::runtime_error("Unknown argument: " + arg);
    }
    if (!result.help && !result.version && result.inputs.empty()) throw std::runtime_error("No input files specified");
    return result;
}

int main(int argc, char** argv) {
    bool symbols = argc == 4 && string(argv[1]) == "--symbols";
    bool check = (argc == 3 && string(argv[1]) == "--check") || symbols;
    CommandLine options;
    if (!check) {
        try { options = ParseCommandLine(argc, argv); }
        catch (const std::exception& e) { std::cerr << "c4rc: " << e.what() << '\n'; return 1; }
        if (options.help) {
            std::cout << "Usage: c4rc -i <source...> [-i <source...>] [-o <output>]\n"
                         "  -i, --input    Input source files (repeatable)\n"
                         "  -o, --output   Output executable\n"
                         "  -h, --help     Show this help\n"
                         "  -v, --version  Show compiler version\n"
                         "       --check <source|-> | --symbols <offset> -\n";
            return 0;
        }
        if (options.version) { std::cout << "c4rc " << C4R_VERSION << '\n'; return 0; }
    }
    string source = "";
    vector<string> sourceFiles;
    string target = check ? argv[symbols ? 3 : 2] : options.inputs.front();

    {
        if (check && target == "-") {
            _setmode(_fileno(stdin), _O_BINARY);
            source.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
        } else {
        auto inputs = check ? vector<string>{target} : options.inputs;
        for (const auto& input : inputs) {
            std::ifstream file(input, std::ios::binary);
            if (!file) { std::cerr << "Failed to open source file: " << input << '\n'; return 1; }
            string contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            sourceFiles.push_back(contents);
            source += contents + '\n';
        }
        }
    }

    class Lexer {
    private:
        const std::string& source;
        size_t position = 0;
        size_t tokenStart = 0;

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
            Token token = ReadToken();
            token.offset = tokenStart;
            return token;
        }
        Token ReadToken() {
            while (position < source.size()) {
                char c = source[position++];

                if (isWhitespace(c)) {
                    continue;
                }
                tokenStart = position - 1;

                if (c == '/' && position < source.size() && source[position] == '/') {
                    while (position < source.size() && source[position] != '\n') ++position;
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    string number(1, c);
                    while (position < source.size() && std::isdigit(static_cast<unsigned char>(source[position])))
                        number += source[position++];
                    if (position + 1 < source.size() && source[position] == '.' &&
                        std::isdigit(static_cast<unsigned char>(source[position + 1]))) {
                        number += source[position++];
                        while (position < source.size() && std::isdigit(static_cast<unsigned char>(source[position])))
                            number += source[position++];
                    }
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
                case ':':
                    return { TokenType::Colon, ":" };
                case '@':
                    return { TokenType::At, "@" };

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
            tokenStart = position;
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
            const size_t expressionOffset = currentToken.offset;
            unique_ptr<Expression> left;
            if (currentToken.type == TokenType::Operator &&
                (currentToken.value == "+" || currentToken.value == "-" || currentToken.value == "!" || currentToken.value == "~" || currentToken.value == "&" || currentToken.value == "++" || currentToken.value == "--")) {
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
            } else if (currentToken.type == TokenType::TypeName) {
                auto value = std::make_unique<ConstructExpr>();
                value->type = currentToken.value;
                Advance(); Expect(TokenType::LeftParen); Advance();
                if (currentToken.type != TokenType::RightParen) {
                    do {
                        value->arguments.push_back(ParseExpression());
                        if (currentToken.type != TokenType::Comma) break;
                        Advance();
                    } while (true);
                }
                Expect(TokenType::RightParen); Advance();
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
            left->offset = expressionOffset;
            while (true) {
                if (currentToken.type == TokenType::Dot && minPrecedence <= PostfixPrecedence) {
                    Advance();
                    Expect(TokenType::Identifier);
                    auto member = std::make_unique<MemberExpr>();
                    member->object = std::move(left);
                    member->name = currentToken.value;
                    member->offset = expressionOffset;
                    Advance();
                    left = std::move(member);
                    continue;
                }
                if (currentToken.type == TokenType::LeftParen && minPrecedence <= PostfixPrecedence) {
                    auto* identifier = dynamic_cast<IdentifierExpr*>(left.get());
                    auto* member = dynamic_cast<MemberExpr*>(left.get());
                    if (!identifier && !member) throw std::runtime_error("Function call requires a function name");
                    if (identifier && identifier->name == "move") {
                        auto move = std::make_unique<MoveExpr>();
                        move->offset = expressionOffset;
                        Advance();
                        move->operand = ParseExpression();
                        Expect(TokenType::RightParen); Advance();
                        left = std::move(move);
                        continue;
                    }
                    auto call = std::make_unique<CallExpr>();
                    if (identifier) call->name = identifier->name;
                    else { call->name = member->name; call->object = std::move(member->object); }
                    Advance();
                    if (currentToken.type != TokenType::RightParen) {
                        do {
                            call->arguments.push_back(ParseExpression());
                            if (currentToken.type != TokenType::Comma) break;
                            Advance();
                        } while (true);
                    }
                    Expect(TokenType::RightParen); Advance();
                    left = std::move(call);
                    continue;
                }
                if (currentToken.type == TokenType::Operator && (currentToken.value == "++" || currentToken.value == "--") && minPrecedence <= PostfixPrecedence) {
                    auto unary = std::make_unique<UnaryExpr>();
                    unary->op = currentToken.value;
                    unary->postfix = true;
                    unary->operand = std::move(left);
                    Advance();
                    left = std::move(unary);
                    left->offset = expressionOffset;
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
                    left->offset = expressionOffset;
                } else {
                    auto binary = std::make_unique<BinaryExpr>();
                    binary->op = op;
                    binary->left = std::move(left);
                    binary->right = ParseExpression(precedence + (precedence == 1 || op == "`" ? 0 : 1));
                    left = std::move(binary);
                    left->offset = expressionOffset;
                }
            }
            left->offset = expressionOffset;
            return left;
        }

        void ParseStringStatement(vector<unique_ptr<ASTNode>>& destination) {
            Expect(TokenType::String);
            size_t offset = currentToken.offset;
            string v = currentToken.value;
            Advance();

            Expect(TokenType::EndOfStatement);
            Advance();

            StringStatement s;
            s.offset = offset;
            s.value = v;
            destination.push_back(std::make_unique<StringStatement>(std::move(s)));
        }

        void ParseFormatStringStatement(vector<unique_ptr<ASTNode>>& destination) {
            Expect(TokenType::FormatString);

            const string& source = currentToken.value;
            FormatStringStatement statement;
            statement.offset = currentToken.offset;

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
            auto block = std::make_unique<Block>();
            block->offset = currentToken.offset;
            Expect(TokenType::LeftBrace); Advance();
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
            if (currentToken.type == TokenType::Identifier) {
                Lexer lookahead = lexer;
                Token next = lookahead.NextToken();
                while (next.type == TokenType::Operator && next.value == "*") next = lookahead.NextToken();
                if (next.value == "mutable") next = lookahead.NextToken();
                if (next.type == TokenType::Identifier) throw std::runtime_error("Unknown type: " + currentToken.value);
            }
            if (currentToken.type == TokenType::TypeName) {
                Lexer lookahead = lexer;
                Token name = lookahead.NextToken();
                while (name.type == TokenType::Operator && name.value == "*") name = lookahead.NextToken();
                if (name.value == "mutable") name = lookahead.NextToken();
                if (name.type == TokenType::Operator && name.value == "&&") name = lookahead.NextToken();
                if (name.type == TokenType::Operator && name.value == "&") {
                    name = lookahead.NextToken();
                    if (name.type == TokenType::At) { name = lookahead.NextToken(); name = lookahead.NextToken(); }
                }
                if (name.type != TokenType::LeftParen) {
                    if (name.type == TokenType::Identifier && lookahead.NextToken().type == TokenType::LeftParen) ParseFunctionStatement(destination);
                    else ParseVariableDeclaration(destination);
                    return;
                }
            }
            if (currentToken.value == "const") { ParseVariableDeclaration(destination); return; }
            if (currentToken.value == "capture") {
                auto capture = std::make_unique<CaptureDeclaration>();
                capture->offset = currentToken.offset;
                Advance();
                do {
                    Expect(TokenType::Identifier);
                    capture->names.push_back(currentToken.value); Advance();
                    if (currentToken.type != TokenType::Comma) break;
                    Advance();
                } while (true);
                Expect(TokenType::EndOfStatement); Advance();
                destination.push_back(std::move(capture)); return;
            }
            if (currentToken.type == TokenType::LeftBrace) { destination.push_back(ParseBlock()); return; }
            if (currentToken.value == "return") {
                size_t offset = currentToken.offset;
                Advance();
                auto statement = std::make_unique<ReturnStatement>();
                statement->offset = offset;
                if (currentToken.type != TokenType::EndOfStatement) statement->value = ParseExpression();
                Expect(TokenType::EndOfStatement); Advance();
                destination.push_back(std::move(statement)); return;
            }
            if (currentToken.value == "break" || currentToken.value == "continue") {
                auto control = std::make_unique<LoopControl>();
                control->offset = currentToken.offset;
                control->isBreak = currentToken.value == "break";
                Advance(); Expect(TokenType::EndOfStatement); Advance();
                destination.push_back(std::move(control)); return;
            }
            if (currentToken.value == "if") {
                auto statement = std::make_unique<IfStatement>();
                statement->offset = currentToken.offset;
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
                loop->offset = currentToken.offset;
                const string kind = currentToken.value;
                loop->kind = kind == "for" ? LoopStatement::Kind::For : kind == "do" ? LoopStatement::Kind::DoWhile : LoopStatement::Kind::While;
                Advance();
                if (kind != "do") {
                    Expect(TokenType::LeftParen); Advance();
                    if (kind == "for") {
                        Lexer initLookahead = lexer;
                        bool typeDeclaration = currentToken.type == TokenType::TypeName && initLookahead.NextToken().type != TokenType::LeftParen;
                        if (typeDeclaration || currentToken.value == "const") {
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

        vector<FunctionDeclaration::Parameter> ParseParameters(bool mustBeEmpty = false) {
            vector<FunctionDeclaration::Parameter> parameters;
            if (mustBeEmpty && currentToken.type != TokenType::RightParen)
                throw std::runtime_error("Destructor cannot have parameters");
            if (currentToken.type != TokenType::RightParen) {
                do {
                    FunctionDeclaration::Parameter parameter;
                    parameter.offset = currentToken.offset;
                    if (currentToken.type == TokenType::Identifier) throw std::runtime_error("Unknown type: " + currentToken.value);
                    Expect(TokenType::TypeName); parameter.type = currentToken.value; Advance();
                    while (currentToken.type == TokenType::Operator && currentToken.value == "*") { parameter.type += "*"; Advance(); }
                    if (currentToken.value == "mutable") { parameter.isMutable = true; Advance(); }
                    if (currentToken.type == TokenType::Operator && currentToken.value == "&&") {
                        parameter.isConsumable = true;
                        Advance();
                    } else if (currentToken.type == TokenType::Operator && currentToken.value == "&") {
                        parameter.isReference = true;
                        parameter.isMutableReference = parameter.isMutable;
                        Advance();
                        if (currentToken.type == TokenType::At) {
                            Advance(); Expect(TokenType::Identifier); parameter.lifetime = currentToken.value; Advance();
                        }
                    }
                    Expect(TokenType::Identifier); parameter.name = currentToken.value; Advance();
                    parameters.push_back(std::move(parameter));
                    if (currentToken.type != TokenType::Comma) break;
                    Advance();
                } while (true);
            }
            Expect(TokenType::RightParen); Advance();
            return parameters;
        }

        void ParseFunctionStatement(vector<unique_ptr<ASTNode>>& destination) {
            Expect(TokenType::TypeName);
            const size_t declarationOffset = currentToken.offset;
            string typev = currentToken.value;
            Advance();
            while (currentToken.type == TokenType::Operator && currentToken.value == "*") { typev += "*"; Advance(); }

            FunctionDeclaration function;
            function.offset = declarationOffset;
            function.returnType = typev;
            if (currentToken.value == "mutable") { function.returnsMutableReference = true; Advance(); }
            if (currentToken.type == TokenType::Operator && currentToken.value == "&&") {
                function.returnsConsumable = true;
                Advance();
            } else if (currentToken.type == TokenType::Operator && currentToken.value == "&") {
                function.returnsReference = true;
                Advance();
                if (currentToken.type == TokenType::At) {
                    Advance(); Expect(TokenType::Identifier); function.returnLifetime = currentToken.value; Advance();
                }
            } else if (function.returnsMutableReference) {
                throw std::runtime_error("mutable return requires a reference type");
            }

            Expect(TokenType::Identifier);
            string ident = currentToken.value;
            Advance();

            Expect(TokenType::LeftParen);
            Advance();

            function.name = ident;

            function.parameters = ParseParameters();

            function.body = std::move(ParseBlock()->statements);

            destination.push_back(
                std::make_unique<FunctionDeclaration>(std::move(function))
            );
        }

        void ParseVariableDeclaration(vector<unique_ptr<ASTNode>>& destination, bool field = false) {
            VariableDeclaration variable;
            variable.offset = currentToken.offset;

            if (currentToken.type == TokenType::Reserved &&
                currentToken.value == "const") {
                variable.isConst = true;
                Advance();
            }

            if (currentToken.type == TokenType::Identifier) throw std::runtime_error("Unknown type: " + currentToken.value);
            Expect(TokenType::TypeName);
            variable.type = currentToken.value;
            Advance();
            while (currentToken.type == TokenType::Operator && currentToken.value == "*") { variable.type += "*"; Advance(); }

            if (currentToken.type == TokenType::Reserved &&
                currentToken.value == "mutable") {
                if (variable.isConst) {
                    throw std::runtime_error("const and mutable cannot coexist");
                }

                variable.isMutable = true;
                Advance();
            }

            if (currentToken.type == TokenType::Operator && currentToken.value == "&&") {
                variable.isConsumable = true;
                Advance();
            } else if (currentToken.type == TokenType::Operator && currentToken.value == "&") {
                variable.isReference = true;
                variable.isMutableReference = variable.isMutable;
                Advance();
            }

            Expect(TokenType::Identifier);
            variable.name = currentToken.value;
            Advance();

            if (currentToken.type == TokenType::Assign) {
                Advance();
                variable.initialValue = ParseExpression();
            } else if (currentToken.type == TokenType::LeftBrace && !field) {
                Advance(); Expect(TokenType::RightBrace); Advance();
                variable.aggregateInit = true;
            } else if (!field) throw std::runtime_error("Variable requires an initializer");

            Expect(TokenType::EndOfStatement);
            Advance();

            destination.push_back(
                std::make_unique<VariableDeclaration>(std::move(variable))
            );
        }

        unique_ptr<FunctionDeclaration> ParseSpecialMember(const string& typeName, bool destructor) {
            const size_t offset = currentToken.offset;
            if (destructor) Advance();
            if (currentToken.value != typeName || currentToken.type != TokenType::TypeName)
                throw std::runtime_error("Special member name must match the enclosing type");
            Advance(); Expect(TokenType::LeftParen); Advance();
            unique_ptr<FunctionDeclaration> declaration = destructor
                ? unique_ptr<FunctionDeclaration>(std::make_unique<DestructorDeclaration>())
                : unique_ptr<FunctionDeclaration>(std::make_unique<ConstructorDeclaration>());
            declaration->offset = offset;
            declaration->name = typeName;
            declaration->returnType = "void";
            declaration->parameters = ParseParameters(destructor);
            declaration->body = std::move(ParseBlock()->statements);
            return declaration;
        }

        void ParseTypeDeclaration() {
            auto type = std::make_unique<TypeDeclaration>();
            type->isStruct = currentToken.value == "struct";
            type->offset = currentToken.offset;
            Advance();
            if (currentToken.type != TokenType::Identifier && currentToken.type != TokenType::TypeName) throw std::runtime_error("Expected type name");
            type->name = currentToken.value;
            Advance(); Expect(TokenType::LeftBrace); Advance();
            bool access = type->isStruct;
            while (currentToken.type != TokenType::RightBrace) {
                if (currentToken.type == TokenType::EndOfFile) throw std::runtime_error("Unexpected EOF in type");
                bool memberAccess = access;
                if (currentToken.value == "public" || currentToken.value == "private") {
                    memberAccess = currentToken.value == "public";
                    Advance();
                    if (currentToken.type == TokenType::Colon) { access = memberAccess; Advance(); continue; }
                }
                vector<unique_ptr<ASTNode>> member;
                if (currentToken.type == TokenType::Operator && currentToken.value == "~") {
                    member.push_back(ParseSpecialMember(type->name, true));
                    type->members.push_back({memberAccess, std::move(member.front())});
                    continue;
                }
                Lexer lookahead = lexer;
                Token name = lookahead.NextToken();
                if (currentToken.type == TokenType::TypeName && currentToken.value == type->name && name.type == TokenType::LeftParen) {
                    member.push_back(ParseSpecialMember(type->name, false));
                    type->members.push_back({memberAccess, std::move(member.front())});
                    continue;
                }
                while (name.type == TokenType::Operator && name.value == "*") name = lookahead.NextToken();
                if (name.value == "mutable") name = lookahead.NextToken();
                if (name.type == TokenType::Operator && name.value == "&") {
                    name = lookahead.NextToken();
                    if (name.type == TokenType::At) { name = lookahead.NextToken(); name = lookahead.NextToken(); }
                }
                if (currentToken.type == TokenType::TypeName && name.type == TokenType::Identifier && lookahead.NextToken().type == TokenType::LeftParen)
                    ParseFunctionStatement(member);
                else ParseVariableDeclaration(member, true);
                type->members.push_back({memberAccess, std::move(member.front())});
            }
            Advance(); Expect(TokenType::EndOfStatement); Advance();
            program.children.push_back(std::move(type));
        }

    public:
        size_t DiagnosticOffset() const { return currentToken.offset; }
        Program program;
        Parser(Lexer& lexer)
            : lexer(lexer) {
            Advance();
        }

        void ParseProgram() {
            while (currentToken.type != TokenType::EndOfFile) {
                if (currentToken.value == "struct" || currentToken.value == "class") {
                    ParseTypeDeclaration();
                } else if (currentToken.type == TokenType::String) {
                    ParseStringStatement(program.children);
                }
                else if (currentToken.type == TokenType::TypeName) {
                    Lexer lookahead = lexer;
                    Token next = lookahead.NextToken();
                    while (next.type == TokenType::Operator && next.value == "*") next = lookahead.NextToken();
                    if (next.value == "mutable") next = lookahead.NextToken();
                    if (next.type == TokenType::Operator && next.value == "&&") next = lookahead.NextToken();
                    if (next.type == TokenType::Operator && next.value == "&") {
                        next = lookahead.NextToken();
                        if (next.type == TokenType::At) { next = lookahead.NextToken(); next = lookahead.NextToken(); }
                    }
                    if (next.type != TokenType::Identifier) throw std::runtime_error("Expected identifier");
                    if (lookahead.NextToken().type == TokenType::LeftParen) ParseFunctionStatement(program.children);
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

    size_t diagnosticOffset = 0;
    try {
        if (symbols) {
            size_t limit = std::min(static_cast<size_t>(std::stoull(argv[2])), source.size());
            string prefix = source.substr(0, limit);
            Lexer lexer(prefix);
            vector<Token> tokens;
            try {
                while (true) {
                    Token token = lexer.NextToken();
                    if (token.type == TokenType::EndOfFile) break;
                    tokens.push_back(std::move(token));
                }
            } catch (const std::exception&) { /* Incomplete input is normal while editing. */ }
            struct Symbol { string kind, name, detail; };
            vector<vector<Symbol>> scopes(1);
            vector<Symbol> parameters;
            for (size_t i = 0; i < tokens.size(); ++i) {
                if (tokens[i].type == TokenType::LeftBrace) {
                    scopes.push_back(std::move(parameters)); parameters.clear();
                } else if (tokens[i].type == TokenType::RightBrace) {
                    if (scopes.size() > 1) scopes.pop_back();
                    parameters.clear();
                } else if (tokens[i].type == TokenType::TypeName && i + 1 < tokens.size()) {
                    size_t j = i + 1;
                    string pointerSuffix;
                    while (j < tokens.size() && tokens[j].type == TokenType::Operator && tokens[j].value == "*") { pointerSuffix += "*"; ++j; }
                    bool isMutable = tokens[j].value == "mutable";
                    if (isMutable) ++j;
                    bool isReference = j < tokens.size() && tokens[j].type == TokenType::Operator && tokens[j].value == "&";
                    if (isReference) {
                        ++j;
                        if (j < tokens.size() && tokens[j].type == TokenType::At) j += 2;
                    }
                    if (j >= tokens.size() || tokens[j].type != TokenType::Identifier) continue;
                    string type = tokens[i].value + pointerSuffix, name = tokens[j].value;
                    if (j + 1 < tokens.size() && tokens[j + 1].type == TokenType::LeftParen) {
                        string detail = type + (isMutable ? " mutable" : "") + (isReference ? "&" : "") + " " + name + "(";
                        vector<Symbol> args;
                        size_t k = j + 2;
                        while (k < tokens.size() && tokens[k].type != TokenType::RightParen) {
                            if (tokens[k].type != TokenType::TypeName || k + 1 >= tokens.size()) break;
                            string argType = tokens[k++].value;
                            while (k < tokens.size() && tokens[k].type == TokenType::Operator && tokens[k].value == "*") { argType += "*"; ++k; }
                            bool argMutable = tokens[k].value == "mutable";
                            if (argMutable) ++k;
                            bool argReference = k < tokens.size() && tokens[k].type == TokenType::Operator && tokens[k].value == "&";
                            if (argReference) {
                                ++k;
                                if (k < tokens.size() && tokens[k].type == TokenType::At) k += 2;
                            }
                            if (k >= tokens.size() || tokens[k].type != TokenType::Identifier) break;
                            if (!args.empty()) detail += ", ";
                            detail += argType + (argMutable ? " mutable" : "") + (argReference ? "&" : "") + " " + tokens[k].value;
                            args.push_back({"variable", tokens[k].value, argType + (argMutable ? " mutable" : "") + (argReference ? "&" : "")});
                            ++k;
                            if (k < tokens.size() && tokens[k].type == TokenType::Comma) ++k;
                            else break;
                        }
                        detail += ")";
                        scopes.back().push_back({"function", name, detail});
                        if (k < tokens.size() && tokens[k].type == TokenType::RightParen) { parameters = std::move(args); i = k; }
                    } else {
                        scopes.back().push_back({"variable", name, type + (isMutable ? " mutable" : "") + (isReference ? "&" : "")});
                        i = j;
                    }
                }
            }
            std::unordered_set<string> seen;
            for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
                for (auto symbol = scope->rbegin(); symbol != scope->rend(); ++symbol) {
                    if (seen.insert(symbol->name).second)
                        std::cout << "C4R_SYMBOL\t" << symbol->kind << '\t' << symbol->name << '\t' << symbol->detail << '\n';
                }
            }
            for (const auto& [name, unused] : StandardType) std::cout << "C4R_SYMBOL\ttype\t" << name << "\t" << name << '\n';
            for (const auto& [name, unused] : ReservedWord) std::cout << "C4R_SYMBOL\tkeyword\t" << name << "\t" << name << '\n';
            for (const auto* name : {"true", "false"}) std::cout << "C4R_SYMBOL\tkeyword\t" << name << "\tbool\n";
            return 0;
        }
        std::unordered_map<string, string> typeLocations;
        auto scanFile = [&](const string& text, const string& filename) {
            Lexer scan(text);
            while (true) {
                Token token = scan.NextToken();
                if (token.type == TokenType::EndOfFile) break;
                if (token.type == TokenType::Reserved && (token.value == "struct" || token.value == "class")) {
                    Token name = scan.NextToken();
                    if (name.type != TokenType::Identifier) {
                        diagnosticOffset = name.offset;
                        auto previous = typeLocations.find(name.value);
                        throw std::runtime_error("Invalid or duplicate type name: " + name.value + " (" + filename +
                            (previous == typeLocations.end() ? "" : ", previously " + previous->second) + ")");
                    }
                    typeLocations.emplace(name.value, filename);
                    StandardType[name.value] = true;
                }
            }
        };
        if (check) scanFile(source, target);
        else for (size_t i = 0; i < sourceFiles.size(); ++i) scanFile(sourceFiles[i], options.inputs[i]);
        if (check) {
            Lexer lexer(source);
            Parser parser(lexer);
            try { parser.ParseProgram(); }
            catch (...) { diagnosticOffset = parser.DiagnosticOffset(); throw; }
            AssignSourceFile(parser.program, target);
            Semantic semantic;
            try { semantic.Collect(parser.program); semantic.CheckBodies(parser.program); }
            catch (...) { diagnosticOffset = semantic.DiagnosticOffset(); throw; }
            for (const auto* function : semantic.FunctionsWithMoves()) {
                auto violations = OwnershipChecker(semantic).checkFunction(*function);
                if (!violations.empty()) {
                    diagnosticOffset = violations.front().offset;
                    throw std::runtime_error(violations.front().action == OwnershipFlow::Action::Reinitialize
                        ? "Copying a destructible value is not permitted: target is not definitely moved"
                        : "Object may have already been moved or be uninitialized");
                }
            }
            LLVM llvm;
            try { llvm.Generate(parser.program, semantic); }
            catch (...) { diagnosticOffset = llvm.DiagnosticOffset(); throw; }
            return 0;
        }
        print("Compiling {}...\n", target);
        print("Parsing program...\n");
        Program program;
        for (size_t i = 0; i < sourceFiles.size(); ++i) {
            const auto& text = sourceFiles[i];
            Lexer lexer(text);
            Parser parser(lexer);
            try { parser.ParseProgram(); }
            catch (const std::exception& e) { throw std::runtime_error(options.inputs[i] + ":" + std::to_string(parser.DiagnosticOffset()) + ": " + e.what()); }
            AssignSourceFile(parser.program, options.inputs[i]);
            for (auto& node : parser.program.children) {
                program.children.push_back(std::move(node));
            }
        }

        Semantic semantic;
        try { semantic.Collect(program, options.inputs.size() > 1); semantic.CheckBodies(program); }
        catch (const std::exception& e) {
            if (semantic.DiagnosticFile().empty()) throw;
            throw std::runtime_error(semantic.DiagnosticFile() + ":" + std::to_string(semantic.DiagnosticOffset()) + ": " + e.what());
        }
        for (const auto* function : semantic.FunctionsWithMoves()) {
            auto violations = OwnershipChecker(semantic).checkFunction(*function);
            if (!violations.empty()) {
                const auto& violation = violations.front();
                const auto& filename = violation.site ? violation.site->sourceFile : function->sourceFile;
                const char* message = violation.action == OwnershipFlow::Action::Reinitialize
                    ? "Copying a destructible value is not permitted: target is not definitely moved"
                    : "Object may have already been moved or be uninitialized";
                throw std::runtime_error(filename + ":" + std::to_string(violation.offset) + ": " + message);
            }
        }
        print("Generating IR...\n");
        LLVM llvm;
        llvm.Generate(program, semantic);
        llvm.Print();

        print("Generating Binary...\n");
        auto executablePath = options.output.empty() ?
            (options.inputs.size() > 1 ? std::filesystem::path("app.exe") : std::filesystem::path(target).replace_extension(".exe")) :
            std::filesystem::path(options.output);

        for (const auto& input : options.inputs) {
            std::error_code error;
            if (std::filesystem::exists(executablePath, error) && !error &&
                std::filesystem::equivalent(executablePath, std::filesystem::path(input), error) && !error)
                throw std::runtime_error("Output file is also an input file: " + input);
        }
        llvm::SmallString<128> temporaryPath;
        auto error = llvm::sys::fs::createTemporaryFile("c4rc", "obj", temporaryPath);
        if (error) throw std::system_error(error, "Failed to create temporary object file");
        struct TemporaryObject {
            std::filesystem::path path;
            ~TemporaryObject() { std::error_code ignored; std::filesystem::remove(path, ignored); }
        } object{std::filesystem::path(temporaryPath.c_str())};

        llvm.WriteObject(object.path);
        llvm.LinkExecutable(object.path, executablePath);

        print("Generated {}\n", executablePath.string());
    }
    catch (const std::exception& e) {
        if (check) {
            string message = e.what();
            for (char& c : message) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
            std::cerr << "C4R_DIAGNOSTIC\t" << diagnosticOffset << "\t" << message << '\n';
            return 1;
        }
        std::cerr << "Compilation Error: " << e.what() << '\n';
        return -1;
    }

    print("Compiled!\n");

    return 0;
}
