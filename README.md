# Emux

Emux é uma linguagem de programação de sistemas e médio nível (situada estrategicamente entre Assembly e C++), focada em emulação, controle fino de memória, compilação estática ultra-rápida e desenvolvimento de sistemas operacionais.

---

## Destaques

- **Zero Runtime Bloat**: Não há coletor de lixo, runtimes ocultos, tabelas de exceção ou RTTI. O código gerado é puro código de máquina nativo.
- **Buffers com Bounds-Checking em Tempo de Compilação**: Tipos de blocos estáticos (`bN` / `BN`) com views de fatiamento (`+ K`) e truncamento de cauda (`- K`) verificados 100% estaticamente em tempo de compilação sem overhead de execução.
- **Tipagem Flexível e Precisa**: Suporte a inteiros de largura arbitrária (de `u1` até `UINT32_MAX` bits, ex: `uN`/`iN` em bits ou `UN`/`IN` em bytes), ponto fixo (`q<bits>.<scale>` em bits ou `Q<bytes>.<scale>` em bytes), floats, e distinção de tipos por identidade nominal ou estrutural (`[[restrict]]` / `[[unrestrict]]`).
- **Structs com Layout Controlado**: Agregados nomeados (`struct`) com campos tipados, acesso `s.campo` (incl. aninhado), alinhamento natural ou `[[packed]]` sem padding, `[[align(N)]]` por struct/campo, e identidade nominal ou estrutural (`[[unrestrict]]`). O layout é calculado em comptime e a struct reusa o caminho de blocos, sem custo de runtime.
- **Arquitetura de Compilação Moderna**: Pipeline baseado em Representação Intermediária (Exir), otimizador de passes e backends dedicados para `x86` e `x86_64` emitindo assembly NASM.
- **Cache Incremental Acelerado**: Recompilação apenas dos módulos alterados via arquivos `.emo` com invalidação automática de cache baseada em assinaturas de tipos e flags.
- **Biblioteca Padrão Unificada (`std::`)**: Módulos unificados sob um único namespace com operações de E/S, memória, texto, caminhos e arquivos.

---

## Compilação e Uso

### Linha de Comando Rápida

```bash
# Compilar para x86_64 (padrão)
./emux arquivo.ex -o output

# Compilar para x86 (32 bits)
./emux arquivo.ex -a x86 -o output

# Otimizar: -O1 (poda de imports/loads) ou -O2 (peephole de registos); padrão -O0
./emux arquivo.ex -o output -O 2

# Escolher o subsistema PE (console ou windows)
./emux arquivo.ex -o output -s windows

# Exigir ';' no fim de cada instrução (padrão: opcional)
./emux arquivo.ex -o output --semicolons required
```

### Projetos Multi-Arquivo (`project.json` / `emux.json`)

```json
{
    "project": {
        "name": "app",
        "type": "executable",
        "include": ["."],
        "sources": ["main.ex", "math.ex"]
    },
    "build": {
        "arch": "64",
        "output": "app",
        "jobs": 4,
        "subsystem": "console",
        "semicolons": "optional",
        "optimize": 2
    }
}
```

Para compilar o projeto:
```bash
./emux --config project.json
```

---

## Exemplo de Código

```emux
[Main]

// Declaração global de um buffer imutável de 14 bytes
[[const]] global mensagem : B14 = "Hello, Emux!\n"

global Start() i32 {
    // Fatiamento estático e leitura: avança 7 bytes para ler o caractere 'E' (ASCII 69)
    local retorno : i32 = *(mensagem + 7)

    return retorno
}
```

---

## Correções e Estabilidade

- `#elif` decide corretamente depois de um `#if` falso, incluindo cadeias múltiplas e blocos aninhados.
- Ficheiros UTF-8 com BOM são aceites; o BOM é descartado antes da análise, sem desalinhar offsets.
- `exir { ... }` continua a ser reconhecido, mas falha com diagnóstico explícito enquanto a emissão de Exir crua não estiver implementada.
- Conversões implícitas entre inteiro e ponteiro exigem cast explícito (exceto o ponteiro nulo `0`); mudanças de sinal na mesma largura exigem `static_cast`, mas constantes que cabem no alvo continuam aceites.
- `#include` procura primeiro na pasta do ficheiro que contém a diretiva e só depois nos caminhos de include do projeto, permitindo includes aninhados.
- `#define NOME(a, b) corpo` cria macros com parâmetros (semântica C: argumentos expandidos uma vez, sem `#`/`##`, recursão prevenida). O `(` colado ao nome define uma macro com parâmetros; com espaço (`#define X (1+2)`) continua a ser uma macro objeto. `#undef`, `defined`, `#ifdef` e `#include` cobrem ambos os tipos.
- Comentários `//` (linha) e `/* ... */` (bloco); diretivas do preprocessador com prefixo `#` (`#include`, `#define`, `#if`, etc.), alinhado com a convenção de C.
- O hash de interface inclui a convenção de chamada e a restrição dos parâmetros; mudar a ABI invalida os módulos que chamam a função.
- O sidecar `.exir` anota cada instrução com a linha de origem (`; !dbg line N`).
- STD trata `INT64_MIN` em `abs`/`itoa`/`print_int`, preserva o separador de `path::join` e copia/preenche/compara memória por palavras de 8 bytes.
- Divisão inteira por zero e o overflow `MIN / -1` terminam num trap explícito (`ud2`) nos dois backends, em vez de um SIGFPE silencioso; divisor zero conhecido em comptime é erro de compilação. Um `local`/frame que exceda 256 KiB é diagnosticado em comptime, com o mesmo limite em x86 e x64.
- O preset `release` compila com `-Werror` e o CI corre o front-end com ASan/UBSan num job Linux.
- `-O2` aplica um peephole de registos sobre o assembly (forwarding store→load que mantém temporários de vida curta em registos, eliminação de `mov reg,reg` e `lea` redundantes), com ganho medido no smoke (`opt_regalloc`: −7 instruções face a `-O0`, resultado idêntico). `-O0`/`-O1` mantêm o código byte a byte.
- `struct` com campos nomeados e acesso `s.campo` (incl. aninhado): layout `[[packed]]`/natural/`[[align(N)]]` calculado em comptime, cópia por bloco e identidade nominal ou estrutural (`[[unrestrict]]`). Structs com nomes distintos são tipos distintos; a atribuição direta entre elas é erro. Lowering para `addr + offset` sobre o caminho de blocos existente, nos dois backends.

---

## Documentação Completa

Para guias aprofundados sobre a linguagem e sua arquitetura interna, consulte a pasta [`docs/`](docs/):

- 📖 **[Sintaxe e Controle de Fluxo](docs/SyntaxAndFlow.md)**: Seções, visibilidade `global`/`local`, `if`/`else`, `for`, `while`, enums e atributos.
- 🧱 **[Tipos e Gerenciamento de Memória](docs/TypesAndMemory.md)**: Tipos primitivos, blocos `bN`/`BN`, fatiamento (`+`/`-`), structs, ponteiros crus e mutabilidade `[[const]]`.
- ⚙️ **[Especificação da Exir v3](docs/ExirV3.md)**: Gramática oficial, controle de fluxo por blocos básicos (CFG) e catálogo completo de instruções intermediárias.
- 🗂️ **[Decisões de Implementação](docs/Decisions.md)**: Arquivo cronológico das decisões, incluindo adiamentos, movido de `Ideias.txt` conforme as regras de ideias.
- 🏷️ **[Política de Versionamento](VERSIONING.md)**: Como o versionamento SemVer e o contrato `object_format` são gerenciados.

---

## Licença

Projeto sob licença MIT. Consulte o arquivo `LICENSE` para mais detalhes.
