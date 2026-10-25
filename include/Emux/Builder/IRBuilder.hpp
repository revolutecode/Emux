#pragma once

#include "Emux/Builder/IBuilder.hpp"
#include "Emux/Builder/IRContext.hpp"
#include <memory>
#include <string>

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

class IRBuilder : public IBuilder
{
public:
    IRBuilder();
    virtual ~IRBuilder();

    virtual std::string Build(const Program& program) override;

private:
    void BuildFunction(const FunctionNode& node);
    void BuildReturn(const ReturnNode& node);
    void BuildVariable(const VariableNode& node);
    void BuildAssignment(const AssignmentNode& node);
    void BuildExpression(Node& node);

private:
    std::unique_ptr<IRContext> m_Context;
};

}
