#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <algorithm>

namespace Emux
{

enum class TypeKind
{
    Invalid,
    Void,
    Unsigned,
    Signed,
    Float,
    // Ponto fixo q<bits>.<scale>: inteiro escalado por 2^scale.
    Fixed,
    Buffer,
    Pointer,
    // Nome nominal (using): a representacao e a do subtipo, mas o tipo e
    // distinto de qualquer outro com a mesma forma. E a base da restricao.
    Nominal
};

struct Type
{
    TypeKind Kind = TypeKind::Invalid;
    std::uint32_t Bits = 0;
    // Bits fracionarios de um tipo de ponto fixo (0 nos restantes).
    std::uint32_t Scale = 0;
    // Subtipo de um ponteiro (pointee) ou tipo base de um using.
    std::shared_ptr<Type> SubType = nullptr;
    // Nome canonico do using (ex: "main::byte"); vazio em tipos anonimos.
    std::string Name;

    Type() = default;

    Type(TypeKind kind, std::uint32_t bits = 0)
        : Kind(kind), Bits(bits)
    {
    }

    Type(TypeKind kind, std::uint32_t bits, const Type& subType)
        : Kind(kind), Bits(bits), SubType(std::make_shared<Type>(subType))
    {
    }

    // Fábricas estáticas
    static Type MakeVoid()
    {
        return Type(TypeKind::Void, 0);
    }

    static Type MakeUnsigned(std::uint32_t bits)
    {
        return Type(TypeKind::Unsigned, bits);
    }

    static Type MakeSigned(std::uint32_t bits)
    {
        return Type(TypeKind::Signed, bits);
    }

    static Type MakeFloat(std::uint32_t bits = 32)
    {
        return Type(TypeKind::Float, bits);
    }

    static Type MakeFixed(std::uint32_t bits, std::uint32_t scale)
    {
        Type type(TypeKind::Fixed, bits);
        type.Scale = scale;
        return type;
    }

    static Type MakeBuffer(std::uint32_t bits)
    {
        return Type(TypeKind::Buffer, bits);
    }

    static Type MakePointer(const Type& pointee, std::uint32_t ptrBits = 64)
    {
        return Type(TypeKind::Pointer, ptrBits, pointee);
    }

    static Type MakeNominal(std::string name, const Type& underlying)
    {
        Type type(TypeKind::Nominal, underlying.Bits, underlying);
        type.Name = std::move(name);
        return type;
    }

    static Type MakeInvalid()
    {
        return Type(TypeKind::Invalid, 0);
    }

    // Forma estrutural do tipo, sem identidade nominal. A Exir e as regras de
    // semelhanca usam esta representacao; a restricao vive apenas no Name.
    Type Representation() const
    {
        if (!IsValid())
        {
            return MakeInvalid();
        }

        if (Kind == TypeKind::Nominal)
        {
            return SubType ? SubType->Representation() : MakeInvalid();
        }

        if (Kind == TypeKind::Pointer && SubType)
        {
            return MakePointer(SubType->Representation(), Bits);
        }

        return *this;
    }

    // Tipo imediato de um using (nullptr se nao for nominal).
    std::shared_ptr<Type> Underlying() const
    {
        return Kind == TypeKind::Nominal ? SubType : nullptr;
    }

    // Pointee de um ponteiro, atravessando o using que o embrulha.
    std::shared_ptr<Type> Pointee() const
    {
        const Type* type = this;
        while (type->Kind == TypeKind::Nominal && type->SubType)
        {
            type = type->SubType.get();
        }

        return type->Kind == TypeKind::Pointer ? type->SubType : nullptr;
    }

    // Consultas de propriedades do tipo
    bool IsValid() const { return Kind != TypeKind::Invalid; }
    bool IsNominal() const { return Kind == TypeKind::Nominal; }
    bool IsVoid() const { return Representation().Kind == TypeKind::Void; }
    bool IsUnsigned() const { return Representation().Kind == TypeKind::Unsigned; }
    bool IsSigned() const { return Representation().Kind == TypeKind::Signed; }
    bool IsInteger() const
    {
        const TypeKind kind = Representation().Kind;
        return kind == TypeKind::Signed || kind == TypeKind::Unsigned;
    }
    bool IsFloat() const { return Representation().Kind == TypeKind::Float; }
    bool IsFixed() const { return Representation().Kind == TypeKind::Fixed; }
    bool IsBuffer() const { return Representation().Kind == TypeKind::Buffer; }
    bool IsPointer() const { return Representation().Kind == TypeKind::Pointer; }
    bool IsNumeric() const { return IsInteger() || IsFloat() || IsFixed(); }

    std::uint32_t GetByteSize() const
    {
        return (Bits + 7) / 8;
    }

    // --- Formato de um float fN -------------------------------------------
    // sinal de 1 bit, E bits de expoente e P bits de mantissa (P inclui o bit
    // implicito, logo P = Bits - E). f16, f32 e f64 seguem a IEEE 754; as
    // restantes larguras usam o menor expoente que evita estouro de indice
    // nos tamanhos grandes e e limitado a 32 bits.
    static std::uint32_t CeilLog2(std::uint32_t value)
    {
        std::uint32_t result = 0;
        std::uint32_t power = 1;

        while (power < value)
        {
            power <<= 1;
            ++result;
        }

        return result;
    }

    std::uint32_t FloatExponentBits() const
    {
        if (Bits <= 16) return 5;
        if (Bits <= 32) return 8;

        const std::uint32_t exponent = 4 * CeilLog2(Bits) - 13;
        return std::min<std::uint32_t>(exponent, 32);
    }

    // Bits de mantissa armazenados (sem o bit implicito).
    std::uint32_t FloatMantissaBits() const
    {
        const std::uint32_t exponent = FloatExponentBits();
        return Bits > exponent + 1 ? Bits - exponent - 1 : 0;
    }

    // Bits significativos totais (com o bit implicito).
    std::uint32_t FloatSignificandBits() const
    {
        const std::uint32_t exponent = FloatExponentBits();
        return Bits > exponent ? Bits - exponent : 0;
    }

    std::uint32_t FloatBias() const
    {
        const std::uint32_t exponent = FloatExponentBits();
        return (std::uint32_t(1) << (exponent - 1)) - 1;
    }

    // Texto para a Exir: sempre a representacao estrutural.
    std::string ToString() const
    {
        switch (Kind)
        {
            case TypeKind::Void:
                return "void";
            case TypeKind::Unsigned:
                return "u" + std::to_string(Bits);
            case TypeKind::Signed:
                return "i" + std::to_string(Bits);
            case TypeKind::Float:
                return "f" + std::to_string(Bits);
            case TypeKind::Fixed:
                return "q" + std::to_string(Bits) + "." + std::to_string(Scale);
            case TypeKind::Buffer:
                return "b" + std::to_string(Bits);
            case TypeKind::Pointer:
                return (SubType ? SubType->ToString() : "void") + "*";
            case TypeKind::Nominal:
                return SubType ? SubType->ToString() : "invalid";
            default:
                return "invalid";
        }
    }

    // Texto para diagnosticos: mostra o nome do using quando existe.
    std::string DisplayName() const
    {
        if (Kind == TypeKind::Nominal)
        {
            const auto separator = Name.rfind("::");
            return separator == std::string::npos ? Name : Name.substr(separator + 2);
        }

        if (Kind == TypeKind::Pointer)
        {
            return (SubType ? SubType->DisplayName() : std::string("void")) + "*";
        }

        return ToString();
    }

    bool operator==(const Type& other) const
    {
        // Usings restritos so sao iguais entre si quando partilham o nome.
        if (Kind == TypeKind::Nominal || other.Kind == TypeKind::Nominal)
        {
            return Kind == other.Kind && Name == other.Name;
        }

        if (Kind != other.Kind || Bits != other.Bits || Scale != other.Scale) return false;
        if (Kind == TypeKind::Pointer)
        {
            if (SubType && other.SubType) return *SubType == *other.SubType;
            return SubType == other.SubType;
        }
        return true;
    }

    bool operator!=(const Type& other) const
    {
        return !(*this == other);
    }

    // Verifica compatibilidade de atribuição
    bool CanAssignFrom(const Type& from) const
    {
        if (!IsValid() || !from.IsValid()) return false;
        if (*this == from) return true;

        // Restricao: um using restrito converte diretamente com o seu tipo
        // base e com usings encadeados, mas nao com outro using distinto.
        if (Kind == TypeKind::Nominal)
        {
            if (!SubType) return false;

            if (from.Kind == TypeKind::Nominal)
            {
                return *SubType == from ||
                    (from.SubType && *from.SubType == *this);
            }

            return SubType->CanAssignFrom(from);
        }

        if (from.Kind == TypeKind::Nominal)
        {
            return from.SubType ? CanAssignFrom(*from.SubType) : false;
        }

        // Um bloco b converte para um ponteiro T* quando a regiao tem pelo
        // menos sizeof(T) bytes; o ponteiro aponta para o primeiro byte.
        if (IsPointer() && from.IsBuffer())
        {
            const std::shared_ptr<Type> pointee = Pointee();
            const std::uint32_t required =
                (pointee && !pointee->IsVoid()) ? pointee->GetByteSize() : 1;
            return from.GetByteSize() >= required;
        }

        // Bloco para bloco: a copia exige que a origem caiba no destino.
        if (IsBuffer() && from.IsBuffer())
        {
            return Bits >= from.Bits;
        }

        // Ponteiros: mesmo ponteiro, void* com qualquer ponteiro, ou um
        // inteiro do mesmo tamanho (endereço/nulo).
        if (IsPointer() && from.IsPointer())
        {
            if (Bits != from.Bits) return false;
            if (SubType && SubType->IsVoid()) return true;
            if (from.SubType && from.SubType->IsVoid()) return true;
            return false;
        }

        if (IsPointer() && from.IsInteger())
        {
            return Bits >= from.Bits;
        }

        if (IsInteger() && from.IsPointer())
        {
            return Bits >= from.Bits;
        }

        // Inteiros podem ser atribuídos entre si se o destino tiver capacidade suficiente
        if (IsInteger() && from.IsInteger())
        {
            return Bits >= from.Bits;
        }

        // Floats entre si
        if (IsFloat() && from.IsFloat())
        {
            return Bits >= from.Bits;
        }

        // Implicitamente, o float tem de guardar sem arredondamento todos os
        // bits significativos da origem; caso contrario exige static_cast.
        if (IsFloat() && (from.IsInteger() || from.IsFixed()))
        {
            return FloatSignificandBits() >= from.Bits;
        }

        // Rescalar um ponto fixo altera o valor: exige static_cast.
        if (IsFixed() && from.IsFixed())
        {
            return Scale == from.Scale && Bits >= from.Bits;
        }

        // Um inteiro cabe num ponto fixo se houver espaco para a parte
        // inteira alem da parte fracionaria.
        if (IsFixed() && from.IsInteger())
        {
            return std::uint64_t(Bits) >= std::uint64_t(from.Bits) + Scale;
        }

        return false;
    }

    // Calcula o tipo comum entre dois operandos para expressões binárias
    Type GetCommonType(const Type& other) const
    {
        if (!IsValid() || !other.IsValid()) return MakeInvalid();
        if (*this == other) return *this;

        // Usings distintos nao se misturam implicitamente: a aritmetica
        // acontece na representacao estrutural.
        const Type left = Representation();
        const Type right = other.Representation();

        if (!left.IsValid() || !right.IsValid()) return MakeInvalid();
        if (left == right) return left;

        // Com um float envolvido o resultado e float com largura que cobre os
        // dois operandos (o float precisa de ser multiplo de 8 e >= 16).
        if (left.IsFloat() || right.IsFloat())
        {
            std::uint32_t bits = std::max(left.Bits, right.Bits);
            bits = std::max<std::uint32_t>(bits, 16);
            bits = (bits + 7) / 8 * 8;
            return MakeFloat(bits);
        }

        // Ponto fixo exige a mesma escala nos dois lados: somar valores com
        // escalas diferentes seria um erro silencioso.
        if (left.IsFixed() || right.IsFixed())
        {
            const Type& fixedType = left.IsFixed() ? left : right;
            const Type& otherType = left.IsFixed() ? right : left;

            if (otherType.IsFixed())
            {
                if (fixedType.Scale != otherType.Scale) return MakeInvalid();
                return MakeFixed(std::max(fixedType.Bits, otherType.Bits), fixedType.Scale);
            }

            const std::uint64_t bits = std::max<std::uint64_t>(
                fixedType.Bits,
                std::uint64_t(otherType.Bits) + fixedType.Scale
            );

            return MakeFixed(
                static_cast<std::uint32_t>(std::min<std::uint64_t>(bits, 65536)),
                fixedType.Scale
            );
        }

        if (left.IsInteger() && right.IsInteger())
        {
            std::uint32_t commonBits = std::max(left.Bits, right.Bits);
            // Se um deles for signed, o resultado promove para signed
            if (left.IsSigned() || right.IsSigned())
            {
                return MakeSigned(commonBits);
            }
            return MakeUnsigned(commonBits);
        }

        return left;
    }
};

}
