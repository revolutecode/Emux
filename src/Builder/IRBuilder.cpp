#include "Emux/Builder/IRBuilder.hpp"
#include "Emux/Compiler/AST/FunctionNode.hpp"
#include "Emux/Compiler/AST/IRNode.hpp"
#include "Emux/Compiler/AST/NodeType.hpp"
#include "Emux/Compiler/AST/Program.hpp"
#include "Emux/Compiler/AST/ReturnNode.hpp"
#include "Emux/Compiler/AST/SectionNode.hpp"
#include <sstream>

namespace Emux
{

IRBuilder::IRBuilder()=default;
IRBuilder::~IRBuilder()=default;

std::string IRBuilder::Build(const Program& program)
{
    std::ostringstream ir;
    for (const auto& ptr : program.Children)
    {
        if (!ptr) continue;

        Node& node = *ptr;
        ir << BuildSection(static_cast<SectionNode&>(node));
    }

    return ir.str();
}

std::string IRBuilder::BuildSection(const SectionNode& node)
{
    std::ostringstream ir;
    for (const auto& ptr : node.Children)
    {
        if (!ptr) continue;
        Node& node = *ptr;

        switch(node.GetType())
        {
            case Emux::NodeType::Function:
            {
                FunctionNode& function = static_cast<FunctionNode&>(node);
                ir << BuildFunction(function);
                break;
            }
            case Emux::NodeType::IR:
            {
                IRNode& irNode = static_cast<IRNode&>(node);
                ir << irNode.Code.Text;
                break;
            }
            default:
            {
                break;
            }
        };
    }

    return ir.str();
}

std::string IRBuilder::BuildFunction(const FunctionNode& function)
{
    std::ostringstream ir;
   
    ir << function.Name.Text << ":\n";
    
    for (const auto& ptr : function.Children)
    {
        if (!ptr) continue;
        Node& node = *ptr;

        switch(node.GetType())
        {
            case Emux::NodeType::IR:
            {
                IRNode& irNode = static_cast<IRNode&>(node);
                ir << "\n" << irNode.Code.Text << "\n";
                break;
            }
            case Emux::NodeType::Return:
            {
                ReturnNode& rNode = static_cast<ReturnNode&>(node);
                ir << "ret" << "\n";
                break;
            }
            default:
            {
                break;
            }
        };
    }

    return ir.str();
}

}
