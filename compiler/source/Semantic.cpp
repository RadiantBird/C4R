#include "Semantic.h"

#include <stdexcept>

std::string Semantic::CanonicalType(std::string type) {
    if (!type.empty() && type.back() == '*') return CanonicalType(type.substr(0, type.size() - 1)) + "*";
    return type == "int" ? std::string("int32") : type;
}

bool Semantic::NeedsDestruction(const std::string& type) const {
    auto found = globals.find(type);
    if (found == globals.end() || found->second.kind != Kind::Type) return false;
    if (auto cached = destructionCache.find(type); cached != destructionCache.end()) return cached->second;
    const auto& lifecycle = specialMembers.at(type);
    if (lifecycle.destructor) return destructionCache[type] = true;
    const auto* declaration = static_cast<const TypeDeclaration*>(found->second.declaration);
    for (const auto& member : declaration->members)
        if (auto* field = dynamic_cast<const VariableDeclaration*>(member.declaration.get()); field && NeedsDestruction(field->type))
            return destructionCache[type] = true;
    return destructionCache[type] = false;
}

const ConstructorDeclaration* Semantic::SelectDefaultConstructor(const std::string& type, const std::string& owner) const {
    const auto& lifecycle = specialMembers.at(type);
    if (lifecycle.constructors.empty()) return nullptr;
    for (const auto& candidate : lifecycle.constructors) {
        auto* declaration = static_cast<const ConstructorDeclaration*>(candidate.declaration);
        if (!declaration->parameters.empty()) continue;
        if (!candidate.isPublic && owner != type) throw std::runtime_error("Private constructor: " + type);
        return declaration;
    }
    throw std::runtime_error("No matching constructor: " + type);
}
