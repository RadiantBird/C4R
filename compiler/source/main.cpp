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

    
};

using std::print;
using std::string;
using std::unordered_map;
using std::vector;
using std::unique_ptr;

unordered_map<string, bool> StandardType = {
    {"int32", true},
    {"void", true},
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
    default:
        return "Invalid";
    }
}

bool isIdentifier(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n';
}

int main() {
    string source = "";
    string target = "playground/hello.c4r";

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

    struct Token {
        TokenType type;
        string value;
    };

    class Lexer {
    private:
        const std::string& source;
        size_t position = 0;
    public:
        Lexer(const std::string& source)
            : source(source) {}

        Token NextToken() {
            while (position < source.size()) {
                char c = source[position++];

                if (isWhitespace(c)) {
                    continue;
                }

                switch (c) {
                case ';':
                    return { TokenType::EndOfStatement, ";" };

                case '(':
                    return { TokenType::LeftParen, "(" };

                case ')':
                    return { TokenType::RightParen, ")" };

                case '"': {
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
                    return { TokenType::String, value };
                }

                case '{':
                    return {TokenType::LeftBrace, "{"};

                case '}':
                    return {TokenType::RightBrace, "}"};

                default:
                    if (isIdentifier(c)) {
                        string name(1, c);

                        while (
                            position < source.size() && (
                                isIdentifier(source[position]) ||
                                std::isdigit(static_cast<unsigned char>(source[position]))
                            )
                        ) {
                            name += source[position++];
                        }

                        if (StandardType.contains(name)) {
                            return {TokenType::TypeName, name};
                        }
                        else {
                            return {TokenType::Identifier, name};
                        }
                    }
                    else {
                        string name(1, c);
                        while (position < source.size() && !isWhitespace(source[position])){
                            name += source[position++];
                        }
                        return { TokenType::Unknown, name };
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
        // FunctionDeclaration currentFunction;

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

            Expect(TokenType::LeftBrace);
            Advance();

            FunctionDeclaration function;
            function.name = ident;
            function.returnType = typev;

            while (currentToken.type != TokenType::RightBrace) {
                if (currentToken.type == TokenType::EndOfFile) {
                    throw std::runtime_error("Unexpected EOF in function");
                }

                if (currentToken.type == TokenType::String) {
                    ParseStringStatement(function.body);
                }
                else {
                    throw std::runtime_error("Unknown statement in function");
                }
            }

            Advance();

            program.children.push_back(
                std::make_unique<FunctionDeclaration>(std::move(function))
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
                    ParseFunctionStatement();
                }
                else {
                    throw std::runtime_error("Unknown statement");
                }
            }
        }
    };

    Lexer lexer(source);
    Parser parser(lexer);

    try {
        Lexer lexer(source);
        Parser parser(lexer);

        print("Compiling {}...\n", target);

        parser.ParseProgram();

        LLVM llvm;
        llvm.Generate(parser.program);
        llvm.Print();

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