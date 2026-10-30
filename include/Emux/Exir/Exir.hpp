#pragma once

#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Core/Abi.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Emux::Exir
{

// Operações aritméticas/lógicas da Exir. São independentes de arquitetura:
// o gerador escolhe registradores, pares de 32 bits, instruções nativas, etc.
enum class BinaryOp
{
    Add,
    Sub,
    Mul,
    Div,
    Rem,
    And,
    Or,
    Xor,
    Shl,
    Shr,
    Sar
};

enum class UnaryOp
{
    Neg,
    Not,
    LogicalNot
};

struct CallArg
{
    Type Ty;
    std::string Value;
};

// Uma instrução da Exir v2. Os valores são nomes de temporários (%tN),
// parâmetros, globais ou literais. Não há registradores físicos na IR.
struct Instruction
{
    enum class Kind
    {
        Const,
        Cast,
        Addr,
        Load,
        Store,
        StoreVar,
        Binary,
        Unary,
        Call,
        Return,
        Asm
    };

    Kind Op = Kind::Const;
    std::string Dst;     // temporário de destino (sem '%')
    Type Ty;             // tipo do resultado/operação
    Type SourceTy;       // tipo de origem em cast
    BinaryOp Binary = BinaryOp::Add;
    UnaryOp Unary = UnaryOp::Neg;
    std::string A;       // primeiro operando / ponteiro / nome
    std::string B;       // segundo operando / valor
    std::string Literal; // literal de const ou texto de asm
    std::string Target;  // alvo de chamada (nome IR)
    Abi Convention = Abi::Cdecl;
    Type ReturnTy;
    std::vector<CallArg> Args;
};

struct Parameter
{
    std::string Name;
    Type Ty;
};

struct Function
{
    std::string Name;
    Type Ret;
    Abi Convention = Abi::Cdecl;
    std::size_t StackBytes = 0;
    std::vector<Parameter> Params;
    std::vector<Instruction> Body;
};

struct Import
{
    std::string Name;   // nome IR (ex: main_Sleep)
    std::string Symbol; // símbolo nativo (ex: Sleep)
    Abi Convention = Abi::Cdecl;
    std::size_t StackBytes = 0;
};

struct Global
{
    std::string Name;
    Type Ty;
};

struct DataBlock
{
    std::string Label;
    std::vector<std::string> Bytes;
};

struct Export
{
    std::string Name;
    std::string Alias;
};

struct Module
{
    std::string Name;
    std::vector<Import> Imports;
    std::vector<Global> Globals;
    std::vector<Global> Externals;
    std::vector<Export> Exports;
    std::vector<DataBlock> Data;
    std::vector<Function> Functions;
};

// Converte o texto da Exir num módulo lógico. Lança std::runtime_error com a
// linha e o motivo quando a sintaxe é inválida.
Module Parse(std::string_view text, std::uint32_t pointerBits);

} // namespace Emux::Exir
