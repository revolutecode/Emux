#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Emux
{

class IRContext
{
public:
    std::array<bool, 4> regs = { false, false, false, false };
    std::array<bool, 4> permRegs = { false, false, false, false };
    std::string funcRetType;
    std::ostringstream ir;
    std::string lastBinary;

    // Tabela de valores constantes conhecidos para propagação (AttrId::Const)
    std::unordered_map<std::string, int64_t> constValues;

    void ResetFunction()
    {
        regs.fill(false);
        funcRetType.clear();
        lastBinary.clear();
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
