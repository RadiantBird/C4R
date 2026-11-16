#pragma once

#include "Lexer.h"

#include <iosfwd>
#include <optional>
#include <vector>

class EditorSymbols {
public:
    void Write(const std::string& source, size_t limit, std::ostream& output) const;

private:
    struct Declaration {
        std::string type;
        std::string name;
        size_t nameIndex;
    };

    static std::vector<Token> ReadPrefix(const std::string& prefix);
    static std::optional<Declaration> ReadDeclaration(const std::vector<Token>& tokens, size_t start);
};
