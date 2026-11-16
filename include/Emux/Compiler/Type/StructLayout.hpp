#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Emux/Compiler/Type/Type.hpp>

namespace Emux
{

// Um campo de uma struct: nome, tipo resolvido e offset em bytes dentro da
// regiao. O tamanho do campo vem de Ty.GetByteSize().
struct StructField
{
    std::string Name;
    Type Ty;
    std::uint32_t OffsetBytes = 0;
};

// Layout calculado de uma struct, registado pela chave canonica em minusculas
// ('main::teste'). O tipo da struct e Nominal(Buffer(SizeBytes*8)); este registo
// guarda os campos e o alinhamento, que o tipo Buffer nao expressa.
struct StructInfo
{
    std::string Name;
    std::vector<StructField> Fields;
    std::uint32_t SizeBytes = 0;
    std::uint32_t Align = 1;
    bool Packed = false;
    // Verdadeiro depois de ComputeStructLayout preencher campos/tamanho.
    bool LaidOut = false;
    // [[unrestrict]]: a compatibilidade passa a ser estrutural (mesmos campos,
    // offsets e tipos), aceitando literais agregados anonimos.
    bool Unrestricted = false;
    // 'local struct': privada ao modulo, nao propagada entre modulos.
    bool IsExported = true;
};

}