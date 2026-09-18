#include "Emux/Builder/IRBuilder.hpp"
#include "Emux/Compiler/AST/AssignmentNode.hpp"
#include "Emux/Compiler/AST/BinaryNode.hpp"
#include "Emux/Compiler/AST/FunctionCallNode.hpp"
#include "Emux/Compiler/AST/FunctionNode.hpp"
#include "Emux/Compiler/AST/IRNode.hpp"
#include "Emux/Compiler/AST/LiteralNode.hpp"
#include "Emux/Compiler/AST/NodeType.hpp"
#include "Emux/Compiler/AST/Program.hpp"
#include "Emux/Compiler/AST/ReturnNode.hpp"
#include "Emux/Compiler/AST/SectionNode.hpp"
#include "Emux/Compiler/AST/VariableCallNode.hpp"
#include "Emux/Compiler/AST/VariableNode.hpp"
#include "Emux/Core/Logger.hpp"
#include <array>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
    std::string replaceDoublePoints(std::string text)
    {
        const std::string alvo = "::";
        const std::string novo = "_";
        size_t pos = 0;

        while((pos = text.find(alvo, pos)) != std::string::npos)
        {
            text.replace(pos,alvo.length(),novo);
            pos += novo.length();
        }

        return text;
    }

    std::array<bool,4> regs;
    std::array<bool,4> permRegs;
    
    std::optional<std::string> GetFreeReg()
    {
        for (size_t i = 0; i < regs.size(); i++)
        {
            bool& r = regs[i];
            if (r) continue;

            r = true;
            permRegs[i] = true;
            std::string value(1,'r');
            value += std::to_string(i);
            return value;
        }

        return std::nullopt;
    }

    void ReleaseReg(std::string_view reg)
    {
        if (reg.size() < 2) return;

        if (!std::isdigit(reg[1])) return;

        int i = reg[1] - '0';
        regs[i] = false;
    }

    std::string UsedRegisters()
    {
        std::string str;
        for (size_t i = 0; i < permRegs.size(); i++)
        {
            if (!permRegs[i]) continue;

            str += 'r';
            str += std::to_string(i);
            str += ',';
        }

        str.pop_back();
        return str;
    }

    bool looksLikeReg(std::string_view str)
    {
        if (str.size() < 2) return false;
        
        bool isReg = str[0] == 'r' && std::isdigit(str[1]);
        if (str.size() > 2)
        {
            isReg |= str[0] == 'r' && str[1] == 'r' && std::isdigit(str[2]);
        }
        return isReg;
    }

    bool isTempNode(Emux::NodeType type)
    {
        return type != Emux::NodeType::VariableCall && type != Emux::NodeType::Literal;
    }

    std::string funcRetType{};
    std::ostringstream ir; 

    std::string HelperBinaryOperand(const std::string& str)
    {
        std::string left(str);
        if (!looksLikeReg(str))
        {
            left = GetFreeReg().value_or(str);
            if (left != str)
            {
                ir << "mov " << left << ',' << str << '\n';
            }
        }
        return left;
    }

    std::string lastBinary{};
}

namespace Emux
{

IRBuilder::IRBuilder()=default;
IRBuilder::~IRBuilder()=default;

std::string IRBuilder::Build(const Program& program)
{
    for (const auto& ptr : program.Children)
    {
        if (!ptr) continue;

        Node& node = *ptr;
        switch(node.GetType())
        {
            case Emux::NodeType::Section:
            {
                BuildSection(static_cast<SectionNode&>(node));
                break;
            }
            default:
            {
                Emux::Logger::Fatal("AST encontra-se corrompida");
                break;
            }
        }
    }

    return ir.str();
}

void IRBuilder::BuildSection(const SectionNode& node)
{
    for (const auto& ptr : node.Children)
    {
        if (!ptr) continue;
        Node& node = *ptr;

        switch(node.GetType())
        {
            case Emux::NodeType::Function:
            {
                FunctionNode& function = static_cast<FunctionNode&>(node);
                BuildFunction(function);
                break;
            }
            default:
            {
                BuildExpression(node);
                break;
            }
        };
    }
}

void IRBuilder::BuildFunction(const FunctionNode& function)
{
    ir << "func " << replaceDoublePoints(function.Name.Text) << '\n';
    ir << "regs r0:u32,r1:u32,r2:u32,r3:u32\n";

    regs.fill(false);
    funcRetType = function.ReturnType.Text;
    if (!function.Parameters.empty())
    {
        ir << "args ";
        for (size_t i = 0; i < function.Parameters.size(); i++)
        {
            auto& param = function.Parameters[i];
            ir << param.Name.Text << ':' << param.Type.Text;
            
            if (i < function.Parameters.size() - 1)
            {
                ir << ',';
            }
        }
        ir << '\n';
    }

    for (const auto& ptr : function.Children)
    {
        if (!ptr) continue;
        Node& node = *ptr;
        
        switch(node.GetType())
        {
            case Emux::NodeType::Return:
            {
                BuildReturn(static_cast<ReturnNode&>(node));
                break;
            }
            default:
            {
                BuildExpression(node);
                break;
            }
        };
    }
}

void IRBuilder::BuildFunctionCall(const FunctionCallNode& functionCall)
{
    ir << "call " << replaceDoublePoints(functionCall.GetName().Text);

    for (size_t i = 0; i < functionCall.Parameters.size(); i++)
    {
        ir << ',' << functionCall.Parameters[i].Value.Text;
    }
    ir << '\n';
}

void IRBuilder::BuildReturn(const ReturnNode& ret)
{
    if (!ret.Children.empty())
    {
        Node& node = *ret.Children[0];

        switch(node.GetType())
        {
            case Emux::NodeType::FunctionCall:
            {
                BuildFunctionCall(static_cast<FunctionCallNode&>(node));
                break;
            }
            case Emux::NodeType::VariableCall:
            {
                VariableCallNode& varCall = static_cast<VariableCallNode&>(node);
                ir << "mov rr0," << replaceDoublePoints(varCall.Name.Text) << '\n';
                break;
            }
            case Emux::NodeType::Literal:
            {
                LiteralNode& literal = static_cast<LiteralNode&>(node);
                ir << "mov rr0," << literal.Value.Text << '\n';
                break;
            }
            default:
            {
                Emux::Logger::Fatal("AST encontra-se corrompida");
                break;
            }
        };
    }

    ir << "ret\n";
}

void IRBuilder::BuildVariable(const VariableNode& node)
{
    ir << "vars " << replaceDoublePoints(node.Name.Text) << ':' << node.Type.Text << '\n';
}

void IRBuilder::BuildAssignment(const AssignmentNode& node)
{
    if (node.Children.empty())
    {
        Emux::Logger::Fatal("AssignmentNode null, empty or corrupted");
        return;
    }

    Node& a = *node.Children[0];

    std::string textA = HelperGetValue(a);

    ir << "mov " << replaceDoublePoints(node.Name.Text) << ',' << textA << '\n'; 

    if (looksLikeReg(textA)) ReleaseReg(textA);
}

void IRBuilder::BuildBinary(const BinaryNode& node)
{
    if (node.Children.size() < 2)
    {
        Emux::Logger::Fatal("Invalid Binary node");
        return;
    }

    Node& a = *node.Children[0];
    Node& b = *node.Children[1];

    std::string textA = HelperGetValue(a);
    std::string textB = HelperGetValue(b);

    std::string left = HelperBinaryOperand(textA);
    std::string right = (std::isdigit(textB[0])) ? textB : HelperBinaryOperand(textB);

    switch (node.Operation)
    {
        case Emux::BinaryOperation::Add:
            ir << "add " << left << ',' << right << '\n';
            break;
        case Emux::BinaryOperation::Sub:
            ir << "sub " << left << ',' << right << '\n';
            break;
        case Emux::BinaryOperation::Mul:
            ir << "mul " << left << ',' << right << '\n';
            break;
        case Emux::BinaryOperation::Xor:
            ir << "xor " << left << ',' << right << '\n'; 
            break;
        case Emux::BinaryOperation::And:
            ir << "and " << left << ',' << right << '\n'; 
            break;
        case Emux::BinaryOperation::Or:
            ir << "or " << left << ',' << right << '\n'; 
            break;
        case Emux::BinaryOperation::Lshift:
            ir << "shl " << left << ',' << right << '\n'; 
            break;
        case Emux::BinaryOperation::Rshift:
            ir << "shr " << left << ',' << right << '\n'; 
            break;
        default:
            Logger::Fatal("This operation not supported");
            break;
    }

    lastBinary = left;
    if (looksLikeReg(right)) ReleaseReg(right);
}

std::string IRBuilder::HelperGetValue(Node& node)
{
    switch(node.GetType())
    {
        case Emux::NodeType::FunctionCall:
        {
            BuildFunctionCall(static_cast<FunctionCallNode&>(node));
            return "rr0";
        }
        case Emux::NodeType::Binary:
            BuildBinary(static_cast<BinaryNode&>(node));    
            return lastBinary;
        case Emux::NodeType::Literal:
            return static_cast<LiteralNode&>(node).Value.Text;
        case Emux::NodeType::VariableCall:
            return replaceDoublePoints(node.Name.Text);
        default:
            Emux::Logger::Fatal("This node can´t devolve value");
            exit(-1);
            return {};
    };
}

void IRBuilder::BuildExpression(Node& node)
{
    switch(node.GetType())
    {
        case Emux::NodeType::Assign:
        {
            BuildAssignment(static_cast<AssignmentNode&>(node));
            break;
        }
        case Emux::NodeType::FunctionCall:
        {
            FunctionCallNode& fcNode = static_cast<FunctionCallNode&>(node);
            BuildFunctionCall(fcNode);
            break;
        }
        case Emux::NodeType::IR:
        {
            IRNode& irNode = static_cast<IRNode&>(node);
            
            std::string code = (irNode.Code.Text.front() == '\n')
                ? irNode.Code.Text.substr(1) 
                : irNode.Code.Text;

            ir << code;

            if (code.back() != '\n')
            {
                ir << '\n';
            }

            break;
        }
        case Emux::NodeType::Variable:
        {
            VariableNode& varNode = static_cast<VariableNode&>(node);
            BuildVariable(varNode);
            break;
        }
        default:
        {
            Emux::Logger::Fatal("Node not solved");
            exit(-1);
            break;
        }
    };
}

}
