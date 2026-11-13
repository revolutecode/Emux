#pragma once

#include "Emux/Compiler/Semantic/FunctionSignature.hpp"
#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Exir/Exir.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Emux
{

class IRContext
{
public:
    std::uint32_t pointerBits = 64;
    bool optimize = false;

    // Modulo Exir sob construcao
    std::unique_ptr<Exir::Module> CurrentModule;

    // Funcao e bloco basico atuais
    Exir::Function* CurrentFunction = nullptr;
    Exir::BasicBlock* CurrentBlock = nullptr;

    std::uint32_t tempCounter = 0;
    std::uint32_t blockCounter = 0;
    std::uint32_t localCounter = 0;
    std::uint32_t globalCounter = 0;

    // Alvos de break/continue para loops (BasicBlock pointers)
    struct LoopTargets
    {
        Exir::BasicBlock* BreakBlock = nullptr;
        Exir::BasicBlock* ContinueBlock = nullptr;
    };
    std::vector<LoopTargets> loopStack;

    const LoopTargets* InnerLoop() const
    {
        return loopStack.empty() ? nullptr : &loopStack.back();
    }

    std::string funcName;
    Type funcRet;

    // Valores constantes conhecidos para folding
    std::unordered_map<std::string, int64_t> constValues;

    const std::unordered_map<std::string, Type>* globalTypes = nullptr;
    std::unordered_map<std::string, const FunctionSignature*> functionSignatures;

    // Mapeamento de nomes de variaveis locais para seus IDs de slot e tipos
    std::unordered_map<std::string, std::uint32_t> localSlotIds;
    std::unordered_map<std::string, Type> localTypes;

    // Mapeamento de nomes de variaveis globais para seus IDs
    std::unordered_map<std::string, std::uint32_t> globalIds;

    // Globais declarados noutro ficheiro do projecto. Os IDs vivem num
    // intervalo disjointe (bit alto) para que @global_<id> nunca colida com um
    // global deste modulo; o gerador emite-os como 'extern' pelo nome.
    std::unordered_map<std::string, std::uint32_t> externGlobalIds;
    std::uint32_t externGlobalCounter = 0x80000000u;

    // Nome cru, nome IR e nome sem namespace: os tres podem aparecer numa
    // referencia, consoante a forma como a declaracao foi recolhida.
    std::optional<std::uint32_t> FindOrImportGlobal(const std::string& name)
    {
        if (auto id = FindGlobalId(name); id.has_value())
        {
            return id;
        }

        const std::string irName = ReplaceDoublePoints(name);
        const std::size_t lastColon = name.rfind("::");
        const std::string base = lastColon == std::string::npos
            ? std::string()
            : name.substr(lastColon + 2);

        for (const std::string& key : { name, irName, base })
        {
            if (auto it = externGlobalIds.find(key); it != externGlobalIds.end())
            {
                return it->second;
            }
        }

        if (!globalTypes)
        {
            return std::nullopt;
        }

        auto decl = globalTypes->find(name);
        if (decl == globalTypes->end()) decl = globalTypes->find(irName);
        if (decl == globalTypes->end()) decl = globalTypes->find(base);
        if (decl == globalTypes->end())
        {
            return std::nullopt;
        }

        Exir::Global g;
        g.Id = externGlobalCounter++;
        g.Name = irName;
        g.Ty = decl->second;

        const std::uint32_t id = g.Id;
        externGlobalIds.emplace(name, id);
        externGlobalIds.emplace(irName, id);
        if (CurrentModule)
        {
            CurrentModule->ExternalGlobals.push_back(std::move(g));
        }
        return id;
    }

    // Mapeamento de funcoes importadas para seus simbolos nativos externos
    std::unordered_map<std::string, std::string> importSymbols;

    // Literais de string interados: a chave e o conteudo, o valor e o id do
    // global de dados. Literais iguais partilham um bloco, pelo que o label
    // __emux_strN e unico por modulo e nunca exportado.
    std::unordered_map<std::string, std::uint32_t> stringLabels;

    std::uint32_t InternString(const std::string& text)
    {
        if (auto it = stringLabels.find(text); it != stringLabels.end())
        {
            return it->second;
        }

        Exir::Global g;
        g.Id = globalCounter++;
        g.Name = "__emux_str" + std::to_string(stringLabels.size());
        g.Ty = Type::MakeBuffer(static_cast<std::uint32_t>(text.size() + 1) * 8);
        g.IsConst = true;
        for (const char c : text)
        {
            g.InitialData.push_back(static_cast<std::uint8_t>(c));
        }
        g.InitialData.push_back(0);

        const std::uint32_t id = g.Id;
        stringLabels.emplace(text, id);
        if (CurrentModule)
        {
            CurrentModule->Globals.push_back(std::move(g));
        }
        return id;
    }

    std::optional<std::uint32_t> FindInternedString(const std::string& text) const
    {
        if (auto it = stringLabels.find(text); it != stringLabels.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    // Um literal com mais de 8 bytes nao cabe no imediato de um Value: vive
    // como bytes na .rdata e chega ao codigo por addr+load. A chave e o proprio
    // conteudo, pelo que literais iguais partilham o bloco.
    std::unordered_map<std::string, std::uint32_t> wideConstantIds;

    std::uint32_t InternWideConstant(std::vector<std::uint8_t> bytes, const Type& ty)
    {
        std::string key;
        key.reserve(bytes.size() + 8);
        key.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        key += '|';
        key += std::to_string(ty.Bits);

        if (auto it = wideConstantIds.find(key); it != wideConstantIds.end())
        {
            return it->second;
        }

        Exir::Global g;
        g.Id = globalCounter++;
        g.Name = "__emux_c" + std::to_string(wideConstantIds.size());
        g.Ty = ty;
        g.IsConst = true;
        g.InitialData = std::move(bytes);

        const std::uint32_t id = g.Id;
        wideConstantIds.emplace(std::move(key), id);
        if (CurrentModule)
        {
            CurrentModule->Globals.push_back(std::move(g));
        }
        return id;
    }

    std::uint32_t NewTempId()
    {
        return tempCounter++;
    }

    Exir::BasicBlock* CreateBlock(std::string name = "")
    {
        if (!CurrentFunction) return nullptr;
        auto bb = std::make_unique<Exir::BasicBlock>();
        bb->Id = blockCounter++;
        bb->Name = std::move(name);
        Exir::BasicBlock* ptr = bb.get();
        CurrentFunction->Blocks.push_back(std::move(bb));
        return ptr;
    }

    void SetInsertPoint(Exir::BasicBlock* bb)
    {
        CurrentBlock = bb;
    }

    void Emit(Exir::Instruction inst)
    {
        if (CurrentBlock)
        {
            if (inst.IsTerminator())
            {
                CurrentBlock->Terminator = std::move(inst);
            }
            else
            {
                CurrentBlock->Instructions.push_back(std::move(inst));
            }
        }
    }

    std::optional<std::uint32_t> FindLocalSlot(const std::string& name) const
    {
        if (auto it = localSlotIds.find(name); it != localSlotIds.end()) return it->second;
        auto lastColon = name.rfind("::");
        if (lastColon != std::string::npos)
        {
            std::string base = name.substr(lastColon + 2);
            if (auto it = localSlotIds.find(base); it != localSlotIds.end()) return it->second;
        }
        return std::nullopt;
    }

    Type FindLocalType(const std::string& name) const
    {
        if (auto it = localTypes.find(name); it != localTypes.end()) return it->second;
        auto lastColon = name.rfind("::");
        if (lastColon != std::string::npos)
        {
            std::string base = name.substr(lastColon + 2);
            if (auto it = localTypes.find(base); it != localTypes.end()) return it->second;
        }
        return Type::MakeInvalid();
    }

    std::optional<std::uint32_t> FindGlobalId(const std::string& name) const
    {
        if (auto it = globalIds.find(name); it != globalIds.end()) return it->second;
        auto lastColon = name.rfind("::");
        if (lastColon != std::string::npos)
        {
            std::string base = name.substr(lastColon + 2);
            if (auto it = globalIds.find(base); it != globalIds.end()) return it->second;
        }
        return std::nullopt;
    }

    std::optional<Type> LookupType(const std::string& name) const
    {
        if (auto local = localTypes.find(name); local != localTypes.end())
        {
            return local->second;
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
        CurrentFunction = nullptr;
        CurrentBlock = nullptr;
        tempCounter = 0;
        blockCounter = 0;
        localCounter = 0;
        localSlotIds.clear();
        localTypes.clear();
        loopStack.clear();
    }
};

} // namespace Emux
