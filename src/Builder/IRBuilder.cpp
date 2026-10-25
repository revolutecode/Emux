#include "Emux/Builder/IRBuilder.hpp"
#include "Emux/Builder/IRExpressionBuilder.hpp"
#include "Emux/Builder/IROptimizer.hpp"
#include "Emux/Compiler/AST/AssignmentNode.hpp"
#include "Emux/Compiler/AST/FunctionNode.hpp"
#include "Emux/Compiler/AST/IRNode.hpp"
#include "Emux/Compiler/AST/NodeType.hpp"
#include "Emux/Compiler/AST/Program.hpp"
#include "Emux/Compiler/AST/ReturnNode.hpp"
#include "Emux/Compiler/AST/VariableCallNode.hpp"
#include "Emux/Compiler/AST/VariableNode.hpp"
#include "Emux/Compiler/AttrSet.hpp"
#include "Emux/Core/Logger.hpp"
#include <cstdlib>

namespace Emux
{

IRBuilder::IRBuilder()
    : m_Context(std::make_unique<IRContext>())
{
}

IRBuilder::~IRBuilder() = default;

std::string IRBuilder::Build(const Program& program)
{
    m_Context = std::make_unique<IRContext>();

    for (const auto& ptr : program.Children)
    {
        if (!ptr) continue;

        Node& node = *ptr;

        switch (node.GetType())
        {
            case NodeType::Function:
                BuildFunction(static_cast<FunctionNode&>(node));
                break;
            default:
                BuildExpression(node);
                break;
        }
    }

    return m_Context->ir.str();
}

void IRBuilder::BuildFunction(const FunctionNode& function)
{
    m_Context->ResetFunction();

    // Otimização com base em AttrSet: noexcept / inline
    if (function.Attrs.Has(AttrId::NoExcept))
    {
        m_Context->ir << "; [attr: noexcept]\n";
    }
    if (function.Attrs.Has(AttrId::Inline) || function.Attrs.Has(AttrId::AlwaysInline))
    {
        m_Context->ir << "; [attr: inline]\n";
    }

    m_Context->ir << "func " << IRContext::ReplaceDoublePoints(function.Name.Text) << '\n';
    m_Context->ir << "regs r0:u32,r1:u32,r2:u32,r3:u32\n";

    m_Context->funcRetType = function.ReturnType.Text;

    if (!function.Parameters.empty())
    {
        m_Context->ir << "args ";
        for (size_t i = 0; i < function.Parameters.size(); i++)
        {
            auto& param = function.Parameters[i];
            m_Context->ir << param.Name.Text << ':' << param.Type.Text;

            if (i < function.Parameters.size() - 1)
            {
                m_Context->ir << ',';
            }
        }
        m_Context->ir << '\n';
    }

    for (const auto& ptr : function.Children)
    {
        if (!ptr) continue;
        Node& node = *ptr;

        switch (node.GetType())
        {
            case NodeType::Return:
                BuildReturn(static_cast<ReturnNode&>(node));
                break;
            default:
                BuildExpression(node);
                break;
        }
    }
}

void IRBuilder::BuildReturn(const ReturnNode& ret)
{
    if (!ret.Children.empty() && ret.Children[0])
    {
        Node& node = *ret.Children[0];
        std::string val = IRExpressionBuilder::GetValue(node, *m_Context);

        m_Context->ir << "mov rr0," << val << '\n';
        if (IRContext::LooksLikeReg(val))
        {
            m_Context->ReleaseReg(val);
        }
    }

    m_Context->ir << "ret\n";
}

void IRBuilder::BuildVariable(const VariableNode& node)
{
    m_Context->ir << "vars " << IRContext::ReplaceDoublePoints(node.Name.Text) << ':' << node.Type.Text << '\n';
}

void IRBuilder::BuildAssignment(const AssignmentNode& node)
{
    if (node.Children.empty() || !node.Children[0])
    {
        Logger::Fatal("AssignmentNode null, empty or corrupted");
        return;
    }

    Node& a = *node.Children[0];
    std::string textA = IRExpressionBuilder::GetValue(a, *m_Context);
    std::string varName = IRContext::ReplaceDoublePoints(node.Name.Text);

    // Otimização: se é const, registrar na tabela para constante propagação
    if (a.Attrs.Has(AttrId::Const))
    {
        if (auto constVal = IROptimizer::TryEvaluateConst(a, *m_Context))
        {
            m_Context->constValues[varName] = *constVal;
        }
    }

    m_Context->ir << "mov " << varName << ',' << textA << '\n';

    if (IRContext::LooksLikeReg(textA))
    {
        m_Context->ReleaseReg(textA);
    }
}

void IRBuilder::BuildExpression(Node& node)
{
    switch (node.GetType())
    {
        case NodeType::Assign:
            BuildAssignment(static_cast<AssignmentNode&>(node));
            break;
        case NodeType::FunctionCall:
            IRExpressionBuilder::BuildFunctionCall(static_cast<FunctionCallNode&>(node), *m_Context);
            break;
        case NodeType::IR:
        {
            auto& irNode = static_cast<IRNode&>(node);
            std::string code = (!irNode.Code.Text.empty() && irNode.Code.Text.front() == '\n')
                ? irNode.Code.Text.substr(1)
                : irNode.Code.Text;

            m_Context->ir << code;
            if (!code.empty() && code.back() != '\n')
            {
                m_Context->ir << '\n';
            }
            break;
        }
        case NodeType::Variable:
            BuildVariable(static_cast<VariableNode&>(node));
            break;
        default:
            Logger::Fatal("Node not solved");
            std::exit(-1);
            break;
    }
}

} // namespace Emux
