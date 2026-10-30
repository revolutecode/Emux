#pragma once

#include "Emux/Compiler/Semantic/FunctionSignature.hpp"
#include "Emux/Compiler/Type/Type.hpp"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Emux
{

// Estado partilhado pelo construtor da Exir v2. A IR guarda valores e
// operações tipadas; registradores são responsabilidade do backend.
class IRContext
{
public:
    std::uint32_t pointerBits = 64;

    std::ostringstream ir;
    std::size_t tempCounter = 0;

    std::string funcName;
    Type funcRet;

    // Valores constantes conhecidos (AttrId::Const / literais) para folding.
    std::unordered_map<std::string, int64_t> constValues;

    const std::unordered_map<std::string, Type>* globalTypes = nullptr;

    // Assinaturas por nome IR (ex: "main_AddShift").
    std::unordered_map<std::string, const FunctionSignature*> functionSignatures;

    // Parâmetros são emitidos com o nome cru ("a"); referências usam o nome
    // escopado ("main::a"). O alias traduz um no outro.
    std::unordered_map<std::string, std::string> localAliases;

    std::string NewTemp()
    {
        return "%t" + std::to_string(tempCounter++);
    }

    std::optional<Type> LookupType(const std::string& name) const
    {
        if (globalTypes)
        {
            if (auto it = globalTypes->find(name); it != globalTypes->end())
            {
                return it->second;
            }
        }

        return std::nullopt;
    }

    static bool IsWide(const Type& type)
    {
        return type.IsValid() && !type.IsVoid() && type.Bits > 32;
    }

    static std::string ReplaceDoublePoints(std::string text)
    {
        const std::string target = "::";
        const std::string replacement = "_";
        std::size_t position = 0;

        while ((position = text.find(target, position)) != std::string::npos)
        {
            text.replace(position, target.length(), replacement);
            position += replacement.length();
        }

        return text;
    }

    void ResetFunction()
    {
        funcName.clear();
        funcRet = Type{};
        tempCounter = 0;
        localAliases.clear();
    }
};

} // namespace Emux
