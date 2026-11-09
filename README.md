# Emux

Emux é uma linguagem de programação de sistemas e médio nível (situada estrategicamente entre Assembly e C++), focada em emulação, controle fino de memória, compilação estática ultra-rápida e desenvolvimento de sistemas operacionais.

---

## Destaques

- **Zero Runtime Bloat**: Não há coletor de lixo, runtimes ocultos, tabelas de exceção ou RTTI. O código gerado é puro código de máquina nativo.
- **Buffers com Bounds-Checking em Tempo de Compilação**: Tipos de blocos estáticos (`bN` / `BN`) com views de fatiamento (`+ K`) e truncamento de cauda (`- K`) verificados 100% estaticamente em tempo de compilação sem overhead de execução.
- **Tipagem Flexível e Precisa**: Suporte a inteiros de largura arbitrária (de `u1` até `UINT32_MAX` bits, ex: `uN`/`iN`), ponto fixo (`q<bits>.<scale>`), floats, e distinção de tipos por identidade nominal ou estrutural (`[[restrict]]` / `[[unrestrict]]`).
- **Arquitetura de Compilação Moderna**: Pipeline baseado em Representação Intermediária (Exir), otimizador de passes e backends dedicados para `x86` e `x86_64` emitindo assembly NASM.
- **Cache Incremental Acelerado**: Recompilação apenas dos módulos alterados via arquivos `.emo` com invalidação automática de cache baseada em assinaturas de tipos e flags.

---

## Compilação e Uso

### Linha de Comando Rápida

```bash
# Compilar para x86_64 (padrão)
./emux arquivo.ex -o output

# Compilar para x86 (32 bits)
./emux arquivo.ex -a x86 -o output

# Otimizar: -O1 (dead code elimination) ou -O2 (peephole); padrão -O0
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

# Declaração global de um buffer de 14 bytes
[[const]] var mensagem : B14 = "Hello, Emux!\n"

func Start() i32 {
    # Fatiamento seguro em comptime: avanca 7 bytes ('E') e trunca 2 bytes da cauda
    # Variaveis de corpo/stack usam estritamente 'local' (var e apenas para globais)
    local view = (mensagem + 7) - 2

    # Variavel de moldura local na stack
    local retorno : i32 = *view

    return retorno
}
```

---

## Documentação Completa

Para guias aprofundados sobre a linguagem e sua arquitetura interna, consulte a pasta [`Docs/`](Docs/):

- 📖 **[Sintaxe e Controle de Fluxo](Docs/SyntaxAndFlow.md)**: Seções, variáveis `var`/`local`, `if`/`else`, `for`, `while`, enums e atributos.
- 🧱 **[Tipos e Gerenciamento de Memória](Docs/TypesAndMemory.md)**: Tipos primitivos, blocos `bN`/`BN`, fatiamento (`+`/`-`), ponteiros crus e mutabilidade `[[const]]`.
- ⚙️ **[Especificação da Exir v3](Docs/ExirV3.md)**: Gramática oficial, controle de fluxo por blocos básicos (CFG) e catálogo completo de instruções intermediárias.
- 🏷️ **[Política de Versionamento](VERSIONING.md)**: Como o versionamento SemVer e o contrato `object_format` são gerenciados.
- 📋 **[Histórico de Mudanças](CHANGELOG.md)**: Registro de versões, novidades e deprecados.

---

## Licença

Desenvolvido com foco em desempenho, modularidade e controle direto de hardware. Consulte a documentação interna para diretrizes de desenvolvimento e arquitetura.
