#include "Emux/Generator/NasmGeneratorBase.hpp"
#include "Emux/Generator/NasmToolchain.hpp"
#include "Emux/Generator/NasmUtils.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Emux
{

NasmGeneratorBase::NasmGeneratorBase(Architecture arch)
    : m_Arch(arch),
      m_PointerBits(arch == Architecture::X86 ? 32 : 64)
{
}

std::string NasmGeneratorBase::GlobalSymbol(std::string_view name) const
{
    return NasmUtils::Exported(name, m_Arch);
}

std::string NasmGeneratorBase::GlobalLabel(const Exir::Value& value) const
{
    if (m_CurrentModule)
    {
        for (const auto& external : m_CurrentModule->ExternalGlobals)
        {
            if (external.Id == value.Id)
            {
                return GlobalSymbol(external.Name);
            }
        }
    }

    return "@global_" + std::to_string(value.Id);
}

void NasmGeneratorBase::AddExtern(std::string_view symbol)
{
    m_AutoExterns.insert(GlobalSymbol(symbol));
}

namespace
{

// Os blocos de literais de string e de constantes largas sao privados do
// modulo: exporta-los faria colidir __emux_str0 entre modulos diferentes.
bool IsPrivateDataLabel(const std::string& name)
{
    return name.rfind("__emux_str", 0) == 0 || name.rfind("__emux_c", 0) == 0;
}

void EmitBytes(std::ostringstream& out, const std::vector<std::uint8_t>& bytes)
{
    out << "    db ";
    if (bytes.empty())
    {
        out << '0';
    }
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        if (i > 0) out << ", ";
        out << static_cast<int>(bytes[i]);
    }
    out << '\n';
}

} // namespace

std::string NasmGeneratorBase::TranslateModule(const Exir::Module& module)
{
    m_CurrentModule = &module;

    // Aliases [[export]] indexados pelo nome IR do simbolo.
    std::map<std::string, std::vector<std::string>> aliases;
    for (const auto& exported : module.Exports)
    {
        aliases[exported.Name].push_back(exported.Alias);
    }

    auto appendAliases = [&aliases, this](std::ostringstream& out, const std::string& name)
    {
        auto it = aliases.find(name);
        if (it == aliases.end())
        {
            return;
        }
        for (const std::string& alias : it->second)
        {
            // EmitFunction ja escreve o label do simbolo; repetir o mesmo
            // nome produziria um label duplicado, que o nasm recusa.
            const std::string decorated = GlobalSymbol(alias);
            if (decorated == GlobalSymbol(name))
            {
                continue;
            }
            out << decorated << ":\n";
        }
    };

    // Cada global tem dois labels: o @global_<id> pelo qual a Exir referencia
    // a regiao e o nome decorado, simbolo visivel entre modulos.
    auto emitLabel = [this](std::ostringstream& out, const Exir::Global& g)
    {
        out << "@global_" << g.Id << ":\n";
        out << GlobalSymbol(g.Name) << ":\n";
    };

    // Simbolos definidos por este modulo, para o bloco de 'global'.
    std::set<std::string> defined;
    for (const auto& func : module.Functions)
    {
        if (func) defined.insert(GlobalSymbol(func->Name));
    }
    for (const auto& g : module.Globals)
    {
        if (!IsPrivateDataLabel(g.Name)) defined.insert(GlobalSymbol(g.Name));
    }
    for (const auto& exported : module.Exports)
    {
        defined.insert(GlobalSymbol(exported.Alias));
    }

    // Simbolos que este modulo nao define: globais de outros ficheiros e
    // alvos de call fora do modulo. O linker so os resolve declarados extern.
    std::set<std::string> externals;
    for (const auto& imp : module.Imports)
    {
        externals.insert(imp.Symbol);
    }
    for (const auto& g : module.ExternalGlobals)
    {
        externals.insert(GlobalSymbol(g.Name));
    }

    {
        std::set<std::string> localFunctions;
        for (const auto& func : module.Functions)
        {
            if (func) localFunctions.insert(func->Name);
        }

        for (const auto& func : module.Functions)
        {
            if (!func)
            {
                continue;
            }
            for (const auto& block : func->Blocks)
            {
                if (!block)
                {
                    continue;
                }

                auto consider = [&](const Exir::Instruction& inst)
                {
                    if (inst.Op != Exir::OpCode::Call || !inst.Extra ||
                        inst.Extra->Text.empty())
                    {
                        return;
                    }
                    if (localFunctions.contains(inst.Extra->Text))
                    {
                        return;
                    }
                    externals.insert(GlobalSymbol(inst.Extra->Text));
                };

                consider(block->Terminator);
                for (const auto& inst : block->Instructions)
                {
                    consider(inst);
                }
            }
        }
    }

    // Os texto gerados partilham a instancia do gerador: o que sobra do
    // modulo anterior nao pode voltar a ser declarado.
    m_AutoExterns.clear();

    std::ostringstream text;
    for (const auto& func : module.Functions)
    {
        if (!func)
        {
            continue;
        }

        // EmitFunction escreve o label com o nome IR cru; so quando a
        // decoracao differe (underscore do x86) e preciso um label extra.
        if (GlobalSymbol(func->Name) != func->Name)
        {
            text << GlobalSymbol(func->Name) << ":\n";
        }
        appendAliases(text, func->Name);
        EmitFunction(text, *func);
        text << "\n";
    }

    EmitArchHelpers(text);

    // As rotinas do runtime numerico sao descobertas durante a emissao, por
    // isso o bloco 'extern' so se fecha depois do corpo.
    for (const std::string& symbol : m_AutoExterns)
    {
        externals.insert(symbol);
    }

    std::ostringstream out;

    if (m_Arch == Architecture::X86_64)
    {
        out << "default rel\n";
    }
    out << "bits " << m_PointerBits << "\n\n";

    for (const std::string& symbol : defined)
    {
        out << "global " << symbol << "\n";
    }

    for (const std::string& symbol : externals)
    {
        out << "extern " << symbol << "\n";
    }

    out << "\nsection .text\n";
    out << text.str();

    std::vector<const Exir::Global*> initializedGlobals;
    std::vector<const Exir::Global*> constGlobals;
    std::vector<const Exir::Global*> reservedGlobals;

    for (const auto& g : module.Globals)
    {
        if (g.IsConst) constGlobals.push_back(&g);
        else if (!g.InitialData.empty()) initializedGlobals.push_back(&g);
        else reservedGlobals.push_back(&g);
    }

    // Mutaveis com inicializador escrevem-se, mas nascem com os bytes do
    // literal: vivem em .data, nao em .bss (que so reserva a zero).
    if (!initializedGlobals.empty())
    {
        out << "\nsection .data\n";
        for (const Exir::Global* g : initializedGlobals)
        {
            emitLabel(out, *g);
            appendAliases(out, g->Name);
            EmitBytes(out, g->InitialData);
        }
    }

    if (!constGlobals.empty())
    {
        out << "\nsection .rdata\n";
        for (const Exir::Global* g : constGlobals)
        {
            emitLabel(out, *g);
            appendAliases(out, g->Name);
            EmitBytes(out, g->InitialData);
        }
    }

    if (!reservedGlobals.empty())
    {
        out << "\nsection .bss\n";
        for (const Exir::Global* g : reservedGlobals)
        {
            emitLabel(out, *g);
            appendAliases(out, g->Name);

            const int bits = g->Ty.IsValid() ? static_cast<int>(g->Ty.Bits) : 32;
            if (g->Ty.IsBuffer() || bits > 64)
            {
                // Regiao estatica de um bloco ou inteiro multi-limb: ocupa o
                // numero de bytes do tipo.
                out << "    resb " << std::max(1, static_cast<int>(g->Ty.GetByteSize())) << "\n";
            }
            else
            {
                out << "    res"
                    << (bits <= 8 ? "b" : bits <= 16 ? "w" : bits <= 32 ? "d" : "q")
                    << " 1\n";
            }
        }
    }

    return out.str();
}

int NasmGeneratorBase::GenerateModule(const Exir::Module& module, const std::filesystem::path& output)
{
    const std::string asmCode = TranslateModule(module);

    std::ofstream file(output, std::ios::trunc);
    if (!file.is_open())
    {
        return 1;
    }
    file << asmCode;
    file.close();

    return 0;
}

int NasmGeneratorBase::Generate(std::string_view, const std::filesystem::path&)
{
    return 1; // Deprecado: compilador agora usa GenerateModule diretamente
}

} // namespace Emux
