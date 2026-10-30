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
    Buffer,
    Pointer
};

struct Type
{
    TypeKind Kind = TypeKind::Invalid;
    std::uint32_t Bits = 0;
    std::shared_ptr<Type> SubType = nullptr; // Usado para ponteiros ou arrays no futuro

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

    static Type MakeBuffer(std::uint32_t bits)
    {
        return Type(TypeKind::Buffer, bits);
    }

    static Type MakePointer(const Type& pointee, std::uint32_t ptrBits = 64)
    {
        return Type(TypeKind::Pointer, ptrBits, pointee);
    }

    static Type MakeInvalid()
    {
        return Type(TypeKind::Invalid, 0);
    }

    // Consultas de propriedades do tipo
    bool IsValid() const { return Kind != TypeKind::Invalid; }
    bool IsVoid() const { return Kind == TypeKind::Void; }
    bool IsUnsigned() const { return Kind == TypeKind::Unsigned; }
    bool IsSigned() const { return Kind == TypeKind::Signed; }
    bool IsInteger() const { return Kind == TypeKind::Signed || Kind == TypeKind::Unsigned; }
    bool IsFloat() const { return Kind == TypeKind::Float; }
    bool IsBuffer() const { return Kind == TypeKind::Buffer; }
    bool IsPointer() const { return Kind == TypeKind::Pointer; }
    bool IsNumeric() const { return IsInteger() || IsFloat(); }

    std::uint32_t GetByteSize() const
    {
        return (Bits + 7) / 8;
    }

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
            case TypeKind::Buffer:
                return "b" + std::to_string(Bits);
            case TypeKind::Pointer:
                return (SubType ? SubType->ToString() : "void") + "*";
            default:
                return "invalid";
        }
    }

    bool operator==(const Type& other) const
    {
        if (Kind != other.Kind || Bits != other.Bits) return false;
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

        return false;
    }

    // Calcula o tipo comum entre dois operandos para expressões binárias
    Type GetCommonType(const Type& other) const
    {
        if (!IsValid() || !other.IsValid()) return MakeInvalid();
        if (*this == other) return *this;

        if (IsFloat() || other.IsFloat())
        {
            return MakeFloat(std::max(Bits, other.Bits));
        }

        if (IsInteger() && other.IsInteger())
        {
            std::uint32_t commonBits = std::max(Bits, other.Bits);
            // Se um deles for signed, o resultado promove para signed
            if (IsSigned() || other.IsSigned())
            {
                return MakeSigned(commonBits);
            }
            return MakeUnsigned(commonBits);
        }

        return *this;
    }
};

}
