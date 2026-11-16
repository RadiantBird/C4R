#include "IRModuleEmitter.h"

#include "IRConstantEmitter.h"
#include "IRFunctionEmitter.h"
#include "Semantic.h"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <stdexcept>

namespace c4r::ir {

IRModuleEmitter::IRModuleEmitter(llvm::Module& module, const Semantic& semantic, size_t& diagnosticOffset)
    : Module(module), Context(module.getContext()), semantic(semantic), diagnosticOffset(diagnosticOffset) {}

void IRModuleEmitter::Emit(const Program& program) {
    const auto& symbols = semantic.Globals();
    std::vector<std::string> names;
    names.reserve(symbols.size());
    for (const auto& [name, symbol] : symbols) names.push_back(name);
    std::sort(names.begin(), names.end());

    DeclareTypes(names);
    IRFunctionEmitter functions(*this);
    for (const auto& name : names) {
        const auto& symbol = symbols.at(name);
        if (symbol.kind == Semantic::Kind::Function)
            functions.DeclareGlobalFunction(*static_cast<const FunctionDeclaration*>(symbol.declaration));
    }

    const auto& entryName = semantic.EntryPoint();
    if (!entryName.empty()) DeclareGlobals(names, functions);
    for (const auto& name : names) {
        if (symbols.at(name).kind == Semantic::Kind::Function) functions.EmitGlobalFunction(name);
    }
    for (auto& [name, type] : types) {
        for (auto& [methodName, method] : type.methods) functions.EmitFunctionBody(method.first);
        for (auto& [constructor, function] : type.constructors) functions.EmitFunctionBody(function);
        if (type.destructor) functions.EmitFunctionBody(type.destructor);
    }
    functions.EmitEntryPoint(program, entryName);
    functions.CheckMethodMutability();

    if (llvm::verifyModule(Module, &llvm::errs())) throw std::runtime_error("LLVM IR verification failed");
}

void IRModuleEmitter::DeclareTypes(const std::vector<std::string>& names) {
    const auto& globalSymbols = semantic.Globals();
    for (const auto& name : names) {
        const auto& symbol = globalSymbols.at(name);
        if (symbol.kind != Semantic::Kind::Type) continue;
        auto* declaration = static_cast<const TypeDeclaration*>(symbol.declaration);
        types.emplace(name, TypeInfo{declaration, llvm::StructType::create(Context, name), {}, {}});
    }
    for (auto& [name, type] : types) typesByLayout.emplace(type.type, &type);
    for (const auto& name : names) {
        if (auto type = types.find(name); type != types.end()) LayoutType(type->second);
    }
}

void IRModuleEmitter::LayoutType(TypeInfo& type) {
    const auto* declaration = type.declaration;
    if (!type.type->isOpaque()) return;
    if (!layingOut.insert(declaration->name).second) throw std::runtime_error("Invalid or incomplete field type: " + declaration->name);
    std::vector<llvm::Type*> fields;
    const auto& memberSymbols = semantic.Members(declaration->name);
    for (const auto& member : declaration->members) {
        if (auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get())) {
            if (types.contains(field->type)) LayoutType(types.at(field->type));
            auto* fieldType = GetType(field->type);
            if (fieldType->isVoidTy() || (fieldType->isStructTy() && !fieldType->isSized())) throw std::runtime_error("Invalid or incomplete field type: " + field->type);
            type.fields.emplace(field->name, FieldInfo{field, memberSymbols.at(field->name).isPublic, static_cast<unsigned>(fields.size())});
            fields.push_back(fieldType);
        }
    }
    type.type->setBody(fields);
    layingOut.erase(declaration->name);
    DeclareMethods(type);
}

void IRModuleEmitter::DeclareMethods(TypeInfo& type) {
    const auto* declaration = type.declaration;
    const auto& memberSymbols = semantic.Members(declaration->name);
    for (const auto& member : declaration->members) {
        auto* method = dynamic_cast<const FunctionDeclaration*>(member.declaration.get());
        if (!method) continue;
        if (dynamic_cast<const ConstructorDeclaration*>(method) || dynamic_cast<const DestructorDeclaration*>(method)) continue;
        std::vector<llvm::Type*> args;
        if (!method->returnsReference && semantic.NeedsDestruction(method->returnType)) args.push_back(llvm::PointerType::getUnqual(Context));
        args.push_back(llvm::PointerType::getUnqual(Context));
        std::unordered_set<std::string> parameters;
        for (const auto& parameter : method->parameters) {
            if (!parameters.insert(parameter.name).second) throw std::runtime_error("Duplicate parameter: " + parameter.name);
            auto* parameterType = GetType(parameter.type);
            if (parameterType->isVoidTy()) throw std::runtime_error("Parameter cannot have void type");
            args.push_back(parameter.isReference || semantic.NeedsDestruction(parameter.type)
                ? llvm::PointerType::getUnqual(Context) : parameterType);
        }
        auto* resultType = method->returnsReference ? static_cast<llvm::Type*>(llvm::PointerType::getUnqual(Context)) :
            semantic.NeedsDestruction(method->returnType) ? static_cast<llvm::Type*>(llvm::Type::getVoidTy(Context)) : GetType(method->returnType);
        auto* signature = llvm::FunctionType::get(resultType, args, false);
        auto* function = llvm::Function::Create(signature, llvm::Function::InternalLinkage, "__c4r_method_" + std::to_string(nextFunctionId++), Module);
        auto symbol = std::make_shared<FunctionSymbol>(FunctionSymbol{function, method, {}, declaration->name});
        type.methods.emplace(method->name, std::make_pair(symbol, memberSymbols.at(method->name).isPublic));
    }
    const auto& lifecycle = semantic.Lifecycle(declaration->name);
    for (const auto& constructor : lifecycle.constructors) {
        auto* ast = static_cast<const ConstructorDeclaration*>(constructor.declaration);
        type.constructors.emplace(ast, DeclareSpecial(*ast, type));
    }
    if (lifecycle.destructor)
        type.destructor = DeclareSpecial(*static_cast<const DestructorDeclaration*>(lifecycle.destructor->declaration), type);
}

std::shared_ptr<FunctionSymbol> IRModuleEmitter::DeclareSpecial(
    const FunctionDeclaration& declaration, const TypeInfo& owner
) {
    std::vector<llvm::Type*> args{llvm::PointerType::getUnqual(Context)};
    for (const auto& parameter : declaration.parameters)
        args.push_back(parameter.isReference || semantic.NeedsDestruction(parameter.type)
            ? llvm::PointerType::getUnqual(Context) : GetType(parameter.type));
    auto* signature = llvm::FunctionType::get(llvm::Type::getVoidTy(Context), args, false);
    auto* function = llvm::Function::Create(signature, llvm::Function::InternalLinkage,
        "__c4r_lifecycle_" + std::to_string(nextFunctionId++), Module);
    return std::make_shared<FunctionSymbol>(FunctionSymbol{function, &declaration, {}, owner.declaration->name});
}

void IRModuleEmitter::DeclareGlobals(const std::vector<std::string>& names, IRFunctionEmitter& functions) {
    IRConstantEmitter constants(Context);
    const auto& globalSymbols = semantic.Globals();
    for (const auto& name : names) {
        const auto& symbol = globalSymbols.at(name);
        if (symbol.kind != Semantic::Kind::GlobalVariable) continue;
        auto* declaration = static_cast<const VariableDeclaration*>(symbol.declaration);
        diagnosticOffset = declaration->offset;
        auto* type = GetType(declaration->type);
        if (type->isVoidTy()) throw std::runtime_error("Variable cannot have void type");
        llvm::Constant* initial = nullptr;
        if (declaration->aggregateInit) {
            if (!types.contains(declaration->type)) throw std::runtime_error("Brace initialization requires a user-defined type");
            initial = ConstantObject(types.at(declaration->type), constants);
        } else initial = constants.Emit(*declaration->initialValue);
        if (initial->getType() != type) throw std::runtime_error("Initializer type mismatch: " + declaration->name);
        auto* storage = new llvm::GlobalVariable(Module, type, !declaration->isMutable,
            llvm::GlobalValue::InternalLinkage, initial, "__c4r_global_" + declaration->name);
        functions.RegisterGlobal(*declaration, storage, type);
    }
}

llvm::Constant* IRModuleEmitter::ConstantObject(const TypeInfo& type, IRConstantEmitter& constants) {
    std::vector<llvm::Constant*> values;
    for (const auto& member : type.declaration->members) {
        auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get());
        if (!field) continue;
        auto* fieldType = GetType(field->type);
        llvm::Constant* value = field->initialValue ? constants.Emit(*field->initialValue) :
            types.contains(field->type) ? ConstantObject(types.at(field->type), constants) : llvm::Constant::getNullValue(fieldType);
        if (value->getType() != fieldType) throw std::runtime_error("Initializer type mismatch: " + field->name);
        values.push_back(value);
    }
    return llvm::ConstantStruct::get(type.type, values);
}

llvm::Type* IRModuleEmitter::GetType(const std::string& name) const {
    if (name == "void") {
        return llvm::Type::getVoidTy(Context);
    }

    if (name == "int32" || name == "int") {
        return llvm::Type::getInt32Ty(Context);
    }
    if (name == "bool") return llvm::Type::getInt1Ty(Context);
    if (name == "float") return llvm::Type::getFloatTy(Context);
    if (!name.empty() && name.back() == '*') return llvm::PointerType::getUnqual(GetType(name.substr(0, name.size() - 1)));
    if (types.contains(name)) return types.at(name).type;

    throw std::runtime_error(
        "Unknown type: " + name
    );
}

}
