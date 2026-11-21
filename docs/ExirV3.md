# Exir v3 — Especificação Oficial da Representação Intermediária

A **Exir v3** (Emux Intermediate Representation versão 3) é a representação intermediária do compilador Emux.

Ela substituiu a Exir v2, eliminando instruções ad-hoc, serialização/parsing de strings intermediárias e coerções de tipos implícitas. Esta especificação descreve apenas o que o front-end emite hoje; instruções reservadas mas ainda não emitidas não fazem parte do contrato atual.

---

## 1. Princípios de Design

1. **Tipagem Estrita (Zero Coerções Ocultas)**:
   - Toda operação binária exige que ambos os operandos sejam estritamente do mesmo tipo (`Type(LHS) == Type(RHS)`).
   - Conversões de largura (`trunc`, `zext`, `sext`), representação (`bitcast`) ou categorias de ponteiro (`inttoptr`, `ptrtoint`) são **sempre explícitas**.
2. **Grafo de Controle de Fluxo Fechado (CFG com Blocos Básicos)**:
   - Funções são compostas exclusivamente por **Blocos Básicos** (`BasicBlock`).
   - Um bloco básico é uma sequência linear de instruções que termina obrigatoriamente com **uma única instrução terminadora** (`br`, `cbr`, `ret`).
   - Não há *fallthrough* acidental ou saltos para o meio de blocos.
3. **Estrutura de Dados Nativa em Memória**:
   - A IR vive como estruturas C++ com IDs numéricos (inteiros para temporários e ponteiros). O texto `.exir` é apenas uma ferramenta de depuração/inspeção visual.
4. **Rastreabilidade de Código-Fonte**:
   - Cada instrução reserva `SourceLine`/`SourceCol`, e o printer do sidecar mostra `; !dbg line N` quando o campo está preenchido.

---

## 2. Catálogo de Instruções

### 2.1 Memória e Ponteiros

Em Exir v3, **variáveis locais e globais são apenas endereços de memória**. Não existem instruções redundantes como `StoreVar` ou `Zero`.

#### `addr`
- **Sintaxe**:
  - `%dst = addr ptr, @simbolo_global` (para variáveis globais e funções)
  - `%dst = addr ptr, %nome_local` (para variáveis locais no frame da stack)
- **O que faz**: Obtém o endereço base (ponteiro) de uma variável global, local de pilha ou função.
- **Por que usar**: Para referenciar qualquer símbolo antes de operações de leitura ou escrita.
- **Exemplo**:
  ```text
  %x_ptr = addr ptr, @main_x
  %local_ptr = addr ptr, %minha_local
  ```

#### `gep`
- **Sintaxe**: `%dst = gep <T_ptr> %base, %index, %elem_bytes`
- **O que faz**: Calcula o endereço base somado a um deslocamento escalado: `%base + %index * %elem_bytes`. O índice conta elementos do tipo apontado e suporta valores negativos em complemento para dois; a escala é em bytes.
- **Por que usar**: Substitui adições e multiplicações cruas em aritmética de ponteiros (`p + i`, `p - i`, `p += i`, `p -= i`), indexação de buffers e ponteiros (`b[i]`, `p[i]`), e cálculo de endereços de campos de structs.
- **Exemplo**:
  ```text
  %elem_ptr = gep ptr %base_ptr, %idx, 4
  ```

#### `load`
- **Sintaxe**: `%dst = load [volatile] <T>, %ptr`
- **O que faz**: Lê um valor do tipo `<T>` contido no endereço `%ptr`. Com `volatile`, o compilador é proibido de eliminar ou reordenar a leitura (essencial para MMIO em kernels).
- **Por que usar**: Acesso explícito de desreferenciação de ponteiro ou leitura de variáveis.
- **Exemplo**:
  ```text
  %val = load i32, %x_ptr
  ```

#### `store`
- **Sintaxe**: `store [volatile] <T> %val, %ptr`
- **O que faz**: Grava o valor `%val` (do tipo `<T>`) no endereço `%ptr`. Com `volatile`, garante que a escrita atinja o hardware de fato (Memory-Mapped I/O).
- **Por que usar**: Para modificar o conteúdo da memória, variáveis locais ou globais.
- **Exemplo**:
  ```text
  store i32 42, %x_ptr
  ```

#### `memcpy`
- **Sintaxe**: `memcpy %dst_ptr, %src_ptr, %bytes`
- **O que faz**: Copia `%bytes` contíguos da origem para o destino.
- **Por que usar**: Atribuições de blocos (`bN`), inicialização de buffers grandes e structs.
- **Exemplo**:
  ```text
  memcpy %dest_ptr, %origem_ptr, 64
  ```

#### `memset`
- **Sintaxe**: `memset %dst_ptr, u8 %val, %bytes`
- **O que faz**: Preenche `%bytes` no destino com o byte `%val`.
- **Por que usar**: Inicialização de memória zerada e limpeza de buffers locais.
- **Exemplo**:
  ```text
  memset %slot_ptr, 0, 32
  ```

#### `memcmp`
- **Sintaxe**: `%dst = memcmp %ptr1, %ptr2, %bytes`
- **O que faz**: Compara lexicograficamente `%bytes` entre dois ponteiros de memória. Devolve um `i32` (`< 0`, `0`, ou `> 0`).
- **Por que usar**: Comparações de buffers, strings e fatias de memória de forma universal.
- **Exemplo**:
  ```text
  %cmp = memcmp %ptr_a, %ptr_b, 16
  ```

---

### 2.2 Operações Aritméticas e Lógicas

Todas as operações binárias exigem que ambos os operandos sejam **estritamente do mesmo tipo** `<T>`, e o valor resultante é também do tipo `<T>`.

| Instrução | Sintaxe | O que faz | Quando usar |
|---|---|---|---|
| **`add`**  | `%d = add <T> %a, %b`  | Soma com wrap | Adição aritmética |
| **`sub`**  | `%d = sub <T> %a, %b`  | Subtração | Diferença aritmética |
| **`mul`**  | `%d = mul <T> %a, %b`  | Multiplicação | Multiplicação inteira |
| **`udiv`** | `%d = udiv <T> %a, %b` | Divisão sem sinal | Operações em `u8`, `u16`, `u32`, `u64` |
| **`sdiv`** | `%d = sdiv <T> %a, %b` | Divisão com sinal | Operações em `i8`, `i16`, `i32`, `i64` |
| **`urem`** | `%d = urem <T> %a, %b` | Resto sem sinal (módulo) | Resto unsigned |
| **`srem`** | `%d = srem <T> %a, %b` | Resto com sinal | Resto signed |
| **`and`**  | `%d = and <T> %a, %b`  | Bitwise AND | Máscaras de bits |
| **`or`**   | `%d = or <T> %a, %b`   | Bitwise OR | Junção de flags |
| **`xor`**  | `%d = xor <T> %a, %b`  | Bitwise XOR | Inversão/combinação de bits |
| **`shl`**  | `%d = shl <T> %a, %shift` | Shift lógico à esquerda | Deslocamento de bits para esquerda |
| **`lshr`** | `%d = lshr <T> %a, %shift`| Shift lógico à direita (zero-fill)| Deslocamento unsigned |
| **`ashr`** | `%d = ashr <T> %a, %shift`| Shift aritmético à direita (sign-fill)| Deslocamento preservando sinal |
| **`neg`**  | `%d = neg <T> %a`      | Negação de complemento para dois | Operador `-x` |
| **`not`**  | `%d = not <T> %a`      | Inversão bitwise (`~x`) | Operador `~x` |

---

### 2.3 Comparações Relacionais (`cmp`)

- **Sintaxe**: `%dst = cmp <predicado> <T> %a, %b`
- **Tipo de Retorno**: Sempre booleano (`u1` ou `u8`).
- **Predicados Suportados**:
  - `eq` (igual)
  - `ne` (diferente)
  - `ult` / `slt` (menor que: unsigned / signed)
  - `ule` / `sle` (menor ou igual: unsigned / signed)
  - `ugt` / `sgt` (maior que: unsigned / signed)
  - `uge` / `sge` (maior ou igual: unsigned / signed)
- **Exemplo**:
  ```text
  %cond = cmp slt i32 %count, 10
  ```

---

### 2.4 Conversões de Tipos (Casts Explícitos)

A forma textual abaixo é a do sidecar `.exir` que o compilador escreve ao lado do executável (debug/inspecção — não existe parser para esta sintaxe). Todas as conversões levam primeiro o tipo de destino e depois o tipo do operando: `%dst = <op> <T_destino> <T_origem> %val`.

Cinco primitivas preservam o padrão de bits; uma única (`conv`) altera o valor.

#### `trunc`
- **Sintaxe**: `%dst = trunc <T_destino> <T_origem> %val`
- **O que faz**: Descarta os bits mais significativos para reduzir a largura.
- **Exemplo**: `%d = trunc u32 u64 %v`

#### `zext`
- **Sintaxe**: `%dst = zext <T_destino> <T_origem> %val`
- **O que faz**: Estende com zeros para um tipo mais largo (zero extension).
- **Exemplo**: `%d = zext u32 u8 %v`

#### `sext`
- **Sintaxe**: `%dst = sext <T_destino> <T_origem> %val`
- **O que faz**: Estende propagando o bit de sinal para um tipo maior (sign extension).
- **Exemplo**: `%d = sext i32 i8 %v`

#### `bitcast`
- **Sintaxe**: `%dst = bitcast <T_destino> <T_origem> %val`
- **O que faz**: Reinterpreta os mesmos bits — é o que `reinterpret_cast` emite, mesmo entre classes numéricas de larguras iguais, e o que ligar endereços/blocos usa.
- **Exemplo**: `%d = bitcast u32 f32 %v`

#### `ptrtoint` / `inttoptr`
- **Sintaxe**:
  - `%dst = ptrtoint <Uint> <ptr> %ptr`
  - `%dst = inttoptr <ptr> <Uint> %int`
- **O que faz**: Conversão entre endereços de ponteiros e inteiros nativos da arquitetura alvo.

#### `conv`
- **Sintaxe**: `%dst = conv <T_destino> <T_origem> %val`
- **O que faz**: Conversão numérica **com mudança de sentido**: entre `int`, `float` e `fixed`, ou dentro de um `float`/`fixed` quando a largura ou a escala mudam. É a única conversão que o backend não pode resolver como uma cópia de bits, e por isso precisa de rotina própria no runtime (`__emux_f_convert`, `__emux_f_to_int`, `__emux_q_rescale`, ...) nos alvos sem instrução nativa.
- **Exemplo**: `%i = conv i32 f32 %f`
- **Nota**: um literal já chega ao IR codificado no tipo do destino (`store f32 1092616192` é o `10` em IEEE-754 bin32), portanto `conv` só aparece quando o operando é um valor calculado.

---

### 2.5 Controle de Fluxo e Terminadores

Todo Bloco Básico termina obrigatoriamente com uma destas instruções:

#### `br` (Salto Incondicional)
- **Sintaxe**: `br label %target_bb`
- **O que faz**: Transfere o controle de execução incondicionalmente para o bloco indicado.

#### `cbr` (Salto Condicional de Duas Vias)
- **Sintaxe**: `cbr u1 %cond, label %true_bb, label %false_bb`
- **O que faz**: Se `%cond` for verdadeiro (`1`), salta para `%true_bb`; caso contrário, salta para `%false_bb`.
- **Por que usar**: Substitui a assimetria do antigo `branchif`/`branchnot`. Garante que não haja fallthrough acidental.

#### `ret` (Retorno de Função)
- **Sintaxe**: `ret <T> %val` ou `ret void`
- **O que faz**: Retorna da função atual entregando o valor de resultado.

---

### 2.6 Chamadas

#### `call`
- **Sintaxe**: `%dst = call <TRet> @funcao <calling_conv> (<T1> %arg1, ...)`
- **Conversão de argumentos**: O builder emite instruções explícitas de conversão (`conv`) na própria IR para conformar os argumentos à assinatura da função de destino, garantindo paridade entre backends x64 e x86 sem que os geradores inventem conversões sintéticas.
- **Convenções**: `cdecl`, `stdcall`, `fastcall`. Em x64 a convenção é aceite mas não altera o código gerado; em x86 determina registos e limpeza da pilha.

---

## 3. Rastreabilidade de Debug (`!dbg`)

Cada instrução reserva `SourceLine` e `SourceCol`. Quando preenchidos, o printer do sidecar acrescenta a anotação:
```text
%t0 = load i32, %ptr  ; !dbg line 42
```
A linha vem do nó da AST que originou a instrução. A geração de tabelas nativas (COFF/DWARF) ainda não faz parte da Exir v3.
