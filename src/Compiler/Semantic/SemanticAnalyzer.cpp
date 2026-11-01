#include <Emux/Compiler/Semantic/SemanticAnalyzer.hpp>
#include <Emux/Compiler/AST/BinaryNode.hpp>
#include <Emux/Compiler/AST/CastNode.hpp>
#include <Emux/Compiler/AST/LiteralNode.hpp>
#include <Emux/Compiler/AST/ReturnNode.hpp>
#include <Emux/Compiler/AST/UnaryNode.hpp>
#include <Emux/Compiler/AST/UsingNode.hpp>
#include <Emux/Compiler/Type/TypeParser.hpp>
#include <Emux/Utilities/BigInt.hpp>

#include <cstddef>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Emux
{

namespace
{

std::string RawFunctionName(std::string_view scoped)
{
    const auto separator = scoped.rfind("::");
    if (separator == std::string_view::npos)
    {
        return std::string(scoped);
    }

    return std::string(scoped.substr(separator + 2));
}

bool IsValidSymbolName(std::string_view name)
{
    if (name.empty() ||
        !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_'))
    {
        return false;
    }

    for (char c : name)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
        {
            return false;
        }
    }

    return true;
}

Abi AttributeAbi(const AttrSet& attrs)
{
    if (attrs.Has(AttrId::Stdcall)) return Abi::Stdcall;
    if (attrs.Has(AttrId::Fastcall)) return Abi::Fastcall;
    return Abi::Cdecl;
}

int AttributeAbiCount(const AttrSet& attrs)
{
    int count = 0;
    count += attrs.Has(AttrId::Stdcall) ? 1 : 0;
    count += attrs.Has(AttrId::Cdecl) ? 1 : 0;
    count += attrs.Has(AttrId::Fastcall) ? 1 : 0;
    return count;
}

bool IsValidLibraryName(std::string_view name)
{
    if (name.empty())
    {
        return true;
    }

    for (char c : name)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.')
        {
            return false;
        }
    }

    return true;
}

std::string ToLower(std::string text)
{
    for (char& c : text)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return text;
}

// "main::Byte" -> "main".
std::string ScopePrefixOf(const std::string& scoped)
{
    const auto separator = scoped.rfind("::");
    if (separator == std::string::npos)
    {
        return {};
    }

    return scoped.substr(0, separator);
}

// Um literal inteiro cabe na representacao do tipo alvo?
bool FitsLiteral(const Type& type, std::int64_t value)
{
    const Type repr = type.Representation();

    if (!repr.IsInteger())
    {
        return false;
    }

    const std::uint32_t bits = repr.Bits;

    if (value >= 0)
    {
        if (bits >= 63)
        {
            return true;
        }

        return static_cast<std::uint64_t>(value) < (std::uint64_t(1) << bits);
    }

    if (!repr.IsSigned())
    {
        return false;
    }

    if (bits >= 64)
    {
        return true;
    }

    return value >= -(std::int64_t(1) << (bits - 1));
}

std::optional<std::int64_t> LiteralValueOf(const Node& node)
{
    if (node.GetType() != NodeType::Literal)
    {
        return std::nullopt;
    }

    const auto& literal = static_cast<const LiteralNode&>(node);

    if (literal.Value.Type != TokenType::Number)
    {
        return std::nullopt;
    }

    const bool isHex =
        literal.Value.Text.size() > 2 &&
        literal.Value.Text[0] == '0' &&
        (literal.Value.Text[1] == 'x' || literal.Value.Text[1] == 'X');

    auto big = BigInt::Parse(literal.Value.Text, isHex ? 16 : 10);

    if (!big.has_value() || !big->FitsInt64())
    {
        return std::nullopt;
    }

    return big->ToInt64();
}

std::optional<BigInt> ParseNumberLiteral(const Token& value)
{
    const bool isHex =
        value.Text.size() > 2 &&
        value.Text[0] == '0' &&
        (value.Text[1] == 'x' || value.Text[1] == 'X');

    return BigInt::Parse(value.Text, isHex ? 16 : 10);
}

std::optional<BigInt> LiteralBigValueOf(const Node& node)
{
    if (node.GetType() != NodeType::Literal)
    {
        return std::nullopt;
    }

    const auto& literal = static_cast<const LiteralNode&>(node);

    if (literal.Value.Type != TokenType::Number)
    {
        return std::nullopt;
    }

    return ParseNumberLiteral(literal.Value);
}

// Tipo mínimo que representa exatamente o valor. Valores que não cabem em
// 64 bits ganham uN/iN com a bit-length exata (arredondada a bytes); o
// armazenamento em runtime usa representação multi-limb.
Type MinimalTypeForValue(const BigInt& value)
{
    if (value.FitsInt64())
    {
        const std::int64_t amount = value.ToInt64();

        if (amount >= 0)
        {
            if (amount <= 255) return Type::MakeUnsigned(8);
            if (amount <= 65535) return Type::MakeUnsigned(16);
            if (amount <= 4294967295LL) return Type::MakeUnsigned(32);
            return Type::MakeUnsigned(64);
        }

        if (amount >= -128) return Type::MakeSigned(8);
        if (amount >= -32768) return Type::MakeSigned(16);
        if (amount >= -2147483648LL) return Type::MakeSigned(32);
        return Type::MakeSigned(64);
    }

    if (value.FitsUInt64())
    {
        return Type::MakeUnsigned(64);
    }

    std::uint32_t bits = value.BitLength();
    if (value.IsNegative())
    {
        ++bits; // bit de sinal
    }

    bits = ((bits + 7) / 8) * 8;
    return value.IsNegative() ? Type::MakeSigned(bits) : Type::MakeUnsigned(bits);
}

bool FitsLiteral(const Type& type, const BigInt& value)
{
    const Type repr = type.Representation();
    return repr.IsInteger() && value.FitsBits(repr.Bits, repr.IsSigned());
}

bool IsWideInteger(const Type& type)
{
    return type.IsInteger() && type.Bits > 64;
}

// Float e ponto fixo ja existem no modelo de tipos e na Exir, mas ainda nao
// tem codegen. Todas as operacoes de runtime sobre eles falham com um
// diagnostico explicito em vez de serem tratadas como inteiros.
bool IsDeferredNumeric(const Type& type)
{
    const Type repr = type.Representation();
    return repr.IsFloat() || repr.IsFixed();
}

// Aplica a semântica de largura fixa do tipo: o resultado é sempre tomado
// módulo 2^bits, como em runtime, inclusive nos tipos multi-limb.
BigInt WrapValue(const BigInt& value, const Type& type)
{
    if (!type.IsValid() || !type.IsInteger() ||
        type.Bits == 0)
    {
        return value;
    }

    return value.WrappedTo(type.Bits, type.IsSigned());
}

std::optional<BigInt> ApplyBinaryConst(
    BinaryOperation operation,
    const BigInt& leftValue,
    const BigInt& rightValue,
    const Type& type
)
{
    if (!type.IsValid() || !type.IsInteger())
    {
        return std::nullopt;
    }

    const BigInt left = WrapValue(leftValue, type);
    const BigInt right = WrapValue(rightValue, type);

    switch (operation)
    {
        case BinaryOperation::Add:
            return WrapValue(left + right, type);
        case BinaryOperation::Sub:
            return WrapValue(left - right, type);
        case BinaryOperation::Mul:
            return WrapValue(left * right, type);
        case BinaryOperation::Div:
        {
            auto divided = left.DivMod(right);
            if (!divided.has_value())
            {
                return std::nullopt;
            }
            return WrapValue(divided->first, type);
        }
        case BinaryOperation::Mod:
        {
            // O resto segue o sinal do dividendo, como em C.
            auto divided = left.DivMod(right);
            if (!divided.has_value())
            {
                return std::nullopt;
            }
            return WrapValue(divided->second, type);
        }
        case BinaryOperation::Xor:
            return WrapValue(left ^ right, type);
        case BinaryOperation::And:
            return WrapValue(left & right, type);
        case BinaryOperation::Or:
            return WrapValue(left | right, type);
        case BinaryOperation::Lshift:
        case BinaryOperation::Rshift:
        {
            constexpr std::uint64_t maxShift = 1ull << 20;

            if (!right.FitsUInt64())
            {
                return std::nullopt;
            }

            const std::uint64_t count = right.ToUInt64();
            if (count > maxShift)
            {
                return std::nullopt;
            }

            BigInt shifted = operation == BinaryOperation::Lshift
                ? left.Shl(static_cast<std::uint32_t>(count))
                : (type.IsSigned()
                    ? left.ShrArithmetic(static_cast<std::uint32_t>(count))
                    : left.Shr(static_cast<std::uint32_t>(count)));

            return WrapValue(shifted, type);
        }
    }

    return std::nullopt;
}

std::optional<BigInt> ApplyUnaryConst(
    UnaryOperation operation,
    const BigInt& value,
    const Type& type
)
{
    switch (operation)
    {
        case UnaryOperation::Neg:
            return WrapValue(-value, type);
        case UnaryOperation::Not:
            return WrapValue(~value, type);
        case UnaryOperation::LogicalNot:
            return BigInt::FromUInt64(value.IsZero() ? 1 : 0);
        default:
            return std::nullopt;
    }
}

std::optional<BigInt> EvaluateConstExpr(const Node& node)
{
    if (node.FoldedValue.has_value())
    {
        return node.FoldedValue;
    }

    if (node.GetType() == NodeType::Literal)
    {
        return LiteralBigValueOf(node);
    }

    if (node.GetType() == NodeType::Binary)
    {
        const auto& binary = static_cast<const BinaryNode&>(node);

        if (binary.Children.size() < 2 || !binary.Children[0] || !binary.Children[1])
        {
            return std::nullopt;
        }

        const auto left = EvaluateConstExpr(*binary.Children[0]);
        const auto right = EvaluateConstExpr(*binary.Children[1]);

        if (!left || !right)
        {
            return std::nullopt;
        }

        return ApplyBinaryConst(
            binary.Operation,
            *left,
            *right,
            binary.EvaluatedType
        );
    }

    if (node.GetType() == NodeType::Unary)
    {
        const auto& unary = static_cast<const UnaryNode&>(node);

        if (unary.Children.empty() || !unary.Children[0])
        {
            return std::nullopt;
        }

        const auto value = EvaluateConstExpr(*unary.Children[0]);
        if (!value)
        {
            return std::nullopt;
        }

        return ApplyUnaryConst(unary.Operation, *value, unary.EvaluatedType);
    }

    return std::nullopt;
}

// Dobra a expressão e, se o tipo ainda for >64 bits mas o valor couber num
// tipo menor, estreita o tipo. Nunca alarga: o tipo de uma expressão com
// operandos largos é o tipo comum das suas partes.
void FoldConstant(Node& node)
{
    if (!node.EvaluatedType.IsInteger())
    {
        return;
    }

    const auto value = EvaluateConstExpr(node);
    if (!value.has_value())
    {
        return;
    }

    node.FoldedValue = *value;

    if (node.EvaluatedType.Bits > 64)
    {
        const Type minimal = MinimalTypeForValue(*value);
        if (minimal.Bits < node.EvaluatedType.Bits)
        {
            node.EvaluatedType = minimal;
        }
    }
}

}

// Um bloco b e uma regiao estatica dona; usado como valor decai para o
// endereco do primeiro byte, tal como um array em C++.
Type DecayBuffer(const Type& type, std::uint32_t pointerBits)
{
    if (!type.IsBuffer())
    {
        return type;
    }

    return Type::MakePointer(Type::MakeUnsigned(8), pointerBits);
}

SemanticAnalyzer::SemanticAnalyzer(
    Program& program,
    SemanticContext& semantic,
    Diagnostics& diagnostics,
    std::uint32_t pointerBits
):
    m_Program(program),
    m_Semantic(semantic),
    m_Diagnostics(diagnostics),
    m_PointerBits(pointerBits)
{
}

void RegisterBuiltinTypes(
    SemanticContext& semantic,
    std::uint32_t pointerBits
)
{
    const auto add = [&semantic](const char* name, const Type& underlying)
    {
        semantic.ResolvedTypes.emplace(name, Type::MakeNominal(name, underlying));
    };

    add("bool", Type::MakeUnsigned(1));
    add("usize", Type::MakeUnsigned(pointerBits));
    add("isize", Type::MakeSigned(pointerBits));
    add("uptr", Type::MakeUnsigned(pointerBits));
    add("iptr", Type::MakeSigned(pointerBits));
}

void SemanticAnalyzer::RegisterTypeAliases()
{
    for (auto& child : m_Program.Children)
    {
        if (!child || child->GetType() != NodeType::Using)
        {
            continue;
        }

        UsingNode& alias = static_cast<UsingNode&>(*child);
        const std::string key = ToLower(alias.Name.Text);

        const auto separator = key.rfind("::");
        const std::string simple = separator == std::string::npos
            ? key
            : key.substr(separator + 2);

        if (simple == "bool" || simple == "usize" || simple == "isize" ||
            simple == "uptr" || simple == "iptr")
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                alias.Name.Location,
                "Type alias '" + alias.Name.Text + "' cannot redefine a built-in type."
            );
            continue;
        }

        if (m_Semantic.TypeAliases.contains(key) || m_Semantic.ResolvedTypes.contains(key))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                alias.Name.Location,
                "Type alias '" + alias.Name.Text + "' already defined."
            );
            continue;
        }

        if (alias.Attrs.Has(AttrId::Restrict) && alias.Attrs.Has(AttrId::Unrestrict))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Warning,
                alias.Name.Location,
                "'" + alias.Name.Text + "' has both [[restrict]] and [[unrestrict]]; [[restrict]] wins."
            );
        }

        TypeAliasDefinition definition;
        definition.Name = alias.Name.Text;
        definition.Target = alias.Type.Text;
        definition.Scope = ToLower(ScopePrefixOf(alias.Name.Text));
        definition.Unrestricted =
            alias.Attrs.Has(AttrId::Unrestrict) && !alias.Attrs.Has(AttrId::Restrict);
        definition.Location = alias.Name.Location;

        m_Semantic.TypeAliases.emplace(key, std::move(definition));
    }
}

std::optional<Type> SemanticAnalyzer::ResolveAlias(
    const std::string& key,
    int depth
)
{
    if (auto it = m_Semantic.ResolvedTypes.find(key); it != m_Semantic.ResolvedTypes.end())
    {
        return it->second.IsValid() ? std::optional<Type>(it->second) : std::nullopt;
    }

    auto definition = m_Semantic.TypeAliases.find(key);
    if (definition == m_Semantic.TypeAliases.end())
    {
        return std::nullopt;
    }

    if (depth > 64 || !m_ResolvingAliases.insert(key).second)
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            definition->second.Location,
            "Circular type alias '" + key + "'."
        );
        m_Semantic.ResolvedTypes.emplace(key, Type::MakeInvalid());
        return std::nullopt;
    }

    auto underlying = ResolveType(definition->second.Target, definition->second.Scope);
    m_ResolvingAliases.erase(key);

    if (!underlying.has_value() || !underlying->IsValid())
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            definition->second.Location,
            "Unknown or invalid type '" + definition->second.Target +
            "' in using '" + key + "'."
        );
        m_Semantic.ResolvedTypes.emplace(key, Type::MakeInvalid());
        return std::nullopt;
    }

    // O nome vive na propria identidade: o using e um tipo distinto mesmo
    // que a representacao seja igual a de outro.
    Type resolved = definition->second.Unrestricted
        ? *underlying
        : Type::MakeNominal(definition->second.Name, *underlying);

    m_Semantic.ResolvedTypes.emplace(key, resolved);
    return resolved;
}

std::optional<Type> SemanticAnalyzer::ResolveNamedType(
    std::string_view name,
    const std::string& scope
)
{
    const std::string base = ToLower(std::string(name));

    if (!scope.empty())
    {
        const std::string scoped = ToLower(scope) + "::" + base;
        if (m_Semantic.ResolvedTypes.contains(scoped) || m_Semantic.TypeAliases.contains(scoped))
        {
            return ResolveAlias(scoped, 0);
        }
    }

    if (m_Semantic.ResolvedTypes.contains(base) || m_Semantic.TypeAliases.contains(base))
    {
        return ResolveAlias(base, 0);
    }

    // Fora do escopo: aceita um using de nome simples unico.
    std::optional<std::string> unique;
    for (const auto& [key, definition] : m_Semantic.TypeAliases)
    {
        (void)definition;

        const auto separator = key.rfind("::");
        const std::string simple = separator == std::string::npos
            ? key
            : key.substr(separator + 2);

        if (simple != base)
        {
            continue;
        }

        if (unique.has_value())
        {
            return std::nullopt; // ambiguo entre escopos
        }

        unique = key;
    }

    if (unique.has_value())
    {
        return ResolveAlias(*unique, 0);
    }

    return std::nullopt;
}

std::optional<Type> SemanticAnalyzer::ResolveType(
    std::string_view text,
    const std::string& scope
)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.remove_suffix(1);
    }

    std::size_t stars = 0;
    while (!text.empty() && text.back() == '*')
    {
        text.remove_suffix(1);
        ++stars;
    }

    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.remove_suffix(1);
    }

    if (text.empty())
    {
        return std::nullopt;
    }

    std::optional<Type> base = TypeParser::Parse(text, m_PointerBits);

    if (!base.has_value() || !base->IsValid())
    {
        base = ResolveNamedType(text, scope);
    }

    if (!base.has_value() || !base->IsValid())
    {
        return std::nullopt;
    }

    for (std::size_t i = 0; i < stars; ++i)
    {
        base = Type::MakePointer(*base, m_PointerBits);
    }

    return base;
}

void SemanticAnalyzer::RegisterDeclarations()
{
    // Variáveis globais primeiro: funções podem usá-las em qualquer ordem.
    for (auto& child : m_Program.Children)
    {
        if (!child || child->GetType() != NodeType::Variable) continue;
        AnalyzeVariable(*static_cast<VariableNode*>(child.get()));
    }

    for (auto& child : m_Program.Children)
    {
        if (!child || child->GetType() != NodeType::Function) continue;
        DeclareFunction(*static_cast<FunctionNode*>(child.get()));
    }
}

void SemanticAnalyzer::AnalyzeBodies()
{
    for (auto& child : m_Program.Children)
    {
        if (!child || child->GetType() != NodeType::Function) continue;
        AnalyzeFunctionBody(*static_cast<FunctionNode*>(child.get()));
    }
}

void SemanticAnalyzer::AnalyzeVariable(
    VariableNode& variable
)
{
    std::string name = variable.Name.Text;

    if(m_Semantic.Variables.contains(name))
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Name.Location,
            "Variable '" + name + "' already defined."
        );
        return;
    }

    auto type = ResolveType(variable.Type.Text, ScopePrefixOf(name));

    if(!type.has_value() || !type->IsValid())
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Type.Location,
            "Unknown or invalid type '" + variable.Type.Text + "'."
        );
        variable.EvaluatedType = Type::MakeInvalid();
        m_Semantic.Variables.emplace(name, variable.EvaluatedType);
        return;
    }

    variable.EvaluatedType = *type;
    m_Semantic.Variables.emplace(name, *type);

    if (m_PointerBits == 32 && IsWideInteger(*type))
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Type.Location,
            "Variavel '" + name + "' de tipo '" + type->DisplayName() +
            "' exige x86_64; inteiros com mais de 64 bits nao sao suportados em x86."
        );
    }

    if (variable.Attrs.Has(AttrId::Const))
    {
        m_Semantic.ConstVariables.insert(name);
        ValidateConstInitializer(variable, *type);
    }

    if (variable.Attrs.Has(AttrId::Export))
    {
        std::string symbol = variable.Attrs.Value(AttrId::Export).empty()
            ? RawFunctionName(name)
            : std::string(variable.Attrs.Value(AttrId::Export));

        if (!IsValidSymbolName(symbol))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                variable.Name.Location,
                "Invalid export symbol '" + symbol + "' for variable '" + name + "'."
            );
        }
        else
        {
            RegisterExportSymbol(symbol, "variable '" + name + "'", variable.Name.Location);
        }
    }
}

void SemanticAnalyzer::ValidateConstInitializer(
    VariableNode& variable,
    const Type& type
)
{
    const std::string& name = variable.Name.Text;
    const Node* init = variable.Children.empty() || !variable.Children[0]
        ? nullptr
        : variable.Children[0].get();

    // Sem inicializador a declaracao continua valida: o bloco/escalar fica a
    // zero em .rdata e permanece imutavel.
    if (!init)
    {
        if (!type.IsBuffer() && !type.Representation().IsInteger())
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                variable.Name.Location,
                "Const declarations of type '" + type.DisplayName() +
                "' are not supported yet; use an integer or buffer type."
            );
        }
        return;
    }

    if (type.IsBuffer())
    {
        if (init->GetType() != NodeType::Literal ||
            static_cast<const LiteralNode&>(*init).Value.Type != TokenType::String)
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                init->GetLocation(),
                "Const buffer '" + name +
                "' must be initialized with a string literal."
            );
            return;
        }

        const auto& literal = static_cast<const LiteralNode&>(*init);
        const std::size_t required = literal.Value.Text.size() + 1;
        const std::uint32_t capacity = type.GetByteSize();

        if (required > capacity)
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                init->GetLocation(),
                "String literal of " + std::to_string(required) +
                " bytes does not fit in buffer '" + name + "' of " +
                std::to_string(capacity) + " bytes."
            );
        }
        return;
    }

    if (!type.Representation().IsInteger())
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Name.Location,
            "Const declarations of type '" + type.DisplayName() +
            "' are not supported yet; use an integer or buffer type."
        );
        return;
    }

    if (init->GetType() != NodeType::Literal ||
        static_cast<const LiteralNode&>(*init).Value.Type != TokenType::Number)
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            init->GetLocation(),
            "Const variable '" + name + "' of type '" + type.DisplayName() +
            "' must be initialized with an integer literal."
        );
        return;
    }

    const auto value = LiteralBigValueOf(*init);

    if (!value.has_value() || !FitsLiteral(type, *value))
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            init->GetLocation(),
            "Initializer does not fit in const variable '" + name +
            "' of type '" + type.DisplayName() + "'."
        );
    }
}

void SemanticAnalyzer::RegisterExportSymbol(
    const std::string& symbol,
    const std::string& owner,
    const SourceLocation& location
)
{
    auto [it, inserted] = m_Semantic.ExportedSymbols.emplace(symbol, owner);
    if (!inserted)
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            location,
            "Export symbol '" + symbol + "' is already defined by " + it->second + "."
        );
    }
}

Type SemanticAnalyzer::AnalyzeVariableCall(
    VariableCallNode& variable
)
{
    std::string name = variable.Name.Text;

    auto it = m_Semantic.Variables.find(name);
    if(it == m_Semantic.Variables.end())
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Name.Location,
            "Variable '" + name + "' not defined."
        );
        variable.EvaluatedType = Type::MakeInvalid();
        return variable.EvaluatedType;
    }

    variable.EvaluatedType = DecayBuffer(it->second, m_PointerBits);
    return variable.EvaluatedType;
}

void SemanticAnalyzer::DeclareFunction(
    FunctionNode& function
)
{
    std::string name = function.Name.Text;
    if(m_Semantic.Functions.contains(name))
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            function.Name.Location,
            "Function '" + name + "' already defined."
        );
    }
    else if (m_Semantic.Variables.contains(name))
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            function.Name.Location,
            "'" + name + "' is already declared as a variable."
        );
    }

    auto rtype = ResolveType(function.ReturnType.Text, ScopePrefixOf(name));
    Type funcRetType = Type::MakeVoid();

    if(!rtype.has_value() || !rtype->IsValid())
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            function.ReturnType.Location,
            "Unknown return type '" + function.ReturnType.Text + "' in function '" + name + "'."
        );
    }
    else
    {
        funcRetType = DecayBuffer(*rtype, m_PointerBits);

        if (IsWideInteger(funcRetType))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                function.ReturnType.Location,
                "Funcao '" + name + "' nao pode retornar '" +
                funcRetType.DisplayName() +
                "'; inteiros com mais de 64 bits nao sao suportados em retornos."
            );
        }
        else if (IsDeferredNumeric(funcRetType))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                function.ReturnType.Location,
                "Funcao '" + name + "' nao pode retornar '" +
                funcRetType.DisplayName() +
                "'; float e ponto fixo ainda nao tem codegen em runtime."
            );
        }
    }

    function.EvaluatedType = funcRetType;

    const int abiCount = AttributeAbiCount(function.Attrs);
    if (abiCount > 1)
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            function.Name.Location,
            "Function '" + name + "' has more than one calling convention attribute."
        );
    }

    FunctionSignature signature{
        .ReturnType = funcRetType,
        .ParameterTypes = {},
        .ParameterRestricted = {},
        .CallingConvention = AttributeAbi(function.Attrs),
        .Location = function.Name.Location
    };

    std::unordered_set<std::string_view> params;

    for (auto& param : function.Parameters)
    {
        const std::string& pname = param.Name.Text;

        if(params.contains(pname))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                param.Name.Location,
                "Parameter '" + pname + "' in function '" + name + "' already defined."
            );
            continue;
        }

        params.insert(pname);

        auto ptype = ResolveType(param.Type.Text, ScopePrefixOf(name));
        Type parameterType = Type::MakeInvalid();

        if(!ptype.has_value() || !ptype->IsValid())
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                param.Type.Location,
                "Unknown parameter type '" + param.Type.Text + "' in function '" + name + "'."
            );
        }
        else
        {
            parameterType = DecayBuffer(*ptype, m_PointerBits);

            if (IsWideInteger(parameterType))
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    param.Type.Location,
                    "Parametro '" + pname + "' de '" + name + "' nao pode ser '" +
                    parameterType.DisplayName() +
                    "'; inteiros com mais de 64 bits nao sao suportados em parametros."
                );
            }
            else if (IsDeferredNumeric(parameterType))
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    param.Type.Location,
                    "Parametro '" + pname + "' de '" + name + "' nao pode ser '" +
                    parameterType.DisplayName() +
                    "'; float e ponto fixo ainda nao tem codegen em runtime."
                );
            }
        }

        param.EvaluatedType = parameterType;
        signature.ParameterTypes.push_back(parameterType);

        // [[restrict]] exige o tipo nominal exato; [[unrestrict]] aceita
        // semelhanca estrutural. Por omissao, usings restritos mantem a
        // restricao e os tipos anonimos usam semelhanca.
        bool restricted;
        if (param.Attrs.Has(AttrId::Unrestrict))
        {
            restricted = false;
        }
        else if (param.Attrs.Has(AttrId::Restrict))
        {
            restricted = true;
        }
        else
        {
            restricted = parameterType.IsValid() && parameterType.IsNominal();
        }

        signature.ParameterRestricted.push_back(restricted);
    }

    m_Semantic.Functions.insert_or_assign(name, signature);

    if (function.Attrs.Has(AttrId::Import))
    {
        if (function.Attrs.Has(AttrId::Export))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                function.Name.Location,
                "Function '" + name + "' cannot be both imported and exported."
            );
        }

        ImportInfo info;
        info.Symbol = RawFunctionName(name);
        info.Library = std::string(function.Attrs.Value(AttrId::Import));
        info.CallingConvention = signature.CallingConvention;

        if (!IsValidSymbolName(info.Symbol))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                function.Name.Location,
                "Invalid native symbol name '" + info.Symbol + "'."
            );
        }

        if (!IsValidLibraryName(info.Library))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                function.Name.Location,
                "Invalid library name '" + info.Library + "' in [[import]]."
            );
        }

        std::vector<std::uint32_t> parameterBytes;
        for (const Type& parameter : signature.ParameterTypes)
        {
            parameterBytes.push_back(parameter.GetByteSize());
        }

        info.StackSize = AbiStackBytes(
            info.CallingConvention,
            parameterBytes,
            m_PointerBits == 32 ? Architecture::X86 : Architecture::X86_64
        );

        m_Semantic.Imports.insert_or_assign(name, std::move(info));
    }
    else if (function.Attrs.Has(AttrId::Export))
    {
        std::string symbol = function.Attrs.Value(AttrId::Export).empty()
            ? RawFunctionName(name)
            : std::string(function.Attrs.Value(AttrId::Export));

        if (!IsValidSymbolName(symbol))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                function.Name.Location,
                "Invalid export symbol '" + symbol + "' for function '" + name + "'."
            );
        }
        else
        {
            RegisterExportSymbol(symbol, "function '" + name + "'", function.Name.Location);
        }
    }
}

void SemanticAnalyzer::AnalyzeFunctionBody(
    FunctionNode& function
)
{
    std::string name = function.Name.Text;
    if (!m_Semantic.Functions.contains(name))
    {
        return;
    }

    m_CurrentReturnType = m_Semantic.Functions.at(name).ReturnType;
    m_CurrentFunctionName = name;

    auto scopeEnd = name.rfind("::");
    std::string scopePrefix = (scopeEnd == std::string::npos) ? name : name.substr(0, scopeEnd);
    m_CurrentScopePrefix = scopePrefix;

    std::unordered_set<std::string_view> params;
    std::vector<std::pair<std::string, std::optional<Type>>> parameterVariables;

    for (const auto& param : function.Parameters)
    {
        const std::string& pname = param.Name.Text;

        if (!params.insert(pname).second)
        {
            continue;
        }

        if (!param.EvaluatedType.IsValid())
        {
            continue;
        }

        std::string key = scopePrefix + "::" + pname;
        auto existing = m_Semantic.Variables.find(key);
        parameterVariables.emplace_back(
            key,
            existing != m_Semantic.Variables.end()
                ? std::optional<Type>(existing->second)
                : std::nullopt
        );
        m_Semantic.Variables.insert_or_assign(key, param.EvaluatedType);
    }

    for (auto& child : function.Children)
    {
        if (child) AnalyzeExpression(*child);
    }

    for (auto it = parameterVariables.rbegin(); it != parameterVariables.rend(); ++it)
    {
        if (it->second.has_value())
        {
            m_Semantic.Variables.insert_or_assign(it->first, *it->second);
        }
        else
        {
            m_Semantic.Variables.erase(it->first);
        }
    }

    m_CurrentReturnType.reset();
    m_CurrentFunctionName.clear();
    m_CurrentScopePrefix.clear();
}

Type SemanticAnalyzer::AnalyzeFunctionCall(
    FunctionCallNode& function
)
{
    std::string name = function.Name.Text;

    FunctionCallSignature signature;
    signature.Location = function.GetLocation();

    for (auto& child : function.Children)
    {
        Type type = AnalyzeExpression(*child);
        std::optional<std::int64_t> literalValue;
        if (child)
        {
            literalValue = LiteralValueOf(*child);
        }

        if (!type.IsValid())
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                signature.Location,
                "Invalid type in parameter of function call '"+
                name +
                "'."
            );
            break;
        }

        if (type.IsVoid())
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                signature.Location,
                "Never use void as parameter in function '"+
                name +
                "'."
            );
            continue;
        }

        if (IsWideInteger(type))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                child->GetLocation(),
                "Argumento de '" + name +
                "' excede 64 bits; inteiros largos nao sao suportados como argumentos."
            );
        }
        else if (IsDeferredNumeric(type))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                child->GetLocation(),
                "Argumento de '" + name + "' e '" + type.DisplayName() +
                "'; float e ponto fixo ainda nao tem codegen em runtime."
            );
        }

        signature.ParameterTypes.push_back(type);
        signature.LiteralValues.push_back(literalValue);
    }

    m_Semantic.FunctionCalls[name].emplace_back(std::move(signature));

    auto it = m_Semantic.Functions.find(name);
    if (it != m_Semantic.Functions.end())
    {
        function.EvaluatedType = it->second.ReturnType;
        return function.EvaluatedType;
    }

    function.EvaluatedType = Type::MakeSigned(32);
    return function.EvaluatedType;
}

void SemanticAnalyzer::AnalyzeAssignment(
    AssignmentNode& node
)
{
    std::string name = node.Name.Text;
    Type targetType = Type::MakeInvalid();

    // Nome de um bloco b const escrito diretamente ou via &bloco. A constness
    // de um ponteiro nao se transmite ao pointee: *p continua permitido.
    const auto constBufferName = [this](const Node* expr) -> std::string
    {
        const Node* call = expr;

        if (expr && expr->GetType() == NodeType::Unary)
        {
            const auto& unary = static_cast<const UnaryNode&>(*expr);
            if (unary.Operation != UnaryOperation::AddressOf ||
                unary.Children.empty() || !unary.Children[0])
            {
                return {};
            }
            call = unary.Children[0].get();
        }

        if (!call || call->GetType() != NodeType::VariableCall)
        {
            return {};
        }

        const auto& variable = static_cast<const VariableCallNode&>(*call);

        if (!m_Semantic.ConstVariables.contains(variable.Name.Text))
        {
            return {};
        }

        const auto type = m_Semantic.Variables.find(variable.Name.Text);
        if (type == m_Semantic.Variables.end() || !type->second.IsBuffer())
        {
            return {};
        }

        return variable.Name.Text;
    };

    if (node.IsDereference)
    {
        if (node.Target)
        {
            const std::string constName = constBufferName(node.Target.get());
            if (!constName.empty())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Cannot write through '" + constName +
                    "' because it is declared [[const]]."
                );
            }

            Type pointerType = AnalyzeExpression(*node.Target);

            if (!pointerType.IsPointer())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Dereference assignment requires a pointer, but got '" + pointerType.DisplayName() + "'."
                );
            }
            else
            {
                auto pointee = pointerType.Pointee();
                targetType = pointee ? *pointee : Type::MakeUnsigned(8);
                if (targetType.IsVoid())
                {
                    targetType = Type::MakeUnsigned(8);
                }
            }
        }
    }
    else
    {
        auto it = m_Semantic.Variables.find(name);
        if (it == m_Semantic.Variables.end())
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                node.Name.Location,
                "Variable '" + name + "' not defined."
            );
        }
        else
        {
            targetType = it->second;

            if (m_Semantic.ConstVariables.contains(name))
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.Name.Location,
                    "Cannot assign to immutable variable '" + name +
                    "' declared [[const]]."
                );
            }
        }
    }

    if (node.Children.empty() || !node.Children[0])
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            node.Name.Location,
            "Assignment expression is empty."
        );
        return;
    }

    Type exprType = AnalyzeExpression(*node.Children[0]);

    // Blocos b sao donos: atribuir copia o conteudo, nao aponta o alvo para
    // outra memoria. Aceita um literal de string ou outro bloco que caiba.
    if (!node.IsDereference && targetType.IsValid() && targetType.IsBuffer())
    {
        Node& valueNode = *node.Children[0];
        const std::uint32_t capacity = targetType.GetByteSize();
        bool compatible = false;
        bool diagnosed = false;

        if (valueNode.GetType() == NodeType::Literal)
        {
            auto& literal = static_cast<LiteralNode&>(valueNode);

            if (literal.Value.Type == TokenType::String)
            {
                const std::size_t required = literal.Value.Text.size() + 1;

                if (required > capacity)
                {
                    m_Diagnostics.Add(
                        DiagnosticLevel::Error,
                        valueNode.GetLocation(),
                        "String literal of " + std::to_string(required) +
                        " bytes does not fit in buffer '" + name + "' of " +
                        std::to_string(capacity) + " bytes."
                    );
                    diagnosed = true;
                }
                else
                {
                    compatible = true;
                }
            }
        }
        else if (valueNode.GetType() == NodeType::VariableCall)
        {
            auto& var = static_cast<VariableCallNode&>(valueNode);
            const auto source = m_Semantic.Variables.find(var.Name.Text);

            if (source != m_Semantic.Variables.end() && source->second.IsBuffer())
            {
                const std::uint32_t size = source->second.GetByteSize();

                if (size > capacity)
                {
                    m_Diagnostics.Add(
                        DiagnosticLevel::Error,
                        valueNode.GetLocation(),
                        "Buffer '" + var.Name.Text + "' of " + std::to_string(size) +
                        " bytes does not fit in buffer '" + name + "' of " +
                        std::to_string(capacity) + " bytes."
                    );
                    diagnosed = true;
                }
                else
                {
                    compatible = true;
                }
            }
        }

        if (!compatible && !diagnosed)
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                node.Name.Location,
                "Cannot assign '" + exprType.DisplayName() +
                "' to buffer '" + name + "'; expected a string literal or another buffer."
            );
        }

        node.EvaluatedType = targetType;
        return;
    }

    if (targetType.IsValid() && exprType.IsVoid())
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            node.Name.Location,
            "Cannot assign a void expression to '" + (node.IsDereference ? "*" + name : name) + "'."
        );
    }
    else if (targetType.IsValid() && exprType.IsValid())
    {
        if (IsDeferredNumeric(targetType) || IsDeferredNumeric(exprType))
        {
            const Type& deferred = IsDeferredNumeric(targetType) ? targetType : exprType;

            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                node.Name.Location,
                "Assignment involving '" + deferred.DisplayName() +
                "' is not supported yet; float and fixed-point runtime codegen is future work."
            );
        }
        else if (!targetType.CanAssignFrom(exprType))
        {
            // Literais podem estreitar se o valor couber no alvo (ex: 1 em
            // bool/u1), sem abrir conversoes implicitas para variaveis.
            const auto literal = LiteralValueOf(*node.Children[0]);
            const bool fits = literal.has_value() && FitsLiteral(targetType, *literal);

            if (!fits)
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Warning,
                    node.Name.Location,
                    "Implicit conversion from '" + exprType.DisplayName() + "' to '" + targetType.DisplayName() + "' in assignment to '" + (node.IsDereference ? "*" + name : name) + "'."
                );
            }
        }
    }

    node.EvaluatedType = targetType;
}

Type SemanticAnalyzer::AnalyzeUnary(
    UnaryNode& node
)
{
    if (node.Children.empty() || !node.Children[0])
    {
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    Type operandType = AnalyzeExpression(*node.Children[0]);

    if (!operandType.IsValid())
    {
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    switch (node.Operation)
    {
        case UnaryOperation::Neg:
            if (!operandType.IsNumeric())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Unary operator '-' requires numeric operand, but got '" + operandType.DisplayName() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }

            if (IsDeferredNumeric(operandType))
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Negation of '" + operandType.DisplayName() +
                    "' is not supported yet; float and fixed-point runtime codegen is future work."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }

            // O literal mínimo de '11' e u8; negar na mesma largura daria 245.
            // Constantes ganham o tipo mínimo do valor negado; operandos sem
            // valor conhecido promovem como em C, para um tipo com sinal.
            node.EvaluatedType = operandType;
            if (operandType.IsInteger())
            {
                if (const auto value = EvaluateConstExpr(*node.Children[0]))
                {
                    node.EvaluatedType = MinimalTypeForValue(-*value);
                }
                else if (operandType.IsUnsigned())
                {
                    const std::uint32_t bits = operandType.Bits;
                    node.EvaluatedType = bits < 64
                        ? Type::MakeSigned(bits * 2)
                        : operandType;
                }
            }

            FoldConstant(node);
            return node.EvaluatedType;

        case UnaryOperation::Not:
            if (!operandType.IsInteger())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Unary operator '~' requires integer operand, but got '" + operandType.DisplayName() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }
            node.EvaluatedType = operandType;
            FoldConstant(node);
            return node.EvaluatedType;

        case UnaryOperation::LogicalNot:
            if (!operandType.IsInteger())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Unary operator '!' requires integer operand, but got '" + operandType.DisplayName() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }
            node.EvaluatedType = Type::MakeUnsigned(8);
            FoldConstant(node);
            return node.EvaluatedType;

        case UnaryOperation::AddressOf:
        {
            Node& operand = *node.Children[0];

            if (operand.GetType() != NodeType::VariableCall)
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "The '&' operator requires a variable."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }

            node.EvaluatedType = Type::MakePointer(operandType, m_PointerBits);

            // O endereco de um bloco b e o endereco do primeiro byte (o
            // proprio bloco ja decaiu para u8*), nao um u8**.
            if (operand.GetType() == NodeType::VariableCall)
            {
                const auto& var = static_cast<VariableCallNode&>(operand);
                const auto declared = m_Semantic.Variables.find(var.Name.Text);
                if (declared != m_Semantic.Variables.end() && declared->second.IsBuffer())
                {
                    node.EvaluatedType = operandType;
                }
            }

            return node.EvaluatedType;
        }

        case UnaryOperation::Deref:
        {
            if (!operandType.IsPointer())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "The '*' operator requires a pointer, but got '" + operandType.DisplayName() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }

            auto pointee = operandType.Pointee();

            if (!pointee || pointee->IsVoid())
            {
                node.EvaluatedType = Type::MakeUnsigned(8);
                return node.EvaluatedType;
            }

            node.EvaluatedType = *pointee;
            return node.EvaluatedType;
        }
    }

    node.EvaluatedType = operandType;
    return node.EvaluatedType;
}

Type SemanticAnalyzer::AnalyzeBinary(
    BinaryNode& node
)
{
    if (node.Children.size() < 2 || !node.Children[0] || !node.Children[1])
    {
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    Type leftType = AnalyzeExpression(*node.Children[0]);
    Type rightType = AnalyzeExpression(*node.Children[1]);

    if (!leftType.IsValid() || !rightType.IsValid())
    {
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    switch (node.Operation)
    {
        case BinaryOperation::Add:
        case BinaryOperation::Sub:
            // Aritmética de ponteiro em bytes: p + n / p - n.
            if (leftType.IsPointer() && rightType.IsInteger())
            {
                if (rightType.Bits > 64)
                {
                    m_Diagnostics.Add(
                        DiagnosticLevel::Error,
                        node.GetLocation(),
                        "Pointer offset excede 64 bits; offsets largos nao sao suportados."
                    );
                }
                node.EvaluatedType = leftType;
                return node.EvaluatedType;
            }

            if (leftType.IsInteger() && rightType.IsPointer() &&
                node.Operation == BinaryOperation::Add)
            {
                if (leftType.Bits > 64)
                {
                    m_Diagnostics.Add(
                        DiagnosticLevel::Error,
                        node.GetLocation(),
                        "Pointer offset excede 64 bits; offsets largos nao sao suportados."
                    );
                }
                node.EvaluatedType = rightType;
                return node.EvaluatedType;
            }

            if (!leftType.IsNumeric() || !rightType.IsNumeric())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Arithmetic operator requires numeric operands, but got '" + leftType.DisplayName() + "' and '" + rightType.DisplayName() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }

            if (IsDeferredNumeric(leftType) || IsDeferredNumeric(rightType))
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Arithmetic on '" + leftType.DisplayName() + "' and '" +
                    rightType.DisplayName() +
                    "' is not supported yet; float and fixed-point runtime codegen is future work."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }

            node.EvaluatedType = leftType.GetCommonType(rightType);
            FoldConstant(node);
            return node.EvaluatedType;

        case BinaryOperation::Mul:
        case BinaryOperation::Div:
            if (!leftType.IsNumeric() || !rightType.IsNumeric())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Arithmetic operator requires numeric operands, but got '" + leftType.DisplayName() + "' and '" + rightType.DisplayName() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }

            if (IsDeferredNumeric(leftType) || IsDeferredNumeric(rightType))
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Arithmetic on '" + leftType.DisplayName() + "' and '" +
                    rightType.DisplayName() +
                    "' is not supported yet; float and fixed-point runtime codegen is future work."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }

            node.EvaluatedType = leftType.GetCommonType(rightType);
            FoldConstant(node);
            return node.EvaluatedType;

        case BinaryOperation::Mod:
            if (!leftType.IsInteger() || !rightType.IsInteger())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Modulo operator requires integer operands, but got '" + leftType.DisplayName() + "' and '" + rightType.DisplayName() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }
            node.EvaluatedType = leftType.GetCommonType(rightType);
            FoldConstant(node);
            return node.EvaluatedType;

        case BinaryOperation::And:
        case BinaryOperation::Or:
        case BinaryOperation::Xor:
        case BinaryOperation::Lshift:
        case BinaryOperation::Rshift:
            if (!leftType.IsInteger() || !rightType.IsInteger())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Bitwise operator requires integer operands, but got '" + leftType.DisplayName() + "' and '" + rightType.DisplayName() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }
            node.EvaluatedType = leftType.GetCommonType(rightType);
            FoldConstant(node);
            return node.EvaluatedType;
    }

    node.EvaluatedType = leftType.GetCommonType(rightType);
    FoldConstant(node);
    return node.EvaluatedType;
}

Type SemanticAnalyzer::AnalyzeCast(
    CastNode& node
)
{
    if (node.Children.empty() || !node.Children[0])
    {
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    auto target = ResolveType(node.Type.Text, m_CurrentScopePrefix);

    if (!target.has_value() || !target->IsValid())
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            node.GetLocation(),
            "Unknown or invalid type '" + node.Type.Text + "' in cast."
        );
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    const Type source = AnalyzeExpression(*node.Children[0]);

    if (!source.IsValid())
    {
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    const bool sameBits = target->Bits == source.Bits;
    bool valid = false;

    if (node.Kind == CastKind::Static)
    {
        if (target->IsVoid())
        {
            valid = true;
        }
        else if (target->IsNumeric() && source.IsNumeric())
        {
            valid = true;
        }
        else if (target->IsPointer() && source.IsPointer() && sameBits)
        {
            // Como em C++: static_cast entre ponteiros só é permitido quando
            // um dos lados é void* ou quando o tipo é o mesmo.
            auto targetPointee = target->Pointee();
            auto sourcePointee = source.Pointee();
            valid =
                (targetPointee && targetPointee->IsVoid()) ||
                (sourcePointee && sourcePointee->IsVoid()) ||
                (*target == source);
        }
    }
    else
    {
        // reinterpret_cast: mesma representação, sem conversão de valor.
        const bool targetScalar = target->IsPointer() || target->IsInteger();
        const bool sourceScalar = source.IsPointer() || source.IsInteger();
        valid = targetScalar && sourceScalar && sameBits;
    }

    if (!valid)
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            node.GetLocation(),
            std::string("Invalid ") +
            (node.Kind == CastKind::Static ? "static_cast" : "reinterpret_cast") +
            " from '" + source.DisplayName() + "' to '" + target->ToString() + "'."
        );
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    node.EvaluatedType = *target;

    if (IsDeferredNumeric(*target) || IsDeferredNumeric(source))
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            node.GetLocation(),
            "Cast between '" + source.DisplayName() + "' and '" +
            target->DisplayName() +
            "' is not supported yet; float and fixed-point runtime codegen is future work."
        );
        return node.EvaluatedType;
    }

    if (m_PointerBits == 32 && IsWideInteger(*target))
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            node.GetLocation(),
            "Cast para '" + target->DisplayName() +
            "' em runtime: inteiros com mais de 64 bits exigem x86_64."
        );
        return node.EvaluatedType;
    }

    if (node.Kind == CastKind::Static &&
        target->Representation().IsInteger())
    {
        if (auto value = EvaluateConstExpr(*node.Children[0]))
        {
            const Type repr = target->Representation();
            node.FoldedValue = value->WrappedTo(repr.Bits, repr.IsSigned());
            return node.EvaluatedType;
        }
    }

    return node.EvaluatedType;
}

Type SemanticAnalyzer::AnalyzeExpression(
    Node& node
)
{
    const NodeType type = node.GetType();

    switch (type)
    {
        case NodeType::Literal:
        {
            LiteralNode& lit = static_cast<LiteralNode&>(node);

            if (lit.Value.Type == TokenType::String)
            {
                lit.EvaluatedType = Type::MakePointer(
                    Type::MakeUnsigned(8),
                    m_PointerBits
                );
                return lit.EvaluatedType;
            }

            const auto value = ParseNumberLiteral(lit.Value);

            if (!value.has_value())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    lit.GetLocation(),
                    "Invalid number '" + lit.Value.Text + "'."
                );
                lit.EvaluatedType = Type::MakeInvalid();
                return lit.EvaluatedType;
            }

            lit.FoldedValue = *value;
            lit.EvaluatedType = MinimalTypeForValue(*value);
            return lit.EvaluatedType;
        }

        case NodeType::VariableCall:
        {
            VariableCallNode& varCall = static_cast<VariableCallNode&>(node);
            return AnalyzeVariableCall(varCall);
        }

        case NodeType::FunctionCall:
        {
            FunctionCallNode& funcCall = static_cast<FunctionCallNode&>(node);
            return AnalyzeFunctionCall(funcCall);
        }

        case NodeType::Assign:
        {
            AssignmentNode& assign = static_cast<AssignmentNode&>(node);
            AnalyzeAssignment(assign);
            return assign.EvaluatedType;
        }

        case NodeType::Unary:
        {
            UnaryNode& unary = static_cast<UnaryNode&>(node);
            return AnalyzeUnary(unary);
        }

        case NodeType::Binary:
        {
            BinaryNode& binary = static_cast<BinaryNode&>(node);
            return AnalyzeBinary(binary);
        }

        case NodeType::Cast:
        {
            CastNode& cast = static_cast<CastNode&>(node);
            return AnalyzeCast(cast);
        }

        case NodeType::Return:
        {
            ReturnNode& ret = static_cast<ReturnNode&>(node);
            const bool hasValue = !ret.Children.empty() && ret.Children[0] != nullptr;
            Type valueType = hasValue
                ? AnalyzeExpression(*ret.Children[0])
                : Type::MakeVoid();

            if (!m_CurrentReturnType.has_value())
            {
                return Type::MakeVoid();
            }

            const Type& returnType = *m_CurrentReturnType;
            const std::string& functionName = m_CurrentFunctionName;

            if (returnType.IsVoid())
            {
                if (hasValue)
                {
                    m_Diagnostics.Add(
                        DiagnosticLevel::Error,
                        ret.GetLocation(),
                        "Void function '" + functionName + "' cannot return a value."
                    );
                }
            }
            else if (!hasValue || valueType.IsVoid())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    ret.GetLocation(),
                    "Function '" + functionName + "' must return a value of type '" + returnType.DisplayName() + "'."
                );
            }
            else if (IsWideInteger(valueType))
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    ret.GetLocation(),
                    "Valor de retorno excede 64 bits; retornos largos nao sao suportados."
                );
            }
            else if (valueType.IsValid() && !returnType.CanAssignFrom(valueType))
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Warning,
                    ret.GetLocation(),
                    "Implicit conversion from '" + valueType.DisplayName() + "' to '" + returnType.DisplayName() + "' in return of '" + functionName + "'."
                );
            }

            return Type::MakeVoid();
        }

        default:
            return Type::MakeVoid();
    }
}

void ValidateSemantic(
    const SemanticContext& semantic,
    Diagnostics& diagnostics
)
{
    for (const auto& [name, signatures] : semantic.FunctionCalls)
    {
        auto it = semantic.Functions.find(name);

        if (it == semantic.Functions.end())
        {
            for (const FunctionCallSignature& signature : signatures)
            {
                diagnostics.Add(
                    DiagnosticLevel::Error,
                    signature.Location,
                    "Function '" + name + "' not defined."
                );
            }
            continue;
        }

        const FunctionSignature& functionSignature = it->second;
        std::size_t numberOfParams = functionSignature.ParameterTypes.size();

        for (const FunctionCallSignature& signature : signatures)
        {
            std::size_t numberOfParamsReceived = signature.ParameterTypes.size();

            if (numberOfParams != numberOfParamsReceived)
            {
                diagnostics.Add(
                    DiagnosticLevel::Error,
                    signature.Location,
                    "Function '" +
                    name + "' expects " + std::to_string(numberOfParams) +
                    ", but received " + std::to_string(numberOfParamsReceived)
                );
                break;
            }

            for (size_t i = 0; i < numberOfParams; ++i)
            {
                const Type& left = signature.ParameterTypes[i];
                const Type& right = functionSignature.ParameterTypes[i];

                if (!left.IsValid() || !right.IsValid()) continue;

                const bool restricted =
                    i < functionSignature.ParameterRestricted.size() &&
                    functionSignature.ParameterRestricted[i];

                bool compatible = restricted
                    ? right.CanAssignFrom(left)
                    : right.Representation().CanAssignFrom(left.Representation());

                if (!compatible &&
                    i < signature.LiteralValues.size() &&
                    signature.LiteralValues[i].has_value() &&
                    FitsLiteral(right, *signature.LiteralValues[i]))
                {
                    compatible = true;
                }

                if (!compatible)
                {
                    diagnostics.Add(
                        DiagnosticLevel::Error,
                        signature.Location,
                        "Impossible conversion of " +
                        left.DisplayName() + " to " +
                        right.DisplayName() +
                        " in function call '"+
                        name +
                        "'."
                    );
                    break;
                }
            }
        }
    }

    auto it = semantic.Scopes.find("main");
    if (it == semantic.Scopes.end())
    {
        diagnostics.Add(
            DiagnosticLevel::Fatal,
            {},
            "Section 'Main' not found! This is the entry point."
        );
    }

    if (!semantic.Functions.contains("main::Start") && !semantic.Functions.contains("main::_Start"))
    {
        diagnostics.Add(
            DiagnosticLevel::Fatal,
            {},
            "Function 'Main::Start' or 'Main::_Start' not found! This is the entry point."
        );
    }

    for (const char* entry : { "main::Start", "main::_Start" })
    {
        if (semantic.Imports.contains(entry))
        {
            diagnostics.Add(
                DiagnosticLevel::Fatal,
                {},
                "Entry point function '" + std::string(entry) + "' cannot be imported."
            );
        }
    }
}

}
