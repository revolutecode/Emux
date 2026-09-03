#include <Emux/Compiler/Semantic/SemanticAnalyzer.hpp>

#include <Emux/Compiler/AST/Program.hpp>

#include <Emux/Compiler/Semantic/TypeParser.hpp>

namespace Emux
{

SemanticAnalyzer::SemanticAnalyzer(
    CompilerContext& context
):
    m_Context(context)
{
}


void SemanticAnalyzer::Analyze()
{
    auto& program = *m_Context.AST;
    m_VisitStates.clear();

    for(auto& child : program.Children)
    {
        auto* section = dynamic_cast<SectionNode*>(
            child.get()
        );

        AnalyzeSection(
            *section
        );
    }

    for (auto&& [name, location] : m_FunctionCalls)
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            location,
            "Function '"+
            name+
            "' not defined"
        );
    }

    SectionNode* mainSection = program.FindSection("Main");
    if (!mainSection)
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Fatal,
            mainSection->GetLocation(),
            "Section 'Main' not found! This is entry point"
        );
    }

    if (!m_Functions.contains("Main::Start"))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Fatal,
            mainSection->GetLocation(),
            "Function 'Main::Start' not found! This is entry point"
        );
    }
}

void SemanticAnalyzer::AnalyzeSection(
    SectionNode& section
)
{
    std::string_view name = section.Name.Text;
    if (IsVisited(name)) return;

    if(!section.Dependencies.empty()){
        AnalyzeDependencies(section);
    }

    for(auto& child : section.Children)
    {
        NodeType type = child->GetType();
        if(type == NodeType::Variable)
        {
            VariableNode* variable = static_cast<VariableNode*>(child.get()); 
            AnalyzeVariable(*variable);
        } else if(type == NodeType::Function)
        {
            FunctionNode* function = dynamic_cast<FunctionNode*>(child.get()); 
            AnalyzeFunction(
                *function
            );
        }
    }

    m_VisitStates[name] = VisitState::Visited;
}

void SemanticAnalyzer::AnalyzeDependencies(
    SectionNode& section
)
{
    for(auto& dependency : section.Dependencies)
    {
        AnalyzeDependence(section, dependency, section.Name.Text);
    }
}

void SemanticAnalyzer::AnalyzeDependence(
    SectionNode& section,
    const Token& dependency,
    std::string_view rootName
)
{
    auto& program = *m_Context.AST;
    std::string_view name = dependency.Text;

    if (IsVisiting(name))
    {
        std::string message = "Circular dependency involving '" +
            std::string(rootName) + "','" +
            std::string(name) + "'.";
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            section.Name.Location,
            message
        );
    } else if (IsVisited(name))
    {
        return;
    } else {
        m_VisitStates[name] = VisitState::Visiting;
        auto depNode = program.FindSection(name);
        if (!depNode) 
        {
            std::string message = "Section '" + std::string(name) + "' not found, required for '" + std::string(rootName) + "'";
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                section.Name.Location,
                message
            );
        } else {
            AnalyzeSection(*depNode);
        }
    }
    m_VisitStates[name] = VisitState::Visited;
}

void SemanticAnalyzer::AnalyzeVariable(
    VariableNode& variable
)
{
    std::string name = variable.Name.Text;


    if(m_Variables.contains(name))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Name.Location,
            "Variable '" + name + "' already defined."
        );

        return;
    }


    m_Variables.insert(name);


    auto type = TypeParser::Parse(
        variable.Type.Text
    );


    if(!type)
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Type.Location,
            "Unknown type '" +
            variable.Type.Text +
            "'."
        );
    }
}

void SemanticAnalyzer::AnalyzeVariableCall(
    VariableCallNode& variable
)
{
    std::string name = variable.Name.Text;

    if(!m_Variables.contains(name))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Name.Location,
            "Variable '" + name + "' not defined."
        );

        return;
    } 
}

void SemanticAnalyzer::AnalyzeFunction(
    FunctionNode& function
)
{
    std::string name = function.Name.Text;
    if(m_Functions.contains(name))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            function.Name.Location,
            "Function '" + name + "' already defined."
        );
    } else {
        m_Functions.insert(name);
    }

    auto rtype = TypeParser::Parse(
        function.ReturnType.Text
    );


    if(!rtype)
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            function.ReturnType.Location,
            "Unknown return type '" +
            function.ReturnType.Text +
            "' in function '" +
            name +
            "'."
        );
    }

    std::unordered_set<std::string_view> Params;
    for (const auto& param : function.Parameters)
    {
        const std::string& pname = param.Name.Text;

        if(Params.contains(pname))
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                param.Name.Location,
                "Parameter '" + 
                pname + 
                "' in function '" +
                name +
                "' already defined."
            );

            continue;
        }


        Params.insert(pname);


        auto ptype = TypeParser::Parse(
            param.Type.Text
        );


        if(!ptype)
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                param.Type.Location,
                "Unknown parameter type '" +
                param.Type.Text +
                "' in function '" +
                name +
                "'."
            );
            continue;
        }
    }

    for (auto& child : function.Children)
    {
        AnalyzeExpression(*child);
    }
}

void SemanticAnalyzer::AnalyzeFunctionCall(
    FunctionCallNode& function
)
{
    std::string name = function.Name.Text;
    m_FunctionCalls.emplace(name, std::cref(function.Name.Location));
}

void SemanticAnalyzer::AnalyzeAssignment(
    AssignmentNode& node
)
{
    VariableCallNode var(Token{ .Text = node.Name.Text }, node.GetLocation());
    AnalyzeVariableCall(var);

    if (node.Children.empty())
    {
        throw std::runtime_error("Invalid assignment");
    }

    AnalyzeExpression(*node.Children[0]);
}

void SemanticAnalyzer::AnalyzeExpression(
    Node& node
)
{
    const NodeType type = node.GetType();

    if (type == NodeType::FunctionCall)
    {
        auto& functionCall = static_cast<FunctionCallNode&>(node);
        AnalyzeFunctionCall(functionCall);
    } else if (type == NodeType::Assign)
    {
        auto& assign = static_cast<AssignmentNode&>(node);
        AnalyzeAssignment(assign);
    }
}

bool SemanticAnalyzer::IsVisited(std::string_view name)
{
    if (const auto it = m_VisitStates.find(name); it != m_VisitStates.cend())
    {
        if (it->second == VisitState::Visited){
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::IsVisiting(std::string_view name)
{
    if (const auto it = m_VisitStates.find(name); it != m_VisitStates.cend())
    {
        if (it->second == VisitState::Visiting){
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::IsVisitedOrVisiting(std::string_view name)
{
    if (const auto it = m_VisitStates.find(name); it != m_VisitStates.cend())
    {
        if (it->second != VisitState::NotVisited){
            return true;
        }
    }
    return false;
}

}
