#pragma once

#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Core/Abi.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Emux::Exir
{

// Opcodes canonicos e ortogonais da Exir.
enum class OpCode : std::uint8_t
{
    // Memoria e Ponteiros
    Addr,        // %dst = addr ptr, @simbolo_global ou %nome_local
    Load,        // %dst = load [volatile] <T>, %ptr
    Store,       // store [volatile] <T> %val, %ptr
    Gep,         // %dst = gep <T_base> %ptr, %offset_ou_indice
    Memcpy,      // memcpy %dst, %src, %bytes
    Memset,      // memset %dst, u8 %val, %bytes
    Memcmp,      // %dst = memcmp %ptr1, %ptr2, %bytes -> i32

    // Aritmetica e Logica Binaria (ambos operandos do mesmo tipo T)
    Add, Sub, Mul,
    UDiv, SDiv, URem, SRem,
    And, Or, Xor,
    Shl, LShr, AShr,

    // Unarios
    Neg, Not,

    // Comparacoes Relacionais (ambos T, devolvem u1 / u8)
    CmpEq, CmpNe,
    CmpUlt, CmpSlt,
    CmpUle, CmpSle,
    CmpUgt, CmpSgt,
    CmpUge, CmpSge,

    // Casts Estritos
    Trunc, ZExt, SExt, BitCast,
    PtrToInt, IntToPtr,
    // Conversao numerica: muda classe (inteiro, float, fixo) ou escala, pelo
    // que NAO preserva o padrao de bits. BitCast e so reinterpretacao.
    Conv,

    // Tuplas e Agregados (Multiplos Retornos)
    Extract, Insert,

    // Operacoes Atomicas
    AtomicLoad, AtomicStore, AtomicCas, Fence,

    // Controle de Fluxo / Terminadores de Bloco Basico
    Br,          // Salto incondicional: br label %target
    CBr,         // Salto condicional: cbr u1 %cond, label %true, label %false
    Switch,      // Despacho O(1): switch <T> %val, default %bb [ val: %bb, ... ]
    Select,      // Condicional funcional (CMOV): select u1 %c, <T> %t, <T> %f
    Ret,         // Retorno de funcao: ret <T> %val ou ret void
    Unreachable, // Ponto morto

    // Chamadas e Sistema
    Call,
    CallPtr,
    Asm,
    Trap
};

enum class ValueKind : std::uint8_t
{
    None,
    Temp,        // Temporario virtual numérico (%t0, %t1...)
    Constant,    // Literal inteiro/imediato (0, 42, -1)
    GlobalRef,   // Referencia a uma global (@nome)
    LocalSlot,   // Slot virtual de pilha de uma variavel 'local'
    BlockRef     // Referencia a um BasicBlock alvo de branch (label)
};

// Um operando de instrucao compacto (sem alocacao de strings na heap).
struct Value
{
    ValueKind Kind = ValueKind::None;
    Type Ty;

    union
    {
        std::uint32_t Id = 0;   // Temp ID, Local Slot ID, Global ID ou Block ID
        std::uint64_t Imm;      // Imediato sem sinal
        std::int64_t SImm;      // Imediato com sinal
    };

    static Value MakeNone() { return Value{}; }

    static Value MakeTemp(std::uint32_t id, Type ty)
    {
        Value v;
        v.Kind = ValueKind::Temp;
        v.Ty = ty;
        v.Id = id;
        return v;
    }

    static Value MakeConstant(std::uint64_t val, Type ty)
    {
        Value v;
        v.Kind = ValueKind::Constant;
        v.Ty = ty;
        v.Imm = val;
        return v;
    }

    static Value MakeLocal(std::uint32_t slotId, Type ty)
    {
        Value v;
        v.Kind = ValueKind::LocalSlot;
        v.Ty = ty;
        v.Id = slotId;
        return v;
    }

    static Value MakeGlobal(std::uint32_t globalId, Type ty)
    {
        Value v;
        v.Kind = ValueKind::GlobalRef;
        v.Ty = ty;
        v.Id = globalId;
        return v;
    }

    static Value MakeBlock(std::uint32_t blockId)
    {
        Value v;
        v.Kind = ValueKind::BlockRef;
        v.Id = blockId;
        return v;
    }
};

// Dados extras sob demanda para instrucoes de tamanho variavel (calls, switches, asm)
struct InstructionExtra
{
    bool IsVolatile = false;
    Abi CallConv = Abi::Cdecl;
    std::string Text;             // Simbolo de call, texto de inline asm ou label
    std::vector<Value> Arguments; // Argumentos excedentes de chamadas ou casos de switch
};

// Uma instrucao Three-Address Code (TAC) canonica.
struct Instruction
{
    OpCode Op = OpCode::Trap;
    Type ResultTy;
    std::uint32_t DstId = 0;      // ID do temporario resultado quando produz valor

    // Operandos fixos (cobrem mais de 90% das instrucoes de computacao e terminadores)
    Value Operands[3];

    // Metadados opcionais para chamadas, inline asm e dados adicionais
    std::unique_ptr<InstructionExtra> Extra;

    // Rastreamento de debug
    std::uint32_t SourceLine = 0;
    std::uint32_t SourceCol = 0;

    bool IsTerminator() const
    {
        return Op == OpCode::Br || Op == OpCode::CBr ||
               Op == OpCode::Switch || Op == OpCode::Ret ||
               Op == OpCode::Unreachable;
    }
};

struct BasicBlock
{
    std::uint32_t Id = 0;
    std::string Name;             // Nome amigavel de debug (ex: "entry", "loop.cond")

    std::vector<Instruction> Instructions; // Instrucoes lineares
    Instruction Terminator;               // Ultima instrucao obrigatoria (br, cbr, ret, etc.)

    // Arestas do Grafo de Controle de Fluxo (CFG)
    std::vector<BasicBlock*> Predecessors;
    std::vector<BasicBlock*> Successors;
};

struct LocalSlot
{
    std::uint32_t Id = 0;
    std::string Name;             // Nome original no codigo Emux para depuracao
    Type Ty;
    std::size_t Alignment = 0;
};

struct Parameter
{
    std::string Name;
    Type Ty;
};

struct Function
{
    std::string Name;
    Type ReturnType;
    Abi Convention = Abi::Cdecl;

    std::vector<Parameter> Parameters;
    std::vector<LocalSlot> Locals;
    std::vector<std::unique_ptr<BasicBlock>> Blocks;

    BasicBlock* GetEntryBlock() const
    {
        return Blocks.empty() ? nullptr : Blocks[0].get();
    }
};

struct Import
{
    std::string Name;             // Nome IR
    std::string Symbol;           // Simbolo nativo externo
    Abi Convention = Abi::Cdecl;
};

struct Global
{
    std::uint32_t Id = 0;
    std::string Name;
    Type Ty;
    bool IsConst = false;
    std::vector<std::uint8_t> InitialData; // Bytes literais (.data / .rdata)
};

struct Export
{
    std::string Name;
    std::string Alias;
};

struct Module
{
    std::string Name;
    std::uint32_t TargetPointerBits = 64;

    std::vector<Import> Imports;
    std::vector<Global> Globals;
    // Globais definidos noutro modulo do projecto: so nome e tipo, porque a
    // reserva de memoria pertence ao ficheiro que os declara.
    std::vector<Global> ExternalGlobals;
    std::vector<Export> Exports;
    std::vector<std::unique_ptr<Function>> Functions;
};

} // namespace Emux::Exir
