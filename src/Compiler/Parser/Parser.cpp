#include "Emux/Compiler/AST/IRNode.hpp"
#include "Emux/Compiler/AttrSet.hpp"
#include "Emux/Compiler/Diagnostic.hpp"
#include "Emux/Compiler/Parser/OptionalToken.hpp"
#include "Emux/Compiler/Token.hpp"
#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Compiler/SourceLocation.hpp"
#include "Emux/Compiler/TokenType.hpp"
#include <Emux/Compiler/Parser/Parser.hpp>
#include <Emux/Compiler/AST/VariableNode.hpp>
#include <Emux/Compiler/AST/VariableCallNode.hpp>
#include <Emux/Compiler/AST/UsingNode.hpp>
#include <Emux/Compiler/AST/FunctionNode.hpp>
#include <Emux/Compiler/AST/FunctionCallNode.hpp>
#include <Emux/Compiler/AST/AssignmentNode.hpp>
#include <Emux/Compiler/AST/CastNode.hpp>
#include <Emux/Compiler/AST/LiteralNode.hpp>
#include <Emux/Compiler/AST/ReturnNode.hpp>
#include <Emux/Compiler/AST/BinaryNode.hpp>
#include <Emux/Compiler/AST/UnaryNode.hpp>
#include <Emux/Compiler/AST/IndexNode.hpp>
#include <Emux/Compiler/AST/EnumNode.hpp>
#include <Emux/Compiler/AST/ControlFlowNodes.hpp>
#include <Emux/Utilities/BigInt.hpp>
#include <Emux/Utilities/NumericLiteral.hpp>
#include <optional>
#include <stdexcept>
#include <charconv>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace Emux
{

namespace
{

// Operação correspondente a um token de atribuição composta ('+=', '-='...).
std::optional<BinaryOperation> CompoundOperation(TokenType type)
{
    switch (type)
    {
        case TokenType::PlusAssign:      return BinaryOperation::Add;
        case TokenType::MinusAssign:     return BinaryOperation::Sub;
        case TokenType::MulAssign:       return BinaryOperation::Mul;
        case TokenType::DivAssign:       return BinaryOperation::Div;
        case TokenType::ModAssign:       return BinaryOperation::Mod;
        case TokenType::AndAssign:       return BinaryOperation::And;
        case TokenType::OrAssign:        return BinaryOperation::Or;
        case TokenType::XorAssign:       return BinaryOperation::Xor;
        case TokenType::LeftShiftAssign: return BinaryOperation::Lshift;
        case TokenType::RightShiftAssign:return BinaryOperation::Rshift;
        default:                         return std::nullopt;
    }
}

// '=' ou qualquer atribuição composta.
bool IsAssignToken(TokenType type)
{
    return type == TokenType::Assign || CompoundOperation(type).has_value();
}

} // namespace

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

std::optional<Token> Parser::ParseTypeToken(std::string_view message)
{
    auto base = Consume(TokenType::Identifier, message);

    if (!base.has_value())
    {
        return std::nullopt;
    }

    Token type = base->get();

    // Ponto fixo q<bits>.<scale>: o lexer separa o '.' do numero.
    if ((type.Text.starts_with("q") || type.Text.starts_with("Q")) &&
        Check(TokenType::Unknown) && Current().Text == "." &&
        Peek(1).Type == TokenType::Number)
    {
        Advance();
        type.Text += "." + Current().Text;
        Advance();
    }

    // Tipo qualificado por escopo: Secao::Tipo (usings definidos noutro módulo).
    while (Check(TokenType::Colon) && Peek(1).Type == TokenType::Colon)
    {
        Advance();
        Advance();

        auto part = Consume(TokenType::Identifier, message);
        if (!part.has_value())
        {
            return std::nullopt;
        }

        type.Text += "::" + part->get().Text;
    }

    while (Check(TokenType::Mul))
    {
        type.Text += '*';
        Advance();
    }

    return type;
}

void Parser::SkipNewLines()
{
    while(Match(TokenType::NewLine).has_value()){}
}

void Parser::AdvanceAndSNL()
{
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

bool Parser::IsGroupAttrStart() const
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

// `nome(` ou `Escopo::nome(`. O lookahead de 4 tokens só vale quando os
// tokens intermédios são realmente '::', senão `a + b + (c)` seria lido
// como uma chamada.
bool Parser::IsCallStart() const
{
    if (Peek(1).Type == TokenType::LeftParen)
    {
        return true;
    }

    return Peek(1).Type == TokenType::Colon &&
        Peek(2).Type == TokenType::Colon &&
        Peek(3).Type == TokenType::Identifier &&
        Peek(4).Type == TokenType::LeftParen;
}

bool Parser::IsAssignmentStart() const
{
    if (IsAssignToken(Peek(1).Type))
    {
        return true;
    }

    return Peek(1).Type == TokenType::Colon &&
        Peek(2).Type == TokenType::Colon &&
        Peek(3).Type == TokenType::Identifier &&
        IsAssignToken(Peek(4).Type);
}

void Parser::Parse()
{
    if (!m_Context.AST)
    {
        m_Context.AST = std::make_unique<Program>();
    }

    // Multi-arquivo: cada arquivo é parseado em sequência e seus nós são
    // anexados ao mesmo Program; escopos ([Main], [stl], ...) são preservados
    // entre arquivos para que reabrir uma seção continue a mesma.
    Program& program = *m_Context.AST;
    m_Scopes = m_Context.Scopes;
    m_Position = 0;

    while(!IsAtEnd())
    {
        // Ignora linhas vazias e terminadores ';' soltos.
        while (true)
        {
            SkipNewLines();
            if (!Match(TokenType::Semicolon)) break;
        }

        if(IsAtEnd())
            break;


        if(IsGroupAttrStart())
        {
            ParseGroupAttr();    
        }
        else
        {
            if (m_CurrentScope.Name.Text.empty())
            {
                m_Context.Diagnostics.Add(
            	    DiagnosticLevel::Fatal,
                    Current().Location,
                    "The file must start with a section, e.g. [Main]."
                );
                return;    
            }

            switch(Current().Type)
            {
                case Emux::TokenType::Keyword:
                {
                    if (Current().Text == "func")
                        ParseFunction(program);
                    else if (Current().Text == "exir")
                        ParseIR(program);
                    else if (Current().Text == "var")
                        ParseVariable(program);
                    else if (Current().Text == "using")
                        ParseUsing(program);
                    else if (Current().Text == "enum")
                        ParseEnum(program);
                    else
                    {
                        Advance();
                        Synchronize();
                    }
                    break;
                }
                case Emux::TokenType::Identifier:
                {
                    if (Peek(1).Type == TokenType::Colon)
                    {
                        m_Context.Diagnostics.Add(
                            DiagnosticLevel::Error,
                            Current().Location,
                            "Variables must be declared with 'var' keyword: 'var " + Current().Text + " : <type>'"
                        );
                        ParseVariable(program);
                        break;
                    }
                    [[fallthrough]];
                }
                default:
                    ParseExpression(program);
                    EndStatement();
                    break;
            }
        }
    }

    m_Context.Scopes = m_Scopes;
}

static std::string ToLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void Parser::ParseGroupAttr()
{
    if(!Consume(
        TokenType::LeftBracket,
        "Expected '[' in Group of Attributes or Scope"
    ))
    {
        Advance();
        Synchronize();
        return;
    }

    // Se o próximo token for '[', estamos diante de uma lista de atributos.
    // Formas aceites: [[a,b]], [[a][b]] e [[a],[b]]; a vírgula é opcional.
    if (Check(TokenType::LeftBracket))
    {
        Consume(TokenType::LeftBracket, "Expected '[' in Attribute set");

        AttrSet group;
        bool closed = false;

        while (!IsAtEnd() && !closed)
        {
            SkipNewLines();

            // ']' fecha o bloco atual. A seguir pode vir ',' e/ou '[' (mais
            // atributos) ou o ']' externo que encerra a lista.
            if (Check(TokenType::RightBracket))
            {
                Advance();

                while (Match(TokenType::Comma)) { }
                SkipNewLines();

                if (Check(TokenType::LeftBracket))
                {
                    Advance(); // Abre o bloco seguinte.
                    continue;
                }

                if (Consume(TokenType::RightBracket, "Expected ']' in Attribute set"))
                {
                    closed = true;
                }
                else
                {
                    Advance();
                    Synchronize();
                }
                break;
            }

            if (Match(TokenType::Comma))
            {
                continue; // Separador opcional entre atributos.
            }

            auto nameTk = Consume(
                TokenType::Identifier,
                "Expected Attribute name"
            );
            if (!nameTk) break;

            auto it = kAttrTable.find(ToLower(nameTk->get().Text));
            if (it == kAttrTable.end())
            {
                m_Context.Diagnostics.Add(
                    DiagnosticLevel::Warning,
                    nameTk->get().Location,
                    "Unknown Attribute '" + nameTk->get().Text + "'"
                );
            }

            // Valor opcional entre parênteses: [[import("kernel32")]]
            std::string attrValue;
            if (Check(TokenType::LeftParen))
            {
                Advance();
                SkipNewLines();

                if (Check(TokenType::String) ||
                    Check(TokenType::Identifier) ||
                    Check(TokenType::Number))
                {
                    attrValue = Current().Text;
                    Advance();
                }
                // Valor negativo, ex: [[step(-1)]]. O lexer não produz um único
                // token para '-1', por isso o sinal é juntado aqui.
                else if (Check(TokenType::Minus) && Peek(1).Type == TokenType::Number)
                {
                    Advance();
                    attrValue = "-" + Current().Text;
                    Advance();
                }
                else if (!Check(TokenType::RightParen))
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        Current().Location,
                        "Expected a value inside attribute '" + nameTk->get().Text + "'."
                    );
                }

                SkipNewLines();
                Consume(
                    TokenType::RightParen,
                    "Expected ')' after attribute value."
                );
            }

            if (it != kAttrTable.end())
            {
                AttrId id = it->second;
                if (group.Has(id))
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Warning,
                        nameTk->get().Location,
                        "Duplicated Attribute '" + nameTk->get().Text + "'"
                    );
                }
                group.Set(id, std::move(attrValue));
            }
        }

        m_Pending.Merge(group);
        return;
    }

    // Caso contrário, é a declaração de um escopo [NomeDoEscopo]
    AttrSet scopeAttrs = m_Pending;
    m_Pending.Clear();

    if (Check(TokenType::Identifier))
    {
        OpenGlobalScope(Current(), scopeAttrs);
        Advance();
    }
    else
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Expected scope identifier inside '[' ']'"
        );
    }

    if (!Consume(
        TokenType::RightBracket,
        "Expected ']' in Scope declaration"
    ))
    {
        Advance();
        Synchronize();
    }
}

void Parser::ParseDependencies(
    Node& /*section*/
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

/*
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
*/
}

void Parser::ParseVariable(
    Node& node
)
{
    if (Check(TokenType::Keyword) && Current().Text == "var")
    {
        Advance(); // Consome 'var'
    }

    auto name = Consume(
        TokenType::Identifier,
        "Expected variable name."
    );


    Consume(
        TokenType::Colon,
        "Expected ':'."
    );


    auto type = ParseTypeToken(
        "Expected type."
    );


    if(!name || !type)
    {
        Synchronize();
        return;
    }

    std::string scopedName = ToLower(m_CurrentScope.Name.Text) + "::" + name->get().Text;
    
    Token scopedToken{
        .Type = TokenType::Identifier,
        .Text = scopedName,
        .Location = name->get().Location
    };

    auto variable = std::make_unique<VariableNode>(
        scopedToken.Location
    );

    StampAttrs(*variable);
    variable->Name = scopedToken;

    variable->Type = *type;

    // Inicializador de declaracao: reservado a [[const]], porque para as
    // restantes variaveis a atribuicao continua a ser um statement.
    if (Match(TokenType::Assign))
    {
        if (!variable->Attrs.Has(AttrId::Const))
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                Current().Location,
                "Variable '" + name->get().Text +
                "' has an initializer but is not declared [[const]]."
            );
        }

        const bool signedLiteral =
            Check(TokenType::Minus) && Peek(1).Type == TokenType::Number;

        if (IsLiteral() || signedLiteral)
        {
            ParseLiteral(*variable);
        }
        else
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                Current().Location,
                "Initializer of '" + name->get().Text +
                "' must be a literal number or string."
            );
            Synchronize();
        }
    }

    node.Children.push_back(
        std::move(variable)
    );

    EndStatement();
}

void Parser::ParseUsing(
    Node& node
)
{
    // using Nome = Tipo
    if (Check(TokenType::Keyword) && Current().Text == "using")
    {
        Advance(); // Consome 'using'
    }

    auto name = Consume(
        TokenType::Identifier,
        "Expected type alias name."
    );

    Consume(
        TokenType::Assign,
        "Expected '=' after type alias name."
    );

    auto type = ParseTypeToken(
        "Expected aliased type."
    );

    if (!name || !type)
    {
        Synchronize();
        return;
    }

    std::string scopedName = ToLower(m_CurrentScope.Name.Text) + "::" + name->get().Text;

    Token scopedToken{
        .Type = TokenType::Identifier,
        .Text = scopedName,
        .Location = name->get().Location
    };

    auto usingNode = std::make_unique<UsingNode>(
        *type,
        scopedToken.Location
    );

    // So os atributos explicitamente pendentes definem a restricao; os do
    // escopo (ex: [[nowarn]]) nao devem mudar o tipo de todos os usings.
    usingNode->Attrs.Merge(m_Pending);
    m_Pending.Clear();
    usingNode->Name = scopedToken;

    node.Children.push_back(std::move(usingNode));

    EndStatement();
}

void Parser::ParseEnum(
    Node& node
)
{
    // enum Nome : Tipo {
    //   Elemento = Valor,
    //   Outro
    // }
    // O nome é opcional: sem ele os elementos ficam diretamente no escopo.
    // Entre elementos aceitam-se vírgula e/ou quebra de linha, ambas opcionais.
    const Token keyword = Current();

    if (Check(TokenType::Keyword) && Current().Text == "enum")
    {
        Advance(); // Consome 'enum'
    }

    SkipNewLines();

    const OptionalToken name = Match(TokenType::Identifier);

    Token type{TokenType::Identifier, "u32", {}};
    if (Match(TokenType::Colon))
    {
        SkipNewLines();
        type = ParseTypeToken(
            "Expected enum type."
        ).value_or(type);
    }

    if (!Consume(TokenType::LeftBrace, "Expected '{' after the enum header."))
    {
        Synchronize();
        return;
    }

    std::vector<EnumElement> elements;

    while (true)
    {
        SkipNewLines();

        if (IsAtEnd() || Check(TokenType::RightBrace))
        {
            break;
        }

        auto elemName = Consume(
            TokenType::Identifier,
            "Expected enum element name."
        );

        if (!elemName)
        {
            Synchronize();
            break;
        }

        EnumElement element;
        element.Name = *elemName;

        // Sem '= valor' o elemento fica dependente do auto-incremento.
        if (Match(TokenType::Assign))
        {
            SkipNewLines();
            element.Value = ParseExpression();
        }

        elements.emplace_back(std::move(element));

        SkipNewLines();
        while (Match(TokenType::Comma))
        {
            SkipNewLines();
        }
    }

    if (!Consume(TokenType::RightBrace, "Expected '}' after the enum elements."))
    {
        Synchronize();
        return;
    }

    // Chave do enum: 'escopo::nome' quando nomeado, só 'escopo' quando anónimo.
    // A semântica deriva cada elemento acrescentando '::elemento', o que dá
    // 'main::color::red' no primeiro caso e 'main::red' no segundo.
    Token scopedToken{
        .Type = TokenType::Identifier,
        .Text = ToLower(m_CurrentScope.Name.Text),
        .Location = keyword.Location
    };

    if (name.has_value())
    {
        scopedToken.Text += "::";
        scopedToken.Text += name->get().Text;
        scopedToken.Location = name->get().Location;
    }

    auto enumNode = std::make_unique<EnumNode>(
        scopedToken.Location
    );
    enumNode->Name = scopedToken;
    enumNode->ElementType = type;
    enumNode->Elements = std::move(elements);
    enumNode->IsAnonymous = !name.has_value();

    StampAttrs(*enumNode);

    node.Children.push_back(std::move(enumNode));

    EndStatement();
}

void Parser::ParseFunction(
    Node& node
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
    StampAttrs(*funcNode);
    
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
        // Atributos do parâmetro: func f([[restrict]] Tipo valor)
        AttrSet paramAttrs;
        while (Check(TokenType::LeftBracket) && Peek(1).Type == TokenType::LeftBracket)
        {
            ParseGroupAttr();
            SkipNewLines();
            paramAttrs.Merge(m_Pending);
            m_Pending.Clear();
        }

        // Tipo do parâmetro
        auto paramTypeOpt = ParseTypeToken("Expected parameter type");
        if (!paramTypeOpt.has_value())
        {
            Advance();
            Synchronize();
            return;
        }

        Token paramType = *paramTypeOpt;
        
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
        funcNode->Parameters.emplace_back(paramType, paramName, paramAttrs);
        
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

    std::string scopedName = ToLower(m_CurrentScope.Name.Text) + "::" + funcName.Text;
    Token scopedToken{
        .Type = TokenType::Identifier,
        .Text = scopedName,
        .Location = funcName.Location
    };

    // Pega tipo de retorno
    auto returnTypeOpt = ParseTypeToken("Expected return type after '->'");
    if (!returnTypeOpt.has_value())
    {
        Advance();
        Synchronize();
        return;
    }
    funcNode->ReturnType = *returnTypeOpt;
    SkipNewLines();

    funcNode->Name = scopedToken;

    const bool imported = funcNode->Attrs.Has(AttrId::Import);

    // Corpo: { ... }
    if (!Check(TokenType::LeftBrace))
    {
        // [[import]] declara uma função nativa: a implementação vem do
        // símbolo externo, portanto não tem corpo Emux.
        if (imported)
        {
            node.Children.push_back(std::move(funcNode));
            return;
        }

        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Expected '{' before function body"
        );
        Advance();
        Synchronize();
        return;
    }

    if (imported)
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            funcName.Location,
            "Imported function '" + funcName.Text + "' must not have a body."
        );
    }

    SkipNewLines();
    Advance(); // Consome "{"
    
    // Parse statements do corpo
    while (!Check(TokenType::RightBrace) && !IsAtEnd())
    {
        // Ignora linhas vazias e terminadores ';' soltos.
        while (true)
        {
            SkipNewLines();
            if (!Match(TokenType::Semicolon)) break;
        }

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
        case TokenType::Mod:        return BinaryOperation::Mod;
        case TokenType::And:        return BinaryOperation::And;
        case TokenType::Or:         return BinaryOperation::Or;
        case TokenType::Xor:        return BinaryOperation::Xor;
        case TokenType::LeftShift:  return BinaryOperation::Lshift;
        case TokenType::RightShift: return BinaryOperation::Rshift;
        case TokenType::Equal:        return BinaryOperation::Eq;
        case TokenType::NotEqual:     return BinaryOperation::Ne;
        case TokenType::Less:         return BinaryOperation::Lt;
        case TokenType::Greater:      return BinaryOperation::Gt;
        case TokenType::LessEqual:    return BinaryOperation::Le;
        case TokenType::GreaterEqual: return BinaryOperation::Ge;
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

    // 0. Operadores Unários: Neg (-) e Not (~ / !)
    if (Check(TokenType::Minus))
    {
        SourceLocation loc = Current().Location;
        Advance();
        auto operand = ParsePrimary();
        if (!operand)
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                loc,
                "Expected expression after '-' unary operator."
            );
            return nullptr;
        }
        return std::make_unique<UnaryNode>(UnaryOperation::Neg, std::move(operand), loc);
    }

    if (Check(TokenType::Not))
    {
        SourceLocation loc = Current().Location;
        Advance();
        auto operand = ParsePrimary();
        if (!operand)
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                loc,
                "Expected expression after '~' unary operator."
            );
            return nullptr;
        }
        return std::make_unique<UnaryNode>(UnaryOperation::Not, std::move(operand), loc);
    }

    if (Check(TokenType::LogicalNot))
    {
        SourceLocation loc = Current().Location;
        Advance();
        auto operand = ParsePrimary();
        if (!operand)
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                loc,
                "Expected expression after '!' unary operator."
            );
            return nullptr;
        }
        return std::make_unique<UnaryNode>(UnaryOperation::LogicalNot, std::move(operand), loc);
    }

    if (Check(TokenType::And))
    {
        SourceLocation loc = Current().Location;
        Advance();
        auto operand = ParsePrimary();
        if (!operand)
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                loc,
                "Expected variable after '&' address-of operator."
            );
            return nullptr;
        }
        return std::make_unique<UnaryNode>(UnaryOperation::AddressOf, std::move(operand), loc);
    }

    if (Check(TokenType::Mul))
    {
        SourceLocation loc = Current().Location;
        Advance();
        auto operand = ParsePrimary();
        if (!operand)
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                loc,
                "Expected pointer expression after '*' dereference operator."
            );
            return nullptr;
        }
        return std::make_unique<UnaryNode>(UnaryOperation::Deref, std::move(operand), loc);
    }

    // 1. Suporte a Parênteses
    if (Match(TokenType::LeftParen))
    {
        ++m_ParenDepth;
        auto expr = ParseExpression(0);
        --m_ParenDepth;
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

    // 3. Casts explícitos: static_cast<T>(expr) e reinterpret_cast<T>(expr).
    if (Check(TokenType::Identifier) &&
        (Current().Text == "static_cast" || Current().Text == "reinterpret_cast") &&
        Peek(1).Type == TokenType::Less)
    {
        return ParseCast();
    }

    // 4. Identificadores (Variáveis e Chamadas de Função)
    if (Check(TokenType::Identifier))
    {
        auto tempContainer = std::make_unique<Node>(NodeType::Program, Token{}, Current().Location);
        
        if (IsCallStart())
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
            return ParsePostfix(std::move(node));
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

std::unique_ptr<Node> Parser::ParsePostfix(std::unique_ptr<Node> base)
{
    while (base && Check(TokenType::LeftBracket))
    {
        SourceLocation location = Current().Location;
        Advance(); // Consome '['

        auto index = ParseExpression(0);
        if (!index)
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                location,
                "Expected index expression after '['."
            );
            Synchronize();
            break;
        }

        if (!Consume(TokenType::RightBracket, "Expected ']' after index expression."))
        {
            Synchronize();
            break;
        }

        base = std::make_unique<IndexNode>(
            std::move(base),
            std::move(index),
            location
        );
    }

    // Incremento é uma forma de atribuição; dentro de uma expressão de valor
    // não há representação para o resultado antigo.
    if (base && (Check(TokenType::PlusPlus) || Check(TokenType::MinusMinus)))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "'" + Current().Text +
            "' must be used as a statement; write 'x++' on its own line."
        );
        Advance();
    }

    return base;
}

// Reconhece 'nome[idx] =' (incluindo 'escopo::nome[idx] =') sem consumir.
bool Parser::IsIndexAssignment() const
{
    if (!Check(TokenType::Identifier))
    {
        return false;
    }

    std::size_t i = 1;

    // Salta qualificadores de escopo: '::' Identifier.
    while (Peek(i).Type == TokenType::Colon &&
           Peek(i + 1).Type == TokenType::Colon &&
           Peek(i + 2).Type == TokenType::Identifier)
    {
        i += 3;
    }

    if (Peek(i).Type != TokenType::LeftBracket)
    {
        return false;
    }

    int depth = 0;
    for (;; ++i)
    {
        const TokenType type = Peek(i).Type;

        if (type == TokenType::EndOfFile || type == TokenType::NewLine)
        {
            return false;
        }

        if (type == TokenType::LeftBracket)
        {
            ++depth;
        }
        else if (type == TokenType::RightBracket)
        {
            --depth;
            if (depth == 0)
            {
                return IsAssignToken(Peek(i + 1).Type);
            }
        }
    }
}

void Parser::ParseIndexAssignment(Node& node)
{
    if (!IsIndexAssignment())
    {
        return;
    }

    SourceLocation location = Current().Location;
    auto name = HelperScopedName();

    if (!name.has_value())
    {
        return;
    }

    Token scopedToken{
        .Type = TokenType::Identifier,
        .Text = *name,
        .Location = location
    };

    auto base = std::make_unique<VariableCallNode>(scopedToken, location);

    if (!Consume(TokenType::LeftBracket, "Expected '[' in index assignment."))
    {
        Synchronize();
        return;
    }

    auto index = ParseExpression(0);
    if (!index)
    {
        Synchronize();
        return;
    }

    if (!Consume(TokenType::RightBracket, "Expected ']' after index expression."))
    {
        Synchronize();
        return;
    }

    auto indexNode = std::make_unique<IndexNode>(
        std::move(base),
        std::move(index),
        location
    );

    auto assignNode = std::make_unique<AssignmentNode>(scopedToken, location);
    assignNode->IsIndexed = true;
    assignNode->Target = std::move(indexNode);

    const TokenType assignType = Current().Type;
    assignNode->CompoundOp = CompoundOperation(assignType);
    StampAttrs(*assignNode);

    if (!Consume(assignType, "Expected an assignment operator after index target."))
    {
        Advance();
        Synchronize();
        return;
    }

    ParseExpression(*assignNode);

    node.Children.push_back(std::move(assignNode));
}

// Algoritmo de Pratt Parsing para resolver Precedência e Associatividade
std::unique_ptr<Node> Parser::ParseExpression(int precedence)
{
    SkipNewLines();

    auto left = ParsePrimary();
    if (!left) return nullptr;

    while (true)
    {
        // Fora de parênteses a expressão termina na nova linha: sem isto uma
        // linha iniciada por '*' (deref) seria lida como multiplicação.
        if (m_ParenDepth > 0)
        {
            SkipNewLines();
        }

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
    // Escrita por índice (base[i] = valor) tem de ser reconhecida antes da
    // atribuição simples, porque o nome é seguido de '[' e não de '='.
    if (Check(TokenType::Identifier) && IsIncrementStart())
    {
        ParseIncrement(node);
        return;
    }

    if (Check(TokenType::Identifier) && IsIndexAssignment())
    {
        ParseIndexAssignment(node);
        return;
    }

    if (Check(TokenType::Identifier) && IsAssignmentStart())
    {
        ParseAssignment(node);
        return;
    }

    if (Check(TokenType::Mul) && IsDerefAssignment())
    {
        ParseDerefAssignment(node);
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
    if (!IsCallStart())
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
        Advance();
        Synchronize();
        return;
    }

    ++m_ParenDepth;
    SkipNewLines();
    while (Current().Type != TokenType::RightParen && 
           Current().Type != TokenType::EndOfFile)
    {
        ParseExpression(*funcCallNode);

        if (Check(TokenType::Comma))  // Consome ,
        {
            AdvanceAndSNL();
            continue;
        }

        if (!Check(TokenType::RightParen))
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                Current().Location,
                "Expected ',' or ')' after argument in call to '" + nameText + "'."
            );
            break;
        }
    }
    --m_ParenDepth;
    
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
    if (!IsAssignmentStart())
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

    const TokenType assignType = Current().Type;

    if(!Consume(assignType, "Expected an assignment operator after '"+nameText+"'."))
    {
        Advance();
        Synchronize();
        return;
    }

    auto assignNode = std::make_unique<AssignmentNode>(
        scopedToken,
        scopedToken.Location
    );
    assignNode->CompoundOp = CompoundOperation(assignType);
    StampAttrs(*assignNode);

    ParseExpression(*assignNode);

    node.Children.push_back(std::move(assignNode));  
}

bool Parser::IsDerefAssignment() const
{
    if (!Check(TokenType::Mul) || Peek(1).Type != TokenType::Identifier)
    {
        return false;
    }

    if (IsAssignToken(Peek(2).Type))
    {
        return true;
    }

    return Peek(2).Type == TokenType::Colon &&
        Peek(3).Type == TokenType::Colon &&
        Peek(4).Type == TokenType::Identifier &&
        IsAssignToken(Peek(5).Type);
}

void Parser::ParseDerefAssignment(Node& node)
{
    SourceLocation location = Current().Location;
    Advance(); // Consome '*'

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

    auto assignNode = std::make_unique<AssignmentNode>(scopedToken, location);
    assignNode->IsDereference = true;
    assignNode->Target = std::make_unique<VariableCallNode>(scopedToken, location);

    const TokenType assignType = Current().Type;
    assignNode->CompoundOp = CompoundOperation(assignType);
    StampAttrs(*assignNode);

    if (!Consume(assignType, "Expected an assignment operator after '*" + nameText + "'."))
    {
        Advance();
        Synchronize();
        return;
    }

    ParseExpression(*assignNode);

    node.Children.push_back(std::move(assignNode));
}

bool Parser::IsIncrementStart() const
{
    if (!Check(TokenType::Identifier))
    {
        return false;
    }

    std::size_t i = 1;

    while (Peek(i).Type == TokenType::Colon &&
           Peek(i + 1).Type == TokenType::Colon &&
           Peek(i + 2).Type == TokenType::Identifier)
    {
        i += 3;
    }

    return Peek(i).Type == TokenType::PlusPlus ||
        Peek(i).Type == TokenType::MinusMinus;
}

void Parser::ParseIncrement(Node& node)
{
    SourceLocation location = Current().Location;
    auto name = HelperScopedName();

    if (!name.has_value())
    {
        return;
    }

    Token scopedToken{
        .Type = TokenType::Identifier,
        .Text = *name,
        .Location = location
    };

    const bool increment = Check(TokenType::PlusPlus);
    Advance();

    Token one{
        .Type = TokenType::Number,
        .Text = "1",
        .Location = location
    };

    auto assignNode = std::make_unique<AssignmentNode>(scopedToken, location);
    assignNode->CompoundOp =
        increment ? BinaryOperation::Add : BinaryOperation::Sub;
    assignNode->Children.push_back(
        std::make_unique<LiteralNode>(one, location)
    );
    StampAttrs(*assignNode);

    node.Children.push_back(std::move(assignNode));
}

void Parser::ParseLiteral(Node& node)
{
    // Num contexto de expressao o '-' chega aqui como operador unario, mas o
    // inicializador de uma declaracao le o literal diretamente.
    const bool negative =
        Check(TokenType::Minus) && Peek(1).Type == TokenType::Number;

    if (negative)
    {
        Advance();
    }

    if (!Check(TokenType::Number) && !Check(TokenType::String))
    {
        return;
    }

    Token value { Current() };

    if (negative)
    {
        value.Text = "-" + value.Text;
    }

    Token type {
        .Type = TokenType::Identifier,
        .Text = "",
        .Location { value.Location }
    };

    if (Check(TokenType::Number))
    {
        if (NumericLiteral::IsDecimalText(value.Text))
        {
            type.Text = "f64";
        }
        else
        {
            const bool isHex =
                value.Text.size() > 2 &&
                value.Text[0] == '0' &&
                (value.Text[1] == 'x' || value.Text[1] == 'X');

            auto big = BigInt::Parse(value.Text, isHex ? 16 : 10);

            if (!big.has_value())
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

            if (big->FitsInt64())
            {
                const std::int64_t amount = big->ToInt64();

                if (amount >= 0)
                {
                    if (amount <= 255) type.Text = "u8";
                    else if (amount <= 65535) type.Text = "u16";
                    else if (amount <= 4294967295LL) type.Text = "u32";
                    else type.Text = "u64";
                }
                else
                {
                    if (amount >= -128) type.Text = "i8";
                    else if (amount >= -32768) type.Text = "i16";
                    else if (amount >= -2147483648LL) type.Text = "i32";
                    else type.Text = "i64";
                }
            }
            else if (big->FitsUInt64())
            {
                type.Text = "u64";
            }
            else
            {
                // Inteiro arbitrário: o tipo guarda a largura exata necessária.
                // O backend ainda só gera até 64 bits, mas a semântica preserva
                // o valor e o diagnóstico acontece na geração de código.
                std::uint32_t bits = big->BitLength();
                bits = ((bits + 7) / 8) * 8;
                type.Text = "u" + std::to_string(bits);
            }
        }
    } else if (Check(TokenType::String))
    {
        // Um literal de string e um ponteiro para bytes estaticos, terminado
        // por NUL. O bloco 'data' e emitido pelo IRBuilder.
        type.Text = "u8*";
    }

    Advance();

    auto literalNode = std::make_unique<LiteralNode>(
        type,
        value,
        value.Location
    );

    node.Children.push_back(std::move(literalNode));
}

std::unique_ptr<Node> Parser::ParseCast()
{
    const Token name = Current();
    Advance();

    const CastKind kind = name.Text == "static_cast"
        ? CastKind::Static
        : CastKind::Reinterpret;

    if (!Consume(TokenType::Less, "Expected '<' after cast name."))
    {
        Synchronize();
        return nullptr;
    }

    SkipNewLines();

    std::string typeText;

    while (!Check(TokenType::Greater) && !IsAtEnd() && !Check(TokenType::NewLine))
    {
        const Token& token = Current();

        if (token.Type == TokenType::Identifier || token.Type == TokenType::Mul ||
            token.Type == TokenType::Number ||
            (token.Type == TokenType::Unknown && token.Text == "."))
        {
            typeText += token.Text;
            Advance();
            continue;
        }

        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            token.Location,
            "Expected a type in cast."
        );
        Synchronize();
        return nullptr;
    }

    if (typeText.empty())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            name.Location,
            "Cast without type."
        );
        Synchronize();
        return nullptr;
    }

    if (!Consume(TokenType::Greater, "Expected '>' after cast type."))
    {
        Synchronize();
        return nullptr;
    }

    SkipNewLines();

    if (!Consume(TokenType::LeftParen, "Expected '(' after cast type."))
    {
        Synchronize();
        return nullptr;
    }

    ++m_ParenDepth;
    auto operand = ParseExpression(0);
    --m_ParenDepth;

    if (!Consume(TokenType::RightParen, "Expected ')' after cast operand."))
    {
        Synchronize();
        return nullptr;
    }

    if (!operand)
    {
        return nullptr;
    }

    Token typeToken
    {
        .Type = TokenType::Identifier,
        .Text = typeText,
        .Location = name.Location
    };

    return std::make_unique<CastNode>(
        kind,
        typeToken,
        std::move(operand),
        name.Location
    );
}

void Parser::ParseIR(Node& node)
{
    auto irTk = Consume(TokenType::Keyword, "Expected exir");
    if (!irTk)
    {
        return;
    }

    if (irTk->get().Text != "exir")
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            irTk->get().Location,
            "Expected 'exir', got '" + irTk->get().Text + "'."
        );
        return;
    }

    if (!Consume(TokenType::LeftBrace, "Expected { after exir"))
    {
        Synchronize();
        return;
    }

    Token code(TokenType::Identifier, {}, Current().Location);
    auto& content = m_Context.Sources[Current().Location.File].GetContent();
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

    while (!Check(TokenType::RightBrace) && !IsAtEnd())
    {
        Advance();
    }

    if (Check(TokenType::RightBrace))
    {
        Advance(); // Consome }
    }
    else
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Expected '}' after 'exir' block."
        );
    }

    auto irNode = std::make_unique<IRNode>(
        code,
        code.Location
    );
    StampAttrs(*irNode);

    node.Children.push_back(std::move(irNode));
}

void Parser::ParseReturn(Node& node)
{
    if (Current().Text != "return")
    {
        return;
    }

    auto retNode = std::make_unique<ReturnNode>(Current().Location);
    StampAttrs(*retNode);
    Advance();
    ParseExpression(*retNode);
    EndStatement();
    node.Children.push_back(std::move(retNode));
}

void Parser::EndStatement()
{
    bool terminated = false;
    while (Check(TokenType::Semicolon))
    {
        Advance();
        terminated = true;
    }

    if (!terminated && m_Context.Project.semicolons == SemicolonMode::Required)
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Expected ';' after statement."
        );
    }
}

void Parser::ParseStatement(Node& node)
{
    SkipNewLines();

    if (IsGroupAttrStart())
    {
        ParseGroupAttr();
        SkipNewLines();
    }

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

    if (Current().Text == "var")
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Local variables inside functions are not supported. Declare variables at section scope with 'var'."
        );
        Advance();
        Synchronize();
        return;
    }

    // Controlo de fluxo. As keywords ja sao reconhecidas pelo lexer; aqui
    // distinguem-se pelo texto porque TokenType::Keyword e partilhado.
    if (Current().Type == TokenType::Keyword)
    {
        if (Current().Text == "if")
        {
            ParseIf(node);
            return;
        }
        if (Current().Text == "while")
        {
            ParseWhile(node);
            return;
        }
        if (Current().Text == "for")
        {
            ParseFor(node);
            return;
        }
    }

    ParseExpression(node);
    EndStatement();
}

// Lê '( <expr> )' como cabeca de uma construcao de controlo. O incremento de
// m_ParenDepth permite saltar novas linhas dentro da condicao, como em qualquer
// parêntese de expressão.
NodePtr Parser::ParseCondition(std::string_view keyword)
{
    if (!Consume(TokenType::LeftParen, ("Expected '(' after '" + std::string(keyword) + "'.")))
    {
        return nullptr;
    }

    ++m_ParenDepth;
    auto expr = ParseExpression(0);
    --m_ParenDepth;

    if (!Consume(TokenType::RightParen, ("Expected ')' after '" + std::string(keyword) + "' condition.")))
    {
        return nullptr;
    }

    if (!expr)
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            Current().Location,
            "Expected an expression after '" + std::string(keyword) + "'."
        );
    }

    return expr;
}

// Recolhe os statements entre '{' e '}'. Cada statement e produzido por
// ParseStatement num holder e movido para o vetor, o que permite aninhamento
// (um if dentro do corpo de outro trata-se pela mesma via).
bool Parser::ParseBlock(std::vector<NodePtr>& statements)
{
    SkipNewLines();

    if (!Consume(TokenType::LeftBrace, "Expected '{' to open a block."))
    {
        return false;
    }

    while (!Check(TokenType::RightBrace) && !IsAtEnd())
    {
        while (true)
        {
            SkipNewLines();
            if (!Match(TokenType::Semicolon))
            {
                break;
            }
        }

        if (Check(TokenType::RightBrace) || IsAtEnd())
        {
            break;
        }

        Node holder(NodeType::Program, Current().Location);
        ParseStatement(holder);

        for (auto& child : holder.Children)
        {
            statements.push_back(std::move(child));
        }
    }

    if (!Consume(TokenType::RightBrace, "Expected '}' to close the block."))
    {
        return false;
    }

    return true;
}

void Parser::ParseIf(Node& node)
{
    const SourceLocation loc = Current().Location;
    Advance(); // consome 'if'

    auto ifNode = std::make_unique<IfNode>(loc);
    ifNode->Condition = ParseCondition("if");

    if (!ParseBlock(ifNode->ThenBody))
    {
        node.Children.push_back(std::move(ifNode));
        return;
    }

    SkipNewLines();

    if (Current().Type == TokenType::Keyword && Current().Text == "else")
    {
        ifNode->HasElse = true;
        Advance(); // consome 'else'
        SkipNewLines();

        if (Current().Type == TokenType::Keyword && Current().Text == "if")
        {
            // "else if" encadeia: o ramo senao passa a conter um unico If.
            Node holder(NodeType::Program, Current().Location);
            ParseIf(holder);
            for (auto& child : holder.Children)
            {
                ifNode->ElseBody.push_back(std::move(child));
            }
        }
        else
        {
            ParseBlock(ifNode->ElseBody);
        }
    }

    node.Children.push_back(std::move(ifNode));
}

void Parser::ParseWhile(Node& node)
{
    const SourceLocation loc = Current().Location;
    Advance(); // consome 'while'

    auto whileNode = std::make_unique<WhileNode>(loc);
    whileNode->Condition = ParseCondition("while");
    ParseBlock(whileNode->Body);

    node.Children.push_back(std::move(whileNode));
}

void Parser::ParseFor(Node& node)
{
    const SourceLocation loc = Current().Location;
    Advance(); // consome 'for'

    auto forNode = std::make_unique<ForNode>(loc);

    if (!Consume(TokenType::LeftParen, "Expected '(' after 'for'."))
    {
        node.Children.push_back(std::move(forNode));
        return;
    }

    ++m_ParenDepth;
    SkipNewLines();

    // Inicializacao (opcional): um statement de expressao (assign, ++, call).
    if (!Check(TokenType::Semicolon))
    {
        Node holder(NodeType::Program, Current().Location);
        ParseExpression(holder);
        if (!holder.Children.empty())
        {
            forNode->Init = std::move(holder.Children[0]);
        }
    }
    Match(TokenType::Semicolon);
    SkipNewLines();

    // Condicao (opcional): ausente = verdadeiro (loop infinito ate break/return).
    if (!Check(TokenType::Semicolon))
    {
        forNode->Condition = ParseExpression(0);
    }
    Match(TokenType::Semicolon);
    SkipNewLines();

    // Passo (opcional).
    if (!Check(TokenType::RightParen))
    {
        Node holder(NodeType::Program, Current().Location);
        ParseExpression(holder);
        if (!holder.Children.empty())
        {
            forNode->Step = std::move(holder.Children[0]);
        }
    }

    --m_ParenDepth;

    if (!Consume(TokenType::RightParen, "Expected ')' after 'for' clauses."))
    {
        node.Children.push_back(std::move(forNode));
        return;
    }

    ParseBlock(forNode->Body);

    node.Children.push_back(std::move(forNode));
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

    std::vector<std::string> parts{first->get().Text};

    while (Check(TokenType::Colon) && Peek(1).Type == TokenType::Colon)
    {
        Advance();
        Advance();

        auto next = Consume(TokenType::Identifier, "Expected identifier");

        if (!next.has_value())
        {
            Advance();
            Synchronize();
            return std::nullopt;
        }

        parts.push_back(next->get().Text);
    }

    // Um nome simples pertence ao escopo ativo; um nome qualificado já traz o
    // escopo (ou o enum) na primeira parte, ex: 'Main::Color::Red'.
    std::string result = parts.size() == 1
        ? ToLower(m_CurrentScope.Name.Text) + "::" + parts.front()
        : ToLower(parts.front());

    for (std::size_t i = 1; i < parts.size(); ++i)
    {
        result += "::";
        result += parts[i];
    }

    return result;
}

void Parser::OpenGlobalScope(const Token& nameTok, const AttrSet& attrs) {
    std::string key = ToLower(nameTok.Text);  // Main == main == MAIN
    if (m_Scopes.count(key)) {
        // Reabertura de escopo existente: preserva e mescla atributos
        m_CurrentScope = m_Scopes[key];
        m_CurrentScope.Attrs.Merge(attrs);
        m_Scopes[key] = m_CurrentScope;
    }
    else {
        m_CurrentScope = Scope{nameTok, attrs};
        m_Scopes[key] = m_CurrentScope;
    }
}
}
