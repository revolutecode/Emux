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

    // Mapeamento de funcoes importadas para seus simbolos nativos externos
    std::unordered_map<std::string, std::string> importSymbols;

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
