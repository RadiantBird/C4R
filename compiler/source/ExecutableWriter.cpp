#include "ExecutableWriter.h"

#include <llvm/ADT/SmallString.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

#include <cerrno>
#include <cstdio>
#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <io.h>
#include <process.h>

namespace {
struct TemporaryObject {
    std::filesystem::path path;

    ~TemporaryObject() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};
}

void ExecutableWriter::Write(llvm::Module& module, const std::filesystem::path& executablePath) const {
    llvm::SmallString<128> temporaryPath;
    const auto error = llvm::sys::fs::createTemporaryFile("c4rc", "obj", temporaryPath);
    if (error) throw std::system_error(error, "Failed to create temporary object file");
    TemporaryObject object{std::filesystem::path(temporaryPath.c_str())};

    WriteObject(module, object.path);
    Link(object.path, executablePath);
}

void ExecutableWriter::WriteObject(llvm::Module& module, const std::filesystem::path& path) {
    static const bool initialized = [] {
        if (llvm::InitializeNativeTarget())
            throw std::runtime_error("Failed to initialize native target");

        if (llvm::InitializeNativeTargetAsmPrinter())
            throw std::runtime_error("Failed to initialize native asm printer");

        return true;
    }();

    (void)initialized;

    const auto targetTriple = llvm::sys::getDefaultTargetTriple();
    const llvm::Triple triple{ llvm::StringRef(targetTriple) };

    std::string error;
    const llvm::Target* target =
        llvm::TargetRegistry::lookupTarget(triple, error);

    if (!target)
        throw std::runtime_error(error);

    llvm::TargetOptions options;

    std::unique_ptr<llvm::TargetMachine> targetMachine(
        target->createTargetMachine(
            triple,
            "generic",
            "",
            options,
            std::nullopt
        )
    );

    if (!targetMachine)
        throw std::runtime_error("Failed to create TargetMachine");

    module.setTargetTriple(triple);
    module.setDataLayout(targetMachine->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream output(
        path.string(),
        ec,
        llvm::sys::fs::OF_None
    );

    if (ec)
        throw std::system_error(ec, "Failed to create object file");

    llvm::legacy::PassManager pass;

    if (targetMachine->addPassesToEmitFile(
        pass,
        output,
        nullptr,
        llvm::CodeGenFileType::ObjectFile
    ))
    {
        throw std::runtime_error("Object file emission is unsupported");
    }

    pass.run(module);
    output.flush();
}

void ExecutableWriter::Link(
    const std::filesystem::path& objectPath,
    const std::filesystem::path& executablePath
) {
    std::vector<std::wstring> storage;

    storage.emplace_back(L"link.exe");
    storage.push_back(objectPath.wstring());

    storage.emplace_back(L"/NOLOGO");
    storage.emplace_back(L"/SUBSYSTEM:CONSOLE");
    storage.emplace_back(L"/MACHINE:X64");

    std::wstring outputArg = L"/OUT:";
    outputArg += executablePath.wstring();
    storage.push_back(std::move(outputArg));

    // Static MSVC CRT
    storage.emplace_back(L"/DEFAULTLIB:libcmt.lib");
    storage.emplace_back(L"/DEFAULTLIB:libvcruntime.lib");
    storage.emplace_back(L"/DEFAULTLIB:libucrt.lib");
    storage.emplace_back(L"/DEFAULTLIB:kernel32.lib");

    std::vector<const wchar_t*> arguments;

    for (const auto& arg : storage)
        arguments.push_back(arg.c_str());

    arguments.push_back(nullptr);

    // link.exe writes diagnostics to stdout; keep the compiler's diagnostics
    // on stderr while preserving stdout for its normal output and editor clients.
    llvm::outs().flush();
    std::cout.flush();
    std::fflush(stdout);
    int savedStdout = _dup(_fileno(stdout));
    if (savedStdout == -1) throw std::system_error(errno, std::generic_category(), "Failed to redirect linker output");
    if (_dup2(_fileno(stderr), _fileno(stdout)) != 0) {
        int error = errno;
        _close(savedStdout);
        throw std::system_error(error, std::generic_category(), "Failed to redirect linker output");
    }
    const auto result = _wspawnvp(_P_WAIT, L"link.exe", arguments.data());
    const int spawnError = errno;
    int restoreResult = _dup2(savedStdout, _fileno(stdout));
    int restoreError = errno;
    _close(savedStdout);
    if (restoreResult != 0) throw std::system_error(restoreError, std::generic_category(), "Failed to restore compiler output");

    if (result == -1)
        throw std::system_error(
            spawnError,
            std::generic_category(),
            "Failed to start link.exe"
        );

    if (result != 0) {
        throw std::runtime_error(
            std::format("link.exe failed with exit code {}", result)
        );
    }
}
