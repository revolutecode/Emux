#include "Emux/Generator/NasmGeneratorBase.hpp"
#include "Emux/Generator/NasmToolchain.hpp"

#include <fstream>

namespace Emux
{

NasmGeneratorBase::NasmGeneratorBase(Architecture arch)
    : m_Arch(arch),
      m_PointerBits(arch == Architecture::X86 ? 32 : 64)
{
}

std::string NasmGeneratorBase::TranslateModule(const Exir::Module& module)
{
    m_CurrentModule = &module;
    std::ostringstream out;

    out << "default rel\n";
    out << "bits " << m_PointerBits << "\n\n";

    // Externs de imports
    for (const auto& imp : module.Imports)
    {
        out << "extern " << imp.Symbol << "\n";
    }

    out << "\nsection .text\n";

    for (const auto& func : module.Functions)
    {
        if (func)
        {
            out << "global " << func->Name << "\n";
            EmitFunction(out, *func);
            out << "\n";
        }
    }

    // Seção de dados
    if (!module.Globals.empty())
    {
        out << "\nsection .data\n";
        for (const auto& g : module.Globals)
        {
            if (!g.IsConst)
            {
                out << "@global_" << g.Id << ":\n";
                out << g.Name << ":\n";
                if (!g.InitialData.empty())
                {
                    out << "    db ";
                    for (size_t i = 0; i < g.InitialData.size(); ++i)
                    {
                        if (i > 0) out << ", ";
                        out << static_cast<int>(g.InitialData[i]);
                    }
                    out << "\n";
                }
                else
                {
                    out << "    times " << std::max<int>(1, g.Ty.GetByteSize()) << " db 0\n";
                }
            }
        }

        out << "\nsection .rdata\n";
        for (const auto& g : module.Globals)
        {
            if (g.IsConst)
            {
                out << "@global_" << g.Id << ":\n";
                out << g.Name << ":\n";
                if (!g.InitialData.empty())
                {
                    out << "    db ";
                    for (size_t i = 0; i < g.InitialData.size(); ++i)
                    {
                        if (i > 0) out << ", ";
                        out << static_cast<int>(g.InitialData[i]);
                    }
                    out << "\n";
                }
                else
                {
                    out << "    times " << std::max<int>(1, g.Ty.GetByteSize()) << " db 0\n";
                }
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
