# Exir v3 — Especificação Oficial da Representação Intermediária

A **Exir v3** (Emux Intermediate Representation versão 3) é a representação intermediária independente de arquitetura do compilador Emux.

Ela foi desenhada para substituir a Exir v2, eliminando instruções ad-hoc, serialização/parsing de strings intermediárias e coerções de tipos implícitas.

---

## 1. Princípios de Design

1. **Tipagem Estrita (Zero Coerções Ocultas)**:
   - Toda operação binária exige que ambos os operandos sejam estritamente do mesmo tipo (`Type(LHS) == Type(RHS)`).
   - Conversões de largura (`zext`, `sext`, `trunc`), representação (`bitcast`) ou categorias de ponteiro (`inttoptr`, `ptrtoint`) são **sempre explícitas**.
2. **Grafo de Controle de Fluxo Fechado (CFG com Blocos Básicos)**:
   - Funções são compostas exclusivamente por **Blocos Básicos** (`BasicBlock`).
   - Um bloco básico é uma sequência linear de instruções que termina obrigatoriamente com **uma única instrução terminadora** (`br`, `cbr`, `ret`, `unreachable`).
   - Não há *fallthrough* acidental ou saltos para o meio de blocos.
3. **Estrutura de Dados Nativa em Memória**:
   - A IR vive como estruturas C++ com IDs numéricos (inteiros para temporários e ponteiros). O texto `.exir` é apenas uma ferramenta de depuração/inspeção visual.
4. **Debuggability de Primeira Classe**:
   - Cada instrução carrega metadados opcionais de rastreamento de código-fonte (`!dbg "arquivo":linha:coluna`), facilitando a geração de símbolos DWARF/PDB e depuração interativa via GDB/LLDB.

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

#### `gep` (GetElementPtr - Endereçamento Composto)
- **Sintaxe**: `%dst = gep <TipoBase> %ptr, %indice_ou_offset`
- **O que faz**: Calcula o endereço efetivo de um elemento indexado ou membro agregado somando o deslocamento proporcional ao tamanho do `<TipoBase>`, sem quebrar em multiplicações e somas manuais.
- **Por que usar**:
  1. Permite ao compilador fazer bounds-check de buffers estáticos diretamente na instrução.
  2. Permite ao backend emitir instruções de endereçamento de máquina de ciclo único (como `lea rax, [rbx + rcx*scale + disp]` no x86/x64).
- **Exemplo**:
  ```text
  %elem_ptr = gep i32 %array_ptr, %idx
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
- **Por que usar**: Inicialização de memória zerada (substitui a antiga instrução ad-hoc `Zero`) e limpeza de buffers locais.
- **Exemplo**:
  ```text
  memset %slot_ptr, 0, 32
  ```

#### `memcmp`
- **Sintaxe**: `%dst = memcmp %ptr1, %ptr2, %bytes`
- **O que faz**: Compara lexicograficamente `%bytes` entre dois ponteiros de memória. Devolve um `i32` (`< 0`, `0`, ou `> 0`).
- **Por que usar**: Substitui a antiga instrução hiper-específica `CmpMem`. Permite comparações de buffers, strings e fatias de memória de forma universal. Se houver desempate estático de tamanho, o compilador resolve com um `sub` comum.
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

Substitui o agrupamento confuso de `Cast`, `BitCast` e `Trunc` da v2 por quatro primitivas ortogonais:

#### `trunc`
- **Sintaxe**: `%dst = trunc <T_origem> to <T_destino> %val`
- **O que faz**: Descarta os bits mais significativos para reduzir a largura.
- **Exemplo**: `%d = trunc u64 to u32 %v`

#### `zext`
- **Sintaxe**: `%dst = zext <T_origem> to <T_destino> %val`
- **O que faz**: Estende com zeros para um tipo mais largo (zero extension).
- **Exemplo**: `%d = zext u8 to u32 %v`

#### `sext`
- **Sintaxe**: `%dst = sext <T_origem> to <T_destino> %val`
- **O que faz**: Estende propagando o bit de sinal para um tipo maior (sign extension).
- **Exemplo**: `%d = sext i8 to i32 %v`

#### `bitcast`
- **Sintaxe**: `%dst = bitcast <T_origem> to <T_destino> %val`
- **O que faz**: Reinterpreta os mesmos bits como outro tipo com a mesma largura (ex: `f32` para `u32`).
- **Exemplo**: `%d = bitcast f32 to u32 %v`

#### `ptrtoint` / `inttoptr`
- **Sintaxe**:
  - `%dst = ptrtoint ptr to <Uint> %ptr`
  - `%dst = inttoptr <Uint> to ptr %int`
- **O que faz**: Conversão entre endereços de ponteiros e inteiros nativos da arquitetura alvo.

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

#### `switch` (Despacho Multicaminho $O(1)$)
- **Sintaxe**:
  ```text
  switch <T> %val, label %default_bb [
      valor_1: label %bb_1,
      valor_2: label %bb_2,
      ...
  ]
  ```
- **O que faz**: Compara `%val` contra uma lista de valores inteiros constantes e salta para o rótulo correspondente. Se nenhum casar, salta para `%default_bb`.
- **Por que usar**: Permite ao backend compilar casamentos densos como Jump Tables indexadas diretas na CPU (`jmp [table + reg*8]`) com custo $O(1)$, em vez de dezenas de `cbr` encadeados. Essencial para despachantes de syscalls em microkernels e parsers de comandos.

#### `select` (Condicional Funcional / CMOV)
- **Sintaxe**: `%dst = select u1 %cond, <T> %val_true, <T> %val_false`
- **O que faz**: Retorna `%val_true` ou `%val_false` sem quebrar o bloco básico em novos branches. Mapeia perfeitamente para instruções `cmov` da CPU.

#### `ret` (Retorno de Função)
- **Sintaxe**: `ret <T> %val` ou `ret void`
- **O que faz**: Retorna da função atual entregando o valor de resultado.

#### `unreachable`
- **Sintaxe**: `unreachable`
- **O que faz**: Informa que a execução não deve chegar a este ponto. Usado após loops infinitos de kernel, aborts e asserts.

---

### 2.6 Tipos Agregados e Múltiplos Retornos

A Exir v3 suporta tipos tupla/agregados `{ T1, T2, ... }` de forma nativa para permitir múltiplos retornos e agrupamentos eficientes.

#### `extract`
- **Sintaxe**: `%dst = extract <TipoAgregado> %valor_agregado, <indice_constante>`
- **O que faz**: Extrai o elemento na posição `<indice_constante>` de uma tupla ou valor composto retornado.
- **Por que usar**: Desempacotar retornos múltiplos de funções (estilo Go: `(val, ok)`).
- **Exemplo**:
  ```text
  %pair = call { i32, u8 } @LerDado cdecl ()
  %val  = extract { i32, u8 } %pair, 0
  %ok   = extract { i32, u8 } %pair, 1
  ```

#### `insert`
- **Sintaxe**: `%dst = insert <TipoAgregado> %valor_agregado, <T_elem> %novo_val, <indice_constante>`
- **O que faz**: Insere um valor em uma posição de um agregado, produzindo uma nova tupla.
- **Por que usar**: Montar tuplas de retorno composto sem alocar memória na stack.

---

### 2.7 Concorrência e Operações Atômicas

Instruções portáteis para algoritmos multithreading, spinlocks e sincronização de CPU no LyraOS.

#### `atomic_load` / `atomic_store`
- **Sintaxe**:
  - `%dst = atomic_load <T> %ptr, <ordem>`
  - `atomic_store <T> %val, %ptr, <ordem>`
- **Ordens de Consistência**: `monotonic`, `acquire`, `release`, `seq_cst`.
- **Por que usar**: Leitura e escrita atômicas sem reordenação indesejada de otimizador.

#### `atomic_cas` (Compare-And-Swap)
- **Sintaxe**: `%res = atomic_cas <T> %ptr, %esperado, %novo_valor, <ordem_sucesso>, <ordem_falha>`
- **O que faz**: Se `*ptr == esperado`, grava `novo_valor` e devolve o valor original de memória.
- **Por que usar**: Primitiva fundamental para mutexes, spinlocks de kernel e estruturas lock-free. Baixa diretamente para `lock cmpxchg` no x86/x64 e `cas` / `ldrex-strex` no ARM.

#### `fence` (Barreira de Memória)
- **Sintaxe**: `fence <ordem>`
- **O que faz**: Garante ordenação de acessos de memória entre threads ou núcleos.

---

### 2.8 Chamadas e Primitivas de Baixo Nível

#### `call`
- **Sintaxe**: `%dst = call <TRet> @funcao <calling_conv> (<T1> %arg1, ...)`
- **Convenções**: `cdecl`, `stdcall`, `fastcall`, `sysv` (essencial para kernels bare-metal como LyraOS).

#### `asm` (Inline Assembly)
- **Sintaxe**: `asm "instrucoes"`
- **Por que usar**: Isolamento de instruções de hardware de nicho ou exclusivas de Ring 0 (`hlt`, `cli`, `sti`, `lidt`, registradores `cr0..cr4`, `inb`/`outb`).
- **Diretriz de Design**: A Exir v3 **NUNCA** inclui instruções específicas de Ring 0 de uma única CPU como opcodes de IR. O controle de baixo nível de arquitetura fica isolado dentro de blocos `asm` na camada HAL do sistema operacional, mantendo a IR 100% universal e multiplataforma.

#### `trap`
- **Sintaxe**: `trap`
- **O que faz**: Emite uma quebra deliberada de execução (`ud2` ou `int3`) para depurador.

---

## 3. Rastreabilidade de Debug (`!dbg`)

Qualquer instrução pode receber anotação de depuração no formato:
```text
%t0 = load i32, %ptr  !dbg "src/main.ex":42:5
```
Isso permite que o gerador de código final repasse as informações para as tabelas DWARF de debugging.
