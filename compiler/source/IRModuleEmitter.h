#pragma once

#include "AST.h"

#include <llvm/IR/Module.h>
#include <unordered_map>
#include <unordered_set>

class Semantic;

namespace llvm {
class AllocaInst;
}

namespace c4r::ir {
class IRConstantEmitter;
class IRFunctionEmitter;

struct Variable {
    llvm::Value* storage;
    llvm::Type* type;
    bool isMutable;
    size_t id;
    bool isReceiver = false;
    llvm::AllocaInst* ownershipActive = nullptr;
};

struct Capture {
    std::string name;
    Variable variable;
};

struct FunctionSymbol {
    llvm::Function* function;
    const FunctionDeclaration* declaration;
    std::vector<Capture> captures;
    std::string owner;
    bool mutates = false;
    std::vector<std::shared_ptr<FunctionSymbol>> calls;
};

struct FieldInfo {
    const VariableDeclaration* declaration;
    bool isPublic;
    unsigned index;
};

struct TypeInfo {
    const TypeDeclaration* declaration;
    llvm::StructType* type;
    std::unordered_map<std::string, FieldInfo> fields;
    std::unordered_map<std::string, std::pair<std::shared_ptr<FunctionSymbol>, bool>> methods;
    std::unordered_map<const ConstructorDeclaration*, std::shared_ptr<FunctionSymbol>> constructors;
    std::shared_ptr<FunctionSymbol> destructor;
};

class IRModuleEmitter {
public:
    IRModuleEmitter(llvm::Module& module, const Semantic& semantic, size_t& diagnosticOffset);
    void Emit(const Program& program);

private:
    friend class IRFunctionEmitter;

    llvm::Module& Module;
    llvm::LLVMContext& Context;
    const Semantic& semantic;
    size_t& diagnosticOffset;
    std::unordered_map<std::string, TypeInfo> types;
    std::unordered_map<llvm::Type*, TypeInfo*> typesByLayout;
    std::unordered_set<std::string> layingOut;
    size_t nextFunctionId = 0;

    llvm::Type* GetType(const std::string& name) const;
    void DeclareTypes(const std::vector<std::string>& names);
    void LayoutType(TypeInfo& type);
    void DeclareMethods(TypeInfo& type);
    std::shared_ptr<FunctionSymbol> DeclareSpecial(const FunctionDeclaration& declaration, const TypeInfo& owner);
    void DeclareGlobals(const std::vector<std::string>& names, IRFunctionEmitter& functions);
    llvm::Constant* ConstantObject(const TypeInfo& type, IRConstantEmitter& constants);
};
}
