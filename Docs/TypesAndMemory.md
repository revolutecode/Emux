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

var text : B16
var copy : B16

func Start() i32 {
    text = "Hello"      # Copia os bytes da string literal (com terminador NUL)
    copy = text         # Copia o bloco completo de 16 bytes
    return *copy        # Lê o primeiro byte: 72 ('H')
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
     # No escopo global:
     var pacote : B64 = ...

     func Processar() {
         # Dentro de funcao, variaveis locais usam estritamente 'local':
         local payload = (pacote + 8) - 4   # view segura com 52 bytes restantes
     }
     ```

3. **Atribuição entre Blocos**:
   - `destino = origem`: O compilador valida estaticamente que `origem` cabe no `destino`. Não há cópia parcial silenciosa.

4. **Blocos Locais na Stack**:
   - Um bloco pode ser alocado na moldura de pilha de uma função:
     ```emux
     func Exemplo() {
         local buf : B32 = "Dados temporarios"
         # buf vive na stack desta função e é destruído no retorno
     }
     ```

5. **Comparação de Blocos**:
   - Operadores `==`, `!=`, `<`, `>`, `<=`, `>=` comparam o conteúdo dos blocos lexicograficamente em tempo de execução quando ambos têm conteúdo compatível, após verificação inicial de capacidade.

---

## 3. Ponteiros e Endereçamento Cru

- Sintaxe de tipo: `T*` (ex: `u8*`, `i32*`).
- Operador de endereço: `&var`.
- Operador de desreferenciação: `*ptr`.
- Aritmética de ponteiros:
  - `ptr + n` e `ptr - n`: avança ou retrocede $n$ elementos proporcionais a `sizeof(T)`.
  - `ptr1 - ptr2`: calcula a distância em número de elementos entre dois ponteiros do mesmo tipo.
