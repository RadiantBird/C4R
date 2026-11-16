#pragma once

#include <filesystem>

namespace llvm {
class Module;
}

class ExecutableWriter {
public:
    void Write(llvm::Module& module, const std::filesystem::path& executablePath) const;

private:
    static void WriteObject(llvm::Module& module, const std::filesystem::path& path);
    static void Link(const std::filesystem::path& objectPath, const std::filesystem::path& executablePath);
};
