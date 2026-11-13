# CHANGELOG

Todas as alterações notáveis neste projeto serão documentadas neste ficheiro.

O formato baseia-se em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.0.0/),
e este projeto adere ao [Versionamento Semântico](https://semver.org/lang/pt-BR/).

---

## [0.6.1] - 2026-11-13

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
