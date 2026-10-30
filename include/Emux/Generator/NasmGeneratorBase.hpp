#pragma once

#include "Emux/Exir/Exir.hpp"
#include "Emux/Generator/IGenerator.hpp"
#include "Emux/Core/Arch.hpp"

#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Emux
{

// Mapa de slots da frame: cada parâmetro e cada temporário ocupa 8 bytes.
struct StackSlots
{
    int Next = 0;
    std::unordered_map<std::string, int> Offsets;

    int Allocate(const std::string& name)
    {
        if (auto it = Offsets.find(name); it != Offsets.end())
        {
            return it->second;
        }

        const int offset = -(++Next) * 8;
        Offsets[name] = offset;
        return offset;
    }

    bool Find(const std::string& name, int& offset) const
    {
        if (auto it = Offsets.find(name); it != Offsets.end())
        {
            offset = it->second;
            return true;
        }

        return false;
    }

    int Bytes() const
    {
        return Next * 8;
    }
};

// Base dos geradores NASM: tabelas de símbolos, decoração, secções e a
// interface IGenerator. Cada arquitetura implementa apenas a baixagem das
// instruções Exir para registradores e pilha.
class NasmGeneratorBase : public IGenerator
{
public:
    explicit NasmGeneratorBase(Architecture arch);
    ~NasmGeneratorBase() override = default;

    std::string Translate(std::string_view code);
    int Generate(std::string_view code, const std::filesystem::path& output) override;

protected:
    virtual void EmitFunction(std::ostringstream& text, const Exir::Function& function) = 0;
    virtual void EmitArchHelpers(std::ostringstream& text) {}
    virtual std::string FunctionSymbol(const Exir::Function& function) const = 0;
    virtual std::string CallSymbol(const std::string& target) const = 0;

    // Símbolo de dados/globais/labels com a decoração da arquitetura.
    std::string GlobalSymbol(std::string_view name) const;

    const std::vector<std::string>& ExportAliases(std::string_view name) const;
    const Exir::Function* FindFunction(std::string_view name) const;
    const Exir::Import* FindImport(std::string_view name) const;
    const Exir::Global* FindGlobal(std::string_view name) const;
    bool IsDataLabel(std::string_view name) const;
    // Parâmetros vivem no slot da frame como valores, não como ponteiros.
    bool IsParameter(std::string_view name) const;

    void AddExtern(const std::string& symbol);
    bool IsDefined(const std::string& symbol) const;

    static bool IsLiteralOperand(const std::string& text);
    static bool IsTempOperand(const std::string& text);
    static std::string StripTemp(const std::string& text);
    static std::string NasmSize(int bits);
    static std::string FormatOffset(int offset);

    Architecture m_Arch;
    std::uint32_t m_PointerBits = 64;
    Exir::Module m_Module;
    const Exir::Function* m_CurrentFunction = nullptr;

    std::unordered_map<std::string, const Exir::Function*> m_Functions;
    std::unordered_map<std::string, const Exir::Import*> m_Imports;
    std::unordered_map<std::string, const Exir::Global*> m_Globals;
    std::unordered_map<std::string, const Exir::Global*> m_Externals;
    std::unordered_map<std::string, const Exir::DataBlock*> m_Data;
    std::unordered_map<std::string, std::vector<std::string>> m_ExportAliases;

    std::unordered_set<std::string> m_Externs;
    std::unordered_set<std::string> m_DefinedSymbols;
};

} // namespace Emux
