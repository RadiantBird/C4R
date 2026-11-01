#include "LLVM.h"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/raw_ostream.h>

#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/CodeGen.h>

#include <memory>
#include <system_error>

#include <stdexcept>
#include <process.h>
#include <unordered_map>


LLVM::LLVM()
    : Module("C4R", Context),
      Builder(Context)
{}

llvm::Type* LLVM::GetType(const std::string& name) {
    if (name == "void") {
        return Builder.getVoidTy();
    }

    if (name == "int32") {
        return Builder.getInt32Ty();
    }

    throw std::runtime_error(
        "Unknown type: " + name
    );
}

llvm::Value* LLVM::CreateString(const std::string& value) {
    auto* global = Builder.CreateGlobalString(
        value,
        "c4r.str",
        0,
        &Module
    );

    llvm::Value* indices[] = {
        Builder.getInt32(0),
        Builder.getInt32(0)
    };

    return Builder.CreateInBoundsGEP(
        global->getValueType(),
        global,
        indices,
        "str.ptr"
    );
}

void LLVM::EmitString(const StringStatement& statement) {
    auto* pointerType = llvm::PointerType::getUnqual(Context);

    llvm::Type* arguments[] = {
        pointerType
    };

    auto* printfType = llvm::FunctionType::get(
        Builder.getInt32Ty(),
        arguments,
        true
    );

    auto printfFunction = Module.getOrInsertFunction(
        "printf",
        printfType
    );

    auto* format = CreateString("%s");
    auto* value = CreateString(statement.value);

    Builder.CreateCall(
        printfFunction,
        { format, value }
    );
}

void LLVM::EmitBody(
    const std::vector<std::unique_ptr<ASTNode>>& body
) {
    std::unordered_map<std::string, llvm::AllocaInst*> variables;

    auto* pointerType = llvm::PointerType::getUnqual(Context);

    llvm::Type* printfArguments[] = {
        pointerType
    };

    auto* printfType = llvm::FunctionType::get(
        Builder.getInt32Ty(),
        printfArguments,
        true
    );

    auto printfFunction = Module.getOrInsertFunction(
        "printf",
        printfType
    );

    for (const auto& node : body) {

        // String statement
        if (auto* statement = dynamic_cast<StringStatement*>(node.get())) {
            EmitString(*statement);
        }

        // Variable declaration
        else if (auto* declaration =
            dynamic_cast<VariableDeclaration*>(node.get()))
        {
            if (declaration->type != "int32") {
                throw std::runtime_error(
                    "Unsupported variable type: " + declaration->type
                );
            }

            if (variables.contains(declaration->name)) {
                throw std::runtime_error(
                    "Duplicate variable: " + declaration->name
                );
            }

            auto* literal = dynamic_cast<NumericLiteral*>(
                declaration->initialValue.get()
            );

            if (!literal) {
                throw std::runtime_error(
                    "Unsupported variable initializer"
                );
            }

            auto* type = Builder.getInt32Ty();

            auto* storage = Builder.CreateAlloca(
                type,
                nullptr,
                declaration->name
            );

            const int value = std::stoi(literal->value);

            Builder.CreateStore(
                Builder.getInt32(value),
                storage
            );

            variables.emplace(
                declaration->name,
                storage
            );
        }

        // Format string
        else if (auto* statement =
            dynamic_cast<FormatStringStatement*>(node.get()))
        {
            std::string format;
            std::vector<llvm::Value*> arguments;

            for (const auto& part : statement->parts) {

                if (auto* text =
                    dynamic_cast<FormatText*>(part.get()))
                {
                    for (char c : text->value) {
                        if (c == '%') {
                            format += "%%";
                        }
                        else {
                            format += c;
                        }
                    }
                }

                else if (auto* identifier =
                    dynamic_cast<IdentifierExpr*>(part.get()))
                {
                    auto it = variables.find(identifier->name);

                    if (it == variables.end()) {
                        throw std::runtime_error(
                            "Undefined variable: " + identifier->name
                        );
                    }

                    format += "%d";

                    arguments.push_back(
                        Builder.CreateLoad(
                            Builder.getInt32Ty(),
                            it->second,
                            identifier->name + ".value"
                        )
                    );
                }

                else {
                    throw std::runtime_error(
                        "Unsupported format string expression"
                    );
                }
            }

            std::vector<llvm::Value*> callArguments;

            callArguments.push_back(CreateString(format));

            for (auto* argument : arguments) {
                callArguments.push_back(argument);
            }

            Builder.CreateCall(
                printfFunction,
                callArguments
            );
        }

        else {
            throw std::runtime_error(
                "Unsupported AST node in function"
            );
        }
    }
}

void LLVM::EmitFunction(const FunctionDeclaration& declaration) {
    if (Module.getFunction(declaration.name)) {
        throw std::runtime_error(
            "Duplicate function: " + declaration.name
        );
    }

    auto* returnType = GetType(declaration.returnType);

    auto* functionType = llvm::FunctionType::get(
        returnType,
        false
    );

    auto* function = llvm::Function::Create(
        functionType,
        llvm::Function::ExternalLinkage,
        declaration.name,
        Module
    );

    auto* entry = llvm::BasicBlock::Create(
        Context,
        "entry",
        function
    );

    Builder.SetInsertPoint(entry);

    EmitBody(declaration.body);

    if (returnType->isVoidTy()) {
        Builder.CreateRetVoid();
    }
    else if (returnType->isIntegerTy(32)) {
        Builder.CreateRet(Builder.getInt32(0));
    }
    else {
        throw std::runtime_error(
            "Unsupported return type"
        );
    }
}

void LLVM::EmitImplicitMain(const Program& program) {
    auto* functionType = llvm::FunctionType::get(
        Builder.getInt32Ty(),
        false
    );

    auto* function = llvm::Function::Create(
        functionType,
        llvm::Function::ExternalLinkage,
        "main",
        Module
    );

    auto* entry = llvm::BasicBlock::Create(
        Context,
        "entry",
        function
    );

    Builder.SetInsertPoint(entry);

    for (const auto& node : program.children) {
        if (auto* statement = dynamic_cast<StringStatement*>(node.get())) {
            EmitString(*statement);
        }
    }

    Builder.CreateRet(Builder.getInt32(0));
}

void LLVM::Generate(const Program& program) {
    bool hasMain = false;
    bool hasTopLevelString = false;

    for (const auto& node : program.children) {
        if (auto* function = dynamic_cast<FunctionDeclaration*>(node.get())) {
            if (function->name == "main") {
                hasMain = true;
            }
        }
        else if (dynamic_cast<StringStatement*>(node.get())) {
            hasTopLevelString = true;
        }
        else {
            throw std::runtime_error(
                "Unsupported AST node in program"
            );
        }
    }

    if (hasMain && hasTopLevelString) {
        throw std::runtime_error(
            "Top-level strings with explicit main are not supported yet"
        );
    }

    for (const auto& node : program.children) {
        if (auto* function = dynamic_cast<FunctionDeclaration*>(node.get())) {
            EmitFunction(*function);
        }
    }

    if (!hasMain) {
        EmitImplicitMain(program);
    }

    if (llvm::verifyModule(Module, &llvm::errs())) {
        throw std::runtime_error(
            "LLVM IR verification failed"
        );
    }
}

void LLVM::Print() {
    Module.print(llvm::outs(), nullptr);
}

void LLVM::WriteObject(const std::filesystem::path& path) {
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

    this->Module.setTargetTriple(triple);
    this->Module.setDataLayout(targetMachine->createDataLayout());

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

    pass.run(Module);
    output.flush();
}

void LLVM::LinkExecutable(
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

    const auto result = _wspawnvp(
        _P_WAIT,
        L"link.exe",
        arguments.data()
    );

    if (result == -1)
        throw std::system_error(
            errno,
            std::generic_category(),
            "Failed to start link.exe"
        );

    if (result != 0) {
        throw std::runtime_error(
            std::format("link.exe failed with exit code {}", result)
        );
    }
}