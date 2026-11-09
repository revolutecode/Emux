# CHANGELOG

Todas as alterações notáveis neste projeto serão documentadas neste ficheiro.

O formato baseia-se em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.0.0/),
e este projeto adere ao [Versionamento Semântico](https://semver.org/lang/pt-BR/).

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
