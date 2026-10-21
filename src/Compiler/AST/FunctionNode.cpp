#include <Emux/Compiler/AST/FunctionNode.hpp>

namespace Emux
{

FunctionNode::FunctionNode(
    const Token& name,
    const SourceLocation& location
):
    Node(NodeType::Function, name, location)
{
}

FunctionNode::FunctionNode(
    const Token& returnType,
    const Token& name,
    const SourceLocation& location
):
    Node(NodeType::Function, name, location),
    ReturnType(returnType)
{
}
/*
void FunctionNode::Accept(ASTVisitor* visitor)
{
    if (visitor)
    {
        visitor->Visit(*this);
    }
}
*/
} // namespace Emux