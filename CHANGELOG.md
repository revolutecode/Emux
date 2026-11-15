# CHANGELOG

Todas as alterações notáveis neste projeto serão documentadas neste ficheiro.

O formato baseia-se em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.0.0/),
e este projeto adere ao [Versionamento Semântico](https://semver.org/lang/pt-BR/).

---

## [Por publicar]

### Alterado (quebra de compatibilidade)
- **Sintaxe de comentários e diretivas (ideia 6)**: Os comentários migraram de `#` (linha) e `###` (bloco) para `//` (linha) e `/* ... */` (bloco). As diretivas do preprocessador migraram de `@` para `#` (`#include`, `#define`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#elifdef`, `#elifndef`, `#else`, `#endif`, `#undef`). A troca dos comentários aconteceu em simultâneo com a troca do prefixo das diretivas, respeitando a regra de conflito registada: `#` deixou de ser comentário antes de passar a prefixo de diretiva. Toda a STD, exemplos e testes foram migrados. É uma mudança incompatível de sintaxe, pelo que exige um `minor` (0.8.0) no corte de release.

### Adicionado
- **Otimização de registos em `-O2` (ideia 8)**: Passe de peephole arch-agnóstico sobre o assembly NASM (`NasmPeephole`) que mantém temporários de vida curta em registos via forwarding store→load e elimina `mov reg,reg` e `lea` redundantes. O ganho é medido no smoke (`opt_regalloc`: −7 instruções face a `-O0`, resultado idêntico). `-O0`/`-O1` mantêm o código byte a byte; o passe só corre em `-O2`.

### Suporte
- `object_format` mantém-se `15`: comentários e diretivas são resolvidos no lexer/preprocessador antes do parsing; a gramática da Exir, o layout do `.emo` e a ABI gerada não mudam. O peephole de `-O2` é codegen-only depois da Exir e não altera a chave de cache (`opt:<n>` e o timestamp do exe já fazem parte dela).

---

## [0.7.3] - 2026-11-15

### Alterado
- **Semântica de divisão inteira (ideia 9)**: Divisão por zero (`/`, `%`, `/=`, `%=`) e o overflow `MIN / -1` terminam num trap explícito (`ud2`) nos backends x64 e x86, em vez de um SIGFPE silencioso ou de lixo. As guardas são omitidas quando um divisor constante prova que o caso é impossível, sem custo no caso comum. O ponto fixo (`__emux_q_div`) também trap em divisor zero. Divisor zero conhecido em comptime continua a ser erro de compilação.

### Adicionado
- **Limite de frame em comptime**: Um `local` (ou a soma dos locais de uma função) que exceda 256 KiB é diagnosticado em comptime, com o mesmo limite em x86 e x64, em vez de rebentar a pilha em execução.
- **Testes de trap e de erro comptime**: `div_zero_runtime`, `div_min_overflow` e `wide_div_zero` (x64), mais `div_zero_runtime_x86` e `div_min_overflow_x86`, verificam o trap (SIGILL, exit 132); `div_zero_error`, `mod_zero_error` e `frame_limit_error` verificam os erros comptime nos dois alvos. `fixed_divzero` passou a validar o trap em vez do antigo resultado zero.

---

## [0.7.2] - 2026-11-14

### Corrigido
- **Ponto fixo em bytes (`Q<bytes>.<scale>`)**: A notação em maiúsculas foi alinhada com a fórmula canônica da linguagem: o primeiro número representa o tamanho total e o segundo a escala, sendo a parte inteira calculada por `(total - scale)`. Em `Q32.16`, são 32 bytes no total, com 16 bytes inteiros e 16 bytes fracionários (metade de cada lado).

### Removido
- **Atributos deprecados/não utilizados**: Remoção dos atributos legados sem suporte ou sem runtime (`[[noexcept]]`, `[[nowarn]]`, `[[everen]]`, `[[inline]]`, `[[alwaysinline]]`, `[[pure]]`) de `AttrSet.hpp`, mantendo apenas os atributos funcionais do compilador.

### Alterado
- **Backend x64 sobre a Exir v3**: `X64Generator` foi reescrito para o modelo
  em memória e cobre agora inteiros largos (multi-limb), larguras sub-byte,
  `%`/`srem`/`urem`, `float`/`fixed` sobre o runtime C, ponto fixo com escala,
  `neg`/`not`, `store` de regiões e alocação de slot para todo o destino. Os
  ajudantes NASM multi-limb (`EmitArchHelpers`) voltaram a ser emitidos.
- **Backend x86 sobre a Exir v3**: `X86Generator` reimplementado, incluindo
  prólogo/epílogo por convenção, chamadas fastcall com os dois primeiros
  argumentos em `ecx`/`edx`, isolamento de inteiros largos em `shl`/`sar` e
  correção do deslocamento de pilha no prólogo fastcall.
- **Coerção de tipos no builder**: `IRExpressionBuilder::Coerce` deixou de
  tratar qualquer diferença de classe como mudança de sentido. Ponteiros,
  buffers e `void` partilham o padrão de bits e continuam a ser uma simples
  reatribuição de tipo; só pares numéricos com classe, largura ou escala
  diferentes emitem instrução de conversão.
- **Literais `float`/`fixed` codificados na origem**: uma atribuição a um
  `f32`/`q<bits>.<scale>` passa a armazenar o padrão de bits do destino
  (`store f32 1092616192` é `10.0f`), e as mudanças de classe ou escala
  passam a ser um `conv` explícito.
- **Convenção de chamada propagada**: `Exir::Function::Convention` é
  preenchida a partir de `[[stdcall]]`/`[[fastcall]]`, pelo que o gerador emite
  `ret N` e decora os símbolos corretamente.
- **Impressão da Exir v3**: o printer emite `conv`, inclui os tipos de origem e
  destino nas conversões, e imprime os `import` sem decoração para que os
  `grep` da smoke testem os símbolos reais.

### Suporte
- `object_format` `14` → `15` (constantes numéricas codificadas, instrução
  `conv` e convenção de chamada na ABI gerada).
- **Casos de teste**: `tests/cases/break_dead_code.ex` cobre `break` com
  instruções mortas a jusante.

---

## [0.6.0] - 2026-11-09

### Deprecado
- **Exir v2**: A representação intermediária baseada em strings e instruções ad-hoc (`CmpMem`, `Zero`, `StoreVar`) foi oficialmente marcada como **deprecada**. A transição para a **Exir v3** foi iniciada com a nova especificação formal em `Docs/ExirV3.md`.

### Adicionado
- **Truncamento de Buffers `b - K`**: Operação de redução de cauda em tempo de compilação para buffers e blocos (`bN`/`BN`).
  - `b + K`: avança o ponteiro base em `K` bytes e reduz o tamanho restante em tempo de compilação.
  - `b - K`: encolhe o tamanho restante da cauda em `K` bytes sem alterar o ponteiro base (custo zero em runtime).
  - Verificação estática total: bloqueia sobredimensionamento e offsets inválidos em tempo de compilação.
- **Especificação da Exir v3** (`Docs/ExirV3.md`):
  - Modelo baseado em CFG (Grafo de Controle de Fluxo) com `BasicBlock` e terminadores estritos (`br`, `cbr`, `ret`, `unreachable`, `switch`).
  - Tipagem estrita por instrução (zero coerções ocultas na IR).
  - Instruções de memória universais (`addr`, `load`, `store`, `gep`, `memcpy`, `memset`, `memcmp`).
  - Suporte nativo a tipos agregados e múltiplos retornos (`extract`, `insert`).
  - Primitivas de sincronização atômica (`atomic_load`, `atomic_store`, `atomic_cas`, `fence`).
  - Metadados de rastreamento de depuração (`!dbg`).
- **Casos de teste**:
  - `tests/cases/buffer_shrink.ex`: validação do fatiamento e encolhimento de cauda `(text + 7) - 2`.
  - `tests/cases/buffer_shrink_oob_error.ex`: validação de erro ao tentar encolher buffer além da capacidade.

### Alterado
- Planejamento de sintaxe futura registrado em `Ideias.txt`:
  - Diretivas do pré-processador migrarão de `@` para `#` (`#include`, `#define`, `#if`, etc.).
  - Sintaxe de comentários migrará de `#` e `###` para `//` e `/* ... */`.

---

## [0.5.0] - 2026-11-09
- Adicionada comparação lexicográfica de blocos de memória (`cmpmem`) com desempate estático.
- Suporte a `object_format 13`.
