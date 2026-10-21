#pragma once
#include <vector>

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>
#include <Emux/Compiler/SourceLocation.hpp>

namespace Emux
{
	
class VariableCallNode final: public Node
{
public:
	VariableCallNode(
		const Token& name,
        const SourceLocation& location
	): 
		Node(NodeType::VariableCall, name, location)
	{	
	}

	VariableCallNode(
        const SourceLocation& location
	): 
		Node(NodeType::VariableCall, {}, location)
	{	
	}
};

}
