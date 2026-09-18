#include "Emux/Compiler/AST/IRNode.hpp"
#include "Emux/Compiler/Diagnostic.hpp"
#include "Emux/Compiler/SourceLocation.hpp"
#include "Emux/Compiler/TokenType.hpp"
#include <Emux/Compiler/Parser.hpp>
#include <Emux/Compiler/AST/SectionNode.hpp>
#include <Emux/Compiler/AST/VariableNode.hpp>
#include <Emux/Compiler/AST/VariableCallNode.hpp>
#include <Emux/Compiler/AST/FunctionNode.hpp>
#include <Emux/Compiler/AST/FunctionCallNode.hpp>
#include <Emux/Compiler/AST/AssignmentNode.hpp>
#include <Emux/Compiler/AST/LiteralNode.hpp>
#include <Emux/Compiler/AST/ReturnNode.hpp>
#include <Emux/Compiler/AST/BinaryNode.hpp>
#include <optional>
#include <stdexcept>
#include <charconv>
#include <cctype>
#include <string>

namespace Emux
{

Parser::Parser(
    CompilerContext& context
):
    m_Context(context)
{
}

bool Parser::IsAtEnd() const
{
    return Current().Type == TokenType::EndOfFile || m_Position >= m_Context.Tokens.size();
}

const Token& Parser::Current() const
{
    return m_Context.Tokens[m_Position];
}

const Token& Parser::Previous() const
{
    return m_Context.Tokens[m_Position - 1];
}

const Token& Parser::Peek(size_t offset) const
{
    if (m_Position + offset >= m_Context.Tokens.size())
        return m_Context.Tokens[m_Context.Tokens.size()-1];
    return m_Context.Tokens[m_Position + offset];
}

void Parser::Advance()
{
    if(!IsAtEnd())
    {
        ++m_Position;
    }
}

bool Parser::Check(TokenType type) const
{
    if(IsAtEnd())
    {
        return false;
    }

    return Current().Type == type;
}

OptionalToken Parser::Match(TokenType type)
{
    if(!Check(type))
    {
        return std::nullopt;
    }

    Advance();

    return Previous();
}

OptionalToken Parser::Consume(
    TokenType type,
    std::string_view message
)
{
    auto token = Match(type);

    if (token)
    {
        return token;
    }

    m_Context.Diagnostics.Add(
    	DiagnosticLevel::Error,
        Current().Location,
        message
    );

    return std::nullopt;
}

void Parser::SkipNewLines()
{
    while(Match(TokenType::NewLine).has_value()){}
}

void Parser::AdvanceAndSNL()
{
    SkipNewLines();
    Advance();
    SkipNewLines();
}

void Parser::Synchronize()
{
    while(!IsAtEnd())
    {
        if(Match(TokenType::LeftBracket))
        {
            return;
        } else if(Match(TokenType::Keyword))
        {
            return;
        } else if(Match(TokenType::RightParen))
        {
            SkipNewLines();
            if (Match(TokenType::LeftBrace))
            {
                continue;
            }
            return;
        } else if(Match(TokenType::RightBrace))
        {
            return;
        }


        Advance();
    }
}

bool Parser::IsSectionStart() const
{
    return Check(
        TokenType::LeftBracket
    );
}

bool Parser::IsFunctionStart() const
{
    return Current().Text == "func";
}

bool Parser::IsFunctionStart(
    const Token& token
) const
{
    return token.Text == "func";
}

bool Parser::IsLiteral() const
{
    return Check(TokenType::Number) || Check(TokenType::String);
}

bool Parser::IsLiteral(
    const Token& token
) const
{
    return (token.Type == TokenType::Number) || (token.Type == TokenType::String);
}

bool Parser::IsBinary() const
{
    return IsBinary(Current());
}

void Parser::Parse()
{
    m_Context.AST = std::make_unique<Program>();


    while(!IsAtEnd())
    {
        SkipNewLines();


        if(IsAtEnd())
            break;


        if(IsSectionStart())
        {
            ParseSection(*m_Context.AST);
        }
        else
        {
            m_Context.Diagnostics.Add(
            	DiagnosticLevel::Error,
                Current().Location,
                "Expected section."
            );

            Synchronize();
        }
    }
}

void Parser::ParseSection(Program& program)
{
    auto left = Consume(
        TokenType::LeftBracket,
        "Expected '['."
    );


    auto name = Consume(
        TokenType::Identifier,
        "Expected section name."
    );


    if(!left || !name)
    {
        Synchronize();
        return;
    }


    auto section = std::make_unique<SectionNode>(
        name->get().Location
    );


    section->Name = name->get();
    m_CurrentScopeName = section->Name;

    if(Match(TokenType::Colon))
    {
        ParseDependencies(
            *section
        );
    }


    auto right = Consume(
        TokenType::RightBracket,
        "Expected ']'."
    );


    if(!right)
    {
        Synchronize();
        return;
    }

    SkipNewLines();

    while (Current().Type != TokenType::LeftBracket 
        && Current().Type != TokenType::EndOfFile)
    {
        SkipNewLines();

        if (Check(TokenType::Identifier))
        {
            ParseVariable(*section);
        } else if (IsFunctionStart(Current())){
            ParseFunction(*section);
        } else if(Current().Text == "exir") {
            ParseIR(*section);
        }
    }
    
    if(!program.AddSection(std::move(section)))
    {
        std::string message = "Section '" + name->get().Text + "' already defined.";
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            name->get().Location,
            message
        );
    }
}

void Parser::ParseDependencies(
    SectionNode& section
)
{

    auto dependency = Consume(
        TokenType::Identifier,
        "Expected dependency name."
    );


    if(!dependency)
    {
        Synchronize();
        return;
    }


    section.Dependencies.push_back(
        dependency->get()
    );


    while(Match(TokenType::Comma))
    {

        dependency = Consume(
            TokenType::Identifier,
            "Expected dependency name."
        );


        if(!dependency)
        {
            Synchronize();
            return;
        }


        section.Dependencies.push_back(
            dependency->get()
        );
    }
}

void Parser::ParseVariable(
    SectionNode& node
)
{
    auto name = Consume(
        TokenType::Identifier,
        "Expected variable name."
    );


    Consume(
        TokenType::Colon,
        "Expected ':'."
    );


    auto type = Consume(
        TokenType::Identifier,
        "Expected type."
    );


    if(!name || !type)
    {
        Synchronize();
        return;
    }

    std::string scopedName = node.Name.Text + "::" + name->get().Text;
    if (m_Variables.contains(scopedName))
    {
        return;
    }

    Token scopedToken{
        .Type = TokenType::Identifier,
        .Text = scopedName,
        .Location = name->get().Location
    };

    auto variable = std::make_unique<VariableNode>(
        scopedToken.Location
    );

    variable->Name = scopedToken;
    m_Variables.insert(scopedName);

    variable->Type = type->get();

    node.Children.push_back(
        std::move(variable)
    );
}

void Parser::ParseFunction(
    SectionNode& node
)
{
    // Espera: "func nomeFuncao ( params ) retType { body }"
    
    if (!IsFunctionStart(Current()))
    {
        return;
    }
    
    Advance(); // Consome "func"
    
    // Pega nome da função
    if (!Check(TokenType::Identifier))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Expected function name"
        );
        Synchronize();
        return;
    }
    Token funcName = Current();
    Advance(); // Consome "name"
    
    auto funcNode = std::make_unique<FunctionNode>(
        funcName,
        funcName.Location
    );
    
    // Parâmetros: ( tipo nome, tipo nome, ... )
    if (!Check(TokenType::LeftParen))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Expected '(' after function name"
        );
        Advance();
        Synchronize();
        return;
    }
    AdvanceAndSNL(); // Consome "("
    
    // Parse parâmetros
    while (!Check(TokenType::RightParen) && !IsAtEnd())
    {
        // Tipo do parâmetro
        if (!Check(TokenType::Identifier))
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                Current().Location,
                "Expected parameter type"
            );
            Advance();
            Synchronize();
            return;
        }

        Token paramType = Current();
        Advance(); // Consome Parameter Type
        
        // Nome do parâmetro
        if (!Check(TokenType::Identifier))
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                Current().Location,
                "Expected parameter name"
            );
            Advance(),
            Synchronize();
            return;
        }
        Token paramName = Current();
        Advance(); // Consome Parameter Name
        
        // Adiciona ao vetor de parâmetros
        funcNode->Parameters.emplace_back(paramType, paramName);
        
        // Verifica se há mais parâmetros
        if (Check(TokenType::Comma))
        {
            AdvanceAndSNL(); // Consome ","
        }
        else if (!Check(TokenType::RightParen))
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                Current().Location,
                "Expected ',' or ')' in parameter list"
            );
            Advance();
            Synchronize();
            return;
        }
    }

    if (!Check(TokenType::RightParen))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Expected ')' after parameters"
        );
        return;
    }
    Advance(); // Consome ")"

    std::string scopedName = node.Name.Text + "::" + funcName.Text;
    m_Functions.emplace(scopedName, std::cref(funcNode->Parameters));

    Token scopedToken{
        .Type = TokenType::Identifier,
        .Text = scopedName,
        .Location = funcName.Location
    };

    // Pega tipo de retorno
    if (!Check(TokenType::Identifier))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Expected return type after '->'"
        );
        Advance();
        Synchronize();
        return;
    }
    Token returnType = Current();
    funcNode->ReturnType = returnType;

    AdvanceAndSNL(); // Consome "Return type"
    
    // Corpo: { ... }
    if (!Check(TokenType::LeftBrace))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Expected '{' before function body"
        );
        Advance();
        Synchronize();
        return;
    }

    SkipNewLines();
    Advance(); // Consome "{"
    
    // Parse statements do corpo
    while (!Check(TokenType::RightBrace) && !IsAtEnd())
    {
        SkipNewLines();

        if (Check(TokenType::RightBrace) || IsAtEnd())
        {
            break;
        }

        ParseStatement(*funcNode);
    }
    
    if (!Check(TokenType::RightBrace))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Expected '}' after function body"
        );
        Advance();
        Synchronize();
        return;
    }
    Advance(); // Consome "}"
    
    funcNode->Name = scopedToken;
    node.Children.push_back(
        std::move(funcNode)
    );
}

std::optional<BinaryOperation> Parser::TokenToBinaryOperation(TokenType type) const
{
    switch (type)
    {
        case TokenType::Plus:       return BinaryOperation::Add;
        case TokenType::Minus:      return BinaryOperation::Sub;
        case TokenType::Mul:        return BinaryOperation::Mul;
        case TokenType::Div:        return BinaryOperation::Div;
        case TokenType::And:        return BinaryOperation::And;
        case TokenType::Or:         return BinaryOperation::Or;
        case TokenType::Xor:        return BinaryOperation::Xor;
        case TokenType::LeftShift:  return BinaryOperation::Lshift;
        case TokenType::RightShift: return BinaryOperation::Rshift;
        default:                    return std::nullopt;
    }
}

bool Parser::IsBinary(const Token& token) const
{
    return TokenToBinaryOperation(token.Type).has_value();
}

// Analisa operandos primários: Literais, Variáveis, Chamadas de Função e Parênteses ()
std::unique_ptr<Node> Parser::ParsePrimary()
{
    SkipNewLines();

    // 1. Suporte a Parênteses
    if (Match(TokenType::LeftParen))
    {
        auto expr = ParseExpression(0);
        if (!Consume(TokenType::RightParen, "Expected ')' after expression."))
        {
            Synchronize();
            return nullptr;
        }
        return expr;
    }

    // 2. Literais
    if (IsLiteral())
    {
        // Cria nó temporário para capturar o Literal do ParseLiteral existente
        auto tempContainer = std::make_unique<Node>(NodeType::Program, Token{}, Current().Location);
        ParseLiteral(*tempContainer);
        if (!tempContainer->Children.empty())
        {
            auto node = std::move(tempContainer->Children.back());
            tempContainer->Children.pop_back();
            return node;
        }
    }

    // 3. Identificadores (Variáveis e Chamadas de Função)
    if (Check(TokenType::Identifier))
    {
        auto tempContainer = std::make_unique<Node>(NodeType::Program, Token{}, Current().Location);
        
        if (Peek(1).Type == TokenType::LeftParen || Peek(4).Type == TokenType::LeftParen)
        {
            ParseFunctionCall(*tempContainer);
        }
        else
        {
            ParseVariableCall(*tempContainer);
        }

        if (!tempContainer->Children.empty())
        {
            auto node = std::move(tempContainer->Children.back());
            tempContainer->Children.pop_back();
            return node;
        }
    }

    m_Context.Diagnostics.Add(
        DiagnosticLevel::Error,
        Current().Location,
        "Expected expression."
    );
    Advance();
    Synchronize();
    return nullptr;
}

// Algoritmo de Pratt Parsing para resolver Precedência e Associatividade
std::unique_ptr<Node> Parser::ParseExpression(int precedence)
{
    SkipNewLines();
    
    auto left = ParsePrimary();
    if (!left) return nullptr;

    while (true)
    {
        SkipNewLines();

        auto opOpt = TokenToBinaryOperation(Current().Type);
        if (!opOpt.has_value())
        {
            break;
        }

        BinaryOperation op = *opOpt;
        int opPrecedence = GetOperationPrecedence(op);

        // Se a precedência do próximo operador for menor ou igual à atual, interrompe o loop
        if (opPrecedence <= precedence)
        {
            break;
        }

        Token opToken = Current();
        Advance(); // Consome o operador

        // Recursão com a precedência atual para respeitar a associação à esquerda
        auto right = ParseExpression(opPrecedence);
        if (!right)
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                Current().Location,
                "Expected right operand."
            );
            break;
        }

        // Reconstrói a árvore mantendo o nó binário no topo com a precedência correta
        left = std::make_unique<BinaryNode>(
            op,
            std::move(left),
            std::move(right),
            opToken.Location
        );
    }

    return left;
}

// Adaptação da interface antiga ParseExpression(Node& node) para o novo retorno
void Parser::ParseExpression(Node& node)
{
    SkipNewLines();

    // Atribuição não é um "valor" combinável com operadores — precisa ser
    // reconhecida aqui, antes de entrar no núcleo de precedência, senão
    // ParsePrimary nunca a vê (Assign não é literal, '(' nem Identifier
    // seguido de '(' — cai no branch de VariableCall e o '=' fica sem consumir).
    if (Check(TokenType::Identifier) &&
        (Peek(1).Type == TokenType::Assign || Peek(4).Type == TokenType::Assign))
    {
        ParseAssignment(node);
        return;
    }

    auto expr = ParseExpression(0);
    if (expr)
    {
        node.Children.push_back(std::move(expr));
    }
}

void Parser::ParseFunctionCall(Node& node)
{
    if (Peek(1).Type != TokenType::LeftParen && Peek(4).Type != TokenType::LeftParen)
    {
        return;
    }

    SourceLocation location = Current().Location;
    auto name = HelperScopedName();

    if (!name.has_value())
    {
        return;
    }

    std::string nameText = *name;

    Token scopedToken;
    scopedToken.Type = TokenType::Identifier;
    scopedToken.Text = nameText;
    scopedToken.Location = location;

    auto funcCallNode = std::make_unique<FunctionCallNode>(
        scopedToken,
        scopedToken.Location
    );

    if(!Consume(TokenType::LeftParen, 
        "Expected ( after '"+
        nameText + "'."
    )) // Consome '('
    {
        Synchronize();
        return;
    }

    auto it = m_Functions.find(nameText);
    if (it == m_Functions.end())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            funcCallNode->GetLocation(),
            "Invalid call, function '" +
            nameText +
            "' not defined"
        );
        Advance();
        Synchronize();
        return;
    }

    const std::vector<FunctionParameter>& Params = it->second.get();

    SkipNewLines();
    while (Current().Type != TokenType::RightParen && 
           Current().Type != TokenType::EndOfFile)
    {
        Token value = Current();

        FunctionParameter parameter({ .Location = Current().Location},{ .Location = Current().Location });
        // Se 'i' estiver dentro do limite dos parâmetros esperados, configuramos
        if (funcCallNode->Parameters.size() < Params.size())
        {
            parameter = Params[funcCallNode->Parameters.size()];
        }
        parameter.Value = value;

        Advance(); // Consome param value

        funcCallNode->Parameters.emplace_back(std::move(parameter));

        if (Check(TokenType::Comma))  // Consome ,
        {
            AdvanceAndSNL();
            continue;
        } else if(Check(TokenType::RightParen)) // Verifica )
        {
            break;
        } else { // Error
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                Current().Location,
                "Expected , or ) after parameter value " + 
                value.Text + 
                " in function '" +
                nameText +
                "'." 
            );
            Advance();
            Synchronize();
            return;
        }
    }

    if (funcCallNode->Parameters.size() != Params.size())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            funcCallNode->GetLocation(),
            "Function '" + nameText + "' expects " +
            std::to_string(Params.size()) + " parameter(s), but received " +
            std::to_string(funcCallNode->Parameters.size())
        );
        Advance();
        Synchronize();
        return;
    }
    
    if (!Consume(TokenType::RightParen, "Expected ')' after parameters"))
    {
        Advance();
        Synchronize();
        return;
    } // Consome ")"

    node.Children.push_back(
        std::move(funcCallNode)
    );
}

void Parser::ParseVariableCall(Node& node)
{
    SourceLocation location = Current().Location;
    auto name = HelperScopedName();

    if (!name.has_value())
    {
        return;
    }

    std::string nameText = *name;
    Token scopedToken{
        .Type = TokenType::Identifier,
        .Text = nameText,
        .Location = location
    };

    if (!m_Variables.contains(nameText))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            location,
            "Variable '" + nameText + "' not defined."
        );
        Advance();
        Synchronize();
        return;
    }

    auto varCallNode = std::make_unique<VariableCallNode>(
        scopedToken,
        scopedToken.Location
    );

    node.Children.push_back(std::move(varCallNode));
}

void Parser::ParseAssignment(Node& node)
{
    // Main::test =
    // test = 
    if (Peek(4).Type != TokenType::Assign && Peek(1).Type != TokenType::Assign)
    {
        return;
    }

    SourceLocation location = Current().Location;
    auto name = HelperScopedName();

    if (!name.has_value())
    {
        return;
    }

    std::string nameText = *name;
    Token scopedToken{
        .Type = TokenType::Identifier,
        .Text = nameText,
        .Location = location
    };

    if(!Consume(TokenType::Assign, "Expected = after '"+nameText+"'."))
    {
        Advance();
        Synchronize();
        return;
    }

    auto assignNode = std::make_unique<AssignmentNode>(
        scopedToken,
        scopedToken.Location
    );

    ParseExpression(*assignNode);

    node.Children.push_back(std::move(assignNode));  
}

void Parser::ParseLiteral(Node& node)
{
    if (!Check(TokenType::Number) && !Check(TokenType::String))
    {
        return;
    }

    Token value { Current() };
    Token type {
        .Type = TokenType::Identifier,
        .Location { value.Location }
    };

    if (Check(TokenType::Number))
    {
        std::int64_t amount = 0;

        auto result = std::from_chars(
            value.Text.data(),
            value.Text.data() + value.Text.size(),
            amount
        );

        if(result.ec != std::errc{})
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                Current().Location,
                "Invalid number '" + value.Text  + "'."
            );
            Advance();
            Synchronize();
            return;
        }

        if (amount > 0 && amount <= 255) 
        {
            type.Text = "u8";
        }
        else if(amount > 255)
        {
            type.Text = "u16";
        }
        else if(amount >= -128 && amount <= 127)
        {
            type.Text = "i8";
        }
        else if(amount < 0)
        {
            type.Text = "i32";
        }
    } else if (Check(TokenType::String))
    {
        type.Text = "U"+std::to_string(value.Location.Length);
    }

    Advance();

    auto literalNode = std::make_unique<LiteralNode>(
        type,
        value,
        value.Location
    );

    node.Children.push_back(std::move(literalNode));
}

void Parser::ParseIR(Node& node)
{
    auto irTk = Consume(TokenType::Keyword, "Expected exir");
    if (!(irTk->get().Text == "exir"))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Fatal,
            irTk->get().Location,
            "Expected exir, impossible to continue"
        );
        return;
    }

    if (!Consume(TokenType::LeftBrace, "Expected { after exir"))
    {
        Advance();
        Synchronize();
        return;
    }

    Token code(TokenType::Identifier, {}, Current().Location);
    auto& content = m_Context.Source.GetContent();
    size_t offset = code.Location.Offset;

    // a}
    char lastChar{};
    while (lastChar != '}')
    {
        if (offset >= content.size()) break;
        lastChar = content[offset++];
        if (lastChar == '}') break;
        code.Text += lastChar;
    }

    while (!Check(TokenType::RightBrace)){ Advance(); }

    Advance(); // Consome }

    auto irNode = std::make_unique<IRNode>(
        code,
        code.Location
    );

    node.Children.push_back(std::move(irNode));
}

void Parser::ParseReturn(Node& node)
{
    if (Current().Text != "return")
    {
        return;
    }

    auto retNode = std::make_unique<ReturnNode>(Current().Location);
    Advance();
    ParseExpression(*retNode);
    node.Children.push_back(std::move(retNode));
}

void Parser::ParseStatement(Node& node)
{
    SkipNewLines();

    if (Current().Text == "return")
    {
        ParseReturn(node);
        return;
    }

    if (Current().Text == "exir")
    {
        ParseIR(node);
        return;
    }

    ParseExpression(node);
}

std::optional<std::string> Parser::HelperScopedName()
{
    auto first = Consume(TokenType::Identifier, "Expected identifier");

    if (!first.has_value())
    {
        Advance();
        Synchronize();
        return std::nullopt;
    }

    std::string result = m_CurrentScopeName.Text + "::" + first->get().Text;
    
    if (Check(TokenType::Colon) && Peek(1).Type == TokenType::Colon)
    {
        Advance();
        Advance();

        auto second = Consume(TokenType::Identifier, "Expected identifier");

        if (!second.has_value())
        {
            Advance();
            Synchronize();
            return std::nullopt;
        }

        result = first->get().Text + "::" + second->get().Text;
    }
    
    return result;
}

}
