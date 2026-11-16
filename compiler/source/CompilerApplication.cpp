#include "CompilerApplication.h"

#include "Compilation.h"
#include "EditorSymbols.h"
#include "ExecutableWriter.h"
#include "LLVM.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <print>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <io.h>

namespace {
enum class Mode { Compile, Check, Symbols, Help, Version };

struct CommandLine {
    Mode mode = Mode::Compile;
    std::vector<std::string> inputs;
    std::filesystem::path output;
    std::string symbolOffset;

    static CommandLine Parse(int argc, char** argv);
};

CommandLine CommandLine::Parse(int argc, char** argv) {
    if (argc == 4 && std::string_view(argv[1]) == "--symbols") return {Mode::Symbols, {argv[3]}, {}, argv[2]};
    if (argc == 3 && std::string_view(argv[1]) == "--check") return {Mode::Check, {argv[2]}, {}, {}};

    CommandLine options;
    bool help = false;
    bool version = false;
    for (int i = 1; i < argc;) {
        std::string arg = argv[i++];
        if (arg == "-h" || arg == "--help") {
            help = true;
        } else if (arg == "-v" || arg == "--version") {
            version = true;
        } else if (arg == "-i" || arg == "--input") {
            const auto previousCount = options.inputs.size();
            while (i < argc && argv[i][0] != '-') {
                if (argv[i][0] == '\0') throw std::runtime_error("Input option requires a non-empty file name");
                options.inputs.emplace_back(argv[i++]);
            }
            if (options.inputs.size() == previousCount) throw std::runtime_error("Input option requires at least one file");
        } else if (arg == "-o" || arg == "--output") {
            if (!options.output.empty()) throw std::runtime_error("Output option specified more than once");
            if (i == argc || argv[i][0] == '-' || argv[i][0] == '\0') throw std::runtime_error("Output option requires a file");
            options.output = argv[i++];
        } else if (!arg.empty() && arg.front() != '-' && argc == 2) {
            options.inputs.push_back(std::move(arg));
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    if (help) options.mode = Mode::Help;
    else if (version) options.mode = Mode::Version;
    else if (options.inputs.empty()) throw std::runtime_error("No input files specified");
    return options;
}

std::vector<SourceFile> ReadSources(const CommandLine& options) {
    const bool editor = options.mode != Mode::Compile;
    if (editor && options.inputs.front() == "-") {
        _setmode(_fileno(stdin), _O_BINARY);
        return {{"-", {std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()}}};
    }

    std::vector<SourceFile> sources;
    sources.reserve(options.inputs.size());
    for (const auto& input : options.inputs) {
        std::ifstream file(input, std::ios::binary);
        if (!file) throw std::runtime_error("Failed to open source file: " + input);
        std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (editor) text += '\n';
        sources.push_back({input, std::move(text)});
    }
    return sources;
}

std::filesystem::path OutputPath(const CommandLine& options) {
    if (!options.output.empty()) return options.output;
    if (options.inputs.size() > 1) return "app.exe";
    auto path = std::filesystem::path(options.inputs.front());
    path.replace_extension(".exe");
    return path;
}

void WriteBinary(LLVM& llvm, const CommandLine& options) {
    std::print("Generating Binary...\n");
    const auto path = OutputPath(options);
    for (const auto& input : options.inputs) {
        std::error_code error;
        if (std::filesystem::exists(path, error) && !error &&
            std::filesystem::equivalent(path, std::filesystem::path(input), error) && !error) {
            throw std::runtime_error("Output file is also an input file: " + input);
        }
    }
    ExecutableWriter().Write(llvm.GetModule(), path);
    std::print("Generated {}\n", path.string());
}

int ReportFailure(const Compilation& compilation, Mode mode, const std::exception& error) {
    if (mode != Mode::Compile) {
        std::string message = error.what();
        for (char& c : message) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        std::cerr << "C4R_DIAGNOSTIC\t" << compilation.DiagnosticOffset() << '\t' << message << '\n';
        return 1;
    }

    std::cerr << "Compilation Error: ";
    if (!compilation.DiagnosticFile().empty()) {
        std::cerr << compilation.DiagnosticFile() << ':' << compilation.DiagnosticOffset() << ": ";
    }
    std::cerr << error.what() << '\n';
    return -1;
}
}

int CompilerApplication::Run(int argc, char** argv) const {
    CommandLine options;
    try {
        options = CommandLine::Parse(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "c4rc: " << error.what() << '\n';
        return 1;
    }
    if (options.mode == Mode::Help) {
        std::cout << "Usage: c4rc -i <source...> [-i <source...>] [-o <output>]\n"
                     "  -i, --input    Input source files (repeatable)\n"
                     "  -o, --output   Output executable\n"
                     "  -h, --help     Show this help\n"
                     "  -v, --version  Show compiler version\n"
                     "       --check <source|-> | --symbols <offset> -\n";
        return 0;
    }
    if (options.mode == Mode::Version) {
        std::cout << "c4rc " << C4R_VERSION << '\n';
        return 0;
    }

    std::vector<SourceFile> sources;
    try {
        sources = ReadSources(options);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    Compilation compilation;
    try {
        if (options.mode == Mode::Symbols) {
            const auto offset = static_cast<size_t>(std::stoull(options.symbolOffset));
            EditorSymbols().Write(sources.front().text, offset, std::cout);
            return 0;
        }

        const bool build = options.mode == Mode::Compile;
        compilation.DiscoverTypes(sources);
        if (build) {
            std::print("Compiling {}...\n", sources.front().name);
            std::print("Parsing program...\n");
        }
        compilation.Parse(sources);
        compilation.Analyze(sources.size() > 1);

        if (build) std::print("Generating IR...\n");
        LLVM llvm;
        compilation.Generate(llvm);
        if (build) {
            llvm.Print();
            WriteBinary(llvm, options);
            std::print("Compiled!\n");
        }
        return 0;
    } catch (const std::exception& error) {
        return ReportFailure(compilation, options.mode, error);
    }
}
