#pragma once

#include "Emux/Builder/IBuilder.hpp"
#include <string>

namespace Emux
{

class Program;
class Node;
class SectionNode;
class FunctionNode;
class FunctionCallNode;
class ReturnNode;
class VariableNode;
class VariableCallNode;
class AssignmentNode;
class BinaryNode;

class IRBuilder : IBuilder
{
public:
    IRBuilder();
    ~IRBuilder();

    virtual std::string Build(const Program& program) override;    

private:
    void BuildSection(const SectionNode& node);
    void BuildFunction(const FunctionNode& node);
    void BuildFunctionCall(const FunctionCallNode& node);
    void BuildReturn(const ReturnNode& node);
    void BuildVariable(const VariableNode& node);
    void BuildAssignment(const AssignmentNode& node);
    void BuildBinary(const BinaryNode& node);
    void BuildExpression(Node& node);
    
    std::string HelperGetValue(Node& node);
};

}
