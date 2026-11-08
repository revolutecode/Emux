#pragma once

#include <Emux/Compiler/AST/AssignmentNode.hpp>
#include <Emux/Compiler/AST/BinaryNode.hpp>
#include <Emux/Compiler/AST/CastNode.hpp>
#include <Emux/Compiler/AST/ControlFlowNodes.hpp>
#include <Emux/Compiler/AST/DeclarationNode.hpp>
#include <Emux/Compiler/AST/EnumNode.hpp>
#include <Emux/Compiler/AST/FunctionCallNode.hpp>
#include <Emux/Compiler/AST/FunctionNode.hpp>
#include <Emux/Compiler/AST/IndexNode.hpp>
#include <Emux/Compiler/AST/LocalNode.hpp>
#include <Emux/Compiler/AST/Program.hpp>
#include <Emux/Compiler/AST/UnaryNode.hpp>
#include <Emux/Compiler/AST/VariableCallNode.hpp>
#include <Emux/Compiler/AST/VariableNode.hpp>
#include <Emux/Compiler/Diagnostics.hpp>
#include <Emux/Compiler/Semantic/SemanticContext.hpp>
#include <Emux/Compiler/Type/Type.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Emux
{

// Regista bool, usize, isize, uptr e iptr como usings restritos embutidos,
// dependentes da largura de ponteiro da plataforma.
void RegisterBuiltinTypes(
    SemanticContext& semantic,
    std::uint32_t pointerBits
);

class SemanticAnalyzer
{
public:
    SemanticAnalyzer(
        Program& program,
        SemanticContext& semantic,
        Diagnostics& diagnostics,
        std::uint32_t pointerBits = 64
    );

    // Regista os using do módulo antes de qualquer variável/função, para que
    // todos os módulos possam usá-los independentemente da ordem.
    void RegisterTypeAliases();

    // Regista os enums do módulo: o tipo nomeado e cada elemento, que passa a
    // ser uma constante de compilação. Corre depois dos using, porque o tipo
    // dos elementos pode ser um alias de outro módulo.
    void RegisterEnums();

    // Primeira fase: registra declarações de todos os módulos antes de
    // analisar qualquer corpo, para que a ordem dos arquivos não importe.
    void RegisterDeclarations();

    // Segunda fase: analisa corpos e chamadas usando as tabelas globais.
    void AnalyzeBodies();

private:
    void AnalyzeVariable(
        VariableNode& variable
    );

    // Declaracao 'local' dentro do corpo: mesma analise de uma global, mas o
    // armazenamento e um slot da frame e o nome so vive ate ao fim do corpo.
    void AnalyzeLocal(
        LocalNode& local
    );

    // Parte comum das duas declaracoes. isLocal troca o armazenamento esperado
    // (regiao estatica vs slot) e as regras que so fazem sentido la.
    void AnalyzeDeclaration(
        DeclarationNode& variable,
        bool isLocal
    );

    // Resolve o tipo dos elementos, aplica o auto-incremento de [[step]] e
    // regista cada elemento como constante imutável.
    void AnalyzeEnum(
        EnumNode& node
    );

    // 'Enum::Elemento' chega do parser como 'enum::Elemento', sem o escopo.
    // Devolve a chave completa quando o sufixo identifica um único elemento.
    std::optional<std::string> ResolveEnumElement(
        const std::string& name
    ) const;

    // Valida o inicializador de uma declaracao: literal de string
    // para blocos b, literal numerico para escalares. [[const]] vai para
    // .rdata; sem [[const]] o valor e mutavel e vive em .data. Nos locais o
    // inicializador materializa-se como um store no slot, em runtime.
    void ValidateInitializer(
        DeclarationNode& variable,
        const Type& type,
        bool isLocal
    );

    Type AnalyzeVariableCall(
        VariableCallNode& variable
    );

    void DeclareFunction(
        FunctionNode& function
    );

    void RegisterExportSymbol(
        const std::string& symbol,
        const std::string& owner,
        const SourceLocation& location
    );

    void AnalyzeFunctionBody(
        FunctionNode& function
    );

    // Nome do bloco b [[restrict]] referido pela expressao, ou vazio quando
    // nao se trata de um bloco restrito.
    std::string RestrictedBufferName(
        const Node& node
    ) const;

    Type AnalyzeFunctionCall(
        FunctionCallNode& function
    );

    void AnalyzeAssignment(
        AssignmentNode& node
    );

    Type AnalyzeUnary(
        UnaryNode& node
    );

    Type AnalyzeIndex(
        IndexNode& node
    );

    Type AnalyzeBinary(
        BinaryNode& node
    );

    Type AnalyzeCast(
        CastNode& node
    );

    Type AnalyzeExpression(
        Node& node
    );

    // Controlo de fluxo: analisam a condicao e recursam nos blocos.
    Type AnalyzeIf(IfNode& node);
    Type AnalyzeWhile(WhileNode& node);
    Type AnalyzeFor(ForNode& node);
    // Analisa uma sequencia de statements (corpo de bloco) e valida a condicao.
    void AnalyzeStatements(std::vector<NodePtr>& statements);
    void AnalyzeCondition(Node* condition, const SourceLocation& location);

    // Resolve um tipo escrito na fonte, incluindo usings e ponteiros.
    std::optional<Type> ResolveType(
        std::string_view text,
        const std::string& scope
    );

    std::optional<Type> ResolveAlias(
        const std::string& key,
        int depth
    );

    std::optional<Type> ResolveNamedType(
        std::string_view name,
        const std::string& scope
    );

private:
    Program& m_Program;
    SemanticContext& m_Semantic;
    Diagnostics& m_Diagnostics;
    std::uint32_t m_PointerBits = 64;
    std::optional<Type> m_CurrentReturnType;
    std::string m_CurrentFunctionName;
    std::string m_CurrentScopePrefix;
    // Chave do enum a ser analisado: dentro dos inicializadores dos seus
    // elementos um nome simples refere-se a outro elemento do mesmo enum.
    std::string m_CurrentEnumKey;
    std::unordered_set<std::string> m_ResolvingAliases;

    // Nomes escopados dos 'local' declarados no corpo em analise; saem do
    // ambito quando AnalyzeFunctionBody termina, como os parametros.
    std::vector<std::string> m_BodyLocals;

    // Verdadeiro enquanto se analisa o corpo de uma funcao [[noframe]]: os
    // seus 'local' sao rejeitados na declaracao.
    bool m_CurrentFunctionNoFrame = false;

    // Profundidade de loops no corpo em analise. 'break' e 'continue' so tem
    // alvo quando e maior que zero; os 'if' no caminho nao a alteram.
    int m_LoopDepth = 0;
};

// Valida o programa inteiro depois que todos os módulos foram analisados.
void ValidateSemantic(
    const SemanticContext& semantic,
    Diagnostics& diagnostics
);

}
