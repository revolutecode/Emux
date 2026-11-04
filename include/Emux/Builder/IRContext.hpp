#pragma once

#include "Emux/Compiler/Semantic/FunctionSignature.hpp"
#include "Emux/Compiler/Type/Type.hpp"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Emux
{

// Estado partilhado pelo construtor da Exir v2. A IR guarda valores e
// operações tipadas; registradores são responsabilidade do backend.
class IRContext
{
public:
    std::uint32_t pointerBits = 64;

    // Quando ativo (nivel >= -O1), o IRBuilder poda na origem o codigo nao
    // usado: imports nunca chamados nao sao emitidos e uma expressao-instrucao
    // descartada e sem efeitos (ex: static_cast<i32>(g)) nem chega ao Exir.
    // Em -O0 permanece falso, preservando o contrato "preserva tudo".
    bool optimize = false;

    std::ostringstream ir;
    std::size_t tempCounter = 0;

    // Rotulos de controlo de fluxo. O prefixo ".Lctl_" nao colide com os rotulos
    // internos do gerador (wext/wsen/truncd via NewLabel); sao locais a funcao,
    // portanto o contador e redefinido em ResetFunction.
    std::size_t labelCounter = 0;

    std::string NewLabel()
    {
        return ".Lctl_" + std::to_string(labelCounter++);
    }

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

    // Blocos 'data' para literais de string. A chave é o conteúdo sem o NUL
    // final, para que literais iguais partilhem o mesmo bloco.
    struct StringLiteral
    {
        std::string Label;
        std::string Bytes;
    };

    std::vector<StringLiteral> stringLiterals;
    std::unordered_map<std::string, std::string> stringLabels;

    std::string InternString(std::string_view text)
    {
        if (auto it = stringLabels.find(std::string(text)); it != stringLabels.end())
        {
            return it->second;
        }

        const std::string label = "__emux_str" + std::to_string(stringLiterals.size());

        std::string bytes;
        for (const unsigned char byte : text)
        {
            bytes += std::to_string(byte);
            bytes += ", ";
        }
        bytes += '0';

        stringLiterals.push_back({ label, bytes });
        stringLabels.emplace(std::string(text), label);
        return label;
    }

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
        labelCounter = 0;
        localAliases.clear();
    }
};

} // namespace Emux
