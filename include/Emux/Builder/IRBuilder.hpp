#pragma once

#include "Emux/Builder/IBuilder.hpp"
#include "Emux/Builder/IRContext.hpp"
#include "Emux/Compiler/Semantic/FunctionSignature.hpp"
#include "Emux/Compiler/Semantic/ImportInfo.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Emux
{

class Program;
class Node;
class FunctionNode;
class FunctionCallNode;
class ReturnNode;
class VariableNode;
class AssignmentNode;
class BinaryNode;
class UnaryNode;
struct Type;

class IRBuilder : public IBuilder
{
public:
    IRBuilder();
    virtual ~IRBuilder();

    virtual std::string Build(const Program& program) override;

    // globalVariables permite declarar (como extvars) as globais de outros
    // módulos usadas aqui, para o backend conhecer tipo e tamanho.
    // imports traz as funções nativas declaradas em qualquer módulo.
    // functions traz as assinaturas globais (ABI e tipos de retorno), para que
    // chamadas a outros módulos usem a convenção correta.
    std::string Build(
        const Program& program,
        const std::unordered_map<std::string, Type>* globalVariables,
        const std::unordered_map<std::string, ImportInfo>* imports = nullptr,
        const std::unordered_map<std::string, FunctionSignature>* functions = nullptr,
        std::uint32_t pointerBits = 64
    );

private:
    void BuildFunction(const FunctionNode& node);
    void BuildImport(const FunctionNode& node);
    void BuildExport(const Node& node, std::string_view irName, std::string_view rawName);
    void BuildReturn(const ReturnNode& node);
    void BuildVariable(const VariableNode& node);
    void BuildAssignment(const AssignmentNode& node);
    void BuildExpression(Node& node);

private:
    std::unique_ptr<IRContext> m_Context;
};

}
