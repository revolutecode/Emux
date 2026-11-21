# CHANGELOG

Todas as alterações notáveis neste projeto serão documentadas neste ficheiro.

O formato baseia-se em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.0.0/),
e este projeto adere ao [Versionamento Semântico](https://semver.org/lang/pt-BR/).

---

## [Por publicar]

### Alterado (quebra de compatibilidade)
- **Sintaxe de comentários e diretivas (ideia 6)**: Os comentários migraram de `#` (linha) e `###` (bloco) para `//` (linha) e `/* ... */` (bloco). As diretivas do preprocessador migraram de `@` para `#` (`#include`, `#define`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#elifdef`, `#elifndef`, `#else`, `#endif`, `#undef`). A troca dos comentários aconteceu em simultâneo com a troca do prefixo das diretivas, respeitando a regra de conflito registada: `#` deixou de ser comentário antes de passar a prefixo de diretiva. Toda a STD, exemplos e testes foram migrados. É uma mudança incompatível de sintaxe, pelo que exige um `minor` (0.8.0) no corte de release.

### Adicionado
- **Quitação da dívida da Exir v3 (ideia 7.1)**:
  * **Instrução `gep`**: O opcode `gep` passa a ser emitido em substituição de `mul` + `add` sobre ponteiros em aritmética de ponteiros (`+`, `-`, `+=`, `-=`), indexação (`b[i]`, `p[i]`), membros de struct (`s.campo`) e agregados. Implementado com paridade nos backends x64 e x86 e serializado no `ExirPrinter`.
  * **ExternalFunctions**: A descoberta de símbolos externos migrou dos geradores para o `IRBuilder`, que expõe `Module::ExternalFunctions`; o gerador apenas emite as diretivas `extern`.
  * **`ValueKind::External`**: Substituição da convenção de IDs `0x80000000` por um kind explícito na IR (`@external_N`).
  * **Conversão de argumentos explícita**: Remoção de instruções `conv` sintéticas no codegen x64; o builder consulta as assinaturas e emite `conv` explícito na IR tanto para x64 como para x86.
- **Structs (ideia 3, núcleo)**: Agregados nomeados (`struct`) com campos `nome : tipo` separados por vírgula e acesso `s.campo` (incl. aninhado `o.inner.b`). Layout calculado em comptime: `[[packed]]` sem padding, alinhamento natural por omissão e `[[align(N)]]` por struct ou campo, com detecção de ciclos em structs aninhadas. Identidade nominal por omissão (structs com nomes distintos são tipos distintos) e equivalência estrutural com `[[unrestrict]]`; cópia por bloco. Structs globais (`.data`/`.bss`) e `local` (frame). Lowering `addr + offset` sobre o caminho de blocos existente, nos backends x64 e x86. A ideia 3 está totalmente implementada (núcleo + incrementos A, B, C e D) e foi removida de `Ideias.txt`.
- **Literais agregados anónimos (ideia 3, incremento A)**: `{ 10, 20 }` inicializa e atribui structs pela ordem dos campos (`local s : Teste = { 5, 7 }`, `s = { 10, 20 }`, `g = { 1, 2 }`), incluindo campos struct aninhados (`{ { 3, 4 }, 9 }`) e a cópia de uma struct compatível para um campo (`{ q, 1 }`). O número de elementos e o encaixe de cada um são verificados em comptime (intervalo, sinal, reinterpretacao); a região nasce zero e cada campo é escrito no seu offset, sem custo de runtime. Uma struct global nasce zero e recebe os campos em runtime. Lowering para `store`/`memcpy` sobre o caminho de blocos, sem opcodes novos.
- **Visibilidade de structs (ideia 3, incremento B)**: `global struct` (ou `struct` sem qualificador) é exportada e resolve-se noutro módulo pelo nome qualificado (`local v : Types::Shared`); `local struct` é privada ao módulo — só o módulo dono resolve o nome, e noutro módulo `Types::Secret` falha com `Unknown or invalid type`. A privacidade filtra a propagação entre módulos do registo de layouts (`Structs`) e dos tipos nominais (`ResolvedTypes`); o módulo dono mantém acesso à sua struct privada. Sem opcodes novos.
- **Structs por valor em parâmetros/retornos (ideia 3, incremento C)**: Uma struct até à palavra nativa (8 bytes em x86_64, 4 em x86) pode ser passada a uma função e retornada por valor (cópia): `global Sum(Pair p) i32`, `global Make() Pair`, `local q : Pair = Make()`. Os bytes crus da struct viajam num registo inteiro da palavra nativa (*carrier*), reutilizando toda a ABI escalar existente — os geradores x64/x86 ficam intactos e não há opcodes novos na Exir. O carrier é carregado com a largura exata da struct e zero-estendido ao registo (padding a zero); o callee recebe uma cópia própria, pelo que mutar o parâmetro não altera o chamador. Structs maiores que a palavra nativa ou em `[[import]]` (ABI nativa) são rejeitadas em comptime — use um ponteiro. A ABI gerada para parâmetros/retornos de struct muda (carrier por valor em vez de ponteiro), pelo que o `object_format` sobe para `16`.
- **`[[restrict]]` em parâmetros de struct (ideia 3, incremento D)**: A compatibilidade do argumento de struct numa chamada passa a seguir a mesma regra da cópia por bloco — identidade nominal por omissão, ou equivalência estrutural quando qualquer dos lados é `[[unrestrict]]` (antes, uma struct equivalente era rejeitada na chamada, incoerente com a cópia). `[[restrict]]` no parâmetro (`global Strict([[restrict]] Pair p) i32`) exige o tipo nominal exato, rejeitando structs apenas com o mesmo layout e literais agregados anónimos. Um agregado `{ .. }` passado a um parâmetro struct dá um diagnóstico claro (declare uma `local` do tipo e passe-a). Verificação só em comptime: o carrier por valor é o mesmo nos dois modos, pelo que não altera o código gerado nem o `object_format` (mantém-se `16`). Fecha a ideia 3.
- **Conversão de struct para buffer (ideia 4)**: Uma struct converte-se para um buffer (`bN`/`BN`) de tamanho igual ou superior, expondo o layout em bytes. A conversão é uma cópia (o buffer nasce zero e recebe os bytes da struct, incl. padding), consistente com a atribuição de blocos — mutar o buffer não altera a struct. A origem pode ser uma variável (`local buf : B8 = s`, `buf = s`), um membro struct (`local buf : B8 = o.inner`) ou uma chamada que retorna a struct por valor (`local buf : B8 = Make()`). O gate de tamanho é comptime: um alvo menor é erro de compilação; um alvo maior é aceite e os bytes restantes ficam a zero. Generaliza o caminho de cópia de blocos para aceitar qualquer expressão de região (antes só variáveis), sem opcodes novos nem mudança de ABI (`object_format` mantém-se `16`). Para aceder aos bytes sem cópia, use um ponteiro (`static_cast<u8*>(s)`). A ideia 4 foi removida de `Ideias.txt`.
- **Propriedades de seção (ideia 1)**: O cabeçalho de seção `[nome]` aceita atributos inline com sintaxe de vírgula — `[[const] Main]` ou `[[const],[volatile] Main]` — equivalentes a escrever `[[attr]]` numa linha anterior ao `[Nome]`. Os atributos do escopo propagam-se uniformemente às variáveis, funções e enums do escopo (ex.: `[[const] Main]` torna todas as globais imutáveis; `[[noframe] Main]` aplica-se às funções). Atributos que definem um tipo (`[[packed]]`/`[[align]]`/`[[restrict]]`/`[[unrestrict]]`) continuam a não se propagar do escopo para `struct`/`using`, para não alterar silenciosamente todos os tipos. Mudança só de parser (sintaxe de cabeçalho); a semântica e a propagação de atributos já existiam, sem mudança de ABI nem de `object_format` (mantém-se `16`). A ideia 1 foi removida de `Ideias.txt`.
- **Otimização de registos em `-O2` (ideia 8)**: Passe de peephole arch-agnóstico sobre o assembly NASM (`NasmPeephole`) que mantém temporários de vida curta em registos via forwarding store→load e elimina `mov reg,reg` e `lea` redundantes. O ganho é medido no smoke (`opt_regalloc`: −7 instruções face a `-O0`, resultado idêntico). `-O0`/`-O1` mantêm o código byte a byte; o passe só corre em `-O2`.

### Suporte
- `object_format` sobe para `17` (era `16`): inclusão de `gep`, directiva `extern` e `@external_` na gramática e texto da Exir, invalidando caches com o formato anterior.
- `object_format` sobe para `16` (era `15`): o incremento C das structs muda a ABI gerada para parâmetros/retornos de struct (carrier inteiro por valor em vez de ponteiro), pelo que o layout do `.emo` e a chave de cache têm de invalidar objetos antigos. Os comentários e diretivas (ideia 6) são resolvidos no lexer/preprocessador antes do parsing e não alteram a gramática da Exir; o peephole de `-O2` é codegen-only depois da Exir e não altera a chave de cache (`opt:<n>` e o timestamp do exe já fazem parte dela). O núcleo das structs e os incrementos A (agregados) e B (visibilidade) reutilizam o tipo nominal e o caminho de blocos (`Addr`/`Load`/`Store`/`Memcpy`), sem opcodes nem tipos serializados novos — só a ABI por valor do incremento C exige o bump.

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
