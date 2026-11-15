# Tipos, Blocos e Memória em Emux

Este documento detalha o sistema de tipos fundamentais, blocos estáticos de memória (`bN`/`BN`), ponteiros e manipulação de memória no Emux.

---

## 1. Tipos Primitivos

### Inteiros
- **Larguras padrão**: `u8`, `u16`, `u32`, `u64`, `usize` (unsigned) e `i8`, `i16`, `i32`, `i64`, `isize` (signed).
- **Sub-byte e larguras arbitrárias**: O Emux permite definir larguras arbitrárias de inteiros em tempo de compilação de `1` bit até `UINT32_MAX` bits (ex.: `u1`, `u7`, `u24`, `u128`, `i256`, `u1000000`).
- **Ponto Fixo e Floats**: Tipos `q<bits>.<scale>` (ex: `q16.16`) e floats `f16` a `fN` (múltiplos de 8 bits).

### Apelidos de Tipos (`using`)
```emux
using Byte = u8
using Handle = usize
```
Por omissão, o `using` cria uma identidade nominal:
- `[[unrestrict]]`: permite semelhança estrutural direta.
- `[[restrict]]`: exige casamento nominal exato de tipos.

---

## 2. Blocos de Memória (`bN` / `BN`)

`bN` declara uma região de $N$ bits; `BN` declara uma região de $N$ bytes (onde $1\text{ byte} = 8\text{ bits}$).

Ao contrário de um simples ponteiro, o bloco é **dono do armazenamento**: a variável é a própria região contígua de memória — alocada em `.bss` (quando não inicializada), em `.data` (inicializada mutável), ou em `.rdata` (quando marcada com `[[const]]`).

### Exemplo Básico:
```emux
[Main]

global text : B16
global copy : B16

global Start() i32 {
    text = "Hello"      // Copia os bytes da string literal (com terminador NUL)
    copy = text         // Copia o bloco completo de 16 bytes
    return *copy        // Lê o primeiro byte: 72 ('H')
}
```

### Regras de Operação com Blocos:

1. **Semelhança e Decaimento**:
   - Em contexto de chamada ou atribuição a ponteiro, o bloco decai para `u8*` apontando para o primeiro byte.
   - `&bloco` obtém o endereço do primeiro byte.
   - `*bloco` acessa o primeiro byte (`bloco[0]`).
   - `bloco[i]` indexa o $i$-ésimo byte diretamente. Índices constantes que excedam a capacidade disparam erro de compilação.

2. **Fatiamento e Truncamento de Janela (Slices)**:
   - **`bloco + K` (Avanço de Cabeça)**: Produz uma *view* deslocada $K$ bytes à frente. O tamanho restante passa a ser $(\text{capacidade} - K)$ bytes em tempo de compilação.
   - **`bloco - K` (Truncamento de Cauda)**: Encolhe a capacidade visível da cauda em $K$ bytes sem mexer no ponteiro base da memória (custo zero em runtime).
   - O offset $K$ deve ser uma constante inteira não-negativa. Se $K > \text{capacidade}$, o compilador bloqueia com erro de *out of bounds*.
   - Exemplo combinado:
     ```emux
     // No escopo global:
     global pacote : B64 = "12345678CABECALHO_CORPO_DA_MENSAGEM_AQUI_TESTE"

     global Processar() i32 {
         // Fatiamento estatico: avanca 8 bytes de cabecalho e le o primeiro byte do payload
         local primeiro_byte : i32 = *(pacote + 8)
         return primeiro_byte
     }
     ```

3. **Atribuição entre Blocos**:
   - `destino = origem`: O compilador valida estaticamente que `origem` cabe no `destino`. Não há cópia parcial silenciosa.

4. **Blocos Locais na Stack**:
   - Um bloco pode ser alocado na moldura de pilha de uma função:
     ```emux
     global Exemplo() void {
         local buf : B32 = "Dados temporarios"
         // buf vive na stack desta função e é destruído no retorno
     }
     ```

5. **Comparação de Blocos**:
   - Operadores `==`, `!=`, `<`, `>`, `<=`, `>=` comparam o conteúdo dos blocos lexicograficamente em tempo de execução quando ambos têm conteúdo compatível, após verificação inicial de capacidade.

---

## 3. Structs

Uma `struct` é uma região de memória agregada com campos nomeados e tipados. O
tipo é nominal (identidade pelo nome) e a sua representação física é um bloco de
bytes, pelo que uma struct vive onde um bloco vive: `.data`/`.bss` quando global,
slot de frame quando `local`.

### Sintaxe

```emux
[[packed]]
struct Teste {
    x : u24,
    y : u8
}
```

- Campos `nome : tipo`, separados por vírgula, com quebra de linha opcional.
- Atributos de struct (`[[packed]]`, `[[align(N)]]`, `[[unrestrict]]`,
  `[[restrict]]`) precedem a palavra `struct`. Um campo pode ter o seu próprio
  `[[align(N)]]`.
- Acesso a campo: `s.x` (leitura) e `s.x = valor` (escrita). Campos aninhados:
  `o.inner.b`.

### Alinhamento e tamanho

- **`[[packed]]`**: alinhamento de 1 byte, sem padding. Cada campo ocupa
  `ceil(bits / 8)` bytes consecutivos. Em `Teste`, `x` ocupa os offsets 0..2
  (3 bytes), `y` o offset 3 (1 byte); total = 4 bytes.
- **Sem `[[packed]]`** (alinhamento natural): o alinhamento de um campo é
  `<= 8 bits → 1`, `9..16 → 2`, `17..32 → 4`, `33..64 → 8` bytes. O alinhamento
  da struct é o maior alinhamento dos membros ou o da palavra nativa (8 bytes em
  x86_64, 4 em x86). O tamanho total é arredondado a múltiplo desse alinhamento.
  Em `Teste`, o total sobe para 8 bytes em x86_64.
- **`[[align(N)]]`** força o alinhamento da struct (ou de um campo) a `N` bytes
  (potência de dois). Ex.: `[[align(16)]]` numa struct de dois `u8` reserva 16
  bytes.
- Structs aninhadas: o campo usa o alinhamento da struct interna (1 se ela for
  `[[packed]]`); o layout é calculado recursivamente, com deteção de ciclos.

### Identidade nominal e estrutural

- Por omissão, structs com nomes diferentes são **tipos distintos**, mesmo com
  o mesmo layout: a atribuição direta entre elas é erro de compilação.
- `[[unrestrict]]` na struct (de qualquer dos lados) muda a verificação para
  **equivalência estrutural**: mesmo número de campos, nos mesmos offsets e com
  tipos compatíveis. A cópia passa a ser aceite.
- A cópia de structs (`local t : Teste = s`, `a = b`) copia o bloco de memória
  byte a byte; o inicializador de uma `local` sem valor zero-a a região.

### Lowering

O acesso a campo é lowered para `addr <base> + offset` seguido de `load`/`store`
do tipo do campo; a cópia usa a cópia de blocos existente. Não há instruções nem
tipos novos na Exir: a struct reusa o caminho de blocos, pelo que o
`object_format` não muda.

> Ainda não implementado (incrementos futuros da ideia 3): literais agregados
> anónimos (`{ 10, 20 }`), `[[restrict]]` estrito em parâmetros, visibilidade de
> struct local/privada ao módulo e passagem/retorno de structs por valor.

## 4. Ponteiros e Endereçamento Cru

- Sintaxe de tipo: `T*` (ex: `u8*`, `i32*`).
- Operador de endereço: `&var`.
- Operador de desreferenciação: `*ptr`.
- Aritmética de ponteiros:
  - `ptr + n` e `ptr - n`: avança ou retrocede $n$ elementos proporcionais a `sizeof(T)`.
  - `ptr1 - ptr2`: calcula a distância em número de elementos entre dois ponteiros do mesmo tipo.
