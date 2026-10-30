#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Compiler/Semantic/FunctionSignature.hpp"

namespace Emux
{

class IRContext
{
public:
    std::array<bool, 4> regs = { false, false, false, false };
    std::array<bool, 4> permRegs = { false, false, false, false };
    std::string funcRetType;
    Type funcRet;
    std::string funcName;
    std::ostringstream ir;
    std::string lastBinary;

    // Largura de ponteiros da arquitetura alvo (32 em x86, 64 em x86_64).
    std::uint32_t pointerBits = 64;

    // Tipos dos parâmetros da função atual (nome cru -> tipo) e das globais
    // visíveis, para dimensionar leituras/escritas de 64 bits.
    std::unordered_map<std::string, Type> localTypes;
    const std::unordered_map<std::string, Type>* globalTypes = nullptr;

    // Assinaturas por nome IR (ex: "main_AddShift"), usadas para coagir
    // argumentos no call site ao tipo do parâmetro.
    std::unordered_map<std::string, const FunctionSignature*> functionSignatures;

    std::size_t tempCounter = 0;

    std::string NewTemp(const Type& type)
    {
        std::string name = "__emux_t_" + (funcName.empty() ? std::string("m") : funcName);
        name += "_";
        name += std::to_string(tempCounter++);
        ir << "vars " << name << ':' << type.ToString() << '\n';
        return name;
    }

    std::optional<Type> LookupType(const std::string& name) const
    {
        if (auto it = localTypes.find(name); it != localTypes.end())
        {
            return it->second;
        }

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

    // Tabela de valores constantes conhecidos para propagação (AttrId::Const)
    std::unordered_map<std::string, int64_t> constValues;

    // Parâmetros são emitidos no IR com o nome cru ("a"), mas as referências
    // no código usam o nome escopado ("main::a"). O alias traduz um no outro.
    std::unordered_map<std::string, std::string> localAliases;

    // Funções [[import]] usam o nome Emux escopado internamente, mas devem
    // chamar o símbolo nativo. Mapeia "windows_GetStdHandle" -> "GetStdHandle".
    std::unordered_map<std::string, std::string> importAliases;

    void ResetFunction()
    {
        regs.fill(false);
        permRegs.fill(false);
        funcRetType.clear();
        funcRet = Type{};
        funcName.clear();
        lastBinary.clear();
        localAliases.clear();
        localTypes.clear();
        tempCounter = 0;
    }

    std::optional<std::string> GetFreeReg()
    {
        for (size_t i = 0; i < regs.size(); i++)
        {
            if (regs[i]) continue;

            regs[i] = true;
            permRegs[i] = true;
            std::string value(1, 'r');
            value += std::to_string(i);
            return value;
        }

        return std::nullopt;
    }

    void ReleaseReg(std::string_view reg)
    {
        if (reg.size() < 2) return;
        if (!std::isdigit(static_cast<unsigned char>(reg[1]))) return;

        int i = reg[1] - '0';
        if (i >= 0 && i < 4)
        {
            regs[i] = false;
        }
    }

    std::string UsedRegisters() const
    {
        std::string str;
        for (size_t i = 0; i < permRegs.size(); i++)
        {
            if (!permRegs[i]) continue;

            str += 'r';
            str += std::to_string(i);
            str += ',';
        }

        if (!str.empty()) str.pop_back();
        return str;
    }

    static bool LooksLikeReg(std::string_view str)
    {
        if (str.size() < 2) return false;

        bool isReg = str[0] == 'r' && std::isdigit(static_cast<unsigned char>(str[1]));
        if (str.size() > 2)
        {
            isReg |= (str[0] == 'r' && str[1] == 'r' && std::isdigit(static_cast<unsigned char>(str[2])));
        }
        return isReg;
    }

    static std::string ReplaceDoublePoints(std::string text)
    {
        const std::string alvo = "::";
        const std::string novo = "_";
        size_t pos = 0;

        while ((pos = text.find(alvo, pos)) != std::string::npos)
        {
            text.replace(pos, alvo.length(), novo);
            pos += novo.length();
        }

        return text;
    }
};

} // namespace Emux
