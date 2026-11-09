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

struct StackSlots
{
    int Used = 0;
    std::unordered_map<std::uint32_t, int> LocalOffsets; // Mapeia por Local Slot ID
    std::unordered_map<std::uint32_t, int> TempOffsets;  // Mapeia por Temp ID

    int AllocateLocal(std::uint32_t id, int bytes = 8)
    {
        if (auto it = LocalOffsets.find(id); it != LocalOffsets.end())
        {
            return it->second;
        }

        const int size = bytes > 8 ? ((bytes + 7) & ~7) : 8;
        Used += size;
        const int offset = -Used;
        LocalOffsets[id] = offset;
        return offset;
    }

    int AllocateTemp(std::uint32_t id, int bytes = 8)
    {
        if (auto it = TempOffsets.find(id); it != TempOffsets.end())
        {
            return it->second;
        }

        const int size = bytes > 8 ? ((bytes + 7) & ~7) : 8;
        Used += size;
        const int offset = -Used;
        TempOffsets[id] = offset;
        return offset;
    }

    int Allocate(std::uint32_t id, int bytes = 8)
    {
        return AllocateTemp(id, bytes);
    }

    int GetLocal(std::uint32_t id) const
    {
        if (auto it = LocalOffsets.find(id); it != LocalOffsets.end())
        {
            return it->second;
        }
        return 0;
    }

    int GetTemp(std::uint32_t id) const
    {
        if (auto it = TempOffsets.find(id); it != TempOffsets.end())
        {
            return it->second;
        }
        return 0;
    }

    int Get(std::uint32_t id) const
    {
        return GetTemp(id);
    }

    int GetValue(const Exir::Value& val) const
    {
        if (val.Kind == Exir::ValueKind::LocalSlot)
        {
            return GetLocal(val.Id);
        }
        return GetTemp(val.Id);
    }
};

class NasmGeneratorBase : public IGenerator
{
public:
    explicit NasmGeneratorBase(Architecture arch);
    ~NasmGeneratorBase() override = default;

    int GenerateModule(const Exir::Module& module, const std::filesystem::path& output) override;
    int Generate(std::string_view code, const std::filesystem::path& output) override;

    virtual std::string TranslateModule(const Exir::Module& module);

protected:
    virtual void EmitFunction(std::ostringstream& text, const Exir::Function& function) = 0;
    virtual void EmitArchHelpers(std::ostringstream& text) = 0;

    Architecture m_Arch;
    std::uint32_t m_PointerBits;
    const Exir::Module* m_CurrentModule = nullptr;
    const Exir::Function* m_CurrentFunction = nullptr;
};

} // namespace Emux
