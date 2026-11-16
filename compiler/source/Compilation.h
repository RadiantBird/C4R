#pragma once

#include "Semantic.h"

#include <unordered_map>

class LLVM;

struct SourceFile {
    std::string name;
    std::string text;
};

class Compilation {
public:
    explicit Compilation(const std::vector<SourceFile>& sources) : sources(sources) {}

    void DiscoverTypes();
    void Parse();
    void Analyze();
    void Generate(LLVM& llvm);

    size_t DiagnosticOffset() const { return diagnosticOffset; }
    const std::string& DiagnosticFile() const { return diagnosticFile; }

private:
    const std::vector<SourceFile>& sources;
    Program program;
    Semantic semantic;
    std::unordered_map<std::string, std::string> typeLocations;
    size_t diagnosticOffset = 0;
    std::string diagnosticFile;
};
