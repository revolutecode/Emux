#include "Emux/Generator/NasmGeneratorBase.hpp"
#include "Emux/Generator/NasmUtils.hpp"
#include "Emux/Compiler/Type/Type.hpp"

#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>

namespace Emux
{

namespace
{

const std::vector<std::string> kNoAliases;

}

NasmGeneratorBase::NasmGeneratorBase(Architecture arch)
    : m_Arch(arch),
      m_PointerBits(arch == Architecture::X86 ? 32 : 64)
{
}

std::string NasmGeneratorBase::Translate(std::string_view code)
{
    m_Module = Exir::Parse(code, m_PointerBits);

    m_Functions.clear();
    m_Imports.clear();
    m_Globals.clear();
    m_Externals.clear();
    m_Data.clear();
    m_ExportAliases.clear();
    m_Externs.clear();
    m_DefinedSymbols.clear();

    for (const auto& function : m_Module.Functions)
    {
        m_Functions[function.Name] = &function;
    }

    for (const auto& import : m_Module.Imports)
    {
        m_Imports[import.Name] = &import;
    }

    for (const auto& global : m_Module.Globals)
    {
        m_Globals[global.Name] = &global;
    }

    for (const auto& global : m_Module.Externals)
    {
        m_Externals[global.Name] = &global;
    }

    for (const auto& block : m_Module.Data)
    {
        m_Data[block.Label] = &block;
    }

    for (const auto& exportSymbol : m_Module.Exports)
    {
        m_ExportAliases[exportSymbol.Name].push_back(exportSymbol.Alias);
    }

    for (const auto& function : m_Module.Functions)
    {
        m_DefinedSymbols.insert(FunctionSymbol(function));
    }

    for (const auto& global : m_Module.Globals)
    {
        m_DefinedSymbols.insert(GlobalSymbol(global.Name));
    }

    for (const auto& block : m_Module.Data)
    {
        m_DefinedSymbols.insert(GlobalSymbol(block.Label));
    }

    for (const auto& [name, aliases] : m_ExportAliases)
    {
        for (const std::string& alias : aliases)
        {
            m_DefinedSymbols.insert(GlobalSymbol(alias));
        }
    }

    std::ostringstream text;
    for (const auto& function : m_Module.Functions)
    {
        m_CurrentFunction = &function;
        EmitFunction(text, function);
    }
    m_CurrentFunction = nullptr;
    EmitArchHelpers(text);

    std::set<std::string> sortedGlobals(m_DefinedSymbols.begin(), m_DefinedSymbols.end());
    std::set<std::string> sortedExterns;
    for (const std::string& symbol : m_Externs)
    {
        if (!m_DefinedSymbols.contains(symbol))
        {
            sortedExterns.insert(symbol);
        }
    }

    std::ostringstream out;
    if (m_Arch == Architecture::X86_64)
    {
        out << "default rel\n";
    }

    for (const std::string& symbol : sortedGlobals)
    {
        out << "global " << symbol << '\n';
    }

    for (const std::string& symbol : sortedExterns)
    {
        out << "extern " << symbol << '\n';
    }

    out << "\nsection .text\n" << text.str() << '\n';

    if (!m_Module.Data.empty())
    {
        out << "section .data\n";
        for (const auto& block : m_Module.Data)
        {
            out << GlobalSymbol(block.Label) << ":\n";
            out << "    db ";
            if (block.Bytes.empty())
            {
                out << '0';
            }
            else
            {
                for (std::size_t i = 0; i < block.Bytes.size(); ++i)
                {
                    if (i > 0)
                    {
                        out << ", ";
                    }
                    out << block.Bytes[i];
                }
            }
            out << '\n';
        }
        out << '\n';
    }

    if (!m_Module.Globals.empty())
    {
        out << "section .bss\n";
        for (const auto& global : m_Module.Globals)
        {
            out << GlobalSymbol(global.Name) << ":\n";
            for (const std::string& alias : ExportAliases(global.Name))
            {
                out << GlobalSymbol(alias) << ":\n";
            }

            const int bits = global.Ty.IsValid() ? static_cast<int>(global.Ty.Bits) : 32;
            out << "    res" << (bits <= 8 ? "b" : bits <= 16 ? "w" : bits <= 32 ? "d" : "q") << " 1\n";
        }
        out << '\n';
    }

    return out.str();
}

int NasmGeneratorBase::Generate(
    std::string_view code,
    const std::filesystem::path& output
)
{
    const std::string text = Translate(code);

    std::ofstream file(output, std::ios::trunc);
    if (!file)
    {
        throw std::runtime_error("Could not create assembly file - " + output.string());
    }

    file << text;
    if (!file)
    {
        throw std::runtime_error("Could not write assembly file - " + output.string());
    }

    return 0;
}

std::string NasmGeneratorBase::GlobalSymbol(std::string_view name) const
{
    return NasmUtils::Exported(name, m_Arch);
}

const std::vector<std::string>& NasmGeneratorBase::ExportAliases(std::string_view name) const
{
    if (auto it = m_ExportAliases.find(std::string(name)); it != m_ExportAliases.end())
    {
        return it->second;
    }

    return kNoAliases;
}

const Exir::Function* NasmGeneratorBase::FindFunction(std::string_view name) const
{
    if (auto it = m_Functions.find(std::string(name)); it != m_Functions.end())
    {
        return it->second;
    }

    return nullptr;
}

const Exir::Import* NasmGeneratorBase::FindImport(std::string_view name) const
{
    if (auto it = m_Imports.find(std::string(name)); it != m_Imports.end())
    {
        return it->second;
    }

    return nullptr;
}

const Exir::Global* NasmGeneratorBase::FindGlobal(std::string_view name) const
{
    if (auto it = m_Globals.find(std::string(name)); it != m_Globals.end())
    {
        return it->second;
    }

    return nullptr;
}

bool NasmGeneratorBase::IsDataLabel(std::string_view name) const
{
    return m_Data.contains(std::string(name));
}

bool NasmGeneratorBase::IsParameter(std::string_view name) const
{
    if (m_CurrentFunction == nullptr)
    {
        return false;
    }

    for (const Exir::Parameter& parameter : m_CurrentFunction->Params)
    {
        if (parameter.Name == name)
        {
            return true;
        }
    }

    return false;
}

void NasmGeneratorBase::AddExtern(const std::string& symbol)
{
    m_Externs.insert(symbol);
}

bool NasmGeneratorBase::IsDefined(const std::string& symbol) const
{
    return m_DefinedSymbols.contains(symbol);
}

bool NasmGeneratorBase::IsLiteralOperand(const std::string& text)
{
    return !text.empty() &&
        (std::isdigit(static_cast<unsigned char>(text[0])) ||
         text[0] == '-' || text[0] == '+');
}

bool NasmGeneratorBase::IsTempOperand(const std::string& text)
{
    return !text.empty() && text.front() == '%';
}

std::string NasmGeneratorBase::StripTemp(const std::string& text)
{
    if (IsTempOperand(text))
    {
        return text.substr(1);
    }

    return text;
}

std::string NasmGeneratorBase::NasmSize(int bits)
{
    if (bits <= 8) return "byte";
    if (bits <= 16) return "word";
    if (bits <= 32) return "dword";
    return "qword";
}

std::string NasmGeneratorBase::FormatOffset(int offset)
{
    return (offset >= 0 ? "+" : "") + std::to_string(offset);
}

} // namespace Emux
