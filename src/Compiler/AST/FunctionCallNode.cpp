#include <Emux/Compiler/AST/FunctionCallNode.hpp>
#include <Emux/Compiler/AST/NodeType.hpp>

namespace Emux
{
	
FunctionCallNode::FunctionCallNode(
	const Token& name,
    const SourceLocation& location
):
	Node(NodeType::FunctionCall, name, location)
{
}

}