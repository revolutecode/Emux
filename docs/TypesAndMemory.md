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

### Visibilidade (local vs global)

Tal como funções e variáveis, uma struct pode ser exportada ou privada ao módulo:

```emux
global struct Shared { x : u8, y : u8 }  // exportada (visível noutros módulos)
struct Shared { x : u8, y : u8 }          // omissão = exportada
local struct Secret { a : u8 }            // privada ao módulo
```

- `global struct` (ou `struct` sem qualificador) é **exportada**: outro módulo
  resolve o nome qualificado, ex.: `local v : Types::Shared`.
- `local struct` é **privada ao módulo**: só o próprio módulo resolve o nome
  (ex.: `local s : Secret` dentro de `[Types]`). Noutro módulo, `Types::Secret`
  falha com `Unknown or invalid type` — o tipo nominal e o layout não saem do
  módulo de origem.
- A privacidade filtra a propagação entre módulos tanto do registo de layouts
  (`Structs`) como dos tipos nominais resolvidos (`ResolvedTypes`); o módulo
  dono mantém sempre acesso à sua struct privada.

### Literais agregados anónimos

Uma struct pode ser inicializada ou atribuída com um literal agregado `{ .. }`,
que lista os valores dos campos pela ordem da declaração:

```emux
local p : Pair = { 5, 7 }   // inicialização
p = { 10, 20 }              // atribuição
g = { 1, 2 }                // também sobre uma struct global
local w : Wrap = { { 3, 4 }, 9 }  // campos struct aninhados
w.inner = { 30, 40 }
local w2 : Wrap = { q, 1 }  // um campo struct pode vir de outra struct compatível
```

- O número de elementos tem de coincidir com o número de campos; cada elemento
  passa a mesma verificação de encaixe dos locais (intervalo de constantes,
  sinal, reinterpretacao). Um campo struct aceita um agregado aninhado ou a
  cópia de uma struct compatível (nominal, ou estrutural com `[[unrestrict]]`).
- A região nasce zero e cada elemento é escrito no offset do seu campo; os bytes
  de padding ficam a zero. Não há verificação de limites em runtime.
- Um agregado só tem tipo no contexto de uma struct alvo: `{ 1, 2 }` não é uma
  expressão autónoma. Uma struct **global** não aceita inicializador agregado
  (nasce zero); os campos escrevem-se em runtime com `g = { .. }` ou `g.campo = ..`.

### Passagem e retorno por valor

Uma struct pode ser passada a uma função e retornada por valor (cópia), desde
que o seu tamanho não exceda a palavra nativa (8 bytes em x86_64, 4 em x86):

```emux
global Sum(Pair p) i32 { return p.a + p.b }   // parâmetro por valor
global Make() Pair { local r : Pair = { 3, 4 } return r }  // retorno por valor
local q : Pair = Make()                       // copia o resultado
```

- O callee recebe uma **cópia própria**: mutar `p.campo` dentro da função não
  altera a struct do chamador.
- Na ABI interna os bytes crus da struct viajam num registo inteiro da palavra
  nativa (um *carrier*); o acesso a campo continua sobre a região em memória.
  O carrier é carregado com a largura exata da struct e zero-estendido ao
  registo, pelo que os bytes de padding ficam a zero.
- Structs maiores que a palavra nativa **não** são suportadas por valor em
  parâmetros/retornos (erro de compilação); use um ponteiro (`Pair*`).
- A ABI nativa (`[[import]]`) não suporta structs por valor em parâmetros nem
  retornos; use um ponteiro.
- O inicializador de uma `local` aceita uma chamada que retorna a struct
  (`local q : Pair = Make()`), copiando o bloco recebido.

### Compatibilidade em chamadas (`[[restrict]]`)

A verificação do argumento de struct numa chamada segue a mesma regra da cópia
por bloco:

```emux
global Sum(Pair p) i32 { return p.a + p.b }          // nominal por omissão
global Loose(Pair p) i32 { return p.a + p.b }        // aceita equivalente [[unrestrict]]
global Strict([[restrict]] Pair p) i32 { return p.a + p.b }  // identidade nominal exata
```

- **Por omissão**, o parâmetro aceita uma struct de **identidade nominal** igual
  ou, quando qualquer dos lados é `[[unrestrict]]`, uma struct **estruturalmente
  equivalente** (mesmos campos, offsets e tipos). É a mesma compatibilidade da
  atribuição/cópia (`StructsCompatible`), pelo que `Loose(t)` com `t : Twin`
  (`[[unrestrict]]`, mesmo layout de `Pair`) é aceite.
- **`[[restrict]]` no parâmetro** exige o tipo nominal **exato**: rejeita structs
  apenas com o mesmo layout (mesmo que `[[unrestrict]]`) e literais agregados
  anónimos (que não têm identidade nominal). O erro é explícito
  (`Parametro [[restrict]] ... exige a struct nominal ...`).
- Um **literal agregado anónimo** (`{ 10, 20 }`) não é aceite como argumento de
  struct em nenhum modo: declare uma `local` do tipo e passe-a. O diagnóstico
  distingue o caso `[[restrict]]`.
- Structs distintas sem `[[unrestrict]]` continuam a ser tipos distintos: a
  passagem direta é erro (`Impossible conversion`), tal como na atribuição.
- A verificação é só em comptime; o carrier por valor (incremento C) é o mesmo
  nos dois modos, pelo que `[[restrict]]` não altera o código gerado nem o
  `object_format` — apenas o que o compilador aceita.

### Lowering

O acesso a campo é lowered para `addr <base> + offset` seguido de `load`/`store`
do tipo do campo; a cópia usa a cópia de blocos existente. Um literal agregado
materializa-se como uma sequência de `store` (um por campo, no offset anotado
pela semântica), recursando em campos struct e usando `memcpy` na cópia de uma
struct compatível. A passagem/retorno por valor lowers para um `load` do carrier
inteiro no lado do chamador/retorno e um `store` no slot do parâmetro, reutilizando
a ABI escalar existente — sem opcodes novos na Exir. Como a ABI gerada para
parâmetros/retornos de struct muda (carrier por valor em vez de ponteiro), o
`object_format` sobe para 16.

## 4. Ponteiros e Endereçamento Cru

- Sintaxe de tipo: `T*` (ex: `u8*`, `i32*`).
- Operador de endereço: `&var`.
- Operador de desreferenciação: `*ptr`.
- Aritmética de ponteiros:
  - `ptr + n` e `ptr - n`: avança ou retrocede $n$ elementos proporcionais a `sizeof(T)`.
  - `ptr1 - ptr2`: calcula a distância em número de elementos entre dois ponteiros do mesmo tipo.
