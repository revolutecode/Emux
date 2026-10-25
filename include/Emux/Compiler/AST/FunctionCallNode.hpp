#pragma once
#include <vector>

#include <Emux/Compiler/AST/Node.hpp>

#include <Emux/Compiler/Token.hpp>
#include <Emux/Compiler/SourceLocation.hpp>

namespace Emux
{
	
class FunctionCallNode final: public Node
{
public:
	FunctionCallNode(
		const Token& name,
        const SourceLocation& location
	);
};

}